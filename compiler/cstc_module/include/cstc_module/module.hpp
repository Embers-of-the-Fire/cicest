#ifndef CICEST_COMPILER_CSTC_MODULE_MODULE_HPP
#define CICEST_COMPILER_CSTC_MODULE_MODULE_HPP

#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_resource_path/resource_path.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>

namespace cstc::module {

struct ModuleError {
    cstc::span::SourceSpan span;
    std::string message;
};

[[nodiscard]] inline std::string
    format_module_error(const cstc::span::SourceMap& source_map, const ModuleError& error) {
    if (const auto resolved = source_map.resolve_span(error.span); resolved.has_value()) {
        return "module error " + std::string(resolved->file_name) + ":"
             + std::to_string(resolved->start.line) + ":" + std::to_string(resolved->start.column)
             + ": " + error.message;
    }

    return "module error: " + error.message;
}

[[nodiscard]] inline std::expected<cstc::ast::Program, ModuleError> load_program(
    cstc::span::SourceMap& source_map, const std::filesystem::path& root_path,
    const std::filesystem::path& std_root_path);

} // namespace cstc::module

namespace cstc::module::detail {

using cstc::symbol::Symbol;
using cstc::symbol::SymbolHash;

enum class BindingKind { Struct, Enum, Fn, ExternFn, ExternStruct };

struct Binding {
    BindingKind kind = BindingKind::Struct;
    Symbol source_name = cstc::symbol::kInvalidSymbol;
    Symbol internal_name = cstc::symbol::kInvalidSymbol;
    cstc::span::SourceSpan span;
};

struct BindingSet {
    std::optional<Binding> type_binding;
    std::optional<Binding> value_binding;

    [[nodiscard]] bool empty() const {
        return !type_binding.has_value() && !value_binding.has_value();
    }
};

enum class ResolveState { Loaded, Resolving, Resolved, Rewritten };

struct ModuleInfo {
    std::filesystem::path path;
    cstc::span::SourceFileId file_id{};
    cstc::ast::Program program;
    std::size_t module_id = 0;
    bool is_root = false;
    bool is_prelude = false;
    ResolveState state = ResolveState::Loaded;
    std::unordered_map<Symbol, BindingSet, SymbolHash> local_bindings;
    std::unordered_map<Symbol, BindingSet, SymbolHash> visible_bindings;
    std::unordered_map<Symbol, BindingSet, SymbolHash> fallback_bindings;
    std::unordered_map<Symbol, BindingSet, SymbolHash> exports;
};

[[nodiscard]] inline std::unexpected<ModuleError>
    make_error(cstc::span::SourceSpan span, std::string message) {
    return std::unexpected(ModuleError{span, std::move(message)});
}

[[nodiscard]] inline std::expected<std::string, ModuleError>
    read_source_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return make_error({}, "failed to open input file: " + path.string());

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof())
        return make_error({}, "failed while reading input file: " + path.string());
    return buffer.str();
}

[[nodiscard]] inline std::string symbol_text(Symbol symbol) {
    return symbol.is_valid() ? std::string(symbol.as_str()) : "<invalid-symbol>";
}

[[nodiscard]] inline std::string duplicate_message(bool type_namespace, Symbol name) {
    return "duplicate " + std::string(type_namespace ? "type" : "function") + " name '"
         + symbol_text(name) + "'";
}

[[nodiscard]] inline std::expected<void, ModuleError> insert_binding_set(
    std::unordered_map<Symbol, BindingSet, SymbolHash>& bindings, Symbol visible_name,
    const BindingSet& incoming, cstc::span::SourceSpan error_span) {
    BindingSet& current = bindings[visible_name];

    if (incoming.type_binding.has_value()) {
        if (current.type_binding.has_value())
            return make_error(error_span, duplicate_message(true, visible_name));
        current.type_binding = incoming.type_binding;
    }

    if (incoming.value_binding.has_value()) {
        if (current.value_binding.has_value())
            return make_error(error_span, duplicate_message(false, visible_name));
        current.value_binding = incoming.value_binding;
    }

    return {};
}

