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
#include <string>
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
    /// Referenced pointee when `kind == TyKind::Ref`.
    std::shared_ptr<Ty> pointee;
    /// Ownership behavior attached to this resolved type.
    ValueSemantics semantics = ValueSemantics::Copy;

    [[nodiscard]] constexpr bool is_unit() const { return kind == TyKind::Unit; }
    [[nodiscard]] constexpr bool is_never() const { return kind == TyKind::Never; }
    [[nodiscard]] constexpr bool is_named() const { return kind == TyKind::Named; }
    [[nodiscard]] constexpr bool is_ref() const { return kind == TyKind::Ref; }
    [[nodiscard]] constexpr bool is_copy() const {
        return semantics == ValueSemantics::Copy || semantics == ValueSemantics::Ref;
    }
    [[nodiscard]] constexpr bool is_move_only() const { return semantics == ValueSemantics::Move; }

    friend constexpr bool operator==(const Ty& lhs, const Ty& rhs) {
        if (lhs.kind != rhs.kind || lhs.name != rhs.name)
            return false;
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
        switch (kind) {
        case TyKind::Ref:
            if (pointee != nullptr)
                return "&" + pointee->display();
            return "&<unknown>";
        case TyKind::Unit: return "Unit";
        case TyKind::Num: return "num";
        case TyKind::Str: return "str";
        case TyKind::Bool: return "bool";
        case TyKind::Never: return "!";
        case TyKind::Named:
            if (display_name.is_valid())
                return std::string(display_name.as_str());
            return name.is_valid() ? std::string(name.as_str()) : "<named>";
        }
        return "<unknown-type>";
    }
};

/// Factory helpers for the well-known primitive types.
namespace ty {
/// Unit type `()`.
inline Ty unit() {
    return {
        TyKind::Unit, cstc::symbol::kInvalidSymbol, cstc::symbol::kInvalidSymbol,
        nullptr,      ValueSemantics::Copy,
    };
}
/// Numeric type `num`.
inline Ty num() {
    return {
        TyKind::Num, cstc::symbol::kInvalidSymbol, cstc::symbol::kInvalidSymbol,
        nullptr,     ValueSemantics::Copy,
    };
}
/// String type `str`.
inline Ty str() {
    return {
        TyKind::Str, cstc::symbol::kInvalidSymbol, cstc::symbol::kInvalidSymbol,
        nullptr,     ValueSemantics::Move,
    };
}
/// Boolean type `bool`.
inline Ty bool_() {
    return {
        TyKind::Bool, cstc::symbol::kInvalidSymbol, cstc::symbol::kInvalidSymbol,
        nullptr,      ValueSemantics::Copy,
    };
}
/// Never / bottom type (diverging expression).
inline Ty never() {
    return {
        TyKind::Never, cstc::symbol::kInvalidSymbol, cstc::symbol::kInvalidSymbol,
        nullptr,       ValueSemantics::Copy,
    };
}
/// User-defined named type (struct or enum).
inline Ty named(
    cstc::symbol::Symbol sym, cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol,
    ValueSemantics semantics = ValueSemantics::Move) {
    return {TyKind::Named, sym, display_name, nullptr, semantics};
}
/// Shared immutable reference type `&T`.
inline Ty ref(const Ty& pointee) {
    return {
        TyKind::Ref,
        cstc::symbol::kInvalidSymbol,
        cstc::symbol::kInvalidSymbol,
        std::make_shared<Ty>(pointee),
        ValueSemantics::Ref,
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
        /// String literal (e.g. `"hello"`).
        Str,
        /// Boolean literal (`true` or `false`).
        Bool,
        /// Unit literal `()`.
        Unit,
    };

    /// Literal category.
    Kind kind = Kind::Unit;
    /// Interned source text of the literal (empty symbol for `Unit`).
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
    /// Type-annotated argument list (count and types match the function signature).
    std::vector<TyExprPtr> args;
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
/// - `span`: the source location.
struct TyExpr {
    /// Variant payload for all typed expression forms.
    using Node = std::variant<
        TyLiteral, LocalRef, EnumVariantRef, TyStructInit, TyBorrow, TyUnary, TyBinary,
        TyFieldAccess, TyCall, TyBlockPtr, TyIf, TyLoop, TyWhile, TyFor, TyBreak, TyContinue,
        TyReturn>;

    /// Concrete expression node payload.
    Node node;
    /// Inferred or annotated type of this expression.
    Ty ty;
    /// Source location for this expression.
    cstc::span::SourceSpan span;
};

/// Constructs a heap-allocated typed expression.
[[nodiscard]] inline TyExprPtr make_ty_expr(cstc::span::SourceSpan span, TyExpr::Node node, Ty ty) {
    return std::make_shared<TyExpr>(TyExpr{std::move(node), std::move(ty), span});
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
    /// Resolved named field list.
    std::vector<TyFieldDecl> fields;
    /// True when declared as `struct Name;` (zero-sized type).
    bool is_zst = false;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
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
    /// Declared variant list.
    std::vector<TyEnumVariant> variants;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
};

/// Typed function parameter declaration.
struct TyParam {
    /// Parameter name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Resolved parameter type.
    Ty ty;
    /// Source location for the parameter.
    cstc::span::SourceSpan span;
};

/// Typed function item declaration.
struct TyFnDecl {
    /// Function name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Typed parameter list.
    std::vector<TyParam> params;
    /// Resolved return type (defaults to `Unit` when absent in source).
    Ty return_ty;
    /// Type-annotated body block.
    TyBlockPtr body;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
};

/// Typed extern function declaration (no body).
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
