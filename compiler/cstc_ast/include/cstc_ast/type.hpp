#ifndef CICEST_COMPILER_CSTC_AST_TYPE_HPP
#define CICEST_COMPILER_CSTC_AST_TYPE_HPP

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <cstc_ast/keyword.hpp>
#include <cstc_ast/node_id.hpp>
#include <cstc_ast/symbol.hpp>
#include <cstc_span/registry.hpp>

namespace cstc::ast {

struct TypeNode;

// ---------------------------------------------------------------------------
// Path (shared with expressions)
// ---------------------------------------------------------------------------

/// A single segment of a path, e.g. `Foo` in `Foo::bar` or `Foo<i32>`.
struct PathSegment {
    cstc::span::SourceSpan span;
    Symbol name;
};

/// A qualified path, e.g. `Foo::Bar::baz`.
struct Path {
    cstc::span::SourceSpan span;
    std::vector<PathSegment> segments;
};

// ---------------------------------------------------------------------------
// Generic arguments (use-site)
// ---------------------------------------------------------------------------

/// Type arguments at a use site, e.g. `<i32, bool>` in `Vec<i32, bool>`.
struct GenericArgs {
    cstc::span::SourceSpan span;
    std::vector<std::unique_ptr<TypeNode>> args;
};

// ---------------------------------------------------------------------------
// Type kinds
// ---------------------------------------------------------------------------

/// A path type, e.g. `i32`, `Vec<T>`, `std::Result<T, E>`.
struct PathType {
    Path path;
    std::optional<GenericArgs> args;
};

/// A keyword-prefixed type, e.g. `async T`, `runtime async T`.
struct KeywordType {
    std::vector<KeywordModifier> keywords;
    std::unique_ptr<TypeNode> inner;
};

/// A reference type, e.g. `&T`.
struct RefType {
    std::unique_ptr<TypeNode> inner;
};

/// A tuple type, e.g. `(i32, bool)`. The unit type `()` is a zero-element tuple.
struct TupleType {
    std::vector<std::unique_ptr<TypeNode>> elements;
};

/// A function type, e.g. `fn(i32, bool) -> i32`.
struct FnType {
    std::vector<std::unique_ptr<TypeNode>> params;
    std::unique_ptr<TypeNode> ret;
};

/// An inferred type placeholder, `_`.
struct InferredType {};

/// Discriminated union of all type forms.
using TypeKind = std::variant<PathType, KeywordType, RefType, TupleType, FnType, InferredType>;

// ---------------------------------------------------------------------------
// TypeNode
// ---------------------------------------------------------------------------

/// A type node in the AST.
struct TypeNode {
    NodeId id;
    cstc::span::SourceSpan span;
    TypeKind kind;
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_TYPE_HPP
