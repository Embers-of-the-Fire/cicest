#ifndef CICEST_COMPILER_CSTC_TYIR_BUILDER_BUILDER_HPP
#define CICEST_COMPILER_CSTC_TYIR_BUILDER_BUILDER_HPP

/// @file builder.hpp
/// @brief AST → TyIR lowering pass.
///
/// The lowering pass transforms a parsed `cstc::ast::Program` into a
/// fully type-annotated `cstc::tyir::TyProgram`.  It performs:
///
///  1. **Name collection** — all struct, enum, and function names are
///     gathered before any item is lowered, so forward references within the
///     same file resolve correctly.
///
///  2. **Type resolution** — `ast::TypeRef` values (which hold string-based
///     `TypeKind`) are resolved to `tyir::Ty` values; user-defined type names
///     are validated against the collected declarations.
///
///  3. **Type inference and checking** — every expression is annotated with
///     its inferred `Ty`; operator operand types, struct field types, function
///     argument types, and return types are validated.
///
///  4. **Name resolution** — `ast::PathExpr` nodes are resolved to either a
///     `tyir::LocalRef` (local binding / parameter), a `tyir::EnumVariantRef`
///     (`EnumName::Variant`), or the `fn_name` field of a `tyir::TyCall`.
///
/// ## Error handling
///
/// The pass returns `std::unexpected<LowerError>` on the first error
/// encountered.  `LowerError` contains a source span and a human-readable
/// diagnostic message.
///
/// ## Usage
///
/// ```cpp
/// cstc::symbol::SymbolSession session;
/// const auto ast = cstc::parser::parse_source(source);
/// if (!ast) { /* handle parse error */ }
///
/// const auto tyir = cstc::tyir_builder::lower_program(*ast);
/// if (!tyir) {
///     std::cerr << "lower error: " << tyir.error().message << "\n";
/// }
/// ```

#include <expected>
#include <string>

#include <cstc_ast/ast.hpp>
#include <cstc_span/span.hpp>
#include <cstc_tyir/tyir.hpp>

namespace cstc::tyir_builder {

/// Diagnostic emitted when lowering fails.
struct LowerError {
    /// Source location where the error was detected.
    cstc::span::SourceSpan span;
    /// Human-readable description of the error.
    std::string message;
};

/// Lowers an AST program to a fully type-annotated TyIR program.
///
/// Requires an active `SymbolSession` on the calling thread.
///
/// On success returns the typed program.  On failure returns the first
/// `LowerError` encountered.
[[nodiscard]] inline std::expected<tyir::TyProgram, LowerError>
    lower_program(const ast::Program& program);

} // namespace cstc::tyir_builder

#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cstc_symbol/symbol.hpp>

namespace cstc::tyir_builder {

namespace detail {

// ─── Type environment ────────────────────────────────────────────────────────

/// Signature stored for each top-level function.
struct FnSignature {
    std::vector<tyir::Ty> param_types;
    tyir::Ty return_ty;
    cstc::span::SourceSpan span;
};

/// Global type and function environment built during the collection passes.
struct TypeEnv {
    /// Maps each struct name → its resolved field list.
    std::unordered_map<
        cstc::symbol::Symbol, std::vector<tyir::TyFieldDecl>, cstc::symbol::SymbolHash>
        struct_fields;

    /// Maps each enum name → its variant list.
    std::unordered_map<
        cstc::symbol::Symbol, std::vector<tyir::TyEnumVariant>, cstc::symbol::SymbolHash>
        enum_variants;

    /// Maps each function name → its signature.
    std::unordered_map<cstc::symbol::Symbol, FnSignature, cstc::symbol::SymbolHash> fn_signatures;

    /// Names of extern (opaque) struct types. These are valid as type
    /// annotations but cannot be constructed via struct-init expressions.
    std::unordered_set<cstc::symbol::Symbol, cstc::symbol::SymbolHash> extern_struct_names;

    [[nodiscard]] bool is_struct(cstc::symbol::Symbol name) const {
        return struct_fields.count(name) > 0;
    }
    [[nodiscard]] bool is_extern_struct(cstc::symbol::Symbol name) const {
        return extern_struct_names.count(name) > 0;
    }
    [[nodiscard]] bool is_enum(cstc::symbol::Symbol name) const {
        return enum_variants.count(name) > 0;
    }
    [[nodiscard]] bool is_fn(cstc::symbol::Symbol name) const {
        return fn_signatures.count(name) > 0;
    }
    [[nodiscard]] bool is_named_type(cstc::symbol::Symbol name) const {
        return is_struct(name) || is_enum(name) || is_extern_struct(name);
    }

    /// Returns the resolved type of `field_name` on struct `struct_name`, or
    /// `std::nullopt` if the field does not exist.
    [[nodiscard]] std::optional<tyir::Ty>
        field_ty(cstc::symbol::Symbol struct_name, cstc::symbol::Symbol field_name) const {
        const auto it = struct_fields.find(struct_name);
        if (it == struct_fields.end())
            return std::nullopt;
        for (const tyir::TyFieldDecl& f : it->second) {
            if (f.name == field_name)
                return f.ty;
        }
        return std::nullopt;
    }

    /// Returns true when `variant_name` is a declared variant of `enum_name`.
    [[nodiscard]] bool
        has_variant(cstc::symbol::Symbol enum_name, cstc::symbol::Symbol variant_name) const {
        const auto it = enum_variants.find(enum_name);
        if (it == enum_variants.end())
            return false;
        for (const tyir::TyEnumVariant& v : it->second) {
            if (v.name == variant_name)
                return true;
        }
        return false;
    }
};

// ─── Scope (local variable environment) ─────────────────────────────────────

/// Scoped stack of local variable type bindings.
///
/// A new stack frame is pushed on entry to every block or `for`-init scope and
/// popped on exit.
class Scope {
public:
    void push() { frames_.push_back({}); }
    void pop() {
        assert(!frames_.empty());
        frames_.pop_back();
    }

    void insert(cstc::symbol::Symbol name, tyir::Ty ty) {
        assert(!frames_.empty());
        frames_.back().emplace(name, std::move(ty));
    }

    [[nodiscard]] std::optional<tyir::Ty> lookup(cstc::symbol::Symbol name) const {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end())
                return found->second;
        }
        return std::nullopt;
    }

private:
    std::vector<std::unordered_map<cstc::symbol::Symbol, tyir::Ty, cstc::symbol::SymbolHash>>
        frames_;
};

