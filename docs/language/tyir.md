# TyIR — Typed Intermediate Representation

TyIR is the first typed IR in the Cicest compiler pipeline.  It is produced
from the AST by the `cstc_tyir_builder` module and consumed by future lowering or
code-generation phases.

## Position in the pipeline

```
Source → Lexer → Tokens → Parser → AST → [Lower] → TyIR → (future: MIR / codegen)
```

## Relationship to the AST

| Aspect | AST (`cstc_ast`) | TyIR (`cstc_tyir`) |
|---|---|---|
| Type annotations | Optional (from source) | Required on every expression |
| Name resolution | Raw `PathExpr` | `LocalRef`, `EnumVariantRef`, `fn_name` in `TyCall` |
| Type checking | None | Fully checked during lowering |
| Operator validity | Syntactic only | Operand types verified |
| ZST structs | Represented | Represented |
| Fieldless enums | Represented | Represented |

## Type system

Cicest has five source-level types, their `runtime`-qualified variants, and one
bottom type:

| Type | Kind | Notes |
|---|---|---|
| `Unit` / `()` | built-in | Zero-information type; implicit return of void functions |
| `num` | built-in | Single numeric type (integer + floating point) |
| `str` | built-in | String type |
| `bool` | built-in | Boolean |
| `TypeName` | user-defined | Struct or enum declared in the same program |
| `runtime T` | qualified | Runtime-tagged form of any other type `T` |
| `!` | bottom | Never type; produced by `break`, `continue`, `return`; also denotable as `-> !` |

The **Never** type (`!`) is a bottom type: it is compatible with any expected
type.  This allows expressions like `return 42` to appear as the value of any
sub-expression without a type error.

For runtime-tagged types, TyIR uses a directional compatibility rule:

- `T` implicitly converts to `runtime T`
- `runtime T` does not convert to `T`
- When control-flow joins `T` with `runtime T`, the resulting type is `runtime T`

TyIR keeps the tag on `Ty` itself. Surface sugar such as `runtime fn` is
normalized during lowering into a runtime-tagged return type, and TyIR also
preserves the original declaration-level runtime marker on function items for
later passes such as const-eval.

## Expression nodes

Every expression in TyIR carries a resolved `Ty`.  The concrete expression
variants are:

```
TyLiteral        — num/str/bool/()/Unit literal
LocalRef         — reference to a let-binding or parameter
EnumVariantRef   — EnumName::Variant (fully resolved)
TyStructInit     — Type { field: expr, … }
TyUnary          — -expr or !expr
TyBinary         — expr op expr
TyFieldAccess    — expr.field
TyCall           — fn_name(args…)   (always direct; no first-class fns)
TyBlockPtr       — { stmts… [tail] }
TyIf             — if (cond) { … } [else { … }]
TyLoop           — loop { … }
TyWhile          — while (cond) { … }
TyFor            — for (init; cond; step) { … }
TyBreak          — break [value]           → type: !
TyContinue       — continue               → type: !
TyReturn         — return [value]          → type: !
```

## Block type rules

A `TyBlock` has type:
- The tail expression's type when a tail is present.
- `Unit` when there is no tail expression.

## If-else type rules

- `if (cond) { T }` without else: type is `Unit`.
- `if (cond) { T } else { E }`: type is `T` when both branches agree, or the
  non-`Never` branch type when one branch diverges.

## Loop type rules

All loop forms (`loop`, `while`, `for`) have type `Unit`.  Break-value typing
(allowing `loop { break 42 }` to have type `num`) is reserved for a future
lowering pass.

## Inspecting TyIR

Use `cstc_inspect` to dump the TyIR of a source file:

```bash
cstc_inspect input.cst --out-type tyir
```

Example output for:

```cicest
struct Point { x: num, y: num }

fn distance(p: Point, q: Point) -> num {
    let dx: num = p.x - q.x;
    let dy: num = p.y - q.y;
    dx * dx + dy * dy
}
```

```
TyProgram
  TyStructDecl Point
    x: num
    y: num
  TyFnDecl distance(p: Point, q: Point) -> num
    TyBlock: num
      Let dx: num =
        TyBinary(-): num
          TyFieldAccess(.x): num
            TyLocal(p): Point
          TyFieldAccess(.x): num
            TyLocal(q): Point
      Let dy: num =
        TyBinary(-): num
          TyFieldAccess(.y): num
            TyLocal(p): Point
          TyFieldAccess(.y): num
            TyLocal(q): Point
      Tail
        TyBinary(+): num
          TyBinary(*): num
            TyLocal(dx): num
            TyLocal(dx): num
          TyBinary(*): num
            TyLocal(dy): num
            TyLocal(dy): num
```

## Limitations (current version)

- Break values are not propagated to loop types; all loops have type `Unit`.
- No support for recursive types beyond single-level named references.
- No generics (none in Cicest source language).
- Functions are always top-level; no closures or first-class function values.
