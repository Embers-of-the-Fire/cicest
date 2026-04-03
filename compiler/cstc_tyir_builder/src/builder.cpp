#include <cstc_tyir_builder/builder.hpp>

#include <algorithm>
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
    std::vector<cstc::ast::GenericParam> generic_params;
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

    /// Declared generic arity for each named type.
    std::unordered_map<cstc::symbol::Symbol, std::size_t, cstc::symbol::SymbolHash>
        type_generic_arity;

    /// Declared generic parameters for each named type.
    std::unordered_map<
        cstc::symbol::Symbol, std::vector<cstc::ast::GenericParam>, cstc::symbol::SymbolHash>
        type_generic_params;

    /// Names of extern (opaque) struct types. These are valid as type
    /// annotations but cannot be constructed via struct-init expressions.
    std::unordered_set<cstc::symbol::Symbol, cstc::symbol::SymbolHash> extern_struct_names;

    /// Cached ownership classification for named type instantiations after field resolution.
    mutable std::unordered_map<std::string, tyir::ValueSemantics> named_semantics_cache;
    /// Recursion guard used while classifying aggregate types.
    mutable std::unordered_set<std::string> named_semantics_in_progress;

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

using GenericParamSet = std::unordered_set<cstc::symbol::Symbol, cstc::symbol::SymbolHash>;
using TypeSubstitution =
    std::unordered_map<cstc::symbol::Symbol, tyir::Ty, cstc::symbol::SymbolHash>;

[[nodiscard]] static std::unexpected<LowerError>
    make_error(cstc::span::SourceSpan span, std::string msg);

[[nodiscard]] static GenericParamSet
    make_generic_param_set(const std::vector<cstc::ast::GenericParam>& generic_params) {
    GenericParamSet params;
    params.reserve(generic_params.size());
    for (const cstc::ast::GenericParam& param : generic_params)
        params.insert(param.name);
    return params;
}

[[nodiscard]] static bool
    is_generic_param_ty(const tyir::Ty& ty, const GenericParamSet& generic_params) {
    return ty.kind == tyir::TyKind::Named && ty.generic_args.empty()
        && generic_params.contains(ty.name);
}

[[nodiscard]] static tyir::Ty
    apply_substitution(const tyir::Ty& ty, const TypeSubstitution& subst) {
    if (ty.kind == tyir::TyKind::Ref) {
        if (ty.pointee == nullptr)
            return ty;
        tyir::Ty rewritten = ty;
        rewritten.pointee = std::make_shared<tyir::Ty>(apply_substitution(*ty.pointee, subst));
        return rewritten;
    }
    if (ty.kind != tyir::TyKind::Named)
        return ty;

    if (ty.generic_args.empty()) {
        const auto it = subst.find(ty.name);
        if (it != subst.end()) {
            tyir::Ty rewritten = it->second;
            rewritten.is_runtime = rewritten.is_runtime || ty.is_runtime;
            if (!rewritten.display_name.is_valid())
                rewritten.display_name = ty.display_name;
            return rewritten;
        }
    }

    tyir::Ty rewritten = ty;
    rewritten.generic_args.clear();
    rewritten.generic_args.reserve(ty.generic_args.size());
    for (const tyir::Ty& arg : ty.generic_args)
        rewritten.generic_args.push_back(apply_substitution(arg, subst));
    return rewritten;
}

[[nodiscard]] static std::string ownership_cache_key(const tyir::Ty& ty) {
    switch (ty.kind) {
    case tyir::TyKind::Ref:
        return ty.pointee != nullptr ? "&" + ownership_cache_key(*ty.pointee) : "&<unknown>";
    case tyir::TyKind::Unit: return "Unit";
    case tyir::TyKind::Num: return "num";
    case tyir::TyKind::Str: return "str";
    case tyir::TyKind::Bool: return "bool";
    case tyir::TyKind::Never: return "!";
    case tyir::TyKind::Named: {
        std::string key = "N:" + std::string(ty.name.as_str());
        if (!ty.generic_args.empty()) {
            key += "<";
            for (std::size_t index = 0; index < ty.generic_args.size(); ++index) {
                if (index > 0)
                    key += ",";
                key += ownership_cache_key(ty.generic_args[index]);
            }
            key += ">";
        }
        return key;
    }
    }
    return "<unknown>";
}

[[nodiscard]] static tyir::Ty annotate_type_semantics(tyir::Ty ty, const TypeEnv& env);

[[nodiscard]] static std::expected<TypeSubstitution, LowerError> build_substitution(
    const std::vector<cstc::ast::GenericParam>& generic_params,
    const std::vector<tyir::Ty>& generic_args, cstc::span::SourceSpan span,
    std::string_view owner_kind, std::string_view owner_name) {
    if (generic_params.size() != generic_args.size()) {
        return make_error(
            span, std::string(owner_kind) + " '" + std::string(owner_name) + "' expects "
                      + std::to_string(generic_params.size()) + " generic argument(s), "
                      + std::to_string(generic_args.size()) + " provided");
    }

    TypeSubstitution subst;
    subst.reserve(generic_params.size());
    for (std::size_t index = 0; index < generic_params.size(); ++index)
        subst.emplace(generic_params[index].name, generic_args[index]);
    return subst;
}

[[nodiscard]] static std::expected<void, LowerError> infer_substitution(
    const tyir::Ty& pattern, const tyir::Ty& actual, const GenericParamSet& generic_params,
    TypeSubstitution& subst, cstc::span::SourceSpan span, std::string_view owner_name) {
    if (is_generic_param_ty(pattern, generic_params)) {
        const auto found = subst.find(pattern.name);
        if (found == subst.end()) {
            subst.emplace(pattern.name, actual);
            return {};
        }
        if (found->second == actual)
            return {};
        return make_error(
            span, "conflicting inferred types for generic parameter '"
                      + std::string(pattern.name.as_str()) + "' in '" + std::string(owner_name)
                      + "'");
    }

    if (pattern.kind != actual.kind)
        return {};
    if (pattern.kind == tyir::TyKind::Ref) {
        if (pattern.pointee != nullptr && actual.pointee != nullptr)
            return infer_substitution(
                *pattern.pointee, *actual.pointee, generic_params, subst, span, owner_name);
        return {};
    }
    if (pattern.kind != tyir::TyKind::Named || pattern.name != actual.name
        || pattern.generic_args.size() != actual.generic_args.size()) {
        return {};
    }
    for (std::size_t index = 0; index < pattern.generic_args.size(); ++index) {
        auto result = infer_substitution(
            pattern.generic_args[index], actual.generic_args[index], generic_params, subst, span,
            owner_name);
        if (!result)
            return result;
    }
    return {};
}

// ─── Scope (local variable environment) ─────────────────────────────────────

/// Scoped stack of local variable type bindings.
///
/// A new stack frame is pushed on entry to every block or `for`-init scope and
/// popped on exit.
class Scope {
public:
    struct LocalState {
        tyir::Ty ty;
        bool moved = false;
        std::size_t active_borrows = 0;
        std::optional<std::size_t> borrowed_local;
    };

    void push() { frames_.push_back(Frame{{}, locals_.size()}); }
    void pop() {
        assert(!frames_.empty());
        while (locals_.size() > frames_.back().start_local_count) {
            LocalState& local = locals_.back();
            if (local.borrowed_local.has_value()) {
                LocalState& owner = locals_.at(*local.borrowed_local);
                assert(owner.active_borrows > 0);
                owner.active_borrows -= 1;
            }
            locals_.pop_back();
        }
        frames_.pop_back();
    }

    [[nodiscard]] bool insert(
        cstc::symbol::Symbol name, tyir::Ty ty,
        std::optional<std::size_t> borrowed_local = std::nullopt) {
        assert(!frames_.empty());
        if (frames_.back().bindings.contains(name))
            return false;

        if (borrowed_local.has_value())
            locals_.at(*borrowed_local).active_borrows += 1;

        const std::size_t index = locals_.size();
        locals_.push_back(LocalState{std::move(ty), false, 0, borrowed_local});
        frames_.back().bindings.emplace(name, index);
        return true;
    }

    [[nodiscard]] std::optional<std::size_t> lookup_local(cstc::symbol::Symbol name) const {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            const auto found = it->bindings.find(name);
            if (found != it->bindings.end())
                return found->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] LocalState& local(std::size_t index) { return locals_.at(index); }
    [[nodiscard]] const LocalState& local(std::size_t index) const { return locals_.at(index); }
    [[nodiscard]] std::size_t locals_size() const { return locals_.size(); }

    void add_ephemeral_borrow(std::size_t index) { locals_.at(index).active_borrows += 1; }
    void release_ephemeral_borrow(std::size_t index) {
        LocalState& local = locals_.at(index);
        assert(local.active_borrows > 0);
        local.active_borrows -= 1;
    }