// ─── Loop context ───────────────────────────────────────────────────────────

/// Discriminates the three loop forms for break-value validation.
enum class LoopKind { Loop, While, For };

/// Per-loop context pushed onto the loop stack on entry to any loop form.
struct LoopCtx {
    LoopKind kind;
    /// Accumulated break type.  `std::nullopt` means no `break` has been seen
    /// yet; once a `break` is encountered the type is set (bare `break` → Unit,
    /// `break expr` → expr type).  Subsequent breaks must unify.
    std::optional<tyir::Ty> break_ty;
};

// ─── Lowering context ────────────────────────────────────────────────────────

/// Per-function lowering state threaded through all expression / statement
/// lowering functions.
struct LowerCtx {
    const TypeEnv& env;
    Scope scope;
    /// Return type of the function currently being lowered.
    tyir::Ty current_return_ty;

    /// Stack of enclosing loops (innermost at back).
    std::vector<LoopCtx> loop_stack;

    /// Returns true when we are inside at least one loop form.
    [[nodiscard]] bool in_loop() const { return !loop_stack.empty(); }

    /// Returns the innermost loop context.  UB if `!in_loop()`.
    [[nodiscard]] LoopCtx& current_loop() {
        assert(!loop_stack.empty());
        return loop_stack.back();
    }

    /// Push a new loop context for the given loop kind.
    void push_loop(LoopKind kind) { loop_stack.push_back(LoopCtx{kind, std::nullopt}); }

    /// Pop the innermost loop context.
    void pop_loop() {
        assert(!loop_stack.empty());
        loop_stack.pop_back();
    }
};

// ─── Type compatibility ──────────────────────────────────────────────────────

/// Returns true when `actual` may appear where `expected` is required.
///
/// `Never` (bottom type) is compatible with any expected type.
[[nodiscard]] inline bool compatible(const tyir::Ty& actual, const tyir::Ty& expected) {
    if (actual.is_never())
        return true;
    return actual == expected;
}

// ─── Error helpers ────────────────────────────────────────────────────────────

[[nodiscard]] inline std::unexpected<LowerError>
    make_error(cstc::span::SourceSpan span, std::string msg) {
    return std::unexpected(LowerError{span, std::move(msg)});
}

/// Returns true if the ABI string is a recognised extern ABI.
[[nodiscard]] inline bool is_supported_abi(cstc::symbol::Symbol abi) {
    const auto sv = abi.as_str();
    return sv == "lang" || sv == "c";
}

/// Validates an extern ABI string, returning an error if unsupported.
[[nodiscard]] inline std::optional<std::unexpected<LowerError>>
    validate_abi(cstc::symbol::Symbol abi, cstc::span::SourceSpan span) {
    if (!is_supported_abi(abi))
        return make_error(span, "unsupported ABI \"" + std::string(abi.as_str()) + "\"");
    return std::nullopt;
}

/// Resolves the linked symbol name for an extern function declaration.
[[nodiscard]] inline std::expected<cstc::symbol::Symbol, LowerError>
    resolve_extern_link_name(const ast::ExternFnDecl& decl) {
    const auto lang_attr_name = cstc::symbol::Symbol::intern("lang");
    std::optional<cstc::symbol::Symbol> link_name;

    for (const ast::Attribute& attr : decl.attributes) {
        if (attr.name != lang_attr_name)
            continue;

        if (decl.abi.as_str() != "lang") {
            return std::unexpected(
                LowerError{
                    attr.span,
                    "attribute `lang` is only supported on `extern \"lang\" fn` declarations",
                });
        }
        if (!attr.value.has_value()) {
            return std::unexpected(
                LowerError{attr.span, "attribute `lang` requires a string value"});
        }
        if (attr.value->as_str().empty()) {
            return std::unexpected(
                LowerError{attr.span, "attribute `lang` requires a non-empty string value"});
        }
        if (link_name.has_value()) {
            return std::unexpected(LowerError{attr.span, "duplicate `lang` attribute"});
        }

        link_name = *attr.value;
    }

    return link_name.value_or(decl.name);
}

// ─── Type resolution ─────────────────────────────────────────────────────────

/// Converts an AST `TypeRef` to a `tyir::Ty`, validating named types against
/// the type environment.
[[nodiscard]] inline std::expected<tyir::Ty, LowerError>
    lower_type(const ast::TypeRef& ref, const TypeEnv& env, cstc::span::SourceSpan span) {
    switch (ref.kind) {
    case ast::TypeKind::Unit: return tyir::ty::unit();
    case ast::TypeKind::Num: return tyir::ty::num();
    case ast::TypeKind::Str: return tyir::ty::str();
    case ast::TypeKind::Bool: return tyir::ty::bool_();
    case ast::TypeKind::Never: return tyir::ty::never();
    case ast::TypeKind::Named:
        if (!ref.symbol.is_valid())
            return make_error(span, "invalid named type reference");
        if (!env.is_named_type(ref.symbol))
            return make_error(span, "undefined type '" + std::string(ref.symbol.as_str()) + "'");
        return tyir::ty::named(ref.symbol);
    }
    assert(false && "unhandled ast::TypeKind in lower_type");
    __builtin_unreachable();
}

// ─── Forward declarations ────────────────────────────────────────────────────

[[nodiscard]] inline std::expected<tyir::TyExprPtr, LowerError>
    lower_expr(const ast::ExprPtr& expr, LowerCtx& ctx);

[[nodiscard]] inline std::expected<tyir::TyBlock, LowerError>
    lower_block(const ast::BlockExpr& block, LowerCtx& ctx);

// ─── Function signature resolution ──────────────────────────────────────────

[[nodiscard]] inline std::expected<FnSignature, LowerError> resolve_fn_signature(
    const std::vector<ast::Param>& params, const std::optional<ast::TypeRef>& return_type,
    cstc::span::SourceSpan span, const TypeEnv& env) {
    FnSignature sig;
    sig.span = span;
    if (return_type.has_value()) {
        auto t = lower_type(*return_type, env, span);
        if (!t)
            return std::unexpected(std::move(t.error()));
        sig.return_ty = *t;
    } else {
        sig.return_ty = tyir::ty::unit();
    }
    for (const ast::Param& p : params) {
        auto pt = lower_type(p.type, env, p.span);
        if (!pt)
            return std::unexpected(std::move(pt.error()));
        sig.param_types.push_back(*pt);
    }
    return sig;
}

