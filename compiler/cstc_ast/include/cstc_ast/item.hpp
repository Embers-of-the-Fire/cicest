#ifndef CICEST_COMPILER_CSTC_AST_ITEM_HPP
#define CICEST_COMPILER_CSTC_AST_ITEM_HPP

#include <memory>
#include <optional>
#include <string>
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

/// A named binding in an import declaration.
/// `import { foo } from "./mod.cst"` or `import { foo as bar } ...`.
struct ImportSpecifier {
    cstc::span::SourceSpan span;
    Symbol imported_name;
    std::optional<Symbol> local_name;
};

// ---------------------------------------------------------------------------
// Item kinds
// ---------------------------------------------------------------------------

/// A module import declaration.
///
/// Supported forms:
/// - `import "./path.cst";`
/// - `import { foo, bar as baz } from "./path.cst";`
struct ImportItem {
    std::vector<ImportSpecifier> specifiers;
    std::string source;
};

/// A function item: `(keyword*) fn name<...>(...) -> Type where ... { body }`.
struct FnItem {
    std::vector<KeywordModifier> keywords;
    Symbol name;
    Generics generics;
    FnSig sig;
    Block body;
    bool is_exported = false;
};

/// A concept method requirement: `(keyword*) fn name<...>(...) -> Type where ...;`.
struct ConceptMethod {
    std::vector<KeywordModifier> keywords;
    Symbol name;
    Generics generics;
    FnSig sig;
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

/// A concept declaration:
/// `concept Name<...> where ... { fn requirement(...); }`.
struct ConceptItem {
    Symbol name;
    Generics generics;
    std::vector<ConceptMethod> methods;
};

/// A member function bundle bound to a type:
/// `with<...> Type where ... { fn ... }`.
struct WithItem {
    std::optional<GenericParams> generic_params;
    std::unique_ptr<TypeNode> target_ty;
    std::optional<WhereClause> where_clause;
    std::vector<FnItem> methods;
};

/// Discriminated union of all item (declaration) forms.
using ItemKind = std::variant<ImportItem, FnItem, ExternFnItem, MarkerStructItem, NamedStructItem,
    TupleStructItem, EnumItem, TypeAliasItem, ConceptItem, WithItem>;

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
