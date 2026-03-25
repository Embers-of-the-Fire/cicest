#ifndef CICEST_COMPILER_CSTC_AST_AST_HPP
#define CICEST_COMPILER_CSTC_AST_AST_HPP

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>

namespace cstc::ast {

/// Base node for all expression forms.
struct Expr;
/// Expression block with statement list and optional tail expression.
struct BlockExpr;

/// Shared pointer used for recursive expression references.
using ExprPtr = std::shared_ptr<Expr>;
/// Shared pointer used for recursive block references.
using BlockPtr = std::shared_ptr<BlockExpr>;
/// Forward declaration for recursive type references.
struct TypeRef;
/// Shared pointer used for recursive type references.
using TypeRefPtr = std::shared_ptr<TypeRef>;

/// Kind of type represented by a `TypeRef`.
enum class TypeKind {
    /// Shared immutable reference type (`&T`).
    Ref,
    /// Built-in unit type.
    Unit,
    /// Built-in numeric type.
    Num,
    /// Built-in string type.
    Str,
    /// Built-in boolean type.
    Bool,
    /// User-defined named type.
    Named,
    /// Never / bottom type (`!`).
    Never,
};

/// Type reference used in declarations and annotations.
struct TypeRef {
    /// Category of the referenced type.
    TypeKind kind = TypeKind::Unit;
    /// Interned symbol for the source type token.
    cstc::symbol::Symbol symbol = cstc::symbol::kInvalidSymbol;
    /// Human-facing name to preserve source diagnostics after resolution.
    cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol;
    /// Referenced inner type when `kind == TypeKind::Ref`.
    TypeRefPtr pointee;
    /// True when this type is prefixed with the inert `runtime` qualifier.
    bool is_runtime = false;
};

/// Declaration attribute attached to an item.
struct Attribute {
    /// Attribute name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional string payload, without surrounding quotes.
    std::optional<cstc::symbol::Symbol> value;
    /// Source location for the full attribute.
    cstc::span::SourceSpan span;
};

/// Named field declaration inside a struct definition.
struct FieldDecl {
    /// Field identifier.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Field type.
    TypeRef type;
    /// Source location for the field declaration.
    cstc::span::SourceSpan span;
};

/// Struct item declaration.
struct StructDecl {
    /// True when the item is declared with `pub`.
    bool is_public = false;
    /// Struct type name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Human-facing type name used in diagnostics after module rewriting.
    cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol;
    /// Declared named fields.
    std::vector<FieldDecl> fields;
    /// True when declared as `struct Name;`.
    bool is_zst = false;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
    /// Attributes attached to the declaration.
    std::vector<Attribute> attributes;
};

/// Fieldless enum variant declaration.
struct EnumVariant {
    /// Variant name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional numeric discriminant text.
    std::optional<cstc::symbol::Symbol> discriminant;
    /// Source location for the variant.
    cstc::span::SourceSpan span;
};

/// Enum item declaration.
struct EnumDecl {
    /// True when the item is declared with `pub`.
    bool is_public = false;
    /// Enum type name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Human-facing type name used in diagnostics after module rewriting.
    cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol;
    /// Declared variant list.
    std::vector<EnumVariant> variants;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
    /// Attributes attached to the declaration.
    std::vector<Attribute> attributes;
};

/// Function parameter declaration.
struct Param {
    /// Parameter name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Parameter type.
    TypeRef type;
    /// Source location for the parameter.
    cstc::span::SourceSpan span;
};

/// Immutable `let` statement.
struct LetStmt {
    /// True when binding pattern is `_`.
    bool discard = false;
    /// Binding name for non-discard patterns.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional explicit type annotation.
    std::optional<TypeRef> type_annotation;
    /// Initializer expression.
    ExprPtr initializer;
    /// Source location for the statement.
    cstc::span::SourceSpan span;
};

/// Expression statement terminated by semicolon.
struct ExprStmt {
    /// Evaluated expression.
    ExprPtr expr;
    /// Source location for the statement.
    cstc::span::SourceSpan span;
};

/// Any statement form allowed in a block.
using Stmt = std::variant<LetStmt, ExprStmt>;

/// Block expression node.
struct BlockExpr {
    /// Ordered statements inside the block.
    std::vector<Stmt> statements;
    /// Optional final value expression.
    std::optional<ExprPtr> tail;
    /// Source location for the full block.
    cstc::span::SourceSpan span;
};

/// Literal expression node.
struct LiteralExpr {
    /// Category of literal value.
    enum class Kind {
        /// Numeric literal.
        Num,
        /// String literal.
        Str,
        /// Boolean literal.
        Bool,
        /// Unit literal `()`.
        Unit,
    };