// ─── Statement lowering ──────────────────────────────────────────────────────

[[nodiscard]] inline std::expected<tyir::TyStmt, LowerError>
    lower_stmt(const ast::Stmt& stmt, LowerCtx& ctx) {
    return std::visit(
        [&](const auto& s) -> std::expected<tyir::TyStmt, LowerError> {
            using S = std::decay_t<decltype(s)>;

            if constexpr (std::is_same_v<S, ast::LetStmt>) {
                // Lower the initializer
                auto init = lower_expr(s.initializer, ctx);
                if (!init)
                    return std::unexpected(std::move(init.error()));

                // Resolve binding type (explicit annotation or infer from init)
                tyir::Ty binding_ty;
                if (s.type_annotation.has_value()) {
                    auto ann = lower_type(*s.type_annotation, ctx.env, s.span);
                    if (!ann)
                        return std::unexpected(std::move(ann.error()));
                    if (!compatible((*init)->ty, *ann))
                        return make_error(
                            s.span, "type mismatch in let binding: expected '" + ann->display()
                                        + "', found '" + (*init)->ty.display() + "'");
                    binding_ty = *ann;
                } else {
                    binding_ty = (*init)->ty;
                }

                // Register binding in scope (unless discard pattern)
                if (!s.discard && s.name.is_valid())
                    ctx.scope.insert(s.name, binding_ty);

                return tyir::TyLetStmt{s.discard, s.name, binding_ty, std::move(*init), s.span};

            } else {                                          // ast::ExprStmt
                auto expr = lower_expr(s.expr, ctx);
                if (!expr)
                    return std::unexpected(std::move(expr.error()));
                return tyir::TyExprStmt{std::move(*expr), s.span};
            }
        },
        stmt);
}

[[nodiscard]] inline bool expr_can_fallthrough(const tyir::TyExpr& expr);

[[nodiscard]] inline bool stmt_can_fallthrough(const tyir::TyStmt& stmt) {
    return std::visit(
        [](const auto& s) {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, tyir::TyLetStmt>)
                return expr_can_fallthrough(*s.init);
            else
                return expr_can_fallthrough(*s.expr);
        },
        stmt);
}

[[nodiscard]] inline bool block_can_fallthrough(const tyir::TyBlock& block);

