# HIR Builder (AST -> HIR)

The HIR builder lowers parsed AST into HIR declarations and expression trees.

## Key Lowering Rules

- `FnItem` -> `FunctionDecl`
- `ImportItem` -> `ImportDecl`
- Struct/enum/extern declarations -> `RawDecl` (when not direct function headers)
- Statement-level expressions lowered directly, with complex forms represented as `RawExpr` when needed
- Contract keyword wrappers are lowered to nested `ContractBlockExpr` nodes

`export fn` is preserved by marking `FunctionDecl::is_exported = true`.

## Constraint Lowering

`where` predicates are lowered into declaration `constraints`.

Special handling:

- `decl(TypeExpr)` in AST is lowered to `DeclConstraintExpr`
- HIR printer renders this as `decl_valid(TypeExpr)`

## Simplified Surface Compatibility

Parser-level removals are reflected by builder input (these constructs are rejected before lowering):

- pattern matching and rich enum payloads
- trait/method-system declarations and method call syntax
- type aliases
- unnamed tuple types, tuple expressions, and tuple structs

Lambdas are accepted, but must be non-capturing so they remain function-pointer-convertible.

The builder remains permissive for some legacy AST variants, but the parser is now the gatekeeper for the simplified language subset.
