# Cicest Language Design (Simplified Prototype)

This prototype intentionally keeps a small core language while AST/HIR evolve.

## Supported High-Level Features

- Functions, extern functions
- Marker and named-field structs
- C++-style enums with unit variants only
- Immutable `let` bindings
- Expression forms: literals, paths, calls, field access, `if`, `loop`, C-style `for`, `return`
- Contract keyword blocks (`runtime`, `const`)
- Generic constraints via `where`
- Compiler intrinsic `decl(TypeExpr)`

## Enum Model

Enums behave like C++ `enum class` in this prototype:

```cicest
enum Color {
    Red,
    Green,
    Blue,
}
```

Variant payloads are rejected.

## Loop Model

Two loop forms exist:

- `loop { ... }`
- C-style `for (init; cond; step) { ... }`

No other `for`-family forms are supported.

## `decl(TypeExpr)` Constraint Intrinsic

`decl` is a reserved keyword and takes a **type expression** argument:

```cicest
fn use_vec<T>(value: T) -> T
    where decl(Vec<T>)
{
    value
}
```

During HIR lowering, `decl(TypeExpr)` is expanded into a dedicated constraint node (`decl_valid(...)`) that represents required type-expression validity.
