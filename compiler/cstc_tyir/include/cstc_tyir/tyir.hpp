#ifndef CICEST_COMPILER_CSTC_TYIR_TYIR_HPP
#define CICEST_COMPILER_CSTC_TYIR_TYIR_HPP

/// @file tyir.hpp
/// @brief Typed Intermediate Representation (TyIR) node definitions.
///
/// TyIR is the compiler's first typed IR, positioned between the AST and any
/// future lowering or code-generation phases.  It corresponds roughly to
/// Rust's THIR (Typed High-level Intermediate Representation) or Zig's ZIR
/// at the typed stage:
///
///   Source → [Lexer] → Tokens → [Parser] → AST → [Lowering] → **TyIR**
///
/// Every expression node in TyIR carries a resolved `Ty` annotation.
/// Names (paths, variables) are resolved to one of the concrete reference
/// kinds (`LocalRef`, `EnumVariantRef`, direct `fn_name` in `TyCall`).
/// Type annotations from source are validated and converted to `Ty` values.
///
/// # Design notes
/// - All string data is represented as interned `Symbol` values; a
///   `cstc::symbol::SymbolSession` must be live for any symbol operations.
/// - Spans use global byte offsets (same convention as AST / lexer).
/// - `UnaryOp` and `BinaryOp` are re-used from `cstc_ast` — TyIR depends on
///   `cstc_ast` for those shared enumerations only.
/// - The module is header-only; `printer.hpp` provides the human-readable tree
///   formatter.

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>

namespace cstc::tyir {

// ─── Forward declarations ────────────────────────────────────────────────────

/// Base node for all typed expression forms.
struct TyExpr;
/// Type-annotated block of statements with an optional tail expression.
struct TyBlock;

/// Shared pointer used for recursive typed-expression references.
using TyExprPtr = std::shared_ptr<TyExpr>;
/// Shared pointer used for recursive typed-block references.
using TyBlockPtr = std::shared_ptr<TyBlock>;

// ─── Type representation ─────────────────────────────────────────────────────

/// Kind of a fully-resolved type.
enum class TyKind {
    /// Shared immutable reference type `&T`.
    Ref,
    /// Built-in unit type `()`.
    Unit,
    /// Built-in numeric type `num`.
    Num,
    /// Built-in string type `str`.
    Str,
    /// Built-in boolean type `bool`.
    Bool,
    /// User-defined named type (struct or enum).
    Named,
    /// Bottom / never type — produced by diverging expressions
    /// (`break`, `continue`, `return`).
    Never,
};

/// Ownership / duplication behavior of a resolved type.
enum class ValueSemantics {
    /// Values of this type are copied on by-value use.
    Copy,
    /// Values of this type are move-only and participate in drop.
    Move,
    /// Values of this type are shared references.
    Ref,
};

/// How a local or field expression is used at a specific TyIR node.
enum class ValueUseKind {
    /// The value is copied.
    Copy,
    /// The value is moved.
    Move,
    /// The expression is used as a borrowed place.
    Borrow,
};

/// A fully-resolved type annotation attached to every TyIR expression.
struct Ty {
    /// Category of this type.
    TyKind kind = TyKind::Unit;
    /// Interned type name; valid only when `kind == TyKind::Named`.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Human-facing type name used in diagnostics.
    cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol;
    /// Applied generic arguments when `kind == TyKind::Named`.
    std::vector<Ty> generic_args;
    /// Referenced pointee when `kind == TyKind::Ref`.
    std::shared_ptr<Ty> pointee;
    /// Ownership behavior attached to this resolved type.
    ValueSemantics semantics = ValueSemantics::Copy;
    /// True when this type carries the `runtime` tag.
    bool is_runtime = false;

    [[nodiscard]] constexpr bool is_unit() const { return kind == TyKind::Unit; }
    [[nodiscard]] constexpr bool is_never() const { return kind == TyKind::Never; }
    [[nodiscard]] constexpr bool is_named() const { return kind == TyKind::Named; }
    [[nodiscard]] constexpr bool is_ref() const { return kind == TyKind::Ref; }
    [[nodiscard]] constexpr bool is_copy() const {
        return semantics == ValueSemantics::Copy || semantics == ValueSemantics::Ref;
    }
    [[nodiscard]] constexpr bool is_move_only() const { return semantics == ValueSemantics::Move; }
    /// Returns true when `other` has the same underlying type shape after
    /// ignoring every `runtime` tag.
    [[nodiscard]] constexpr bool same_shape_as(const Ty& other) const {
        if (kind != other.kind)
            return false;
        switch (kind) {
        case TyKind::Ref:
            if (pointee == nullptr || other.pointee == nullptr)
                return pointee == other.pointee;
            return pointee->same_shape_as(*other.pointee);
        case TyKind::Named:
            if (name != other.name || generic_args.size() != other.generic_args.size())
                return false;
            for (std::size_t index = 0; index < generic_args.size(); ++index) {
                if (!generic_args[index].same_shape_as(other.generic_args[index]))
                    return false;
            }
            return true;
        case TyKind::Unit:
        case TyKind::Num:
        case TyKind::Str:
        case TyKind::Bool:
        case TyKind::Never: return true;
        }
        return false;
    }