[[nodiscard]] inline Symbol mangle_name(const ModuleInfo& module, Symbol source_name) {
    if (module.is_root)
        return source_name;

    return Symbol::intern(
        "__cst_mod_" + std::to_string(module.module_id) + "__" + std::string(source_name.as_str()));
}

[[nodiscard]] inline std::expected<void, ModuleError> collect_local_bindings(ModuleInfo& module) {
    for (const cstc::ast::Item& item : module.program.items) {
        BindingSet binding_set;
        Symbol visible_name = cstc::symbol::kInvalidSymbol;
        cstc::span::SourceSpan error_span;

        if (const auto* decl = std::get_if<cstc::ast::StructDecl>(&item)) {
            visible_name = decl->name;
            error_span = decl->span;
            binding_set.type_binding = Binding{
                .kind = BindingKind::Struct,
                .source_name = decl->name,
                .internal_name = mangle_name(module, decl->name),
                .span = decl->span,
            };
        } else if (const auto* decl = std::get_if<cstc::ast::EnumDecl>(&item)) {
            visible_name = decl->name;
            error_span = decl->span;
            binding_set.type_binding = Binding{
                .kind = BindingKind::Enum,
                .source_name = decl->name,
                .internal_name = mangle_name(module, decl->name),
                .span = decl->span,
            };
        } else if (const auto* decl = std::get_if<cstc::ast::FnDecl>(&item)) {
            visible_name = decl->name;
            error_span = decl->span;
            binding_set.value_binding = Binding{
                .kind = BindingKind::Fn,
                .source_name = decl->name,
                .internal_name = mangle_name(module, decl->name),
                .span = decl->span,
            };
        } else if (const auto* decl = std::get_if<cstc::ast::ExternFnDecl>(&item)) {
            visible_name = decl->name;
            error_span = decl->span;
            binding_set.value_binding = Binding{
                .kind = BindingKind::ExternFn,
                .source_name = decl->name,
                .internal_name = mangle_name(module, decl->name),
                .span = decl->span,
            };
        } else if (const auto* decl = std::get_if<cstc::ast::ExternStructDecl>(&item)) {
            visible_name = decl->name;
            error_span = decl->span;
            binding_set.type_binding = Binding{
                .kind = BindingKind::ExternStruct,
                .source_name = decl->name,
                .internal_name = mangle_name(module, decl->name),
                .span = decl->span,
            };
        } else {
            continue;
        }

        auto inserted =
            insert_binding_set(module.local_bindings, visible_name, binding_set, error_span);
        if (!inserted.has_value())
            return inserted;
    }

    return {};
}

[[nodiscard]] inline BindingSet
    binding_set_for_decl(const ModuleInfo& module, Symbol visible_name, bool type_namespace) {
    const BindingSet& binding = module.local_bindings.at(visible_name);
    BindingSet decl_binding;
    if (type_namespace)
        decl_binding.type_binding = binding.type_binding;
    else
        decl_binding.value_binding = binding.value_binding;
    return decl_binding;
}

[[nodiscard]] inline BindingSet
    merge_binding_sets(const BindingSet& visible, const BindingSet& fallback) {
    BindingSet merged = visible;
    if (!merged.type_binding.has_value())
        merged.type_binding = fallback.type_binding;
    if (!merged.value_binding.has_value())
        merged.value_binding = fallback.value_binding;
    return merged;
}

class Resolver {
public:
    Resolver(
        cstc::span::SourceMap& source_map, const std::filesystem::path& root_path,
        const std::filesystem::path& std_root_path)
        : source_map_(source_map)
        , std_dir_(cstc::resource_path::resolve_std_dir(std_root_path)) {
        root_path_ = cstc::resource_path::canonicalize_or_throw(root_path, "input file");
        prelude_path_ =
            cstc::resource_path::canonicalize_or_throw(std_dir_ / "prelude.cst", "std prelude");
    }