    void merge_from(const Scope& other) {
        assert(locals_.size() == other.locals_.size());
        for (std::size_t i = 0; i < locals_.size(); ++i) {
            locals_[i].moved = locals_[i].moved || other.locals_[i].moved;
            locals_[i].active_borrows =
                std::max(locals_[i].active_borrows, other.locals_[i].active_borrows);
        }
    }

private:
    struct Frame {
        std::unordered_map<cstc::symbol::Symbol, std::size_t, cstc::symbol::SymbolHash> bindings;
        std::size_t start_local_count = 0;
    };

    std::vector<Frame> frames_;
    std::vector<LocalState> locals_;
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

/// Returns true when two types share the same non-`runtime` shape.
[[nodiscard]] static bool same_type_shape(const tyir::Ty& lhs, const tyir::Ty& rhs) {
    return lhs.same_shape_as(rhs);
}

/// Returns true when `actual` may appear where `expected` is required.
///
/// `Never` (bottom type) is compatible with any expected type.  Outside of
/// `Never`, the only implicit conversion is `T -> runtime T`, applied
/// structurally to the type tree.
[[nodiscard]] static bool compatible(const tyir::Ty& actual, const tyir::Ty& expected) {
    if (actual.is_never())
        return true;
    if (!same_type_shape(actual, expected))
        return false;
    if (actual.is_runtime && !expected.is_runtime)
        return false;
    if (actual.kind != tyir::TyKind::Ref)
        return true;
    if (actual.pointee == nullptr || expected.pointee == nullptr)
        return actual.pointee == expected.pointee;
    return compatible(*actual.pointee, *expected.pointee);
}

/// Returns true when `actual` matches the structural shape expected by an
/// ordinary expression operator or condition.
///
/// Unlike `compatible`, this ignores the `runtime` qualifier and therefore
/// does not permit expression checks to double as coercion sites.
[[nodiscard]] static bool matches_type_shape(const tyir::Ty& actual, const tyir::Ty& expected) {
    return actual.is_never() || same_type_shape(actual, expected);
}

/// Returns the least common supertype of `lhs` and `rhs`, if one exists.
///
/// The runtime qualifier joins by promotion: mixing `T` with `runtime T`
/// yields `runtime T`.
[[nodiscard]] static std::optional<tyir::Ty> common_type(const tyir::Ty& lhs, const tyir::Ty& rhs) {
    if (lhs.is_never())
        return rhs;
    if (rhs.is_never())
        return lhs;
    if (!same_type_shape(lhs, rhs))
        return std::nullopt;

    tyir::Ty joined = lhs;
    joined.is_runtime = lhs.is_runtime || rhs.is_runtime;
    if (!joined.display_name.is_valid())
        joined.display_name = rhs.display_name;

    if (joined.kind == tyir::TyKind::Ref) {
        if (lhs.pointee == nullptr || rhs.pointee == nullptr) {
            if (lhs.pointee != rhs.pointee)
                return std::nullopt;
        } else {
            auto pointee = common_type(*lhs.pointee, *rhs.pointee);
            if (!pointee.has_value())
                return std::nullopt;
            joined.pointee = std::make_shared<tyir::Ty>(std::move(*pointee));
        }
    }

    return joined;
}

[[nodiscard]] static tyir::Ty unary_result_type(const tyir::Ty& operand, tyir::Ty result_shape) {
    if (operand.is_never())
        return tyir::ty::never();
    result_shape.is_runtime = operand.is_runtime;
    return result_shape;
}

[[nodiscard]] static tyir::Ty
    binary_result_type(const tyir::Ty& lhs, const tyir::Ty& rhs, tyir::Ty result_shape) {
    if (lhs.is_never() || rhs.is_never())
        return tyir::ty::never();
    result_shape.is_runtime = lhs.is_runtime || rhs.is_runtime;
    return result_shape;
}

[[nodiscard]] static tyir::Ty propagate_runtime_tag(tyir::Ty ty, bool inherited_runtime) {
    ty.is_runtime = ty.is_runtime || inherited_runtime;
    return ty;
}

// ─── Error helpers ────────────────────────────────────────────────────────────

[[nodiscard]] static std::unexpected<LowerError>
    make_error(cstc::span::SourceSpan span, std::string msg) {
    return std::unexpected(LowerError{span, std::move(msg)});
}

[[nodiscard]] static std::string
    display_symbol(cstc::symbol::Symbol display_name, cstc::symbol::Symbol fallback) {
    if (display_name.is_valid())
        return std::string(display_name.as_str());
    if (fallback.is_valid())
        return std::string(fallback.as_str());
    return "<invalid-symbol>";
}

[[nodiscard]] static cstc::symbol::Symbol
    source_symbol(cstc::symbol::Symbol display_name, cstc::symbol::Symbol fallback) {
    if (display_name.is_valid())
        return display_name;
    return fallback;
}

template <typename Decl>
[[nodiscard]] static std::string display_decl_name(const Decl& decl) {
    return display_symbol(decl.display_name, decl.name);
}

[[nodiscard]] static tyir::ValueSemantics primitive_semantics(tyir::TyKind kind) {
    switch (kind) {
    case tyir::TyKind::Ref: return tyir::ValueSemantics::Ref;
    case tyir::TyKind::Str: return tyir::ValueSemantics::Move;
    case tyir::TyKind::Unit:
    case tyir::TyKind::Num:
    case tyir::TyKind::Bool:
    case tyir::TyKind::Never: return tyir::ValueSemantics::Copy;
    case tyir::TyKind::Named: return tyir::ValueSemantics::Move;
    }
    return tyir::ValueSemantics::Move;
}

[[nodiscard]] static tyir::ValueSemantics named_semantics(const tyir::Ty& ty, const TypeEnv& env) {
    assert(ty.kind == tyir::TyKind::Named);

    const cstc::symbol::Symbol name = ty.name;
    const std::string cache_key = ownership_cache_key(ty);
    const auto cached = env.named_semantics_cache.find(cache_key);
    if (cached != env.named_semantics_cache.end())
        return cached->second;

    if (env.is_enum(name)) {
        env.named_semantics_cache.emplace(cache_key, tyir::ValueSemantics::Copy);
        return tyir::ValueSemantics::Copy;
    }
    if (env.is_extern_struct(name)) {
        env.named_semantics_cache.emplace(cache_key, tyir::ValueSemantics::Move);
        return tyir::ValueSemantics::Move;
    }

    if (!env.named_semantics_in_progress.insert(cache_key).second)
        return tyir::ValueSemantics::Move;

    tyir::ValueSemantics semantics = tyir::ValueSemantics::Copy;
    const auto it = env.struct_fields.find(name);
    if (it != env.struct_fields.end()) {
        TypeSubstitution substitution;
        if (!ty.generic_args.empty()) {
            const auto generic_params_it = env.type_generic_params.find(name);
            assert(generic_params_it != env.type_generic_params.end());
            assert(generic_params_it->second.size() == ty.generic_args.size());
            substitution.reserve(ty.generic_args.size());
            for (std::size_t index = 0; index < ty.generic_args.size(); ++index) {
                substitution.emplace(generic_params_it->second[index].name, ty.generic_args[index]);
            }
        }

        for (const tyir::TyFieldDecl& field : it->second) {
            tyir::Ty field_ty = field.ty;
            if (!substitution.empty())
                field_ty = apply_substitution(field_ty, substitution);
            field_ty = annotate_type_semantics(std::move(field_ty), env);

            if (field_ty.is_move_only()) {
                semantics = tyir::ValueSemantics::Move;
                break;
            }
        }
    }

    env.named_semantics_in_progress.erase(cache_key);
    env.named_semantics_cache[cache_key] = semantics;
    return semantics;
}

[[nodiscard]] static tyir::Ty annotate_type_semantics(tyir::Ty ty, const TypeEnv& env) {
    switch (ty.kind) {
    case tyir::TyKind::Ref:
        if (ty.pointee != nullptr)
            ty.pointee = std::make_shared<tyir::Ty>(annotate_type_semantics(*ty.pointee, env));
        ty.semantics = tyir::ValueSemantics::Ref;
        return ty;
    case tyir::TyKind::Named:
        for (tyir::Ty& generic_arg : ty.generic_args)
            generic_arg = annotate_type_semantics(std::move(generic_arg), env);
        ty.semantics = named_semantics(ty, env);
        return ty;
    case tyir::TyKind::Unit:
    case tyir::TyKind::Num:
    case tyir::TyKind::Str:
    case tyir::TyKind::Bool:
    case tyir::TyKind::Never: ty.semantics = primitive_semantics(ty.kind); return ty;
    }
    return ty;
}

/// Returns true if the ABI string is a recognised extern ABI.
[[nodiscard]] static bool is_supported_abi(cstc::symbol::Symbol abi) {
    const auto sv = abi.as_str();
    return sv == "lang" || sv == "c";
}

/// Validates an extern ABI string, returning an error if unsupported.
[[nodiscard]] static std::optional<std::unexpected<LowerError>>
    validate_abi(cstc::symbol::Symbol abi, cstc::span::SourceSpan span) {
    if (!is_supported_abi(abi))
        return make_error(span, "unsupported ABI \"" + std::string(abi.as_str()) + "\"");
    return std::nullopt;
}

/// Resolves the linked symbol name for an extern function declaration.
[[nodiscard]] static std::expected<cstc::symbol::Symbol, LowerError>
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