[[nodiscard]] inline bool expr_can_fallthrough(const tyir::TyExpr& expr) {
    if (expr.ty.is_never())
        return false;

    return std::visit(
        [](const auto& node) {
            using N = std::decay_t<decltype(node)>;

            if constexpr (
                std::is_same_v<N, tyir::TyLiteral> || std::is_same_v<N, tyir::LocalRef>
                || std::is_same_v<N, tyir::EnumVariantRef>) { // NOLINT(bugprone-branch-clone)
                return true;
            } else if constexpr (std::is_same_v<N, tyir::TyStructInit>) {
                for (const tyir::TyStructInitField& field : node.fields) {
                    if (!expr_can_fallthrough(*field.value))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<N, tyir::TyUnary>) {
                return expr_can_fallthrough(*node.rhs);
            } else if constexpr (std::is_same_v<N, tyir::TyBinary>) {
                return expr_can_fallthrough(*node.lhs) && expr_can_fallthrough(*node.rhs);
            } else if constexpr (std::is_same_v<N, tyir::TyFieldAccess>) {
                return expr_can_fallthrough(*node.base);
            } else if constexpr (std::is_same_v<N, tyir::TyCall>) {
                for (const tyir::TyExprPtr& arg : node.args) {
                    if (!expr_can_fallthrough(*arg))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<N, tyir::TyBlockPtr>) {
                return block_can_fallthrough(*node);
            } else if constexpr (std::is_same_v<N, tyir::TyIf>) {
                if (!expr_can_fallthrough(*node.condition))
                    return false;
                if (!node.else_branch.has_value())
                    return true;
                return block_can_fallthrough(*node.then_block)
                    || expr_can_fallthrough(**node.else_branch);
            } else if constexpr (std::is_same_v<N, tyir::TyLoop>) {
                return true;
            } else if constexpr (std::is_same_v<N, tyir::TyWhile>) {
                return expr_can_fallthrough(*node.condition);
            } else if constexpr (std::is_same_v<N, tyir::TyFor>) {
                if (node.init.has_value() && !expr_can_fallthrough(*node.init->init))
                    return false;
                if (node.condition.has_value() && !expr_can_fallthrough(**node.condition))
                    return false;
                return true;
            } else if constexpr (
                std::is_same_v<N, tyir::TyBreak> || std::is_same_v<N, tyir::TyContinue>
                || std::is_same_v<N, tyir::TyReturn>) {
                return false;
            } else {
                return true;
            }
        },
        expr.node);
}

[[nodiscard]] inline bool block_can_fallthrough(const tyir::TyBlock& block) {
    for (const tyir::TyStmt& stmt : block.stmts) {
        if (!stmt_can_fallthrough(stmt))
            return false;
    }
    if (!block.tail.has_value())
        return true;
    return expr_can_fallthrough(**block.tail);
}

// ─── Block lowering ──────────────────────────────────────────────────────────

[[nodiscard]] inline std::expected<tyir::TyBlock, LowerError>
    lower_block(const ast::BlockExpr& block, LowerCtx& ctx) {
    ctx.scope.push();

    tyir::TyBlock result;
    result.span = block.span;

    for (const ast::Stmt& stmt : block.statements) {
        auto lowered = lower_stmt(stmt, ctx);
        if (!lowered) {
            ctx.scope.pop();
            return std::unexpected(std::move(lowered.error()));
        }
        result.stmts.push_back(std::move(*lowered));
    }

    const bool reaches_tail = block_can_fallthrough(result);

    if (block.tail.has_value()) {
        auto tail = lower_expr(*block.tail, ctx);
        if (!tail) {
            ctx.scope.pop();
            return std::unexpected(std::move(tail.error()));
        }
        result.tail = std::move(*tail);
        result.ty = reaches_tail ? (*result.tail)->ty : tyir::ty::never();
    } else {
        result.ty = reaches_tail ? tyir::ty::unit() : tyir::ty::never();
    }

    ctx.scope.pop();
    return result;
}

// ─── Expression lowering ─────────────────────────────────────────────────────

[[nodiscard]] inline std::expected<tyir::TyExprPtr, LowerError>
    lower_expr(const ast::ExprPtr& expr, LowerCtx& ctx) {
    assert(expr != nullptr);

    return std::visit(
        [&](const auto& node) -> std::expected<tyir::TyExprPtr, LowerError> {
            using N = std::decay_t<decltype(node)>;

            // ── Literal ───────────────────────────────────────────────────
            if constexpr (std::is_same_v<N, ast::LiteralExpr>) {
                tyir::TyLiteral lit;
                tyir::Ty ty;
                switch (node.kind) {
                case ast::LiteralExpr::Kind::Num:
                    lit = {tyir::TyLiteral::Kind::Num, node.symbol, false};
                    ty = tyir::ty::num();
                    break;
                case ast::LiteralExpr::Kind::Str:
                    lit = {tyir::TyLiteral::Kind::Str, node.symbol, false};
                    ty = tyir::ty::str();
                    break;
                case ast::LiteralExpr::Kind::Bool:
                    lit = {
                        tyir::TyLiteral::Kind::Bool, cstc::symbol::kInvalidSymbol, node.bool_value};
                    ty = tyir::ty::bool_();
                    break;
                case ast::LiteralExpr::Kind::Unit:
                    lit = {tyir::TyLiteral::Kind::Unit, cstc::symbol::kInvalidSymbol, false};
                    ty = tyir::ty::unit();
                    break;
                }
                return tyir::make_ty_expr(expr->span, std::move(lit), std::move(ty));
            }

            // ── Path ──────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::PathExpr>) {
                if (node.tail.has_value()) {
                    // EnumName::Variant
                    const cstc::symbol::Symbol enum_name = node.head;
                    const cstc::symbol::Symbol variant_name = *node.tail;
                    if (!ctx.env.is_enum(enum_name))
                        return make_error(
                            expr->span,
                            "'" + std::string(enum_name.as_str()) + "' is not an enum type");
                    if (!ctx.env.has_variant(enum_name, variant_name))
                        return make_error(
                            expr->span, "no variant '" + std::string(variant_name.as_str())
                                            + "' in enum '" + std::string(enum_name.as_str())
                                            + "'");
                    return tyir::make_ty_expr(
                        expr->span, tyir::EnumVariantRef{enum_name, variant_name},
                        tyir::ty::named(enum_name));
                }

                // Local variable reference
                const auto local_ty = ctx.scope.lookup(node.head);
                if (local_ty.has_value())
                    return tyir::make_ty_expr(expr->span, tyir::LocalRef{node.head}, *local_ty);

                return make_error(
                    expr->span, "undefined variable '" + std::string(node.head.as_str()) + "'");
            }

            // ── Struct init ───────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::StructInitExpr>) {
                const cstc::symbol::Symbol type_name = node.type_name;
                if (ctx.env.is_extern_struct(type_name))
                    return make_error(
                        expr->span, "cannot construct extern type '"
                                        + std::string(type_name.as_str())
                                        + "'; extern structs are opaque");
                if (!ctx.env.is_struct(type_name))
                    return make_error(
                        expr->span,
                        "'" + std::string(type_name.as_str()) + "' is not a struct type");

                const auto& expected_fields = ctx.env.struct_fields.at(type_name);

                // Validate: no extra/duplicate fields, each field type matches
                std::vector<tyir::TyStructInitField> lowered_fields;
                lowered_fields.reserve(node.fields.size());
                std::unordered_map<
                    cstc::symbol::Symbol, cstc::span::SourceSpan, cstc::symbol::SymbolHash>
                    seen_fields;
                seen_fields.reserve(node.fields.size());

                for (const ast::StructInitField& field : node.fields) {
                    const auto expected_ty = ctx.env.field_ty(type_name, field.name);
                    if (!expected_ty)
                        return make_error(
                            field.span, "no field '" + std::string(field.name.as_str())
                                            + "' in struct '" + std::string(type_name.as_str())
                                            + "'");

                    const auto [_, inserted] = seen_fields.emplace(field.name, field.span);
                    if (!inserted)
                        return make_error(
                            field.span, "duplicate field '" + std::string(field.name.as_str())
                                            + "' in struct initializer for '"
                                            + std::string(type_name.as_str()) + "'");

                    auto val = lower_expr(field.value, ctx);
                    if (!val)
                        return std::unexpected(std::move(val.error()));

                    if (!compatible((*val)->ty, *expected_ty))
                        return make_error(
                            field.span, "field '" + std::string(field.name.as_str())
                                            + "': expected '" + expected_ty->display()
                                            + "', found '" + (*val)->ty.display() + "'");

                    lowered_fields.push_back(
                        tyir::TyStructInitField{field.name, std::move(*val), field.span});
                }

                // Validate that every declared struct field is initialised exactly once.
                for (const tyir::TyFieldDecl& expected_field : expected_fields) {
                    if (seen_fields.count(expected_field.name) == 0)
                        return make_error(
                            expr->span, "missing field '"
                                            + std::string(expected_field.name.as_str())
                                            + "' in struct initializer for '"
                                            + std::string(type_name.as_str()) + "'");
                }

                return tyir::make_ty_expr(
                    expr->span, tyir::TyStructInit{type_name, std::move(lowered_fields)},
                    tyir::ty::named(type_name));
            }

            // ── Unary ─────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::UnaryExpr>) {
                auto rhs = lower_expr(node.rhs, ctx);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));

                tyir::Ty result_ty;
                switch (node.op) {
                case ast::UnaryOp::Negate:
                    if (!compatible((*rhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span,
                            "unary '-' requires 'num', found '" + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::num();
                    break;
                case ast::UnaryOp::Not:
                    if (!compatible((*rhs)->ty, tyir::ty::bool_()))
                        return make_error(
                            expr->span,
                            "unary '!' requires 'bool', found '" + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::bool_();
                    break;
                }
                return tyir::make_ty_expr(
                    expr->span, tyir::TyUnary{node.op, std::move(*rhs)}, std::move(result_ty));
            }

            // ── Binary ────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::BinaryExpr>) {
                auto lhs = lower_expr(node.lhs, ctx);
                if (!lhs)
                    return std::unexpected(std::move(lhs.error()));
                auto rhs = lower_expr(node.rhs, ctx);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));

                tyir::Ty result_ty;
                using Op = ast::BinaryOp;
                switch (node.op) {
                case Op::Add:
                case Op::Sub:
                case Op::Mul:
                case Op::Div:
                case Op::Mod:
                    if (!compatible((*lhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "arithmetic operator requires 'num' on left, found '"
                                            + (*lhs)->ty.display() + "'");
                    if (!compatible((*rhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "arithmetic operator requires 'num' on right, found '"
                                            + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::num();
                    break;

                case Op::Lt:
                case Op::Le:
                case Op::Gt:
                case Op::Ge:
                    if (!compatible((*lhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "comparison operator requires 'num' on left, found '"
                                            + (*lhs)->ty.display() + "'");
                    if (!compatible((*rhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "comparison operator requires 'num' on right, found '"
                                            + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::bool_();
                    break;

                case Op::Eq:
                case Op::Ne: {
                    // Both operands must have the same type (Never unifies)
                    const tyir::Ty& lty = (*lhs)->ty;
                    const tyir::Ty& rty = (*rhs)->ty;
                    if (!lty.is_never() && !rty.is_never() && lty != rty)
                        return make_error(
                            expr->span,
                            "equality operator requires same types on both sides, found '"
                                + lty.display() + "' and '" + rty.display() + "'");
                    result_ty = tyir::ty::bool_();
                    break;
                }

                case Op::And:
                case Op::Or:
                    if (!compatible((*lhs)->ty, tyir::ty::bool_()))
                        return make_error(
                            expr->span, "logical operator requires 'bool' on left, found '"
                                            + (*lhs)->ty.display() + "'");
                    if (!compatible((*rhs)->ty, tyir::ty::bool_()))
                        return make_error(
                            expr->span, "logical operator requires 'bool' on right, found '"
                                            + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::bool_();
                    break;
                }
                return tyir::make_ty_expr(
                    expr->span, tyir::TyBinary{node.op, std::move(*lhs), std::move(*rhs)},
                    std::move(result_ty));
            }

            // ── Field access ──────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::FieldAccessExpr>) {
                auto base = lower_expr(node.base, ctx);
                if (!base)
                    return std::unexpected(std::move(base.error()));

                if ((*base)->ty.is_never())
                    return tyir::make_ty_expr(
                        expr->span, tyir::TyFieldAccess{std::move(*base), node.field},
                        tyir::ty::never());

                if (!(*base)->ty.is_named())
                    return make_error(
                        expr->span,
                        "field access on non-struct type '" + (*base)->ty.display() + "'");

                const auto field_ty = ctx.env.field_ty((*base)->ty.name, node.field);
                if (!field_ty)
                    return make_error(
                        expr->span, "no field '" + std::string(node.field.as_str())
                                        + "' in struct '" + std::string((*base)->ty.name.as_str())
                                        + "'");

                return tyir::make_ty_expr(
                    expr->span, tyir::TyFieldAccess{std::move(*base), node.field}, *field_ty);
            }

            // ── Call ──────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::CallExpr>) {
                // Cicest has no first-class functions; callee must be a plain
                // PathExpr resolving to a top-level function name.
                const ast::PathExpr* callee_path = std::get_if<ast::PathExpr>(&node.callee->node);
                if (callee_path == nullptr || callee_path->tail.has_value())
                    return make_error(expr->span, "call callee must be a function name");

                const cstc::symbol::Symbol fn_name = callee_path->head;
                if (!ctx.env.is_fn(fn_name))
                    return make_error(
                        expr->span, "undefined function '" + std::string(fn_name.as_str()) + "'");

                const FnSignature& sig = ctx.env.fn_signatures.at(fn_name);

                if (node.args.size() != sig.param_types.size())
                    return make_error(
                        expr->span, "function '" + std::string(fn_name.as_str()) + "' expects "
                                        + std::to_string(sig.param_types.size()) + " argument(s), "
                                        + std::to_string(node.args.size()) + " provided");

                std::vector<tyir::TyExprPtr> lowered_args;
                lowered_args.reserve(node.args.size());

                for (std::size_t i = 0; i < node.args.size(); ++i) {
                    auto arg = lower_expr(node.args[i], ctx);
                    if (!arg)
                        return std::unexpected(std::move(arg.error()));
                    if (!compatible((*arg)->ty, sig.param_types[i]))
                        return make_error(
                            node.args[i]->span, "argument " + std::to_string(i + 1) + " of '"
                                                    + std::string(fn_name.as_str())
                                                    + "': expected '" + sig.param_types[i].display()
                                                    + "', found '" + (*arg)->ty.display() + "'");
                    lowered_args.push_back(std::move(*arg));
                }

                return tyir::make_ty_expr(
                    expr->span, tyir::TyCall{fn_name, std::move(lowered_args)}, sig.return_ty);
            }

            // ── Block expression ──────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::BlockPtr>) {
                auto block = lower_block(*node, ctx);
                if (!block)
                    return std::unexpected(std::move(block.error()));
                tyir::Ty block_ty = block->ty;
                auto block_ptr = std::make_shared<tyir::TyBlock>(std::move(*block));
                return tyir::make_ty_expr(expr->span, std::move(block_ptr), block_ty);
            }

            // ── If ────────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::IfExpr>) {
                auto cond = lower_expr(node.condition, ctx);
                if (!cond)
                    return std::unexpected(std::move(cond.error()));
                if (!compatible((*cond)->ty, tyir::ty::bool_()))
                    return make_error(
                        node.condition->span, "'if' condition must have type 'bool', found '"
                                                  + (*cond)->ty.display() + "'");

                auto then_block_val = lower_block(*node.then_block, ctx);
                if (!then_block_val)
                    return std::unexpected(std::move(then_block_val.error()));

                auto then_ptr = std::make_shared<tyir::TyBlock>(std::move(*then_block_val));

                tyir::Ty result_ty = then_ptr->ty;

                std::optional<tyir::TyExprPtr> else_branch;
                if (node.else_branch.has_value()) {
                    auto else_val = lower_expr(*node.else_branch, ctx);
                    if (!else_val)
                        return std::unexpected(std::move(else_val.error()));

                    // Unify then / else types (Never unifies with anything)
                    const tyir::Ty& else_ty = (*else_val)->ty;
                    if (!compatible(then_ptr->ty, else_ty) && !compatible(else_ty, then_ptr->ty))
                        return make_error(
                            expr->span, "'if' then-branch has type '" + then_ptr->ty.display()
                                            + "' but else-branch has type '" + else_ty.display()
                                            + "'");

                    if (then_ptr->ty.is_never())
                        result_ty = else_ty;

                    else_branch = std::move(*else_val);
                } else {
                    // No else branch: result type is Unit
                    result_ty = tyir::ty::unit();
                }

                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyIf{std::move(*cond), std::move(then_ptr), std::move(else_branch)},
                    result_ty);
            }

            // ── Loop ──────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::LoopExpr>) {
                ctx.push_loop(LoopKind::Loop);
                auto body = lower_block(*node.body, ctx);
                if (!body) {
                    ctx.pop_loop();
                    return std::unexpected(std::move(body.error()));
                }
                // Infer loop type from accumulated break types:
                //   - no break seen  → Never (loop diverges)
                //   - bare break     → Unit
                //   - break expr     → expr type
                const tyir::Ty loop_ty = ctx.current_loop().break_ty.value_or(tyir::ty::never());
                ctx.pop_loop();
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));
                return tyir::make_ty_expr(expr->span, tyir::TyLoop{std::move(body_ptr)}, loop_ty);
            }

            // ── While ─────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::WhileExpr>) {
                ctx.push_loop(LoopKind::While);
                auto cond = lower_expr(node.condition, ctx);
                if (!cond) {
                    ctx.pop_loop();
                    return std::unexpected(std::move(cond.error()));
                }
                if (!compatible((*cond)->ty, tyir::ty::bool_())) {
                    ctx.pop_loop();
                    return make_error(
                        node.condition->span, "'while' condition must have type 'bool', found '"
                                                  + (*cond)->ty.display() + "'");
                }

                auto body = lower_block(*node.body, ctx);
                if (!body) {
                    ctx.pop_loop();
                    return std::unexpected(std::move(body.error()));
                }
                ctx.pop_loop();
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));

                return tyir::make_ty_expr(
                    expr->span, tyir::TyWhile{std::move(*cond), std::move(body_ptr)},
                    tyir::ty::unit());
            }

            // ── For ───────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::ForExpr>) {
                // The for-init introduces a new scope that outlives the header
                ctx.scope.push();
                ctx.push_loop(LoopKind::For);

                std::optional<tyir::TyForInit> lowered_init;

                if (node.init.has_value()) {
                    const auto& init_var = *node.init;
                    if (const auto* init_let = std::get_if<ast::ForInitLet>(&init_var)) {
                        auto init_expr = lower_expr(init_let->initializer, ctx);
                        if (!init_expr) {
                            ctx.pop_loop();
                            ctx.scope.pop();
                            return std::unexpected(std::move(init_expr.error()));
                        }
                        tyir::Ty init_ty;
                        if (init_let->type_annotation.has_value()) {
                            auto ann =
                                lower_type(*init_let->type_annotation, ctx.env, init_let->span);
                            if (!ann) {
                                ctx.pop_loop();
                                ctx.scope.pop();
                                return std::unexpected(std::move(ann.error()));
                            }
                            if (!compatible((*init_expr)->ty, *ann)) {
                                ctx.pop_loop();
                                ctx.scope.pop();
                                return make_error(
                                    init_let->span, "for-init type mismatch: expected '"
                                                        + ann->display() + "', found '"
                                                        + (*init_expr)->ty.display() + "'");
                            }
                            init_ty = *ann;
                        } else {
                            init_ty = (*init_expr)->ty;
                        }
                        if (!init_let->discard && init_let->name.is_valid())
                            ctx.scope.insert(init_let->name, init_ty);
                        lowered_init = tyir::TyForInit{
                            init_let->discard, init_let->name, init_ty, std::move(*init_expr),
                            init_let->span};
                    } else {
                        // Expression initializer
                        const auto& init_expr_ptr = std::get<ast::ExprPtr>(init_var);
                        auto init_expr = lower_expr(init_expr_ptr, ctx);
                        if (!init_expr) {
                            ctx.pop_loop();
                            ctx.scope.pop();
                            return std::unexpected(std::move(init_expr.error()));
                        }
                        // Treat as a discard init — wrap in TyForInit with discard=true
                        lowered_init = tyir::TyForInit{
                            true, cstc::symbol::kInvalidSymbol, (*init_expr)->ty,
                            std::move(*init_expr), init_expr_ptr->span};
                    }
                }

                std::optional<tyir::TyExprPtr> lowered_cond;
                if (node.condition.has_value()) {
                    auto cond = lower_expr(*node.condition, ctx);
                    if (!cond) {
                        ctx.pop_loop();
                        ctx.scope.pop();
                        return std::unexpected(std::move(cond.error()));
                    }
                    if (!compatible((*cond)->ty, tyir::ty::bool_())) {
                        ctx.pop_loop();
                        ctx.scope.pop();
                        return make_error(
                            (*node.condition)->span,
                            "'for' condition must have type 'bool', found '" + (*cond)->ty.display()
                                + "'");
                    }
                    lowered_cond = std::move(*cond);
                }

                std::optional<tyir::TyExprPtr> lowered_step;
                if (node.step.has_value()) {
                    auto step = lower_expr(*node.step, ctx);
                    if (!step) {
                        ctx.pop_loop();
                        ctx.scope.pop();
                        return std::unexpected(std::move(step.error()));
                    }
                    lowered_step = std::move(*step);
                }

                auto body = lower_block(*node.body, ctx);
                if (!body) {
                    ctx.pop_loop();
                    ctx.scope.pop();
                    return std::unexpected(std::move(body.error()));
                }
                ctx.pop_loop();
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));

                ctx.scope.pop();

                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyFor{
                        std::move(lowered_init), std::move(lowered_cond), std::move(lowered_step),
                        std::move(body_ptr)},
                    tyir::ty::unit());
            }

            // ── Break ─────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::BreakExpr>) {
                if (!ctx.in_loop())
                    return make_error(expr->span, "'break' outside of a loop");

                // Capture the loop kind by value before any recursive
                // lowering.  lower_expr() may push new entries onto
                // ctx.loop_stack (e.g. `break (loop { break 1; })`),
                // which can reallocate the vector and invalidate any
                // reference obtained from current_loop().
                const LoopKind break_loop_kind = ctx.current_loop().kind;

                if (node.value.has_value()) {
                    // break-with-value is only allowed inside `loop`
                    if (break_loop_kind != LoopKind::Loop)
                        return make_error(
                            expr->span, "'break' with a value is only allowed inside 'loop'");

                    auto val = lower_expr(*node.value, ctx);
                    if (!val)
                        return std::unexpected(std::move(val.error()));

                    // Re-acquire the reference after recursion.
                    auto& loop_ctx = ctx.current_loop();
                    const tyir::Ty& val_ty = (*val)->ty;
                    if (!loop_ctx.break_ty.has_value()) {
                        loop_ctx.break_ty = val_ty;
                    } else {
                        // Unify: Never is compatible with anything
                        const tyir::Ty& prev = *loop_ctx.break_ty;
                        if (prev.is_never()) {
                            loop_ctx.break_ty = val_ty;
                        } else if (!compatible(val_ty, prev)) {
                            return make_error(
                                expr->span, "'break' value type mismatch: expected '"
                                                + prev.display() + "', found '" + val_ty.display()
                                                + "'");
                        }
                    }

                    return tyir::make_ty_expr(
                        expr->span, tyir::TyBreak{std::move(*val)}, tyir::ty::never());
                }

                // Bare break — no recursive call, so a fresh reference is safe.
                auto& loop_ctx = ctx.current_loop();
                if (loop_ctx.kind == LoopKind::Loop) {
                    // Bare break in `loop` contributes Unit
                    if (!loop_ctx.break_ty.has_value()) {
                        loop_ctx.break_ty = tyir::ty::unit();
                    } else {
                        const tyir::Ty& prev = *loop_ctx.break_ty;
                        if (prev.is_never()) {
                            loop_ctx.break_ty = tyir::ty::unit();
                        } else if (!compatible(tyir::ty::unit(), prev)) {
                            return make_error(
                                expr->span, "'break' value type mismatch: expected '"
                                                + prev.display() + "', found 'Unit'");
                        }
                    }
                }
                // Bare break in while/for: no type tracking needed

                return tyir::make_ty_expr(
                    expr->span, tyir::TyBreak{std::nullopt}, tyir::ty::never());
            }

            // ── Continue ──────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::ContinueExpr>) {
                if (!ctx.in_loop())
                    return make_error(expr->span, "'continue' outside of a loop");
                return tyir::make_ty_expr(expr->span, tyir::TyContinue{}, tyir::ty::never());
            }

            // ── Return ────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::ReturnExpr>) {
                std::optional<tyir::TyExprPtr> lowered_val;
                if (node.value.has_value()) {
                    auto val = lower_expr(*node.value, ctx);
                    if (!val)
                        return std::unexpected(std::move(val.error()));
                    if (!compatible((*val)->ty, ctx.current_return_ty))
                        return make_error(
                            expr->span, "return type mismatch: expected '"
                                            + ctx.current_return_ty.display() + "', found '"
                                            + (*val)->ty.display() + "'");
                    lowered_val = std::move(*val);
                } else {
                    // Bare `return` — valid only in functions returning Unit
                    if (!compatible(tyir::ty::unit(), ctx.current_return_ty))
                        return make_error(
                            expr->span, "bare 'return' in function returning '"
                                            + ctx.current_return_ty.display() + "'");
                }
                return tyir::make_ty_expr(
                    expr->span, tyir::TyReturn{std::move(lowered_val)}, tyir::ty::never());
            }

            // Should be exhaustive — but keep the compiler happy
            return make_error(expr->span, "unsupported expression kind");
        },
        expr->node);
}