    /// Literal category.
    Kind kind = Kind::Unit;
    /// Interned symbol for the original literal text.
    cstc::symbol::Symbol symbol = cstc::symbol::kInvalidSymbol;
    /// Parsed boolean value for boolean literals.
    bool bool_value = false;
};

/// Path expression (`name` or `Enum::Variant`).
struct PathExpr {
    /// Left segment (`name` or enum name).
    cstc::symbol::Symbol head = cstc::symbol::kInvalidSymbol;
    /// Optional right segment (`Variant`).
    std::optional<cstc::symbol::Symbol> tail;
    /// Human-facing head segment used in diagnostics.
    cstc::symbol::Symbol display_head = cstc::symbol::kInvalidSymbol;
};

/// Single field initializer inside a struct construction.
struct StructInitField {
    /// Field name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Field value expression.
    ExprPtr value;
    /// Source location for the field initializer.
    cstc::span::SourceSpan span;
};

/// Struct initializer expression (`Type { ... }`).
struct StructInitExpr {
    /// Target type name.
    cstc::symbol::Symbol type_name = cstc::symbol::kInvalidSymbol;
    /// Human-facing type name used in diagnostics.
    cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol;
    /// Field initializer list.
    std::vector<StructInitField> fields;
};

/// Unary operator kind.
enum class UnaryOp {
    /// Shared immutable borrow (`&expr`).
    Borrow,
    /// Arithmetic negation (`-expr`).
    Negate,
    /// Logical negation (`!expr`).
    Not,
};

/// Unary operation expression.
struct UnaryExpr {
    /// Unary operator.
    UnaryOp op = UnaryOp::Not;
    /// Operand expression.
    ExprPtr rhs;
};

/// Binary operator kind.
enum class BinaryOp {
    /// Addition.
    Add,
    /// Subtraction.
    Sub,
    /// Multiplication.
    Mul,
    /// Division.
    Div,
    /// Remainder.
    Mod,
    /// Equality.
    Eq,
    /// Inequality.
    Ne,
    /// Less-than.
    Lt,
    /// Less-than-or-equal.
    Le,
    /// Greater-than.
    Gt,
    /// Greater-than-or-equal.
    Ge,
    /// Logical conjunction.
    And,
    /// Logical disjunction.
    Or,
};

/// Binary operation expression.
struct BinaryExpr {
    /// Binary operator.
    BinaryOp op = BinaryOp::Add;
    /// Left operand.
    ExprPtr lhs;
    /// Right operand.
    ExprPtr rhs;
};

/// Field access expression (`base.field`).
struct FieldAccessExpr {
    /// Base object expression.
    ExprPtr base;
    /// Accessed field name.
    cstc::symbol::Symbol field = cstc::symbol::kInvalidSymbol;
};

/// Function call expression.
struct CallExpr {
    /// Called expression.
    ExprPtr callee;
    /// Positional call arguments.
    std::vector<ExprPtr> args;
};

/// Conditional expression.
struct IfExpr {
    /// Condition expression.
    ExprPtr condition;
    /// Then block.
    BlockPtr then_block;
    /// Optional else branch (block or nested if).
    std::optional<ExprPtr> else_branch;
};

/// Infinite `loop` expression.
struct LoopExpr {
    /// Loop body.
    BlockPtr body;
};

/// `while` loop expression.
struct WhileExpr {
    /// Loop condition.
    ExprPtr condition;
    /// Loop body.
    BlockPtr body;
};

/// `let` initializer form used in `for` loop headers.
struct ForInitLet {
    /// True when init binding is `_`.
    bool discard = false;
    /// Binding name for non-discard patterns.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional type annotation.
    std::optional<TypeRef> type_annotation;
    /// Initializer expression.
    ExprPtr initializer;
    /// Source location for the initializer fragment.
    cstc::span::SourceSpan span;
};

/// C-style `for` loop expression.
struct ForExpr {
    /// Optional initializer (`let` or expression).
    std::optional<std::variant<ForInitLet, ExprPtr>> init;
    /// Optional loop condition.
    std::optional<ExprPtr> condition;
    /// Optional step expression.
    std::optional<ExprPtr> step;
    /// Loop body.
    BlockPtr body;
};

/// `break` expression.
struct BreakExpr {
    /// Optional break value.
    std::optional<ExprPtr> value;
};

/// `continue` expression.
struct ContinueExpr {};

/// `return` expression.
struct ReturnExpr {
    /// Optional return value.
    std::optional<ExprPtr> value;
};

/// Discriminated union for all expression forms.
struct Expr {
    /// Variant payload for expression nodes.
    using Node = std::variant<
        LiteralExpr, PathExpr, StructInitExpr, UnaryExpr, BinaryExpr, FieldAccessExpr, CallExpr,
        BlockPtr, IfExpr, LoopExpr, WhileExpr, ForExpr, BreakExpr, ContinueExpr, ReturnExpr>;