    return link_name.value_or(source_symbol(decl.display_name, decl.name));
}

// ─── Type resolution ─────────────────────────────────────────────────────────

/// Converts an AST `TypeRef` to a `tyir::Ty` shape, validating named types
/// against the type environment.
[[nodiscard]] static std::expected<tyir::Ty, LowerError> lower_type_shape(
    const ast::TypeRef& ref, const TypeEnv& env, cstc::span::SourceSpan span,
    const GenericParamSet& generic_params = {}) {
    switch (ref.kind) {
    case ast::TypeKind::Ref:
        if (ref.pointee == nullptr)
            return make_error(span, "invalid reference type");
        {
            auto pointee = lower_type_shape(*ref.pointee, env, span, generic_params);
            if (!pointee)
                return std::unexpected(std::move(pointee.error()));
            return tyir::ty::ref(*pointee, ref.is_runtime);
        }
    case ast::TypeKind::Unit: return tyir::ty::unit(ref.is_runtime);
    case ast::TypeKind::Num: return tyir::ty::num(ref.is_runtime);
    case ast::TypeKind::Str: return tyir::ty::str(ref.is_runtime);
    case ast::TypeKind::Bool: return tyir::ty::bool_(ref.is_runtime);
    case ast::TypeKind::Never: return tyir::ty::never(ref.is_runtime);
    case ast::TypeKind::Named:
        if (!ref.symbol.is_valid())
            return make_error(span, "invalid named type reference");
        if (generic_params.contains(ref.symbol)) {
            if (!ref.generic_args.empty()) {
                return make_error(
                    span, "generic parameter '" + display_symbol(ref.display_name, ref.symbol)
                              + "' cannot take generic arguments");
            }
            return tyir::ty::named(
                ref.symbol, ref.display_name, tyir::ValueSemantics::Move, ref.is_runtime);
        }
        if (!env.is_named_type(ref.symbol))
            return make_error(
                span, "undefined type '" + display_symbol(ref.display_name, ref.symbol) + "'");
        const std::size_t expected_arity =
            env.type_generic_arity.contains(ref.symbol) ? env.type_generic_arity.at(ref.symbol) : 0;
        if (ref.generic_args.size() != expected_arity) {
            return make_error(
                span, "type '" + display_symbol(ref.display_name, ref.symbol) + "' expects "
                          + std::to_string(expected_arity) + " generic argument(s), "
                          + std::to_string(ref.generic_args.size()) + " provided");
        }
        std::vector<tyir::Ty> lowered_generic_args;
        lowered_generic_args.reserve(ref.generic_args.size());
        for (const ast::TypeRef& generic_arg : ref.generic_args) {
            auto lowered_arg = lower_type_shape(generic_arg, env, span, generic_params);
            if (!lowered_arg)
                return std::unexpected(std::move(lowered_arg.error()));
            lowered_generic_args.push_back(std::move(*lowered_arg));
        }
        return tyir::ty::named(
            ref.symbol, ref.display_name, tyir::ValueSemantics::Move, ref.is_runtime,
            std::move(lowered_generic_args));
    }
    assert(false && "unhandled ast::TypeKind in lower_type_shape");
    __builtin_unreachable();
}

[[nodiscard]] static std::expected<tyir::Ty, LowerError> lower_type(
    const ast::TypeRef& ref, const TypeEnv& env, cstc::span::SourceSpan span,
    const GenericParamSet& generic_params = {}) {
    auto ty = lower_type_shape(ref, env, span, generic_params);
    if (!ty)
        return std::unexpected(std::move(ty.error()));
    return annotate_type_semantics(std::move(*ty), env);
}

static void finalize_env_type_semantics(TypeEnv& env) {
    env.named_semantics_cache.clear();
    env.named_semantics_in_progress.clear();
    for (auto& [_, fields] : env.struct_fields) {
        for (tyir::TyFieldDecl& field : fields)
            field.ty = annotate_type_semantics(field.ty, env);
    }
}

// ─── Forward declarations ────────────────────────────────────────────────────

struct LoweredExpr {
    tyir::TyExprPtr expr;
    std::vector<std::size_t> temp_borrows;
    std::optional<std::size_t> persistent_borrow_owner;
};

struct LoweredPlace {
    tyir::TyExprPtr expr;
    std::optional<std::size_t> owner_local;
};

[[nodiscard]] static std::expected<LoweredExpr, LowerError>
    lower_expr(const ast::ExprPtr& expr, LowerCtx& ctx);

[[nodiscard]] static std::expected<LoweredPlace, LowerError>
    lower_place_expr(const ast::ExprPtr& expr, LowerCtx& ctx);

[[nodiscard]] static std::expected<tyir::TyBlock, LowerError>
    lower_block(const ast::BlockExpr& block, LowerCtx& ctx);

// ─── Function signature resolution ──────────────────────────────────────────

[[nodiscard]] static std::expected<FnSignature, LowerError> resolve_fn_signature(
    const std::vector<ast::Param>& params, const std::optional<ast::TypeRef>& return_type,
    cstc::span::SourceSpan span, const TypeEnv& env, bool is_runtime,
    const std::vector<ast::GenericParam>& generic_params = {}) {
    FnSignature sig;
    sig.span = span;
    sig.generic_params = generic_params;
    const GenericParamSet generic_param_set = make_generic_param_set(generic_params);
    if (return_type.has_value()) {
        auto t = lower_type(*return_type, env, span, generic_param_set);
        if (!t)
            return std::unexpected(std::move(t.error()));
        if (is_runtime)
            t->is_runtime = true;
        if (t->is_ref())
            return make_error(span, "reference return types are not supported");
        sig.return_ty = *t;
    } else {
        sig.return_ty = tyir::ty::unit(is_runtime);
    }
    for (const ast::Param& p : params) {
        auto pt = lower_type(p.type, env, p.span, generic_param_set);
        if (!pt)
            return std::unexpected(std::move(pt.error()));
        sig.param_types.push_back(*pt);
    }
    return sig;
}

static void append_temp_borrows(std::vector<std::size_t>& into, std::vector<std::size_t> from) {
    into.insert(into.end(), from.begin(), from.end());
}

static void release_temp_borrows(LowerCtx& ctx, const std::vector<std::size_t>& borrows) {
    for (const std::size_t index : borrows)
        ctx.scope.release_ephemeral_borrow(index);
}

static void consume_temp_borrow(std::vector<std::size_t>& borrows, std::size_t owner_local) {
    const auto it = std::find(borrows.begin(), borrows.end(), owner_local);
    if (it != borrows.end())
        borrows.erase(it);
}

// ─── Statement lowering ──────────────────────────────────────────────────────

[[nodiscard]] static std::expected<tyir::TyStmt, LowerError>
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
                    if (!compatible(init->expr->ty, *ann))
                        return make_error(
                            s.span, "type mismatch in let binding: expected '" + ann->display()
                                        + "', found '" + init->expr->ty.display() + "'");
                    binding_ty = *ann;
                } else {
                    binding_ty = init->expr->ty;
                }

                // Register binding in scope (unless discard pattern)
                std::optional<std::size_t> borrowed_local;
                if (binding_ty.is_ref() && init->persistent_borrow_owner.has_value()) {
                    borrowed_local = init->persistent_borrow_owner;
                    consume_temp_borrow(init->temp_borrows, borrowed_local.value());
                }
                if (binding_ty.is_ref() && !init->temp_borrows.empty()
                    && !init->persistent_borrow_owner.has_value()) {
                    return make_error(
                        s.span,
                        "this reference value cannot be stored in a local yet; bind a direct "
                        "borrow instead");
                }
                if (!s.discard && s.name.is_valid()
                    && !ctx.scope.insert(s.name, binding_ty, borrowed_local))
                    return make_error(
                        s.span, "duplicate local binding '" + std::string(s.name.as_str()) + "'");

                release_temp_borrows(ctx, init->temp_borrows);

                return tyir::TyLetStmt{
                    s.discard, s.name, binding_ty, std::move(init->expr), s.span};

            } else {                                          // ast::ExprStmt
                auto expr = lower_expr(s.expr, ctx);
                if (!expr)
                    return std::unexpected(std::move(expr.error()));
                release_temp_borrows(ctx, expr->temp_borrows);
                return tyir::TyExprStmt{std::move(expr->expr), s.span};
            }
        },
        stmt);
}

[[nodiscard]] static bool expr_can_fallthrough(const tyir::TyExpr& expr);