    /// Exact type identity, including all `runtime` tags.
    friend constexpr bool operator==(const Ty& lhs, const Ty& rhs) {
        if (lhs.kind != rhs.kind || lhs.name != rhs.name || lhs.semantics != rhs.semantics
            || lhs.is_runtime != rhs.is_runtime
            || lhs.generic_args.size() != rhs.generic_args.size())
            return false;
        for (std::size_t index = 0; index < lhs.generic_args.size(); ++index) {
            if (!(lhs.generic_args[index] == rhs.generic_args[index]))
                return false;
        }
        if (lhs.kind != TyKind::Ref)
            return true;
        if (lhs.pointee == nullptr || rhs.pointee == nullptr)
            return lhs.pointee == rhs.pointee;
        return *lhs.pointee == *rhs.pointee;
    }

    /// Returns a human-readable display string (e.g. `"num"`, `"MyStruct"`, `"!"`).
    ///
    /// Requires an active `SymbolSession` for `Named` types.
    [[nodiscard]] std::string display() const {
        std::string rendered;
        switch (kind) {
        case TyKind::Ref:
            if (pointee != nullptr)
                rendered = "&" + pointee->display();
            else
                rendered = "&<unknown>";
            break;
        case TyKind::Unit: rendered = "Unit"; break;
        case TyKind::Num: rendered = "num"; break;
        case TyKind::Str: rendered = "str"; break;
        case TyKind::Bool: rendered = "bool"; break;
        case TyKind::Never: rendered = "!"; break;
        case TyKind::Named:
            if (display_name.is_valid())
                rendered = std::string(display_name.as_str());
            else
                rendered = name.is_valid() ? std::string(name.as_str()) : "<named>";
            if (!generic_args.empty()) {
                rendered += "<";
                for (std::size_t index = 0; index < generic_args.size(); ++index) {
                    if (index > 0)
                        rendered += ", ";
                    rendered += generic_args[index].display();
                }
                rendered += ">";
            }
            break;
        }
        if (is_runtime)
            return "runtime " + rendered;
        return rendered;
    }
};

/// Returns true when `ty` or any nested type argument/pointee carries the
/// source-level `runtime` qualifier.
[[nodiscard]] inline bool ty_contains_runtime_tag(const Ty& ty) {
    if (ty.is_runtime)
        return true;
    if (ty.kind == TyKind::Ref)
        return ty.pointee != nullptr && ty_contains_runtime_tag(*ty.pointee);
    if (ty.kind != TyKind::Named)
        return false;
    for (const Ty& arg : ty.generic_args) {
        if (ty_contains_runtime_tag(arg))
            return true;
    }
    return false;
}

/// Factory helpers for the well-known primitive types.
namespace ty {
/// Unit type `()`.
inline Ty unit(bool is_runtime = false) {
    return {
        TyKind::Unit,
        cstc::symbol::kInvalidSymbol,
        cstc::symbol::kInvalidSymbol,
        {},
        nullptr,
        ValueSemantics::Copy,
        is_runtime,
    };
}
/// Numeric type `num`.
inline Ty num(bool is_runtime = false) {
    return {
        TyKind::Num,
        cstc::symbol::kInvalidSymbol,
        cstc::symbol::kInvalidSymbol,
        {},
        nullptr,
        ValueSemantics::Copy,
        is_runtime,
    };
}
/// String type `str`.
inline Ty str(bool is_runtime = false) {
    return {
        TyKind::Str,
        cstc::symbol::kInvalidSymbol,
        cstc::symbol::kInvalidSymbol,
        {},
        nullptr,
        ValueSemantics::Move,
        is_runtime,
    };
}
/// Boolean type `bool`.
inline Ty bool_(bool is_runtime = false) {
    return {
        TyKind::Bool,
        cstc::symbol::kInvalidSymbol,
        cstc::symbol::kInvalidSymbol,
        {},
        nullptr,
        ValueSemantics::Copy,
        is_runtime,
    };
}
/// Never / bottom type (diverging expression).
inline Ty never(bool is_runtime = false) {
    return {
        TyKind::Never,
        cstc::symbol::kInvalidSymbol,
        cstc::symbol::kInvalidSymbol,
        {},
        nullptr,
        ValueSemantics::Copy,
        is_runtime,
    };
}
/// User-defined named type (struct or enum).
inline Ty named(
    cstc::symbol::Symbol sym, cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol,
    ValueSemantics semantics = ValueSemantics::Move, bool is_runtime = false,
    std::vector<Ty> generic_args = {}) {
    return {TyKind::Named, sym,       display_name, std::move(generic_args),
            nullptr,       semantics, is_runtime};
}
/// Shared immutable reference type `&T`.
inline Ty ref(const Ty& pointee, bool is_runtime = false) {
    return {
        TyKind::Ref, cstc::symbol::kInvalidSymbol,  cstc::symbol::kInvalidSymbol,
        {},          std::make_shared<Ty>(pointee), ValueSemantics::Ref,
        is_runtime,
    };
}
} // namespace ty

// ─── Expression sub-nodes ────────────────────────────────────────────────────
// All sub-nodes use TyExprPtr / TyBlockPtr (shared_ptr to forward-declared
// types) so they may appear in TyExpr::Node before TyExpr / TyBlock are fully
// defined.

/// Typed literal expression; type is uniquely determined by `kind`.
struct TyLiteral {
    /// Literal value category.
    enum class Kind {
        /// Numeric literal (e.g. `42`, `3.14`).
        Num,
        /// Borrowed string literal (e.g. `"hello"`), with type `&str`.
        Str,
        /// Owned compile-time string, with type `str`.
        OwnedStr,
        /// Boolean literal (`true` or `false`).
        Bool,
        /// Unit literal `()`.
        Unit,
    };