    [[nodiscard]] std::expected<cstc::ast::Program, ModuleError> run() {
        auto root_index = load_module(root_path_, true, root_path_ == prelude_path_);
        if (!root_index.has_value())
            return std::unexpected(root_index.error());
        root_index_ = *root_index;

        if (prelude_path_ == root_path_) {
            prelude_index_ = root_index_;
        } else {
            auto prelude_index = load_module(prelude_path_, false, true);
            if (!prelude_index.has_value())
                return std::unexpected(prelude_index.error());
            prelude_index_ = *prelude_index;
        }

        auto resolved = resolve_module(root_index_, std::nullopt);
        if (!resolved.has_value())
            return std::unexpected(resolved.error());

        return flatten_program();
    }

private:
    [[nodiscard]] std::expected<std::size_t, ModuleError>
        load_module(const std::filesystem::path& canonical_path, bool is_root, bool is_prelude) {
        const auto existing = modules_by_path_.find(canonical_path.string());
        if (existing != modules_by_path_.end()) {
            ModuleInfo& module = modules_[existing->second];
            module.is_root = module.is_root || is_root;
            module.is_prelude = module.is_prelude || is_prelude;
            return existing->second;
        }

        ModuleInfo module;
        module.path = canonical_path;
        module.is_root = is_root;
        module.is_prelude = is_prelude;
        module.module_id = is_root ? 0 : next_module_id_++;

        const auto source = read_source_file(canonical_path);
        if (!source.has_value())
            return std::unexpected(source.error());
        module.file_id = source_map_.add_file(canonical_path.string(), *source);

        const cstc::span::SourceFile* source_file = source_map_.file(module.file_id);
        if (source_file == nullptr)
            return make_error({}, "invalid source file id while loading module");

        const auto parsed =
            cstc::parser::parse_source_at(source_file->source, source_file->start_pos);
        if (!parsed.has_value()) {
            return make_error(parsed.error().span, parsed.error().message);
        }
        module.program = *parsed;

        auto collected = collect_local_bindings(module);
        if (!collected.has_value())
            return std::unexpected(collected.error());

        const std::size_t index = modules_.size();
        modules_by_path_.emplace(canonical_path.string(), index);
        modules_.push_back(std::move(module));
        return index;
    }

    [[nodiscard]] std::expected<std::size_t, ModuleError> load_import_target(
        const std::filesystem::path& from_path, const cstc::ast::ImportDecl& decl) {
        try {
            const std::filesystem::path resolved = resolve_import_path(from_path, decl.path);
            return load_module(resolved, false, resolved == prelude_path_);
        } catch (const std::exception& error) {
            return make_error(decl.span, error.what());
        }
    }

    [[nodiscard]] std::filesystem::path
        resolve_import_path(const std::filesystem::path& from_path, Symbol import_path) const {
        const std::string_view raw = import_path.as_str();
        if (raw.starts_with("@std/")) {
            return cstc::resource_path::canonicalize_or_throw(
                std_dir_ / std::filesystem::path(raw.substr(5)), "std module");
        }

        return cstc::resource_path::canonicalize_or_throw(
            from_path.parent_path() / std::filesystem::path(raw), "module import");
    }