[[nodiscard]] static bool stmt_can_fallthrough(const tyir::TyStmt& stmt) {
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

[[nodiscard]] static bool block_can_fallthrough(const tyir::TyBlock& block);

[[nodiscard]] static bool expr_can_fallthrough(const tyir::TyExpr& expr) {
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
            } else if constexpr (
                std::is_same_v<N, tyir::TyBorrow> || std::is_same_v<N, tyir::TyUnary>) {
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

[[nodiscard]] static bool block_can_fallthrough(const tyir::TyBlock& block) {
    for (const tyir::TyStmt& stmt : block.stmts) {
        if (!stmt_can_fallthrough(stmt))
            return false;
    }
    if (!block.tail.has_value())
        return true;
    return expr_can_fallthrough(**block.tail);
}

// ─── Block lowering ──────────────────────────────────────────────────────────

[[nodiscard]] static std::expected<tyir::TyBlock, LowerError>
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
        if (tail->expr->ty.is_ref()) {
            release_temp_borrows(ctx, tail->temp_borrows);
            ctx.scope.pop();
            return make_error(block.span, "block expressions cannot yield references yet");
        }
        release_temp_borrows(ctx, tail->temp_borrows);
        result.tail = std::move(tail->expr);
        result.ty = reaches_tail ? (*result.tail)->ty : tyir::ty::never();
    } else {
        result.ty = reaches_tail ? tyir::ty::unit() : tyir::ty::never();
    }

    ctx.scope.pop();
    return result;
}

// ─── Expression lowering ─────────────────────────────────────────────────────

static std::expected<void, LowerError> merge_loop_break_types(
    std::vector<LoopCtx>& target, const std::vector<LoopCtx>& source, cstc::span::SourceSpan span) {
    assert(target.size() == source.size());
    for (std::size_t i = 0; i < target.size(); ++i) {
        if (!source[i].break_ty.has_value())
            continue;
        if (!target[i].break_ty.has_value()) {
            target[i].break_ty = source[i].break_ty;
            continue;
        }

        const tyir::Ty& existing = *target[i].break_ty;
        const tyir::Ty& incoming = *source[i].break_ty;
        const auto joined = common_type(existing, incoming);
        if (!joined.has_value())
            return make_error(
                span, "'break' value type mismatch: expected '" + existing.display() + "', found '"
                          + incoming.display() + "'");
        target[i].break_ty = *joined;
    }
    return {};
}

[[nodiscard]] static std::expected<LoweredPlace, LowerError>
    lower_place_expr(const ast::ExprPtr& expr, LowerCtx& ctx) {
    assert(expr != nullptr);

    return std::visit(
        [&](const auto& node) -> std::expected<LoweredPlace, LowerError> {
            using N = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<N, ast::PathExpr>) {
                const std::string display_head = display_symbol(node.display_head, node.head);
                if (!node.generic_args.empty()) {
                    return make_error(
                        expr->span, "explicit generic arguments are only supported in function "
                                    "call or type positions");
                }
                if (node.tail.has_value())
                    return make_error(expr->span, "enum variants cannot be borrowed directly");

                const auto local_index = ctx.scope.lookup_local(node.head);
                if (!local_index.has_value())
                    return make_error(expr->span, "undefined variable '" + display_head + "'");

                const auto& local = ctx.scope.local(*local_index);
                if (local.moved)
                    return make_error(expr->span, "use of moved value '" + display_head + "'");

                return LoweredPlace{
                    tyir::make_ty_expr(
                        expr->span, tyir::LocalRef{node.head, tyir::ValueUseKind::Borrow},
                        local.ty),
                    local_index,
                };
            } else if constexpr (std::is_same_v<N, ast::FieldAccessExpr>) {
                auto base = lower_place_expr(node.base, ctx);
                if (!base)
                    return std::unexpected(std::move(base.error()));

                if (!base->expr->ty.is_named())
                    return make_error(
                        expr->span,
                        "field access on non-struct type '" + base->expr->ty.display() + "'");

                auto field_ty = ctx.env.field_ty(base->expr->ty.name, node.field);
                if (!field_ty)
                    return make_error(
                        expr->span, "no field '" + std::string(node.field.as_str())
                                        + "' in struct '" + base->expr->ty.display() + "'");
                if (!base->expr->ty.generic_args.empty()) {
                    const auto generic_params_it =
                        ctx.env.type_generic_params.find(base->expr->ty.name);
                    assert(generic_params_it != ctx.env.type_generic_params.end());
                    auto subst = build_substitution(
                        generic_params_it->second, base->expr->ty.generic_args, expr->span, "type",
                        base->expr->ty.name.as_str());
                    if (!subst)
                        return std::unexpected(std::move(subst.error()));
                    field_ty =
                        annotate_type_semantics(apply_substitution(*field_ty, *subst), ctx.env);
                }
                tyir::Ty lowered_field_ty =
                    propagate_runtime_tag(*field_ty, base->expr->ty.is_runtime);

                return LoweredPlace{
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyFieldAccess{
                            std::move(base->expr), node.field, tyir::ValueUseKind::Borrow},
                        lowered_field_ty),
                    base->owner_local,
                };
            } else {
                return make_error(expr->span, "borrow expressions require a local or field place");
            }
        },
        expr->node);
}