    /// Literal category.
    Kind kind = Kind::Unit;
    /// Interned source text of the literal (empty symbol for `Bool` / `Unit`).
    cstc::symbol::Symbol symbol = cstc::symbol::kInvalidSymbol;
    /// Parsed boolean value; meaningful only when `kind == Bool`.
    bool bool_value = false;
};

/// Reference to a local variable introduced by a `let` binding or parameter.
struct LocalRef {
    /// Interned name of the local binding.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Whether this read copies, moves, or merely borrows the local place.
    ValueUseKind use_kind = ValueUseKind::Copy;

    LocalRef() = default;
    explicit LocalRef(cstc::symbol::Symbol name, ValueUseKind use_kind = ValueUseKind::Copy)
        : name(name)
        , use_kind(use_kind) {}
};

/// Reference to a fieldless enum variant (`EnumType::Variant`).
struct EnumVariantRef {
    /// Interned name of the enum type.
    cstc::symbol::Symbol enum_name = cstc::symbol::kInvalidSymbol;
    /// Interned name of the variant.
    cstc::symbol::Symbol variant_name = cstc::symbol::kInvalidSymbol;
};

/// First source location that introduced body-internal runtime dependence.
struct TyRuntimeEvidence {
    /// Source location of the runtime contributor.
    cstc::span::SourceSpan span;
    /// Short diagnostic description of the contributor.
    std::string reason;
};

/// Canonical compile-time/runtime availability classification for a TyIR value.
enum class AvailabilityKind {
    /// The value is known to be available during compile-time evaluation.
    Ct,
    /// The value depends on runtime input or a runtime authorization boundary.
    Rt,
};

struct Availability;

/// Symbolic availability expression kind used by function signatures.
enum class AvailabilityExprKind {
    /// Always compile-time available.
    Ct,
    /// Unavoidable runtime dependence.
    Rt,
    /// Dependence on the availability of a function parameter.
    Param,
    /// Join of two symbolic availability expressions.
    Join,
};

/// Symbolic availability expression carried by function signatures.
///
/// `Availability` remains the concrete per-expression summary used while
/// lowering. `AvailabilityExpr` records the declaration-level contract that can
/// be instantiated at call sites with actual argument availability.
struct AvailabilityExpr {
    /// Expression variant.
    AvailabilityExprKind kind = AvailabilityExprKind::Ct;
    /// Parameter index when `kind == Param`.
    std::size_t param_index = 0;
    /// Left child when `kind == Join`.
    std::shared_ptr<AvailabilityExpr> lhs;
    /// Right child when `kind == Join`.
    std::shared_ptr<AvailabilityExpr> rhs;
};

/// Symbolic compile-time availability.
[[nodiscard]] inline AvailabilityExpr availability_expr_ct() { return {}; }

/// Symbolic runtime availability.
[[nodiscard]] inline AvailabilityExpr availability_expr_rt() {
    AvailabilityExpr expr;
    expr.kind = AvailabilityExprKind::Rt;
    return expr;
}

/// Symbolic parameter availability.
[[nodiscard]] inline AvailabilityExpr availability_expr_param(std::size_t index) {
    AvailabilityExpr expr;
    expr.kind = AvailabilityExprKind::Param;
    expr.param_index = index;
    return expr;
}

/// Joins two symbolic availability expressions with minimal simplification.
[[nodiscard]] inline AvailabilityExpr
    availability_expr_join(const AvailabilityExpr& lhs, const AvailabilityExpr& rhs) {
    if (lhs.kind == AvailabilityExprKind::Ct)
        return rhs;
    if (rhs.kind == AvailabilityExprKind::Ct)
        return lhs;
    if (lhs.kind == AvailabilityExprKind::Rt || rhs.kind == AvailabilityExprKind::Rt)
        return availability_expr_rt();

    AvailabilityExpr expr;
    expr.kind = AvailabilityExprKind::Join;
    expr.lhs = std::make_shared<AvailabilityExpr>(lhs);
    expr.rhs = std::make_shared<AvailabilityExpr>(rhs);
    return expr;
}

/// Projects a concrete lowering summary into a symbolic expression.
[[nodiscard]] inline AvailabilityExpr
    availability_expr_from_availability(const Availability& availability);

/// Substitutes parameter variables with concrete argument availability.
[[nodiscard]] inline Availability availability_expr_substitute(
    const AvailabilityExpr& expr, const std::vector<Availability>& args);

/// Returns true when the expression is definitely CT under all instantiations.
[[nodiscard]] inline bool availability_expr_always_ct(const AvailabilityExpr& expr) {
    switch (expr.kind) {
    case AvailabilityExprKind::Ct: return true;
    case AvailabilityExprKind::Rt:
    case AvailabilityExprKind::Param: return false;
    case AvailabilityExprKind::Join:
        return expr.lhs != nullptr && expr.rhs != nullptr && availability_expr_always_ct(*expr.lhs)
            && availability_expr_always_ct(*expr.rhs);
    }
    return false;
}

/// Returns true when the expression contains unavoidable runtime dependence.
[[nodiscard]] inline bool availability_expr_forces_rt(const AvailabilityExpr& expr) {
    switch (expr.kind) {
    case AvailabilityExprKind::Ct:
    case AvailabilityExprKind::Param: return false;
    case AvailabilityExprKind::Rt: return true;
    case AvailabilityExprKind::Join:
        return (expr.lhs != nullptr && availability_expr_forces_rt(*expr.lhs))
            || (expr.rhs != nullptr && availability_expr_forces_rt(*expr.rhs));
    }
    return false;
}

/// Renders a symbolic availability expression for diagnostics and TyIR output.
[[nodiscard]] inline std::string availability_expr_display(const AvailabilityExpr& expr) {
    switch (expr.kind) {
    case AvailabilityExprKind::Ct: return "CT";
    case AvailabilityExprKind::Rt: return "RT";
    case AvailabilityExprKind::Param: return "param" + std::to_string(expr.param_index);
    case AvailabilityExprKind::Join: {
        const std::string lhs = expr.lhs != nullptr ? availability_expr_display(*expr.lhs) : "?";
        const std::string rhs = expr.rhs != nullptr ? availability_expr_display(*expr.rhs) : "?";
        return "(" + lhs + " | " + rhs + ")";
    }
    }
    return "?";
}

/// Canonical TyIR availability summary.
///
/// `evidence` records the first concrete runtime origin when one is known. A
/// runtime-qualified source type can make an expression `Rt` without producing a
/// body-internal diagnostic evidence span.
struct Availability {
    /// Availability lattice point for the expression or block.
    AvailabilityKind kind = AvailabilityKind::Ct;
    /// First concrete runtime contributor, when available.
    std::optional<TyRuntimeEvidence> evidence;
    /// True when a CT-looking expression may depend on a runtime-allowed
    /// declaration parameter under a non-CT call-site instantiation.
    bool depends_on_runtime_allowed_param = false;
    /// Specific runtime-allowed declaration parameters contributing to this
    /// summary. Empty preserves the legacy unknown-parameter case.
    std::set<std::size_t> runtime_allowed_param_indices;
};

[[nodiscard]] inline AvailabilityExpr
    availability_expr_from_availability(const Availability& availability) {
    if (availability.kind == AvailabilityKind::Rt)
        return availability_expr_rt();
    return availability_expr_ct();
}

/// Compile-time-available summary.
[[nodiscard]] inline Availability availability_ct() { return {}; }

/// Runtime-dependent summary, optionally carrying first-origin evidence.
[[nodiscard]] inline Availability
    availability_rt(std::optional<TyRuntimeEvidence> evidence = std::nullopt) {
    return Availability{AvailabilityKind::Rt, std::move(evidence), false, {}};
}

/// Symbolic availability for an ordinary plain parameter inside a declaration.
[[nodiscard]] inline Availability availability_runtime_allowed_param() {
    return Availability{AvailabilityKind::Ct, std::nullopt, true, {}};
}

/// Symbolic availability for a specific ordinary plain declaration parameter.
[[nodiscard]] inline Availability availability_runtime_allowed_param(std::size_t index) {
    return Availability{AvailabilityKind::Ct, std::nullopt, true, {index}};
}

/// Joins two availability summaries; runtime wins and the first concrete
/// evidence is preserved.
[[nodiscard]] inline Availability
    availability_join(const Availability& lhs, const Availability& rhs) {
    if (lhs.kind == AvailabilityKind::Ct && rhs.kind == AvailabilityKind::Ct) {
        Availability joined{
            AvailabilityKind::Ct,
            std::nullopt,
            lhs.depends_on_runtime_allowed_param || rhs.depends_on_runtime_allowed_param,
            {}};
        joined.runtime_allowed_param_indices = lhs.runtime_allowed_param_indices;
        joined.runtime_allowed_param_indices.insert(
            rhs.runtime_allowed_param_indices.begin(), rhs.runtime_allowed_param_indices.end());
        return joined;
    }
    if (lhs.kind == AvailabilityKind::Ct) {
        Availability joined = rhs;
        joined.depends_on_runtime_allowed_param =
            lhs.depends_on_runtime_allowed_param || rhs.depends_on_runtime_allowed_param;
        joined.runtime_allowed_param_indices.insert(
            lhs.runtime_allowed_param_indices.begin(), lhs.runtime_allowed_param_indices.end());
        return joined;
    }
    if (rhs.kind == AvailabilityKind::Ct) {
        Availability joined = lhs;
        joined.depends_on_runtime_allowed_param =
            lhs.depends_on_runtime_allowed_param || rhs.depends_on_runtime_allowed_param;
        joined.runtime_allowed_param_indices.insert(
            rhs.runtime_allowed_param_indices.begin(), rhs.runtime_allowed_param_indices.end());
        return joined;
    }
    Availability joined{
        AvailabilityKind::Rt,
        lhs.evidence.has_value() ? lhs.evidence : rhs.evidence,
        lhs.depends_on_runtime_allowed_param || rhs.depends_on_runtime_allowed_param,
        {}};
    joined.runtime_allowed_param_indices = lhs.runtime_allowed_param_indices;
    joined.runtime_allowed_param_indices.insert(
        rhs.runtime_allowed_param_indices.begin(), rhs.runtime_allowed_param_indices.end());
    return joined;
}

[[nodiscard]] inline Availability availability_expr_substitute(
    const AvailabilityExpr& expr, const std::vector<Availability>& args) {
    switch (expr.kind) {
    case AvailabilityExprKind::Ct: return availability_ct();
    case AvailabilityExprKind::Rt: return availability_rt();
    case AvailabilityExprKind::Param:
        if (expr.param_index < args.size())
            return args[expr.param_index];
        return availability_ct();
    case AvailabilityExprKind::Join:
        return availability_join(
            expr.lhs != nullptr ? availability_expr_substitute(*expr.lhs, args) : availability_ct(),
            expr.rhs != nullptr ? availability_expr_substitute(*expr.rhs, args)
                                : availability_ct());
    }
    return availability_ct();
}

/// Projects a source/runtime-qualified type into an availability summary.
[[nodiscard]] inline Availability availability_from_type(const Ty& ty) {
    return ty_contains_runtime_tag(ty) ? availability_rt() : availability_ct();
}

/// Applies an availability summary to a type used for expression display or
/// lowering decisions.
[[nodiscard]] inline Ty with_availability_projection(Ty shape, const Availability& availability) {
    if (shape.is_never())
        return shape;
    shape.is_runtime = availability.kind == AvailabilityKind::Rt;
    return shape;
}

/// Returns true when the canonical availability summary is compile-time.
[[nodiscard]] inline bool is_ct_available(const Availability& availability) {
    return availability.kind == AvailabilityKind::Ct;
}

/// Returns true when an expression may satisfy a CT-required position.
[[nodiscard]] inline bool is_ct_required_available(const Availability& availability) {
    return availability.kind == AvailabilityKind::Ct
        && !availability.depends_on_runtime_allowed_param;
}

/// Single named-field initializer inside a struct construction expression.
struct TyStructInitField {
    /// Field name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Type-annotated value expression for this field.
    TyExprPtr value;
    /// Source location for the field initializer.
    cstc::span::SourceSpan span;
};

/// Typed struct construction expression (`Type { field: expr, … }`).
struct TyStructInit {
    /// Target struct type name.
    cstc::symbol::Symbol type_name = cstc::symbol::kInvalidSymbol;
    /// Resolved generic arguments applied to the constructed type.
    std::vector<Ty> generic_args;
    /// Typed field initializer list.
    std::vector<TyStructInitField> fields;
};

/// Typed unary operation expression.
struct TyUnary {
    /// Unary operator.
    cstc::ast::UnaryOp op = cstc::ast::UnaryOp::Not;
    /// Operand expression.
    TyExprPtr rhs;
};

/// Typed shared borrow expression (`&expr`).
struct TyBorrow {
    /// Borrowed place or value expression.
    TyExprPtr rhs;
};

/// Typed binary operation expression.
struct TyBinary {
    /// Binary operator.
    cstc::ast::BinaryOp op = cstc::ast::BinaryOp::Add;
    /// Left operand.
    TyExprPtr lhs;
    /// Right operand.
    TyExprPtr rhs;
};

/// Typed field access expression (`base.field`).
struct TyFieldAccess {
    /// Base object expression (must have a Named struct type).
    TyExprPtr base;
    /// Accessed field name.
    cstc::symbol::Symbol field = cstc::symbol::kInvalidSymbol;
    /// Whether the field access copies a value or denotes a borrowed place.
    ValueUseKind use_kind = ValueUseKind::Copy;