    [[nodiscard]] std::expected<void, ModuleError> add_local_bindings_to_scope(ModuleInfo& module) {
        const auto add_decl = [&](Symbol name, cstc::span::SourceSpan span, bool is_public,
                                  bool type_namespace) -> std::expected<void, ModuleError> {
            const BindingSet binding_set = binding_set_for_decl(module, name, type_namespace);
            auto inserted = insert_binding_set(module.visible_bindings, name, binding_set, span);
            if (!inserted.has_value())
                return inserted;

            if (!is_public)
                return {};

            auto exported = insert_binding_set(module.exports, name, binding_set, span);
            if (!exported.has_value())
                return exported;

            return {};
        };

        for (const cstc::ast::Item& item : module.program.items) {
            if (const auto* decl = std::get_if<cstc::ast::StructDecl>(&item)) {
                auto added = add_decl(decl->name, decl->span, decl->is_public, true);
                if (!added.has_value())
                    return added;
            } else if (const auto* decl = std::get_if<cstc::ast::EnumDecl>(&item)) {
                auto added = add_decl(decl->name, decl->span, decl->is_public, true);
                if (!added.has_value())
                    return added;
            } else if (const auto* decl = std::get_if<cstc::ast::FnDecl>(&item)) {
                auto added = add_decl(decl->name, decl->span, decl->is_public, false);
                if (!added.has_value())
                    return added;
            } else if (const auto* decl = std::get_if<cstc::ast::ExternFnDecl>(&item)) {
                auto added = add_decl(decl->name, decl->span, decl->is_public, false);
                if (!added.has_value())
                    return added;
            } else if (const auto* decl = std::get_if<cstc::ast::ExternStructDecl>(&item)) {
                auto added = add_decl(decl->name, decl->span, decl->is_public, true);
                if (!added.has_value())
                    return added;
            }
        }

        return {};
    }

    [[nodiscard]] std::expected<void, ModuleError>
        add_implicit_prelude_fallback(ModuleInfo& module) {
        if (module.is_prelude)
            return {};

        if (prelude_index_ >= modules_.size())
            return make_error({}, "std prelude module was not loaded");

        auto resolved = resolve_module(prelude_index_, std::nullopt);
        if (!resolved.has_value())
            return std::unexpected(resolved.error());

        module.fallback_bindings = modules_[prelude_index_].exports;

        return {};
    }

    [[nodiscard]] std::expected<void, ModuleError> resolve_imports(std::size_t module_index) {
        const std::filesystem::path from_path = modules_[module_index].path;

        std::vector<cstc::ast::ImportDecl> imports;
        for (const cstc::ast::Item& item : modules_[module_index].program.items) {
            const auto* import = std::get_if<cstc::ast::ImportDecl>(&item);
            if (import == nullptr)
                continue;
            imports.push_back(*import);
        }

        for (const cstc::ast::ImportDecl& import : imports) {
            auto target_index = load_import_target(from_path, import);
            if (!target_index.has_value())
                return std::unexpected(target_index.error());

            auto target_resolved = resolve_module(*target_index, import.span);
            if (!target_resolved.has_value())
                return std::unexpected(target_resolved.error());

            const ModuleInfo& target = modules_[*target_index];
            for (const cstc::ast::ImportItem& import_item : import.items) {
                const Symbol visible_name = import_item.alias.value_or(import_item.name);

                const auto exported = target.exports.find(import_item.name);
                if (exported == target.exports.end() || exported->second.empty()) {
                    const auto private_binding = target.visible_bindings.find(import_item.name);
                    if (private_binding != target.visible_bindings.end()
                        && !private_binding->second.empty()) {
                        return make_error(
                            import_item.span, "item '" + symbol_text(import_item.name)
                                                  + "' is private in module '"
                                                  + target.path.string() + "'");
                    }

                    return make_error(
                        import_item.span, "module '" + target.path.string()
                                              + "' has no public item '"
                                              + symbol_text(import_item.name) + "'");
                }

                auto inserted = insert_binding_set(
                    modules_[module_index].visible_bindings, visible_name, exported->second,
                    import_item.span);
                if (!inserted.has_value())
                    return inserted;

                if (import.is_public) {
                    auto reexported = insert_binding_set(
                        modules_[module_index].exports, visible_name, exported->second,
                        import_item.span);
                    if (!reexported.has_value())
                        return reexported;
                }
            }
        }

        return {};
    }

