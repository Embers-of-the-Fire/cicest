#ifndef CICEST_COMPILER_CSTC_AST_GENERICS_HPP
#define CICEST_COMPILER_CSTC_AST_GENERICS_HPP

#include <memory>
#include <optional>
#include <vector>

#include <cstc_ast/ast_fwd.hpp>
#include <cstc_ast/node_id.hpp>
#include <cstc_ast/symbol.hpp>
#include <cstc_span/registry.hpp>

namespace cstc::ast {

/// A single generic parameter declaration, e.g. `T` in `fn<T>`.
/// No inline bounds — there are no traits.
struct GenericParam {
    NodeId id;
    cstc::span::SourceSpan span;
    Symbol name;
};

/// The generic parameter list `<T, U, V>`.
struct GenericParams {
    cstc::span::SourceSpan span;
    std::vector<GenericParam> params;
};

/// A single predicate in a `where` clause.
/// Each predicate is a compile-time boolean expression (e.g. `sizeof(T) == 4`).
struct WhereExpr {
    cstc::span::SourceSpan span;
    std::unique_ptr<Expr> expr;
};

/// A `where` clause: `where sizeof(T) == 4, is_power_of_two(sizeof(T))`.
struct WhereClause {
    cstc::span::SourceSpan span;
    std::vector<WhereExpr> predicates;
};

/// Combined generics: optional parameter list + optional where clause.
struct Generics {
    std::optional<GenericParams> params;
    std::optional<WhereClause> where_clause;
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_GENERICS_HPP