    TyFieldAccess() = default;
    TyFieldAccess(
        TyExprPtr base, cstc::symbol::Symbol field, ValueUseKind use_kind = ValueUseKind::Copy)
        : base(std::move(base))
        , field(field)
        , use_kind(use_kind) {}
};

/// Typed direct function call expression.
///
/// Cicest has no first-class functions; all callees are resolved to a
/// top-level function name during lowering.
struct TyCall {
    /// Resolved top-level function name.
    cstc::symbol::Symbol fn_name = cstc::symbol::kInvalidSymbol;
    /// Resolved generic arguments applied to the callee.
    std::vector<Ty> generic_args;
    /// Type-annotated argument list (count and types match the function signature).
    std::vector<TyExprPtr> args;
};

/// Typed generic function call that still needs more type information.
struct TyDeferredGenericCall {
    /// Resolved top-level function name.
    cstc::symbol::Symbol fn_name = cstc::symbol::kInvalidSymbol;
    /// Generic arguments aligned to the callee's generic parameter order.
    std::vector<std::optional<Ty>> generic_args;
    /// Type-annotated argument list.
    std::vector<TyExprPtr> args;
};

/// Typed declaration-validity probe expression.
struct TyDeclProbe {
    /// Probed expression when lowering successfully produced a typed form.
    std::optional<TyExprPtr> expr;
    /// True when the probe already failed during lowering.
    bool is_invalid = false;
    /// Optional lowering-time diagnostic retained for deferred reporting.
    std::optional<std::string> invalid_reason;
};

/// Typed runtime-authorized block expression (`runtime { ... }`).
struct TyRuntimeBlock {
    /// Typed body evaluated within a runtime authorization boundary.
    TyBlockPtr body;
};

/// Typed conditional expression (`if … { … } else { … }`).
struct TyIf {
    /// Condition expression; must have type `bool`.
    TyExprPtr condition;
    /// Then-branch block.
    TyBlockPtr then_block;
    /// Optional else-branch (block or nested `if`).
    std::optional<TyExprPtr> else_branch;
};

/// Typed infinite `loop` expression.
struct TyLoop {
    /// Loop body block.
    TyBlockPtr body;
};

/// Typed `while` loop expression.
struct TyWhile {
    /// Loop condition; must have type `bool`.
    TyExprPtr condition;
    /// Loop body block.
    TyBlockPtr body;
};

/// Initializer clause for a C-style `for` loop (the `let x = …` part).
struct TyForInit {
    /// True when the binding pattern is `_`.
    bool discard = false;
    /// Binding name for non-discard patterns.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Resolved type of the binding.
    Ty ty;
    /// Lowered initializer expression.
    TyExprPtr init;
    /// Source location for the initializer clause.
    cstc::span::SourceSpan span;
};

/// Typed C-style `for` loop expression.
struct TyFor {
    /// Optional initializer clause.
    std::optional<TyForInit> init;
    /// Optional loop condition; must have type `bool` when present.
    std::optional<TyExprPtr> condition;
    /// Optional step expression (evaluated after each iteration).
    std::optional<TyExprPtr> step;
    /// Loop body block.
    TyBlockPtr body;
};

/// Typed `break` expression (diverges the current control-flow path).
struct TyBreak {
    /// Optional break value propagated to the enclosing `loop`.
    std::optional<TyExprPtr> value;
};

/// Typed `continue` expression (diverges the current control-flow path).
struct TyContinue {};

/// Typed `return` expression (diverges the current control-flow path).
struct TyReturn {
    /// Optional return value.
    std::optional<TyExprPtr> value;
};

// ─── TyExpr ──────────────────────────────────────────────────────────────────

/// Type-annotated expression node — the core unit of TyIR.
///
/// Every `TyExpr` carries:
/// - `node`: the concrete expression variant,
/// - `ty`:   the inferred or annotated type,
/// - `span`: the source location,
/// - `availability`: canonical compile-time/runtime availability for the value.
struct TyExpr {
    /// Variant payload for all typed expression forms.
    using Node = std::variant<
        TyLiteral, LocalRef, EnumVariantRef, TyStructInit, TyBorrow, TyUnary, TyBinary,
        TyFieldAccess, TyCall, TyDeferredGenericCall, TyDeclProbe, TyRuntimeBlock, TyBlockPtr, TyIf,
        TyLoop, TyWhile, TyFor, TyBreak, TyContinue, TyReturn>;