// ─── Item lowering ────────────────────────────────────────────────────────────

/// Lowers a function declaration using the fully-built `TypeEnv`.
[[nodiscard]] inline std::expected<tyir::TyFnDecl, LowerError>
    lower_fn(const ast::FnDecl& fn, const TypeEnv& env) {
    const FnSignature& sig = env.fn_signatures.at(fn.name);

    // Build typed param list
    std::vector<tyir::TyParam> ty_params;
    ty_params.reserve(fn.params.size());
    for (std::size_t i = 0; i < fn.params.size(); ++i)
        ty_params.push_back(
            tyir::TyParam{fn.params[i].name, sig.param_types[i], fn.params[i].span});

    // Set up lowering context with params in scope
    LowerCtx ctx{env, {}, sig.return_ty, {}};
    ctx.scope.push();
    for (const tyir::TyParam& p : ty_params)
        ctx.scope.insert(p.name, p.ty);

    auto body = lower_block(*fn.body, ctx);
    ctx.scope.pop();
    if (!body)
        return std::unexpected(std::move(body.error()));

    // Check that body type matches declared return type (when a tail is present)
    if (body->tail.has_value() && !compatible(body->ty, sig.return_ty))
        return make_error(
            fn.body->span, "function '" + std::string(fn.name.as_str()) + "' body has type '"
                               + body->ty.display() + "' but return type is '"
                               + sig.return_ty.display() + "'");

    // Non-Unit functions without a tail expression must not reach the end of
    // the body without returning.
    if (!body->tail.has_value() && block_can_fallthrough(*body)
        && !compatible(tyir::ty::unit(), sig.return_ty))
        return make_error(
            fn.body->span, "function '" + std::string(fn.name.as_str())
                               + "' may fall through without returning a value of type '"
                               + sig.return_ty.display() + "'");

    auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));

    return tyir::TyFnDecl{
        fn.name, std::move(ty_params), sig.return_ty, std::move(body_ptr), fn.span};
}

} // namespace detail