    [[nodiscard]] std::expected<void, ModuleError> resolve_module(
        std::size_t module_index, std::optional<cstc::span::SourceSpan> import_span) {
        if (modules_[module_index].state == ResolveState::Resolved
            || modules_[module_index].state == ResolveState::Rewritten) {
            return {};
        }
        if (modules_[module_index].state == ResolveState::Resolving) {
            const cstc::span::SourceSpan span = import_span.value_or(cstc::span::SourceSpan{});
            return make_error(
                span,
                "cyclic module import involving '" + modules_[module_index].path.string() + "'");
        }

        modules_[module_index].state = ResolveState::Resolving;
        modules_[module_index].visible_bindings.clear();
        modules_[module_index].fallback_bindings.clear();
        modules_[module_index].exports.clear();

        auto locals = add_local_bindings_to_scope(modules_[module_index]);
        if (!locals.has_value())
            return locals;

        auto imports = resolve_imports(module_index);
        if (!imports.has_value())
            return imports;

        auto prelude = add_implicit_prelude_fallback(modules_[module_index]);
        if (!prelude.has_value())
            return prelude;

        modules_[module_index].state = ResolveState::Resolved;
        return {};
    }

    class AstRewriter {
    public:
        explicit AstRewriter(const ModuleInfo& module)
            : module_(module) {}

        void rewrite_program(cstc::ast::Program& program) {
            for (cstc::ast::Item& item : program.items)
                rewrite_item(item);
        }

    private:
        [[nodiscard]] std::optional<BindingSet> lookup_visible(Symbol name) const {
            const auto visible = module_.visible_bindings.find(name);
            const auto fallback = module_.fallback_bindings.find(name);
            if (visible == module_.visible_bindings.end()) {
                if (fallback == module_.fallback_bindings.end())
                    return std::nullopt;
                return fallback->second;
            }

            if (fallback == module_.fallback_bindings.end())
                return visible->second;

            return merge_binding_sets(visible->second, fallback->second);
        }

        [[nodiscard]] const BindingSet* lookup_local(Symbol name) const {
            const auto it = module_.local_bindings.find(name);
            if (it == module_.local_bindings.end())
                return nullptr;
            return &it->second;
        }

        void rewrite_decl_name(
            Symbol& name, Symbol& display_name, const BindingSet* binding, bool type_namespace) {
            if (binding == nullptr)
                return;

            const std::optional<Binding>& resolved_binding =
                type_namespace ? binding->type_binding : binding->value_binding;
            if (!resolved_binding.has_value())
                return;

            if (!display_name.is_valid())
                display_name = resolved_binding->source_name;
            name = resolved_binding->internal_name;
        }

        [[nodiscard]] bool is_local_name(Symbol name) const {
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                if (it->count(name) > 0)
                    return true;
            }
            return false;
        }

        void push_scope() { scopes_.push_back({}); }

        void pop_scope() { scopes_.pop_back(); }

        void add_local_name(Symbol name) {
            if (!name.is_valid())
                return;
            scopes_.back().insert(name);
        }

        void rewrite_type(cstc::ast::TypeRef& type) {
            if (type.kind == cstc::ast::TypeKind::Ref) {
                if (type.pointee)
                    rewrite_type(*type.pointee);
                return;
            }

            if (type.kind != cstc::ast::TypeKind::Named)
                return;

            const std::optional<BindingSet> binding = lookup_visible(type.symbol);
            if (!binding.has_value() || !binding->type_binding.has_value())
                return;

            if (!type.display_name.is_valid())
                type.display_name = type.symbol;
            type.symbol = binding->type_binding->internal_name;
        }

        void rewrite_block(const cstc::ast::BlockPtr& block) {
            push_scope();

            for (cstc::ast::Stmt& stmt : block->statements)
                rewrite_stmt(stmt);

            if (block->tail.has_value())
                rewrite_expr(*block->tail);

            pop_scope();
        }