[[nodiscard]] static std::expected<LoweredExpr, LowerError>
    lower_expr(const ast::ExprPtr& expr, LowerCtx& ctx) {
    assert(expr != nullptr);

    return std::visit(
        [&](const auto& node) -> std::expected<LoweredExpr, LowerError> {
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
                    ty = tyir::ty::ref(tyir::ty::str());
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
                return LoweredExpr{
                    tyir::make_ty_expr(expr->span, std::move(lit), std::move(ty)),
                    {},
                    std::nullopt,
                };
            }

            // ── Path ──────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::PathExpr>) {
                const std::string display_head = display_symbol(node.display_head, node.head);
                if (!node.generic_args.empty()) {
                    return make_error(
                        expr->span,
                        "explicit generic arguments are only supported in function call or type "
                        "positions");
                }
                if (node.tail.has_value()) {
                    // EnumName::Variant
                    const cstc::symbol::Symbol enum_name = node.head;
                    const cstc::symbol::Symbol variant_name = *node.tail;
                    if (!ctx.env.is_enum(enum_name))
                        return make_error(expr->span, "'" + display_head + "' is not an enum type");
                    if (!ctx.env.has_variant(enum_name, variant_name))
                        return make_error(
                            expr->span, "no variant '" + std::string(variant_name.as_str())
                                            + "' in enum '" + display_head + "'");
                    return LoweredExpr{
                        tyir::make_ty_expr(
                            expr->span, tyir::EnumVariantRef{enum_name, variant_name},
                            annotate_type_semantics(
                                tyir::ty::named(enum_name, node.display_head), ctx.env)),
                        {},
                        std::nullopt,
                    };
                }

                // Local variable reference
                const auto local_index = ctx.scope.lookup_local(node.head);
                if (local_index.has_value()) {
                    auto& local = ctx.scope.local(*local_index);
                    if (local.moved)
                        return make_error(expr->span, "use of moved value '" + display_head + "'");

                    tyir::ValueUseKind use_kind = tyir::ValueUseKind::Copy;
                    if (local.ty.is_move_only()) {
                        if (local.active_borrows > 0)
                            return make_error(
                                expr->span,
                                "cannot move '" + display_head + "' while it is borrowed");
                        local.moved = true;
                        use_kind = tyir::ValueUseKind::Move;
                    }

                    return LoweredExpr{
                        tyir::make_ty_expr(
                            expr->span, tyir::LocalRef{node.head, use_kind}, local.ty),
                        {},
                        local.ty.is_ref() ? local.borrowed_local : std::nullopt,
                    };
                }

                return make_error(expr->span, "undefined variable '" + display_head + "'");
            }

            // ── Explicit generic application ──────────────────────────────
            else if constexpr (std::is_same_v<N, ast::GenericAppExpr>) {
                for (const ast::TypeRef& arg : node.args) {
                    auto lowered_arg = lower_type(arg, ctx.env, expr->span);
                    if (!lowered_arg)
                        return std::unexpected(std::move(lowered_arg.error()));
                }

                return make_error(
                    expr->span,
                    "explicit generic application is not a first-class expression; use it in a "
                    "supported generic call or type position");
            }

            // ── Struct init ───────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::StructInitExpr>) {
                const cstc::symbol::Symbol type_name = node.type_name;
                const std::string display_type_name =
                    display_symbol(node.display_name, node.type_name);
                if (ctx.env.is_extern_struct(type_name))
                    return make_error(
                        expr->span, "cannot construct extern type '" + display_type_name
                                        + "'; extern structs are opaque");
                if (!ctx.env.is_struct(type_name))
                    return make_error(
                        expr->span, "'" + display_type_name + "' is not a struct type");

                const auto& expected_fields = ctx.env.struct_fields.at(type_name);
                std::vector<tyir::Ty> lowered_generic_args;
                lowered_generic_args.reserve(node.generic_args.size());
                for (const ast::TypeRef& arg : node.generic_args) {
                    auto lowered_arg = lower_type(arg, ctx.env, expr->span);
                    if (!lowered_arg)
                        return std::unexpected(std::move(lowered_arg.error()));
                    lowered_generic_args.push_back(std::move(*lowered_arg));
                }
                const auto generic_params_it = ctx.env.type_generic_params.find(type_name);
                assert(generic_params_it != ctx.env.type_generic_params.end());
                auto subst = build_substitution(
                    generic_params_it->second, lowered_generic_args, expr->span, "type",
                    display_type_name);
                if (!subst)
                    return std::unexpected(std::move(subst.error()));

                // Validate: no extra/duplicate fields, each field type matches
                std::vector<tyir::TyStructInitField> lowered_fields;
                lowered_fields.reserve(node.fields.size());
                std::vector<std::size_t> temp_borrows;
                std::unordered_map<
                    cstc::symbol::Symbol, cstc::span::SourceSpan, cstc::symbol::SymbolHash>
                    seen_fields;
                seen_fields.reserve(node.fields.size());

                for (const ast::StructInitField& field : node.fields) {
                    auto expected_ty = ctx.env.field_ty(type_name, field.name);
                    if (!expected_ty)
                        return make_error(
                            field.span, "no field '" + std::string(field.name.as_str())
                                            + "' in struct '" + display_type_name + "'");
                    expected_ty =
                        annotate_type_semantics(apply_substitution(*expected_ty, *subst), ctx.env);

                    const auto [_, inserted] = seen_fields.emplace(field.name, field.span);
                    if (!inserted)
                        return make_error(
                            field.span, "duplicate field '" + std::string(field.name.as_str())
                                            + "' in struct initializer for '" + display_type_name
                                            + "'");

                    auto val = lower_expr(field.value, ctx);
                    if (!val)
                        return std::unexpected(std::move(val.error()));

                    if (!compatible(val->expr->ty, *expected_ty))
                        return make_error(
                            field.span, "field '" + std::string(field.name.as_str())
                                            + "': expected '" + expected_ty->display()
                                            + "', found '" + val->expr->ty.display() + "'");

                    append_temp_borrows(temp_borrows, std::move(val->temp_borrows));

                    lowered_fields.push_back(
                        tyir::TyStructInitField{field.name, std::move(val->expr), field.span});
                }

                // Validate that every declared struct field is initialised exactly once.
                for (const tyir::TyFieldDecl& expected_field : expected_fields) {
                    if (seen_fields.count(expected_field.name) == 0)
                        return make_error(
                            expr->span,
                            "missing field '" + std::string(expected_field.name.as_str())
                                + "' in struct initializer for '" + display_type_name + "'");
                }

                const tyir::Ty result_ty = annotate_type_semantics(
                    tyir::ty::named(
                        type_name, node.display_name, tyir::ValueSemantics::Move, false,
                        lowered_generic_args),
                    ctx.env);
                auto lowered = LoweredExpr{
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyStructInit{
                            type_name, std::move(lowered_generic_args), std::move(lowered_fields)},
                        result_ty),
                    {},
                    std::nullopt,
                };
                release_temp_borrows(ctx, temp_borrows);
                return lowered;
            }

            // ── Unary ─────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::UnaryExpr>) {
                switch (node.op) {
                case ast::UnaryOp::Borrow: {
                    if (std::holds_alternative<ast::PathExpr>(node.rhs->node)
                        || std::holds_alternative<ast::FieldAccessExpr>(node.rhs->node)) {
                        auto place = lower_place_expr(node.rhs, ctx);
                        if (!place)
                            return std::unexpected(std::move(place.error()));

                        std::vector<std::size_t> temp_borrows;
                        if (place->owner_local.has_value()) {
                            ctx.scope.add_ephemeral_borrow(*place->owner_local);
                            temp_borrows.push_back(*place->owner_local);
                        }
                        const tyir::Ty ref_ty = tyir::ty::ref(place->expr->ty);

                        return LoweredExpr{
                            tyir::make_ty_expr(
                                expr->span, tyir::TyBorrow{std::move(place->expr)}, ref_ty),
                            std::move(temp_borrows),
                            place->owner_local,
                        };
                    }

                    auto rhs = lower_expr(node.rhs, ctx);
                    if (!rhs)
                        return std::unexpected(std::move(rhs.error()));

                    if (rhs->expr->ty.is_never()) {
                        return LoweredExpr{
                            tyir::make_ty_expr(
                                expr->span, tyir::TyBorrow{std::move(rhs->expr)},
                                tyir::ty::never()),
                            std::move(rhs->temp_borrows),
                            std::nullopt,
                        };
                    }
                    const tyir::Ty ref_ty = tyir::ty::ref(rhs->expr->ty);

                    return LoweredExpr{
                        tyir::make_ty_expr(
                            expr->span, tyir::TyBorrow{std::move(rhs->expr)}, ref_ty),
                        std::move(rhs->temp_borrows),
                        std::nullopt,
                    };
                }
                case ast::UnaryOp::Negate:
                case ast::UnaryOp::Not: {
                    auto rhs = lower_expr(node.rhs, ctx);
                    if (!rhs)
                        return std::unexpected(std::move(rhs.error()));

                    tyir::Ty result_ty;
                    if (node.op == ast::UnaryOp::Negate) {
                        if (!matches_type_shape(rhs->expr->ty, tyir::ty::num()))
                            return make_error(
                                expr->span, "unary '-' requires 'num', found '"
                                                + rhs->expr->ty.display() + "'");
                        result_ty = unary_result_type(rhs->expr->ty, tyir::ty::num());
                    } else {
                        if (!matches_type_shape(rhs->expr->ty, tyir::ty::bool_()))
                            return make_error(
                                expr->span, "unary '!' requires 'bool', found '"
                                                + rhs->expr->ty.display() + "'");
                        result_ty = unary_result_type(rhs->expr->ty, tyir::ty::bool_());
                    }

                    auto lowered = LoweredExpr{
                        tyir::make_ty_expr(
                            expr->span, tyir::TyUnary{node.op, std::move(rhs->expr)}, result_ty),
                        {},
                        std::nullopt,
                    };
                    release_temp_borrows(ctx, rhs->temp_borrows);
                    return lowered;
                }
                }
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
                    if (!matches_type_shape(lhs->expr->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "arithmetic operator requires 'num' on left, found '"
                                            + lhs->expr->ty.display() + "'");
                    if (!matches_type_shape(rhs->expr->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "arithmetic operator requires 'num' on right, found '"
                                            + rhs->expr->ty.display() + "'");
                    result_ty = binary_result_type(lhs->expr->ty, rhs->expr->ty, tyir::ty::num());
                    break;

                case Op::Lt:
                case Op::Le:
                case Op::Gt:
                case Op::Ge:
                    if (!matches_type_shape(lhs->expr->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "comparison operator requires 'num' on left, found '"
                                            + lhs->expr->ty.display() + "'");
                    if (!matches_type_shape(rhs->expr->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "comparison operator requires 'num' on right, found '"
                                            + rhs->expr->ty.display() + "'");
                    result_ty = binary_result_type(lhs->expr->ty, rhs->expr->ty, tyir::ty::bool_());
                    break;

                case Op::Eq:
                case Op::Ne: {
                    const tyir::Ty& lty = lhs->expr->ty;
                    const tyir::Ty& rty = rhs->expr->ty;
                    if (!common_type(lty, rty).has_value())
                        return make_error(
                            expr->span,
                            "equality operator requires same types on both sides, found '"
                                + lty.display() + "' and '" + rty.display() + "'");
                    result_ty = binary_result_type(lty, rty, tyir::ty::bool_());
                    break;
                }

                case Op::And:
                case Op::Or:
                    if (!matches_type_shape(lhs->expr->ty, tyir::ty::bool_()))
                        return make_error(
                            expr->span, "logical operator requires 'bool' on left, found '"
                                            + lhs->expr->ty.display() + "'");
                    if (!matches_type_shape(rhs->expr->ty, tyir::ty::bool_()))
                        return make_error(
                            expr->span, "logical operator requires 'bool' on right, found '"
                                            + rhs->expr->ty.display() + "'");
                    result_ty = binary_result_type(lhs->expr->ty, rhs->expr->ty, tyir::ty::bool_());
                    break;
                }
                auto lowered = LoweredExpr{
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyBinary{node.op, std::move(lhs->expr), std::move(rhs->expr)},
                        std::move(result_ty)),
                    {},
                    std::nullopt,
                };
                release_temp_borrows(ctx, lhs->temp_borrows);
                release_temp_borrows(ctx, rhs->temp_borrows);
                return lowered;
            }

            // ── Field access ──────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::FieldAccessExpr>) {
                auto place = lower_place_expr(expr, ctx);
                if (!place)
                    return std::unexpected(std::move(place.error()));

                if (!place->expr->ty.is_copy())
                    return make_error(
                        expr->span, "cannot move field '" + std::string(node.field.as_str())
                                        + "' out of '" + place->expr->ty.display()
                                        + "'; borrow the field instead");

                const auto* access = std::get_if<tyir::TyFieldAccess>(&place->expr->node);
                assert(access != nullptr);
                return LoweredExpr{
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyFieldAccess{access->base, access->field, tyir::ValueUseKind::Copy},
                        place->expr->ty),
                    {},
                    place->expr->ty.is_ref() ? place->owner_local : std::nullopt,
                };
            }

            // ── Call ──────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::CallExpr>) {
                // Cicest has no first-class functions; callee must be a plain
                // PathExpr resolving to a top-level function name.
                const ast::PathExpr* callee_path = nullptr;
                std::vector<tyir::Ty> explicit_generic_args;
                if (const auto* generic_app =
                        std::get_if<ast::GenericAppExpr>(&node.callee->node)) {
                    for (const ast::TypeRef& arg : generic_app->args) {
                        auto lowered_arg = lower_type(arg, ctx.env, node.callee->span);
                        if (!lowered_arg)
                            return std::unexpected(std::move(lowered_arg.error()));
                        explicit_generic_args.push_back(std::move(*lowered_arg));
                    }

                    callee_path = std::get_if<ast::PathExpr>(&generic_app->callee->node);
                    if (callee_path == nullptr || callee_path->tail.has_value()) {
                        return make_error(
                            expr->span,
                            "call callee must be a function name, optionally with explicit generic "
                            "arguments");
                    }
                }

                if (callee_path == nullptr) {
                    callee_path = std::get_if<ast::PathExpr>(&node.callee->node);
                    if (callee_path == nullptr || callee_path->tail.has_value())
                        return make_error(expr->span, "call callee must be a function name");
                    if (explicit_generic_args.empty() && !callee_path->generic_args.empty()) {
                        explicit_generic_args.reserve(callee_path->generic_args.size());
                        for (const ast::TypeRef& arg : callee_path->generic_args) {
                            auto lowered_arg = lower_type(arg, ctx.env, node.callee->span);
                            if (!lowered_arg)
                                return std::unexpected(std::move(lowered_arg.error()));
                            explicit_generic_args.push_back(std::move(*lowered_arg));
                        }
                    }
                }

                const cstc::symbol::Symbol fn_name = callee_path->head;
                const std::string display_fn_name =
                    display_symbol(callee_path->display_head, callee_path->head);
                if (!ctx.env.is_fn(fn_name))
                    return make_error(expr->span, "undefined function '" + display_fn_name + "'");

                const FnSignature& sig = ctx.env.fn_signatures.at(fn_name);

                std::vector<tyir::TyExprPtr> lowered_args;
                lowered_args.reserve(node.args.size());
                std::vector<std::size_t> temp_borrows;

                for (const ast::ExprPtr& arg_expr : node.args) {
                    auto arg = lower_expr(arg_expr, ctx);
                    if (!arg)
                        return std::unexpected(std::move(arg.error()));
                    append_temp_borrows(temp_borrows, std::move(arg->temp_borrows));
                    lowered_args.push_back(std::move(arg->expr));
                }

                const GenericParamSet generic_param_set =
                    make_generic_param_set(sig.generic_params);
                TypeSubstitution substitution;
                if (!explicit_generic_args.empty()) {
                    auto subst = build_substitution(
                        sig.generic_params, explicit_generic_args, expr->span, "function",
                        display_fn_name);
                    if (!subst)
                        return std::unexpected(std::move(subst.error()));
                    substitution = std::move(*subst);
                }

                if (node.args.size() != sig.param_types.size())
                    return make_error(
                        expr->span, "function '" + display_fn_name + "' expects "
                                        + std::to_string(sig.param_types.size()) + " argument(s), "
                                        + std::to_string(node.args.size()) + " provided");

                for (std::size_t i = 0; i < node.args.size(); ++i) {
                    auto inferred = infer_substitution(
                        sig.param_types[i], lowered_args[i]->ty, generic_param_set, substitution,
                        node.args[i]->span, display_fn_name);
                    if (!inferred)
                        return std::unexpected(std::move(inferred.error()));
                }

                std::vector<tyir::Ty> resolved_generic_args;
                resolved_generic_args.reserve(sig.generic_params.size());
                for (const ast::GenericParam& generic_param : sig.generic_params) {
                    const auto found = substitution.find(generic_param.name);
                    if (found == substitution.end()) {
                        return make_error(
                            expr->span, "cannot infer generic argument '"
                                            + std::string(generic_param.name.as_str())
                                            + "' for function '" + display_fn_name + "'");
                    }
                    resolved_generic_args.push_back(found->second);
                }

                for (std::size_t i = 0; i < node.args.size(); ++i) {
                    const tyir::Ty expected_param_ty = annotate_type_semantics(
                        apply_substitution(sig.param_types[i], substitution), ctx.env);
                    if (!compatible(lowered_args[i]->ty, expected_param_ty))
                        return make_error(
                            node.args[i]->span, "argument " + std::to_string(i + 1) + " of '"
                                                    + display_fn_name + "': expected '"
                                                    + expected_param_ty.display() + "', found '"
                                                    + lowered_args[i]->ty.display() + "'");
                }

                const tyir::Ty resolved_return_ty = annotate_type_semantics(
                    apply_substitution(sig.return_ty, substitution), ctx.env);

                auto lowered = LoweredExpr{
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyCall{
                            fn_name, std::move(resolved_generic_args), std::move(lowered_args)},
                        resolved_return_ty),
                    {},
                    std::nullopt,
                };
                release_temp_borrows(ctx, temp_borrows);
                return lowered;
            }

            // ── Block expression ──────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::BlockPtr>) {
                auto block = lower_block(*node, ctx);
                if (!block)
                    return std::unexpected(std::move(block.error()));
                tyir::Ty block_ty = block->ty;
                auto block_ptr = std::make_shared<tyir::TyBlock>(std::move(*block));
                return LoweredExpr{
                    tyir::make_ty_expr(expr->span, std::move(block_ptr), block_ty),
                    {},
                    std::nullopt,
                };
            }

            // ── If ────────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::IfExpr>) {
                auto cond = lower_expr(node.condition, ctx);
                if (!cond)
                    return std::unexpected(std::move(cond.error()));
                if (!matches_type_shape(cond->expr->ty, tyir::ty::bool_()))
                    return make_error(
                        node.condition->span, "'if' condition must have type 'bool', found '"
                                                  + cond->expr->ty.display() + "'");
                release_temp_borrows(ctx, cond->temp_borrows);

                LowerCtx then_ctx = ctx;
                auto then_block_val = lower_block(*node.then_block, then_ctx);
                if (!then_block_val)
                    return std::unexpected(std::move(then_block_val.error()));

                auto then_ptr = std::make_shared<tyir::TyBlock>(std::move(*then_block_val));
                const bool then_reaches_join = block_can_fallthrough(*then_ptr);

                tyir::Ty result_ty = then_ptr->ty;

                std::optional<tyir::TyExprPtr> else_branch;
                if (node.else_branch.has_value()) {
                    LowerCtx else_ctx = ctx;
                    auto else_val = lower_expr(*node.else_branch, else_ctx);
                    if (!else_val)
                        return std::unexpected(std::move(else_val.error()));

                    const tyir::Ty& else_ty = else_val->expr->ty;
                    const auto joined_ty = common_type(then_ptr->ty, else_ty);
                    if (!joined_ty.has_value())
                        return make_error(
                            expr->span, "'if' then-branch has type '" + then_ptr->ty.display()
                                            + "' but else-branch has type '" + else_ty.display()
                                            + "'");
                    result_ty = *joined_ty;

                    if (auto merged =
                            merge_loop_break_types(ctx.loop_stack, then_ctx.loop_stack, expr->span);
                        !merged) {
                        return std::unexpected(std::move(merged.error()));
                    }
                    if (auto merged =
                            merge_loop_break_types(ctx.loop_stack, else_ctx.loop_stack, expr->span);
                        !merged) {
                        return std::unexpected(std::move(merged.error()));
                    }
                    // Only branches that can reach the join may contribute
                    // post-if move/borrow state.
                    if (then_reaches_join)
                        ctx.scope.merge_from(then_ctx.scope);
                    if (expr_can_fallthrough(*else_val->expr))
                        ctx.scope.merge_from(else_ctx.scope);

                    else_branch = std::move(else_val->expr);
                } else {
                    // No else branch: result type is Unit
                    result_ty = tyir::ty::unit();
                    if (auto merged =
                            merge_loop_break_types(ctx.loop_stack, then_ctx.loop_stack, expr->span);
                        !merged) {
                        return std::unexpected(std::move(merged.error()));
                    }
                    if (then_reaches_join)
                        ctx.scope.merge_from(then_ctx.scope);
                }

                if (result_ty.is_ref())
                    return make_error(expr->span, "'if' expressions cannot yield references yet");

                return LoweredExpr{
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyIf{
                            std::move(cond->expr), std::move(then_ptr), std::move(else_branch)},
                        result_ty),
                    {},
                    std::nullopt,
                };
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
                if (loop_ty.is_ref()) {
                    ctx.pop_loop();
                    return make_error(expr->span, "loop expressions cannot yield references yet");
                }
                ctx.pop_loop();
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));
                return LoweredExpr{
                    tyir::make_ty_expr(expr->span, tyir::TyLoop{std::move(body_ptr)}, loop_ty),
                    {},
                    std::nullopt,
                };
            }

            // ── While ─────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::WhileExpr>) {
                auto cond = lower_expr(node.condition, ctx);
                if (!cond) {
                    return std::unexpected(std::move(cond.error()));
                }
                if (!matches_type_shape(cond->expr->ty, tyir::ty::bool_())) {
                    return make_error(
                        node.condition->span, "'while' condition must have type 'bool', found '"
                                                  + cond->expr->ty.display() + "'");
                }
                release_temp_borrows(ctx, cond->temp_borrows);

                ctx.push_loop(LoopKind::While);
                auto body = lower_block(*node.body, ctx);
                if (!body) {
                    ctx.pop_loop();
                    return std::unexpected(std::move(body.error()));
                }
                ctx.pop_loop();
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));

                return LoweredExpr{
                    tyir::make_ty_expr(
                        expr->span, tyir::TyWhile{std::move(cond->expr), std::move(body_ptr)},
                        tyir::ty::unit()),
                    {},
                    std::nullopt,
                };
            }

            // ── For ───────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::ForExpr>) {
                // The for-init introduces a new scope that outlives the header
                ctx.scope.push();

                std::optional<tyir::TyForInit> lowered_init;

                if (node.init.has_value()) {
                    const auto& init_var = *node.init;
                    if (const auto* init_let = std::get_if<ast::ForInitLet>(&init_var)) {
                        auto init_expr = lower_expr(init_let->initializer, ctx);
                        if (!init_expr) {
                            ctx.scope.pop();
                            return std::unexpected(std::move(init_expr.error()));
                        }
                        tyir::Ty init_ty;
                        if (init_let->type_annotation.has_value()) {
                            auto ann =
                                lower_type(*init_let->type_annotation, ctx.env, init_let->span);
                            if (!ann) {
                                ctx.scope.pop();
                                return std::unexpected(std::move(ann.error()));
                            }
                            if (!compatible(init_expr->expr->ty, *ann)) {
                                ctx.scope.pop();
                                return make_error(
                                    init_let->span, "for-init type mismatch: expected '"
                                                        + ann->display() + "', found '"
                                                        + init_expr->expr->ty.display() + "'");
                            }
                            init_ty = *ann;
                        } else {
                            init_ty = init_expr->expr->ty;
                        }

                        std::optional<std::size_t> borrowed_local;
                        if (init_ty.is_ref() && init_expr->persistent_borrow_owner.has_value()) {
                            borrowed_local = init_expr->persistent_borrow_owner;
                            consume_temp_borrow(init_expr->temp_borrows, borrowed_local.value());
                        }
                        if (!init_let->discard && init_let->name.is_valid()
                            && !ctx.scope.insert(init_let->name, init_ty, borrowed_local)) {
                            ctx.scope.pop();
                            return make_error(
                                init_let->span, "duplicate local binding '"
                                                    + std::string(init_let->name.as_str()) + "'");
                        }
                        release_temp_borrows(ctx, init_expr->temp_borrows);
                        lowered_init = tyir::TyForInit{
                            init_let->discard, init_let->name, init_ty, std::move(init_expr->expr),
                            init_let->span};
                    } else {
                        // Expression initializer
                        const auto& init_expr_ptr = std::get<ast::ExprPtr>(init_var);
                        auto init_expr = lower_expr(init_expr_ptr, ctx);
                        if (!init_expr) {
                            ctx.scope.pop();
                            return std::unexpected(std::move(init_expr.error()));
                        }
                        release_temp_borrows(ctx, init_expr->temp_borrows);
                        // Treat as a discard init — wrap in TyForInit with discard=true
                        lowered_init = tyir::TyForInit{
                            true, cstc::symbol::kInvalidSymbol, init_expr->expr->ty,
                            std::move(init_expr->expr), init_expr_ptr->span};
                    }
                }

                std::optional<tyir::TyExprPtr> lowered_cond;
                if (node.condition.has_value()) {
                    auto cond = lower_expr(*node.condition, ctx);
                    if (!cond) {
                        ctx.scope.pop();
                        return std::unexpected(std::move(cond.error()));
                    }
                    if (!matches_type_shape(cond->expr->ty, tyir::ty::bool_())) {
                        ctx.scope.pop();
                        return make_error(
                            (*node.condition)->span,
                            "'for' condition must have type 'bool', found '"
                                + cond->expr->ty.display() + "'");
                    }
                    release_temp_borrows(ctx, cond->temp_borrows);
                    lowered_cond = std::move(cond->expr);
                }

                LowerCtx loop_ctx = ctx;

                loop_ctx.push_loop(LoopKind::For);
                auto body = lower_block(*node.body, loop_ctx);
                if (!body) {
                    loop_ctx.pop_loop();
                    ctx.scope.pop();
                    return std::unexpected(std::move(body.error()));
                }
                loop_ctx.pop_loop();
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));

                std::optional<tyir::TyExprPtr> lowered_step;
                if (node.step.has_value()) {
                    auto step = lower_expr(*node.step, loop_ctx);
                    if (!step) {
                        ctx.scope.pop();
                        return std::unexpected(std::move(step.error()));
                    }
                    release_temp_borrows(loop_ctx, step->temp_borrows);
                    lowered_step = std::move(step->expr);
                }

                ctx.scope.pop();

                return LoweredExpr{
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyFor{
                            std::move(lowered_init), std::move(lowered_cond),
                            std::move(lowered_step), std::move(body_ptr)},
                        tyir::ty::unit()),
                    {},
                    std::nullopt,
                };
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
                    if (val->expr->ty.is_ref())
                        return make_error(expr->span, "'break' values cannot be references yet");

                    // Re-acquire the reference after recursion.
                    auto& loop_ctx = ctx.current_loop();
                    const tyir::Ty& val_ty = val->expr->ty;
                    if (!loop_ctx.break_ty.has_value()) {
                        loop_ctx.break_ty = val_ty;
                    } else {
                        const tyir::Ty& prev = *loop_ctx.break_ty;
                        const auto joined = common_type(prev, val_ty);
                        if (!joined.has_value()) {
                            return make_error(
                                expr->span, "'break' value type mismatch: expected '"
                                                + prev.display() + "', found '" + val_ty.display()
                                                + "'");
                        }
                        loop_ctx.break_ty = *joined;
                    }

                    release_temp_borrows(ctx, val->temp_borrows);
                    return LoweredExpr{
                        tyir::make_ty_expr(
                            expr->span, tyir::TyBreak{std::move(val->expr)}, tyir::ty::never()),
                        {},
                        std::nullopt,
                    };
                }

                // Bare break — no recursive call, so a fresh reference is safe.
                auto& loop_ctx = ctx.current_loop();
                if (loop_ctx.kind == LoopKind::Loop) {
                    // Bare break in `loop` contributes Unit
                    if (!loop_ctx.break_ty.has_value()) {
                        loop_ctx.break_ty = tyir::ty::unit();
                    } else {
                        const tyir::Ty& prev = *loop_ctx.break_ty;
                        const auto joined = common_type(prev, tyir::ty::unit());
                        if (!joined.has_value()) {
                            return make_error(
                                expr->span, "'break' value type mismatch: expected '"
                                                + prev.display() + "', found 'Unit'");
                        }
                        loop_ctx.break_ty = *joined;
                    }
                }
                // Bare break in while/for: no type tracking needed

                return LoweredExpr{
                    tyir::make_ty_expr(expr->span, tyir::TyBreak{std::nullopt}, tyir::ty::never()),
                    {},
                    std::nullopt,
                };
            }

            // ── Continue ──────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::ContinueExpr>) {
                if (!ctx.in_loop())
                    return make_error(expr->span, "'continue' outside of a loop");
                return LoweredExpr{
                    tyir::make_ty_expr(expr->span, tyir::TyContinue{}, tyir::ty::never()),
                    {},
                    std::nullopt,
                };
            }

            // ── Return ────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::ReturnExpr>) {
                std::optional<tyir::TyExprPtr> lowered_val;
                if (node.value.has_value()) {
                    auto val = lower_expr(*node.value, ctx);
                    if (!val)
                        return std::unexpected(std::move(val.error()));
                    if (!compatible(val->expr->ty, ctx.current_return_ty))
                        return make_error(
                            expr->span, "return type mismatch: expected '"
                                            + ctx.current_return_ty.display() + "', found '"
                                            + val->expr->ty.display() + "'");
                    release_temp_borrows(ctx, val->temp_borrows);
                    lowered_val = std::move(val->expr);
                } else {
                    // Bare `return` — valid only in functions returning Unit
                    if (!compatible(tyir::ty::unit(), ctx.current_return_ty))
                        return make_error(
                            expr->span, "bare 'return' in function returning '"
                                            + ctx.current_return_ty.display() + "'");
                }
                return LoweredExpr{
                    tyir::make_ty_expr(
                        expr->span, tyir::TyReturn{std::move(lowered_val)}, tyir::ty::never()),
                    {},
                    std::nullopt,
                };
            }

            // Should be exhaustive — but keep the compiler happy
            return make_error(expr->span, "unsupported expression kind");
        },
        expr->node);
}

