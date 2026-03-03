#ifndef CICEST_COMPILER_CSTC_AST_PAT_HPP
#define CICEST_COMPILER_CSTC_AST_PAT_HPP

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <cstc_ast/node_id.hpp>
#include <cstc_ast/symbol.hpp>
#include <cstc_ast/type.hpp>
#include <cstc_span/registry.hpp>

namespace cstc::ast {

// ---------------------------------------------------------------------------
// Literals (shared with expressions)
// ---------------------------------------------------------------------------

/// The kind of a literal value.
enum class LitKind {
    Int,
    Float,
    Bool,
    String,
    Char,
};

/// A literal value, e.g. `42`, `3.14`, `true`, `"hello"`, `'c'`.
struct Lit {
    cstc::span::SourceSpan span;
    LitKind kind;
    /// The raw text of the literal as it appeared in source.
    std::string value;
};

// ---------------------------------------------------------------------------
// Pattern kinds
// ---------------------------------------------------------------------------

struct Pat;

/// Wildcard pattern: `_`
struct WildcardPat {};

/// Variable binding pattern: `name`
struct BindingPat {
    Symbol name;
};

/// Literal pattern: `42`, `true`, `"hello"`
struct LitPat {
    Lit lit;
};

/// Named-field constructor pattern: `Circle { radius: r }`
struct ConstructorFieldsPat {
    Path constructor;
    struct Field {
        cstc::span::SourceSpan span;
        Symbol name;
        std::unique_ptr<Pat> pat;
    };
    std::vector<Field> fields;
};

/// Positional constructor pattern: `Cons(x, xs)`
struct ConstructorPositionalPat {
    Path constructor;
    std::vector<std::unique_ptr<Pat>> args;
};

/// Unit constructor pattern: `None`, `Nil`
struct ConstructorUnitPat {
    Path constructor;
};

/// Tuple pattern: `(a, b, c)`
struct TuplePat {
    std::vector<std::unique_ptr<Pat>> elements;
};

/// Or-pattern: `A | B`
struct OrPat {
    std::vector<std::unique_ptr<Pat>> alternatives;
};

/// As-pattern: `name @ inner`
struct AsPat {
    Symbol name;
    std::unique_ptr<Pat> inner;
};

/// Discriminated union of all pattern forms.
using PatKind = std::variant<WildcardPat, BindingPat, LitPat, ConstructorFieldsPat,
    ConstructorPositionalPat, ConstructorUnitPat, TuplePat, OrPat, AsPat>;

// ---------------------------------------------------------------------------
// Pat
// ---------------------------------------------------------------------------

/// A pattern node in the AST.
struct Pat {
    NodeId id;
    cstc::span::SourceSpan span;
    PatKind kind;
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_PAT_HPP