        void rewrite_stmt(cstc::ast::Stmt& stmt) {
            std::visit(
                [&](auto& node) {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, cstc::ast::LetStmt>) {
                        if (node.type_annotation.has_value())
                            rewrite_type(*node.type_annotation);
                        rewrite_expr(node.initializer);
                        if (!node.discard)
                            add_local_name(node.name);
                    } else {
                        rewrite_expr(node.expr);
                    }
                },
                stmt);
        }

        void rewrite_for_init(std::variant<cstc::ast::ForInitLet, cstc::ast::ExprPtr>& init) {
            std::visit(
                [&](auto& node) {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, cstc::ast::ForInitLet>) {
                        if (node.type_annotation.has_value())
                            rewrite_type(*node.type_annotation);
                        rewrite_expr(node.initializer);
                        if (!node.discard)
                            add_local_name(node.name);
                    } else {
                        rewrite_expr(node);
                    }
                },
                init);
        }

        void rewrite_expr(const cstc::ast::ExprPtr& expr) {
            std::visit(
                [&](auto& node) {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (
                        std::is_same_v<Node, cstc::ast::LiteralExpr>
                        || std::is_same_v<Node, cstc::ast::ContinueExpr>) {
                        return;
                    } else if constexpr (std::is_same_v<Node, cstc::ast::PathExpr>) {
                        if (!node.display_head.is_valid())
                            node.display_head = node.head;

                        if (node.tail.has_value()) {
                            const std::optional<BindingSet> binding = lookup_visible(node.head);
                            if (binding.has_value() && binding->type_binding.has_value())
                                node.head = binding->type_binding->internal_name;
                            return;
                        }

                        if (is_local_name(node.head))
                            return;

                        const std::optional<BindingSet> binding = lookup_visible(node.head);
                        if (binding.has_value() && binding->value_binding.has_value())
                            node.head = binding->value_binding->internal_name;
                    } else if constexpr (std::is_same_v<Node, cstc::ast::StructInitExpr>) {
                        if (!node.display_name.is_valid())
                            node.display_name = node.type_name;
                        const std::optional<BindingSet> binding = lookup_visible(node.type_name);
                        if (binding.has_value() && binding->type_binding.has_value())
                            node.type_name = binding->type_binding->internal_name;
                        for (cstc::ast::StructInitField& field : node.fields)
                            rewrite_expr(field.value);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::UnaryExpr>) {
                        rewrite_expr(node.rhs);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::BinaryExpr>) {
                        rewrite_expr(node.lhs);
                        rewrite_expr(node.rhs);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::FieldAccessExpr>) {
                        rewrite_expr(node.base);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::CallExpr>) {
                        rewrite_expr(node.callee);
                        for (const cstc::ast::ExprPtr& arg : node.args)
                            rewrite_expr(arg);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::BlockPtr>) {
                        rewrite_block(node);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::IfExpr>) {
                        rewrite_expr(node.condition);
                        rewrite_block(node.then_block);
                        if (node.else_branch.has_value())
                            rewrite_expr(*node.else_branch);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::LoopExpr>) {
                        rewrite_block(node.body);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::WhileExpr>) {
                        rewrite_expr(node.condition);
                        rewrite_block(node.body);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::ForExpr>) {
                        push_scope();
                        if (node.init.has_value())
                            rewrite_for_init(*node.init);
                        if (node.condition.has_value())
                            rewrite_expr(*node.condition);
                        if (node.step.has_value())
                            rewrite_expr(*node.step);
                        rewrite_block(node.body);
                        pop_scope();
                    } else if constexpr (
                        std::is_same_v<Node, cstc::ast::BreakExpr>
                        || std::is_same_v<Node, cstc::ast::ReturnExpr>) {
                        if (node.value.has_value())
                            rewrite_expr(*node.value);
                    }
                },
                expr->node);
        }

        void rewrite_item(cstc::ast::Item& item) {
            std::visit(
                [&](auto& node) {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, cstc::ast::StructDecl>) {
                        rewrite_decl_name(
                            node.name, node.display_name, lookup_local(node.name), true);
                        for (cstc::ast::FieldDecl& field : node.fields)
                            rewrite_type(field.type);
                    } else if constexpr (
                        std::is_same_v<Node, cstc::ast::EnumDecl>
                        || std::is_same_v<Node, cstc::ast::ExternStructDecl>) {
                        rewrite_decl_name(
                            node.name, node.display_name, lookup_local(node.name), true);
                    } else if constexpr (std::is_same_v<Node, cstc::ast::FnDecl>) {
                        rewrite_decl_name(
                            node.name, node.display_name, lookup_local(node.name), false);
                        for (cstc::ast::Param& param : node.params)
                            rewrite_type(param.type);
                        if (node.return_type.has_value())
                            rewrite_type(*node.return_type);
                        push_scope();
                        for (const cstc::ast::Param& param : node.params)
                            add_local_name(param.name);
                        rewrite_block(node.body);
                        pop_scope();
                    } else if constexpr (std::is_same_v<Node, cstc::ast::ExternFnDecl>) {
                        rewrite_decl_name(
                            node.name, node.display_name, lookup_local(node.name), false);
                        for (cstc::ast::Param& param : node.params)
                            rewrite_type(param.type);
                        if (node.return_type.has_value())
                            rewrite_type(*node.return_type);
                    } else {
                        return;
                    }
                },
                item);
        }

        const ModuleInfo& module_;
        std::vector<std::unordered_set<Symbol, SymbolHash>> scopes_;
    };

    void rewrite_module(std::size_t module_index) {
        ModuleInfo& module = modules_[module_index];
        if (module.state == ResolveState::Rewritten)
            return;

        AstRewriter rewriter(module);
        rewriter.rewrite_program(module.program);
        module.state = ResolveState::Rewritten;
    }

    [[nodiscard]] cstc::ast::Program flatten_program() {
        std::vector<std::size_t> ordered;
        ordered.reserve(modules_.size());

        if (prelude_index_ < modules_.size())
            ordered.push_back(prelude_index_);

        for (std::size_t index = 0; index < modules_.size(); ++index) {
            if (index == root_index_ || index == prelude_index_)
                continue;
            ordered.push_back(index);
        }

        if (root_index_ < modules_.size() && root_index_ != prelude_index_)
            ordered.push_back(root_index_);

        cstc::ast::Program merged;
        for (const std::size_t index : ordered) {
            rewrite_module(index);
            for (const cstc::ast::Item& item : modules_[index].program.items) {
                if (std::holds_alternative<cstc::ast::ImportDecl>(item))
                    continue;
                merged.items.push_back(item);
            }
        }

        return merged;
    }

    cstc::span::SourceMap& source_map_;
    std::filesystem::path root_path_;
    std::filesystem::path std_dir_;
    std::filesystem::path prelude_path_;
    std::unordered_map<std::string, std::size_t> modules_by_path_;
    std::vector<ModuleInfo> modules_;
    std::size_t next_module_id_ = 1;
    std::size_t root_index_ = 0;
    std::size_t prelude_index_ = 0;
};

} // namespace cstc::module::detail

namespace cstc::module {

inline std::expected<cstc::ast::Program, ModuleError> load_program(
    cstc::span::SourceMap& source_map, const std::filesystem::path& root_path,
    const std::filesystem::path& std_root_path) {
    try {
        detail::Resolver resolver(source_map, root_path, std_root_path);
        return resolver.run();
    } catch (const std::exception& error) {
        return std::unexpected(ModuleError{{}, error.what()});
    }
}

} // namespace cstc::module

#endif // CICEST_COMPILER_CSTC_MODULE_MODULE_HPP