// ─── Item lowering ────────────────────────────────────────────────────────────

/// Lowers a function declaration using the fully-built `TypeEnv`.
[[nodiscard]] static std::expected<tyir::TyFnDecl, LowerError>
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
    for (const tyir::TyParam& p : ty_params) {
        if (!ctx.scope.insert(p.name, p.ty))
            return make_error(p.span, "duplicate parameter '" + std::string(p.name.as_str()) + "'");
    }

    auto body = lower_block(*fn.body, ctx);
    ctx.scope.pop();
    if (!body)
        return std::unexpected(std::move(body.error()));

    // Check that body type matches declared return type (when a tail is present)
    if (body->tail.has_value() && !compatible(body->ty, sig.return_ty))
        return make_error(
            fn.body->span, "function '" + display_decl_name(fn) + "' body has type '"
                               + body->ty.display() + "' but return type is '"
                               + sig.return_ty.display() + "'");

    // Non-Unit functions without a tail expression must not reach the end of
    // the body without returning.
    if (!body->tail.has_value() && block_can_fallthrough(*body)
        && !compatible(tyir::ty::unit(), sig.return_ty))
        return make_error(
            fn.body->span, "function '" + display_decl_name(fn)
                               + "' may fall through without returning a value of type '"
                               + sig.return_ty.display() + "'");

    if (fn.is_runtime)
        body->ty.is_runtime = true;

    auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));

    tyir::TyFnDecl lowered_fn;
    lowered_fn.name = fn.name;
    lowered_fn.generic_params = fn.generic_params;
    lowered_fn.params = std::move(ty_params);
    lowered_fn.return_ty = sig.return_ty;
    lowered_fn.body = std::move(body_ptr);
    lowered_fn.span = fn.span;
    lowered_fn.is_runtime = fn.is_runtime;
    lowered_fn.where_clause = fn.where_clause;
    return lowered_fn;
}

} // namespace detail

