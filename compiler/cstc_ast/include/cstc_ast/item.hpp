#ifndef CICEST_COMPILER_CSTC_AST_ITEM_HPP
#define CICEST_COMPILER_CSTC_AST_ITEM_HPP

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <cstc_ast/expr.hpp>
#include <cstc_ast/generics.hpp>
#include <cstc_ast/keyword.hpp>
#include <cstc_ast/node_id.hpp>
#include <cstc_ast/symbol.hpp>
#include <cstc_ast/type.hpp>
#include <cstc_span/registry.hpp>

namespace cstc::ast {

// ---------------------------------------------------------------------------
// Function signature components
// ---------------------------------------------------------------------------

/// Self-parameter forms: `&self` or `self: Type`.
struct SelfParam {
    cstc::span::SourceSpan span;
    /// Keyword modifiers on self, e.g. `runtime` in `runtime &self`.
    std::vector<KeywordModifier> keywords;
    /// True if `&self` (reference form).
    bool is_ref;
    /// Explicit type for `self: Type` form. Absent for `&self`.
    std::optional<std::unique_ptr<TypeNode>> explicit_ty;
};

/// A function parameter: `name: Type`.
struct FnParam {
    cstc::span::SourceSpan span;
    Symbol name;
    std::unique_ptr<TypeNode> ty;
};

/// A function signature (shared between fn and extern fn).
struct FnSig {
    std::optional<SelfParam> self_param;
    std::vector<FnParam> params;
    std::unique_ptr<TypeNode> ret_ty;
};

// ---------------------------------------------------------------------------
// Struct fields and enum variants
// ---------------------------------------------------------------------------

/// A named field in a struct or enum variant: `name: Type`.
struct StructField {
    cstc::span::SourceSpan span;
    Symbol name;
    std::unique_ptr<TypeNode> ty;
};

/// The data payload of an enum variant.
struct UnitVariant {};

struct FieldsVariant {
    std::vector<StructField> fields;
};

struct TupleVariant {
    std::vector<std::unique_ptr<TypeNode>> types;
};

using VariantKind = std::variant<UnitVariant, FieldsVariant, TupleVariant>;

/// A single enum variant: `| Name` / `| Name { ... }` / `| Name(...)`.
struct EnumVariant {
    cstc::span::SourceSpan span;
    Symbol name;
    VariantKind kind;
};

// ---------------------------------------------------------------------------
// Item kinds
// ---------------------------------------------------------------------------

/// A function item: `(keyword*) fn name<...>(...) -> Type where ... { body }`.
struct FnItem {
    std::vector<KeywordModifier> keywords;
    Symbol name;
    Generics generics;
    FnSig sig;
    Block body;
};

/// An extern function declaration: `(keyword*) extern fn name(...) -> Type;`.
struct ExternFnItem {
    std::vector<KeywordModifier> keywords;
    Symbol name;
    FnSig sig;
};

/// A marker struct: `struct Name;`.
struct MarkerStructItem {
    Symbol name;
    Generics generics;
};

/// A named-field struct: `struct Name { field: Type, ... }`.
struct NamedStructItem {
    Symbol name;
    Generics generics;
    std::vector<StructField> fields;
};

/// A tuple struct: `struct Name(Type, ...);`.
struct TupleStructItem {
    Symbol name;
    Generics generics;
    std::vector<std::unique_ptr<TypeNode>> fields;
};

/// An enum: `enum Name { | Variant1 | Variant2 { ... } }`.
struct EnumItem {
    Symbol name;
    Generics generics;
    std::vector<EnumVariant> variants;
};

/// A type alias: `type Name = Type;`.
struct TypeAliasItem {
    Symbol name;
    Generics generics;
    std::unique_ptr<TypeNode> ty;
};

/// Discriminated union of all item (declaration) forms.
using ItemKind = std::variant<FnItem, ExternFnItem, MarkerStructItem, NamedStructItem,
    TupleStructItem, EnumItem, TypeAliasItem>;

// ---------------------------------------------------------------------------
// Item
// ---------------------------------------------------------------------------

/// A top-level or nested declaration node in the AST.
struct Item {
    NodeId id;
    cstc::span::SourceSpan span;
    ItemKind kind;
};

// ---------------------------------------------------------------------------
// Crate
// ---------------------------------------------------------------------------

/// Root of the AST — a compilation unit.
struct Crate {
    std::vector<Item> items;
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_ITEM_HPP