    /// Concrete expression node payload.
    Node node;
    /// Inferred or annotated type of this expression.
    Ty ty;
    /// Source location for this expression.
    cstc::span::SourceSpan span;
    /// Canonical compile-time/runtime availability summary.
    Availability availability;
};

/// Sets expression availability, preserving runtime-qualified type projections.
inline void set_availability(TyExpr& expr, const Availability& availability) {
    expr.availability = availability;
    expr.ty = with_availability_projection(std::move(expr.ty), expr.availability);
}

/// Returns true when an expression value is compile-time available.
[[nodiscard]] inline bool is_ct_available(const TyExpr& expr) {
    return is_ct_available(expr.availability);
}

/// Returns true when an expression pointer is non-null and compile-time
/// available.
[[nodiscard]] inline bool is_ct_available(const TyExprPtr& expr) {
    return expr != nullptr && is_ct_available(*expr);
}

/// Constructs a heap-allocated typed expression.
[[nodiscard]] inline TyExprPtr make_ty_expr(
    cstc::span::SourceSpan span, TyExpr::Node node, Ty ty,
    const Availability& availability = availability_ct()) {
    TyExpr expr;
    expr.node = std::move(node);
    expr.ty = std::move(ty);
    expr.span = span;
    set_availability(expr, availability_join(availability_from_type(expr.ty), availability));
    return std::make_shared<TyExpr>(std::move(expr));
}

// ─── Statements ──────────────────────────────────────────────────────────────

/// Typed immutable `let` statement.
struct TyLetStmt {
    /// True when the binding pattern is `_` (discard).
    bool discard = false;
    /// Binding name for non-discard patterns.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Resolved (and possibly inferred) type of the binding.
    Ty ty;
    /// Lowered initializer expression.
    TyExprPtr init;
    /// Source location for the statement.
    cstc::span::SourceSpan span;
};

/// Typed expression statement (expression evaluated for side effects).
struct TyExprStmt {
    /// Evaluated expression.
    TyExprPtr expr;
    /// Source location for the statement.
    cstc::span::SourceSpan span;
};

/// Any statement form allowed in a typed block.
using TyStmt = std::variant<TyLetStmt, TyExprStmt>;

// ─── TyBlock ─────────────────────────────────────────────────────────────────

/// Type-annotated block expression (statement list + optional tail expression).
///
/// When execution can reach the tail expression, the block's `ty` matches the
/// tail expression type. If an earlier statement diverges, the block type is
/// `Never` even when a (syntactically present) tail expression exists.
/// Without a tail, `ty` is `Unit` if control can fall through the block end,
/// otherwise `Never`.
struct TyBlock {
    /// Ordered typed statements.
    std::vector<TyStmt> stmts;
    /// Optional final expression whose value is the block's value.
    std::optional<TyExprPtr> tail;
    /// Inferred block type (reachable `tail->ty`, else `Unit`/`Never`
    /// based on fallthrough).
    Ty ty;
    /// Source location for the full block.
    cstc::span::SourceSpan span;
    /// Canonical compile-time/runtime availability summary.
    Availability availability;
};

/// Sets block availability, preserving runtime-qualified type projections.
inline void set_availability(TyBlock& block, const Availability& availability) {
    block.availability = availability;
    block.ty = with_availability_projection(std::move(block.ty), block.availability);
}

/// Returns true when a block value is compile-time available.
[[nodiscard]] inline bool is_ct_available(const TyBlock& block) {
    return is_ct_available(block.availability);
}

/// Returns true when a block pointer is non-null and compile-time available.
[[nodiscard]] inline bool is_ct_available(const TyBlockPtr& block) {
    return block != nullptr && is_ct_available(*block);
}

/// Typed generic constraint attached to a generic declaration.
struct TyGenericConstraint {
    /// Type-annotated constraint expression.
    TyExprPtr expr;
    /// Source location for the full constraint.
    cstc::span::SourceSpan span;
};

// ─── Item declarations ────────────────────────────────────────────────────────

/// Typed named field declaration inside a struct.
struct TyFieldDecl {
    /// Field name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Resolved field type.
    Ty ty;
    /// Source location for the field.
    cstc::span::SourceSpan span;
};

/// Typed struct item declaration.
struct TyStructDecl {
    /// Struct type name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional lang item name for compiler-recognized types.
    cstc::symbol::Symbol lang_name = cstc::symbol::kInvalidSymbol;
    /// Declared generic parameter list preserved from the AST.
    std::vector<cstc::ast::GenericParam> generic_params;
    /// Resolved named field list.
    std::vector<TyFieldDecl> fields;
    /// True when declared as `struct Name;` (zero-sized type).
    bool is_zst = false;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
    /// Optional generic `where` constraints preserved from the AST.
    std::vector<cstc::ast::GenericConstraint> where_clause;
    /// Lowered generic `where` constraints used by later evaluation passes.
    std::vector<TyGenericConstraint> lowered_where_clause;
};

/// Typed fieldless enum variant.
struct TyEnumVariant {
    /// Variant name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional numeric discriminant text.
    std::optional<cstc::symbol::Symbol> discriminant;
    /// Source location for the variant.
    cstc::span::SourceSpan span;
};

/// Typed enum item declaration.
struct TyEnumDecl {
    /// Enum type name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional lang item name for compiler-recognized types.
    cstc::symbol::Symbol lang_name = cstc::symbol::kInvalidSymbol;
    /// Declared generic parameter list preserved from the AST.
    std::vector<cstc::ast::GenericParam> generic_params;
    /// Declared variant list.
    std::vector<TyEnumVariant> variants;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
    /// Optional generic `where` constraints preserved from the AST.
    std::vector<cstc::ast::GenericConstraint> where_clause;
    /// Lowered generic `where` constraints used by later evaluation passes.
    std::vector<TyGenericConstraint> lowered_where_clause;
};

/// Availability requirement attached to a function parameter.
enum class ParamRequirement {
    /// Runtime-dependent arguments are accepted by the ordinary lifted-call rule.
    RuntimeAllowed,
    /// The call argument must be compile-time available.
    CtRequired,
};

/// Typed function parameter declaration.
struct TyParam {
    /// Parameter name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Resolved parameter type.
    Ty ty;
    /// Source location for the parameter.
    cstc::span::SourceSpan span;
    /// Availability requirement enforced at call sites.
    ParamRequirement requirement = ParamRequirement::RuntimeAllowed;

