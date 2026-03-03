#ifndef CICEST_COMPILER_CSTC_AST_EXPR_HPP
#define CICEST_COMPILER_CSTC_AST_EXPR_HPP

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <cstc_ast/keyword.hpp>
#include <cstc_ast/node_id.hpp>
#include <cstc_ast/pat.hpp>
#include <cstc_ast/stmt.hpp>
#include <cstc_ast/type.hpp>
#include <cstc_span/registry.hpp>

namespace cstc::ast {

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

enum class UnaryOp {
    /// `-expr`
    Neg,
    /// `!expr`
    Not,
    /// `&expr`
    Borrow,
    /// `*expr`
    Deref,
};

enum class BinaryOp {
    Add,    // +
    Sub,    // -
    Mul,    // *
    Div,    // /
    Mod,    // %
    BitAnd, // &
    BitOr,  // |
    BitXor, // ^
    Shl,    // <<
    Shr,    // >>
    Eq,     // ==
    Ne,     // !=
    Lt,     // <
    Gt,     // >
    Le,     // <=
    Ge,     // >=
    And,    // &&
    Or,     // ||
    Assign, // =
};

// ---------------------------------------------------------------------------
// Helper structs
// ---------------------------------------------------------------------------

struct Expr;

/// A single arm in a `match` expression: `pattern => body`.
struct MatchArm {
    cstc::span::SourceSpan span;
    std::unique_ptr<Pat> pat;
    std::unique_ptr<Expr> body;
};

/// A lambda parameter: `name (: Type)?`.
struct LambdaParam {
    cstc::span::SourceSpan span;
    Symbol name;
    std::optional<std::unique_ptr<TypeNode>> ty;
};

/// A named field in a constructor expression: `name: expr`.
struct ExprField {
    cstc::span::SourceSpan span;
    Symbol name;
    std::unique_ptr<Expr> value;
};

// ---------------------------------------------------------------------------
// Expression kinds
// ---------------------------------------------------------------------------

/// A literal expression: `42`, `true`, `"hello"`.
struct LitExpr {
    Lit lit;
};

/// A path expression: `x`, `Foo::bar`.
struct PathExpr {
    Path path;
};

/// A block expression: `{ stmts }`.
struct BlockExpr {
    Block block;
};

/// A grouped (parenthesized) expression: `(expr)`.
struct GroupedExpr {
    std::unique_ptr<Expr> inner;
};

/// A tuple expression: `(a, b, c)`.
struct TupleExpr {
    std::vector<std::unique_ptr<Expr>> elements;
};

/// A unary operation: `-x`, `!x`, `&x`, `*x`.
struct UnaryExpr {
    UnaryOp op;
    std::unique_ptr<Expr> operand;
};

/// A binary operation: `a + b`, `x == y`.
struct BinaryExpr {
    BinaryOp op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

/// A function call: `foo(a, b)`.
struct CallExpr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
};

/// A method call: `expr.method(args)`.
struct MethodCallExpr {
    std::unique_ptr<Expr> receiver;
    PathSegment method;
    std::optional<GenericArgs> turbofish;
    std::vector<std::unique_ptr<Expr>> args;
};

/// A field access: `expr.field`.
struct FieldExpr {
    std::unique_ptr<Expr> object;
    Symbol field;
};

/// A named-field constructor: `Point { x: 1.0, y: 2.0 }`.
struct ConstructorFieldsExpr {
    Path constructor;
    std::vector<ExprField> fields;
};

/// A positional constructor: `Some(42)`.
struct ConstructorPositionalExpr {
    Path constructor;
    std::vector<std::unique_ptr<Expr>> args;
};

/// A lambda expression: `lambda(x: i32) { x + 1 }`.
struct LambdaExpr {
    std::vector<LambdaParam> params;
    Block body;
};

/// A match expression: `match expr { arm, ... }`.
struct MatchExpr {
    std::unique_ptr<Expr> scrutinee;
    std::vector<MatchArm> arms;
};

/// An if expression: `if cond { then } else { otherwise }`.
struct IfExpr {
    std::unique_ptr<Expr> cond;
    Block then_block;
    /// Either an `else { }` block or an `else if` (boxed IfExpr).
    std::optional<std::unique_ptr<Expr>> else_expr;
};

/// A loop expression: `loop { body }`.
struct LoopExpr {
    Block body;
};

/// A return expression: `return expr`.
struct ReturnExpr {
    std::optional<std::unique_ptr<Expr>> value;
};

/// A keyword-prefixed block expression: `async { expr }`, `runtime { expr }`.
struct KeywordBlockExpr {
    std::vector<KeywordModifier> keywords;
    Block body;
};

/// A turbofish expression: `expr::<Type, ...>`.
struct TurbofishExpr {
    std::unique_ptr<Expr> base;
    GenericArgs args;
};

/// Discriminated union of all expression forms.
using ExprKind = std::variant<
    LitExpr, PathExpr, BlockExpr, GroupedExpr, TupleExpr, UnaryExpr, BinaryExpr, CallExpr,
    MethodCallExpr, FieldExpr, ConstructorFieldsExpr, ConstructorPositionalExpr, LambdaExpr,
    MatchExpr, IfExpr, LoopExpr, ReturnExpr, KeywordBlockExpr, TurbofishExpr>;

// ---------------------------------------------------------------------------
// Expr
// ---------------------------------------------------------------------------

/// An expression node in the AST.
struct Expr {
    NodeId id;
    cstc::span::SourceSpan span;
    ExprKind kind;
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_EXPR_HPP