// ─── Public API ──────────────────────────────────────────────────────────────

inline std::expected<tyir::TyProgram, LowerError> lower_program(const ast::Program& program) {
    detail::TypeEnv env;

    // ── Phase 1: collect named-type names (placeholders) ─────────────────
    for (const ast::Item& item : program.items) {
        if (const auto* struct_decl = std::get_if<ast::StructDecl>(&item)) {
            if (env.enum_variants.count(struct_decl->name) > 0
                || env.extern_struct_names.count(struct_decl->name) > 0)
                return detail::make_error(
                    struct_decl->span,
                    "duplicate struct name '" + std::string(struct_decl->name.as_str()) + "'");

            const auto insert_result =
                env.struct_fields.emplace(struct_decl->name, std::vector<tyir::TyFieldDecl>{});
            if (!insert_result.second)
                return detail::make_error(
                    struct_decl->span,
                    "duplicate struct name '" + std::string(struct_decl->name.as_str()) + "'");
        } else if (const auto* enum_decl = std::get_if<ast::EnumDecl>(&item)) {
            if (env.struct_fields.count(enum_decl->name) > 0
                || env.extern_struct_names.count(enum_decl->name) > 0)
                return detail::make_error(
                    enum_decl->span,
                    "duplicate enum name '" + std::string(enum_decl->name.as_str()) + "'");

            const auto insert_result =
                env.enum_variants.emplace(enum_decl->name, std::vector<tyir::TyEnumVariant>{});
            if (!insert_result.second)
                return detail::make_error(
                    enum_decl->span,
                    "duplicate enum name '" + std::string(enum_decl->name.as_str()) + "'");
        } else if (const auto* extern_struct = std::get_if<ast::ExternStructDecl>(&item)) {
            // Validate the ABI string.
            if (auto err = detail::validate_abi(extern_struct->abi, extern_struct->span))
                return *err;
            if (env.enum_variants.count(extern_struct->name) > 0
                || env.struct_fields.count(extern_struct->name) > 0
                || env.extern_struct_names.count(extern_struct->name) > 0)
                return detail::make_error(
                    extern_struct->span,
                    "duplicate type name '" + std::string(extern_struct->name.as_str()) + "'");

            env.extern_struct_names.insert(extern_struct->name);
        }
    }

    // ── Phase 2: resolve struct fields and enum variants ──────────────────
    for (const ast::Item& item : program.items) {
        if (const auto* s = std::get_if<ast::StructDecl>(&item)) {
            auto& fields = env.struct_fields.at(s->name);
            for (const ast::FieldDecl& f : s->fields) {
                auto ty = detail::lower_type(f.type, env, f.span);
                if (!ty)
                    return std::unexpected(std::move(ty.error()));
                fields.push_back(tyir::TyFieldDecl{f.name, *ty, f.span});
            }
        } else if (const auto* e = std::get_if<ast::EnumDecl>(&item)) {
            auto& variants = env.enum_variants.at(e->name);
            for (const ast::EnumVariant& v : e->variants)
                variants.push_back(tyir::TyEnumVariant{v.name, v.discriminant, v.span});
        }
    }

    // ── Phase 3: resolve function signatures ─────────────────────────────
    for (const ast::Item& item : program.items) {
        if (const auto* fn = std::get_if<ast::FnDecl>(&item)) {
            auto sig = detail::resolve_fn_signature(fn->params, fn->return_type, fn->span, env);
            if (!sig)
                return std::unexpected(std::move(sig.error()));
            const auto insert_result = env.fn_signatures.emplace(fn->name, std::move(*sig));
            if (!insert_result.second)
                return detail::make_error(
                    fn->span, "duplicate function name '" + std::string(fn->name.as_str()) + "'");
        } else if (const auto* ext_fn = std::get_if<ast::ExternFnDecl>(&item)) {
            // Validate the ABI string.
            if (auto err = detail::validate_abi(ext_fn->abi, ext_fn->span))
                return *err;
            auto link_name = detail::resolve_extern_link_name(*ext_fn);
            if (!link_name)
                return std::unexpected(std::move(link_name.error()));
            // Build a signature from the extern fn declaration.
            auto sig = detail::resolve_fn_signature(
                ext_fn->params, ext_fn->return_type, ext_fn->span, env);
            if (!sig)
                return std::unexpected(std::move(sig.error()));
            const auto insert_result = env.fn_signatures.emplace(ext_fn->name, std::move(*sig));
            if (!insert_result.second)
                return detail::make_error(
                    ext_fn->span,
                    "duplicate function name '" + std::string(ext_fn->name.as_str()) + "'");
        }
    }

    // ── Phase 3.5: validate main return type ─────────────────────────────
    {
        const auto main_sym = cstc::symbol::Symbol::intern("main");
        const auto it = env.fn_signatures.find(main_sym);
        if (it != env.fn_signatures.end()) {
            const auto& ret = it->second.return_ty;
            if (ret.kind != tyir::TyKind::Unit && ret.kind != tyir::TyKind::Num
                && ret.kind != tyir::TyKind::Never) {
                return detail::make_error(
                    it->second.span,
                    "'main' function must return 'Unit', 'num', or '!' (never), found '"
                        + ret.display() + "'");
            }
        }
    }

    // ── Phase 4: lower all items in source order ──────────────────────────
    tyir::TyProgram result;
    for (const ast::Item& item : program.items) {
        if (const auto* s = std::get_if<ast::StructDecl>(&item)) {
            tyir::TyStructDecl ty_decl;
            ty_decl.name = s->name;
            ty_decl.is_zst = s->is_zst;
            ty_decl.span = s->span;
            ty_decl.fields = env.struct_fields.at(s->name);
            result.items.push_back(std::move(ty_decl));
        } else if (const auto* e = std::get_if<ast::EnumDecl>(&item)) {
            tyir::TyEnumDecl ty_decl;
            ty_decl.name = e->name;
            ty_decl.span = e->span;
            ty_decl.variants = env.enum_variants.at(e->name);
            result.items.push_back(std::move(ty_decl));
        } else if (const auto* fn = std::get_if<ast::FnDecl>(&item)) {
            auto fn_result = detail::lower_fn(*fn, env);
            if (!fn_result)
                return std::unexpected(std::move(fn_result.error()));
            result.items.push_back(std::move(*fn_result));
        } else if (const auto* ext_fn = std::get_if<ast::ExternFnDecl>(&item)) {
            const auto& sig = env.fn_signatures.at(ext_fn->name);
            auto link_name = detail::resolve_extern_link_name(*ext_fn);
            if (!link_name)
                return std::unexpected(std::move(link_name.error()));
            tyir::TyExternFnDecl ty_decl;
            ty_decl.abi = ext_fn->abi;
            ty_decl.name = ext_fn->name;
            ty_decl.link_name = *link_name;
            ty_decl.return_ty = sig.return_ty;
            ty_decl.span = ext_fn->span;
            for (std::size_t i = 0; i < ext_fn->params.size(); ++i) {
                ty_decl.params.push_back(
                    tyir::TyParam{
                        .name = ext_fn->params[i].name,
                        .ty = sig.param_types[i],
                        .span = ext_fn->params[i].span,
                    });
            }
            result.items.push_back(std::move(ty_decl));
        } else if (const auto* ext_struct = std::get_if<ast::ExternStructDecl>(&item)) {
            tyir::TyExternStructDecl ty_decl;
            ty_decl.abi = ext_struct->abi;
            ty_decl.name = ext_struct->name;
            ty_decl.span = ext_struct->span;
            result.items.push_back(std::move(ty_decl));
        }
    }

    return result;
}

} // namespace cstc::tyir_builder

#endif // CICEST_COMPILER_CSTC_TYIR_BUILDER_BUILDER_HPP
