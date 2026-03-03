#ifndef CICEST_COMPILER_CSTC_AST_STMT_HPP
#define CICEST_COMPILER_CSTC_AST_STMT_HPP

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <cstc_ast/ast_fwd.hpp>
#include <cstc_ast/node_id.hpp>
#include <cstc_ast/pat.hpp>
#include <cstc_ast/type.hpp>
#include <cstc_span/registry.hpp>

namespace cstc::ast {

// ---------------------------------------------------------------------------
// Block
// ---------------------------------------------------------------------------

struct Stmt;

/// A block `{ stmt; stmt; ... }`.
/// The last statement may be an expression without a trailing semicolon,
/// making it the block's value.
struct Block {
    NodeId id;
    cstc::span::SourceSpan span;
    std::vector<Stmt> stmts;
};

// ---------------------------------------------------------------------------
// Statement kinds
// ---------------------------------------------------------------------------

/// `let pat (: Type)? (= expr)?;`
struct LetStmt {
    std::unique_ptr<Pat> pat;
    std::optional<std::unique_ptr<TypeNode>> ty;
    std::optional<std::unique_ptr<Expr>> init;
};

/// An expression used as a statement.
/// `has_semi` distinguishes `expr;` (value discarded) from `expr` (tail expression).
struct ExprStmt {
    std::unique_ptr<Expr> expr;
    bool has_semi;
};

/// A declaration used as a statement (e.g. `fn` inside a block).
struct ItemStmt {
    std::unique_ptr<Item> item;
};

/// Discriminated union of all statement forms.
using StmtKind = std::variant<LetStmt, ExprStmt, ItemStmt>;

// ---------------------------------------------------------------------------
// Stmt
// ---------------------------------------------------------------------------

/// A statement node in the AST.
struct Stmt {
    NodeId id;
    cstc::span::SourceSpan span;
    StmtKind kind;
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_STMT_HPP