    /// Returns true when this parameter rejects runtime-dependent arguments.
    [[nodiscard]] constexpr bool requires_ct() const {
        return requirement == ParamRequirement::CtRequired;
    }
};

/// Typed function item declaration.
///
/// Surface sugar such as `runtime fn` is normalized into `return_ty`.
///
/// The original item-level marker is also preserved in `is_runtime` so later
/// passes can distinguish declaration-level runtime boundaries from nested
/// type-level runtime tags.
struct TyFnDecl {
    /// Function name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Declared generic parameter list preserved from the AST.
    std::vector<cstc::ast::GenericParam> generic_params;
    /// Typed parameter list.
    std::vector<TyParam> params;
    /// Resolved return type (defaults to `Unit` when absent in source).
    Ty return_ty;
    /// Type-annotated body block.
    TyBlockPtr body;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
    /// True when the function was declared with the `runtime` item modifier.
    bool is_runtime = false;
    /// Optional generic `where` constraints preserved from the AST.
    std::vector<cstc::ast::GenericConstraint> where_clause;
    /// Lowered generic `where` constraints used by later evaluation passes.
    std::vector<TyGenericConstraint> lowered_where_clause;
    /// Symbolic availability for each declared parameter.
    std::vector<AvailabilityExpr> param_availability;
    /// Symbolic availability summary for the function result.
    AvailabilityExpr result_availability;
    /// First unavoidable body-internal runtime contributor, when present.
    std::optional<TyRuntimeEvidence> internal_runtime_evidence;
};

/// Typed extern function declaration (no body).
///
/// Surface sugar such as `runtime extern ... fn` is normalized into `return_ty`.
///
/// The original item-level marker is also preserved in `is_runtime` so later
/// passes can distinguish declaration-level runtime boundaries from nested
/// type-level runtime tags.
struct TyExternFnDecl {
    /// ABI string (e.g. "lang", "c").
    cstc::symbol::Symbol abi = cstc::symbol::kInvalidSymbol;
    /// Function name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Linked symbol name used by codegen.
    cstc::symbol::Symbol link_name = cstc::symbol::kInvalidSymbol;
    /// Typed parameter list.
    std::vector<TyParam> params;
    /// Resolved return type (defaults to `Unit` when absent in source).
    Ty return_ty;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
    /// True when the function was declared with the `runtime` item modifier.
    bool is_runtime = false;
    /// Symbolic availability for each declared parameter.
    std::vector<AvailabilityExpr> param_availability;
    /// Symbolic availability summary for the extern result.
    AvailabilityExpr result_availability;
    /// Extern runtime-result declarations are unavoidable runtime contributors.
    std::optional<TyRuntimeEvidence> internal_runtime_evidence;
};

/// Typed extern struct declaration (opaque foreign type, no fields).
///
/// Unlike `TyStructDecl` with `is_zst`, an extern struct preserves its ABI
/// and foreign/opaque nature so that later passes can distinguish it from a
/// normal user-defined zero-sized type.
struct TyExternStructDecl {
    /// ABI string (e.g. "lang", "c").
    cstc::symbol::Symbol abi = cstc::symbol::kInvalidSymbol;
    /// Struct type name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional lang item name for compiler-recognized types.
    cstc::symbol::Symbol lang_name = cstc::symbol::kInvalidSymbol;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
};

/// Any top-level TyIR item declaration.
using TyItem = std::variant<TyStructDecl, TyEnumDecl, TyFnDecl, TyExternFnDecl, TyExternStructDecl>;

/// Full typed program — root of the TyIR tree.
struct TyProgram {
    /// Top-level item list in source order.
    std::vector<TyItem> items;
};

} // namespace cstc::tyir

#endif // CICEST_COMPILER_CSTC_TYIR_TYIR_HPP