    /// Concrete expression node payload.
    Node node;
    /// Source location for this expression.
    cstc::span::SourceSpan span;
};

/// Constructs a heap-allocated expression node.
[[nodiscard]] inline ExprPtr make_expr(cstc::span::SourceSpan span, Expr::Node node) {
    return std::make_shared<Expr>(Expr{std::move(node), span});
}

/// Function item declaration.
struct FnDecl {
    /// True when the item is declared with `pub`.
    bool is_public = false;
    /// Function name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Human-facing function name used in diagnostics after module rewriting.
    cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol;
    /// Function parameter list.
    std::vector<Param> params;
    /// Optional explicit return type.
    std::optional<TypeRef> return_type;
    /// Function body block.
    BlockPtr body;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
    /// Attributes attached to the declaration.
    std::vector<Attribute> attributes;
    /// True when the declaration is prefixed with `runtime`.
    bool is_runtime = false;
};

/// Extern function declaration (no body).
struct ExternFnDecl {
    /// True when the item is declared with `pub`.
    bool is_public = false;
    /// ABI string (e.g. "lang", "c").
    cstc::symbol::Symbol abi = cstc::symbol::kInvalidSymbol;
    /// Function name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Human-facing function name used in diagnostics after module rewriting.
    cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol;
    /// Function parameter list.
    std::vector<Param> params;
    /// Optional explicit return type.
    std::optional<TypeRef> return_type;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
    /// Attributes attached to the declaration.
    std::vector<Attribute> attributes;
    /// True when the declaration is prefixed with `runtime`.
    bool is_runtime = false;
};

/// Extern struct declaration (opaque, no fields).
struct ExternStructDecl {
    /// True when the item is declared with `pub`.
    bool is_public = false;
    /// ABI string (e.g. "lang", "c").
    cstc::symbol::Symbol abi = cstc::symbol::kInvalidSymbol;
    /// Struct type name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Human-facing type name used in diagnostics after module rewriting.
    cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
    /// Attributes attached to the declaration.
    std::vector<Attribute> attributes;
};

/// One imported binding inside an import declaration.
struct ImportItem {
    /// Exported name in the source module.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional local alias introduced by `as`.
    std::optional<cstc::symbol::Symbol> alias;
    /// Source location for the import item.
    cstc::span::SourceSpan span;
};

/// Import declaration.
struct ImportDecl {
    /// True when the item is declared with `pub`.
    bool is_public = false;
    /// Imported item list.
    std::vector<ImportItem> items;
    /// Module path literal contents, without surrounding quotes.
    cstc::symbol::Symbol path = cstc::symbol::kInvalidSymbol;
    /// Source location for the full item.
    cstc::span::SourceSpan span;
};

/// Any top-level declaration item.
using Item = std::variant<StructDecl, EnumDecl, FnDecl, ExternFnDecl, ExternStructDecl, ImportDecl>;

/// Full parsed source file.
struct Program {
    /// Top-level item list.
    std::vector<Item> items;
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_AST_HPP
