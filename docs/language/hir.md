# Cicest HIR Overview

HIR is a semantically simplified IR produced from AST.

## Module Shape

A module contains declarations, each with:

- header (`FunctionDecl`, `ImportDecl`, or `RawDecl`)
- body expressions
- constraint expressions

`FunctionDecl` may be flagged as exported (`export fn ...`), and `ImportDecl`
represents module imports (`import ...`).

## Types

Current HIR type forms:

- `PathType`
- `ContractType`
- `RefType`
- `FnPointerType`
- `InferredType`

## Expressions

Current HIR expression forms include:

- `RawExpr`, `LiteralExpr`, `PathExpr`, `BinaryExpr`, `CallExpr`
- `MemberAccessExpr` (field access)
- Contract/block forms (`ContractBlockExpr`)
- `LiftedConstantExpr`
- `DeclConstraintExpr`

## `decl(TypeExpr)` Lowering

Source:

```cicest
where decl(Vec<T>)
```

HIR constraint:

```text
decl_valid(Vec<T>)
```

This records that the type expression must be valid during constraint checking.

## Removed Surface Features

HIR may still keep general-purpose node categories, but source parsing no longer accepts:

- `match`/pattern matching
- method-system declarations (`concept`, `with`) and method-call syntax
- type aliases
- rich enum payloads
- tuple types, tuple expressions, and tuple structs
