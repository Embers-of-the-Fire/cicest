#ifndef CICEST_COMPILER_CSTC_AST_KEYWORD_HPP
#define CICEST_COMPILER_CSTC_AST_KEYWORD_HPP

#include <optional>
#include <vector>

#include <cstc_ast/symbol.hpp>
#include <cstc_span/registry.hpp>

namespace cstc::ast {

/// The kind of keyword modifier on a type or expression.
enum class KeywordKind {
    /// `runtime` or `runtime<R>`
    Runtime,
    /// `!runtime` or `const`
    NotRuntime,
};

/// A single keyword modifier occurrence, e.g. `runtime<R>` or `const`.
/// Positive forms may carry an optional type variable.
struct KeywordModifier {
    cstc::span::SourceSpan span;
    KeywordKind kind;
    /// Optional type variable on positive forms: `runtime<R>`.
    /// Always `std::nullopt` for negative forms.
    std::optional<Symbol> type_var;
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_KEYWORD_HPP