// ─── Public API ──────────────────────────────────────────────────────────────

std::expected<tyir::TyProgram, LowerError> lower_program(const ast::Program& program) {
    detail::TypeEnv env;

    // ── Phase 1: collect named-type names (placeholders) ─────────────────
    for (const ast::Item& item : program.items) {
        if (const auto* struct_decl = std::get_if<ast::StructDecl>(&item)) {
            if (env.enum_variants.count(struct_decl->name) > 0
                || env.extern_struct_names.count(struct_decl->name) > 0)
                return detail::make_error(
                    struct_decl->span,
                    "duplicate struct name '" + detail::display_decl_name(*struct_decl) + "'");

            const auto insert_result =
                env.struct_fields.emplace(struct_decl->name, std::vector<tyir::TyFieldDecl>{});
            if (!insert_result.second)
                return detail::make_error(
                    struct_decl->span,
                    "duplicate struct name '" + detail::display_decl_name(*struct_decl) + "'");
            env.type_generic_arity.emplace(struct_decl->name, struct_decl->generic_params.size());
            env.type_generic_params.emplace(struct_decl->name, struct_decl->generic_params);
        } else if (const auto* enum_decl = std::get_if<ast::EnumDecl>(&item)) {
            if (env.struct_fields.count(enum_decl->name) > 0
                || env.extern_struct_names.count(enum_decl->name) > 0)
                return detail::make_error(
                    enum_decl->span,
                    "duplicate enum name '" + detail::display_decl_name(*enum_decl) + "'");

            const auto insert_result =
                env.enum_variants.emplace(enum_decl->name, std::vector<tyir::TyEnumVariant>{});
            if (!insert_result.second)
                return detail::make_error(
                    enum_decl->span,
                    "duplicate enum name '" + detail::display_decl_name(*enum_decl) + "'");
            env.type_generic_arity.emplace(enum_decl->name, enum_decl->generic_params.size());
            env.type_generic_params.emplace(enum_decl->name, enum_decl->generic_params);
        } else if (const auto* extern_struct = std::get_if<ast::ExternStructDecl>(&item)) {
            // Validate the ABI string.
            if (auto err = detail::validate_abi(extern_struct->abi, extern_struct->span))
                return *err;
            if (env.enum_variants.count(extern_struct->name) > 0
                || env.struct_fields.count(extern_struct->name) > 0
                || env.extern_struct_names.count(extern_struct->name) > 0)
                return detail::make_error(
                    extern_struct->span,
                    "duplicate type name '" + detail::display_decl_name(*extern_struct) + "'");

            env.extern_struct_names.insert(extern_struct->name);
            env.type_generic_arity.emplace(extern_struct->name, 0);
            env.type_generic_params.emplace(extern_struct->name, std::vector<ast::GenericParam>{});
        }
    }

    // ── Phase 2: resolve struct fields and enum variants ──────────────────
    for (const ast::Item& item : program.items) {
        if (const auto* s = std::get_if<ast::StructDecl>(&item)) {
            auto& fields = env.struct_fields.at(s->name);
            const detail::GenericParamSet generic_params =
                detail::make_generic_param_set(s->generic_params);
            for (const ast::FieldDecl& f : s->fields) {
                auto ty = detail::lower_type_shape(f.type, env, f.span, generic_params);
                if (!ty)
                    return std::unexpected(std::move(ty.error()));
                if (ty->is_ref())
                    return detail::make_error(
                        f.span, "reference fields are not supported in structs");
                fields.push_back(tyir::TyFieldDecl{f.name, *ty, f.span});
            }
        } else if (const auto* e = std::get_if<ast::EnumDecl>(&item)) {
            auto& variants = env.enum_variants.at(e->name);
            for (const ast::EnumVariant& v : e->variants)
                variants.push_back(tyir::TyEnumVariant{v.name, v.discriminant, v.span});
        }
    }

    detail::finalize_env_type_semantics(env);

    // ── Phase 3: resolve function signatures ─────────────────────────────
    for (const ast::Item& item : program.items) {
        if (const auto* fn = std::get_if<ast::FnDecl>(&item)) {
            auto sig = detail::resolve_fn_signature(
                fn->params, fn->return_type, fn->span, env, fn->is_runtime, fn->generic_params);
            if (!sig)
                return std::unexpected(std::move(sig.error()));
            const auto insert_result = env.fn_signatures.emplace(fn->name, std::move(*sig));
            if (!insert_result.second)
                return detail::make_error(
                    fn->span, "duplicate function name '" + detail::display_decl_name(*fn) + "'");
        } else if (const auto* ext_fn = std::get_if<ast::ExternFnDecl>(&item)) {
            // Validate the ABI string.
            if (auto err = detail::validate_abi(ext_fn->abi, ext_fn->span))
                return *err;
            auto link_name = detail::resolve_extern_link_name(*ext_fn);
            if (!link_name)
                return std::unexpected(std::move(link_name.error()));
            // Build a signature from the extern fn declaration.
            auto sig = detail::resolve_fn_signature(
                ext_fn->params, ext_fn->return_type, ext_fn->span, env, ext_fn->is_runtime);
            if (!sig)
                return std::unexpected(std::move(sig.error()));
            const auto insert_result = env.fn_signatures.emplace(ext_fn->name, std::move(*sig));
            if (!insert_result.second)
                return detail::make_error(
                    ext_fn->span,
                    "duplicate function name '" + detail::display_decl_name(*ext_fn) + "'");
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
            ty_decl.generic_params = s->generic_params;
            ty_decl.is_zst = s->is_zst;
            ty_decl.span = s->span;
            ty_decl.fields = env.struct_fields.at(s->name);
            ty_decl.where_clause = s->where_clause;
            result.items.push_back(std::move(ty_decl));
        } else if (const auto* e = std::get_if<ast::EnumDecl>(&item)) {
            tyir::TyEnumDecl ty_decl;
            ty_decl.name = e->name;
            ty_decl.generic_params = e->generic_params;
            ty_decl.span = e->span;
            ty_decl.variants = env.enum_variants.at(e->name);
            ty_decl.where_clause = e->where_clause;
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
            ty_decl.is_runtime = ext_fn->is_runtime;
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
