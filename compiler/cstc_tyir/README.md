# cstc_tyir — Typed Intermediate Representation

`cstc_tyir` defines the **TyIR** (Typed Intermediate Representation), the first
typed IR in the Cicest compiler pipeline.

## Role in the pipeline

```
Source → Lexer → Tokens → Parser → AST → [cstc_tyir_builder] → TyIR
                                                            ↓
                                                   cstc_inspect --out-type tyir
```

TyIR sits directly above the AST.  It is produced by the `cstc_tyir_builder` module,
which performs name resolution and type checking during the lowering pass.

## What TyIR adds over AST

| Property | AST | TyIR |
|---|---|---|
| Type annotations | Optional (source annotations only) | Every expression carries a `Ty` |
| Name resolution | Raw `PathExpr` symbols | `LocalRef`, `EnumVariantRef`, `fn_name` in `TyCall` |
| Type checking | None | Verified during lowering |
| Operator validity | Syntactic | Operand types validated |

## Key types

### `Ty` — resolved type

```cpp
enum class TyKind { Unit, Num, Str, Bool, Named, Never };

struct Ty {
    TyKind kind;
    Symbol name;  // only for Named types

    std::string display() const;
};

// Factory helpers
cstc::tyir::ty::unit()    // ()
cstc::tyir::ty::num()     // num
cstc::tyir::ty::str()     // str
cstc::tyir::ty::bool_()   // bool
cstc::tyir::ty::never()   // ! (bottom / diverging)
cstc::tyir::ty::named(sym) // user-defined struct or enum
```

The `Never` kind represents diverging expressions (`break`, `continue`,
`return`).  It acts as a bottom type: compatible with any expected type.

`Ty` also carries a `runtime` tag. Conceptually, `runtime T` behaves like a
wrapper-shaped type such as `Runtime<T>`: it keeps the same underlying type
shape as `T`, but its tag participates in exact type identity and in the
directional promotion rule `T -> runtime T`.

### `TyExpr` — type-annotated expression

Every expression node carries:
- `node` — one of the concrete expression variants
- `ty`   — the resolved `Ty`
- `span` — source location

Concrete expression variants:

| Variant | Description |
|---|---|
| `TyLiteral` | Number, string, bool, or unit literal |
| `LocalRef` | Reference to a `let` binding or parameter |
| `EnumVariantRef` | Resolved `EnumName::Variant` reference |
| `TyStructInit` | Struct construction `Type { field: expr }` |
| `TyUnary` | Unary `-` or `!` |
| `TyBinary` | Binary arithmetic, comparison, or logical op |
| `TyFieldAccess` | Field access `base.field` |
| `TyCall` | Direct function call (always resolved to a top-level function) |
| `TyRuntimeBlock` | Explicit runtime boundary that wraps a typed block body |
| `TyBlockPtr` | Inline block expression |
| `TyIf` | Conditional expression |
| `TyLoop` | Infinite loop |
| `TyWhile` | Conditional loop |
| `TyFor` | C-style for loop |
| `TyBreak` | Break (type is `Never`) |
| `TyContinue` | Continue (type is `Never`) |
| `TyReturn` | Return (type is `Never`) |

### `TyBlock` — typed block

```cpp
struct TyBlock {
    std::vector<TyStmt> stmts;
    std::optional<TyExprPtr> tail;
    Ty ty;   // = tail->ty when tail exists; else Unit or Never (by fallthrough)
    SourceSpan span;
    bool ct_available;
    std::optional<TyRuntimeEvidence> runtime_evidence;
};
```

`ct_available` is computed from the whole reachable block term, not just the tail
value. Reachable statement initializers, expression statements, control-flow
headers, loop bodies, and the tail all contribute. `runtime_evidence` records the
first reachable body-internal runtime contributor that must be exposed by an
explicit runtime result contract.

### Item declarations

- `TyStructDecl` — struct with resolved field types
- `TyEnumDecl`   — fieldless enum
- `TyFnDecl`     — function with resolved param/return types and typed body

Surface sugar such as `runtime fn` is normalized into runtime-qualified types,
and TyIR also preserves the original declaration-level marker on
`TyFnDecl::is_runtime` / `TyExternFnDecl::is_runtime` so later passes can
distinguish item-level runtime boundaries from nested type tags.

`runtime { ... }` is represented explicitly as `TyRuntimeBlock` rather than as a
plain `TyBlock` whose result type is merely runtime-tagged. This preserves the
runtime authorization boundary while still allowing pure inner expressions to be
analyzed or folded.

## Printer

```cpp
#include <cstc_tyir/printer.hpp>

std::string output = cstc::tyir::format_program(program);
```

Sample output:

```
TyProgram
  TyStructDecl Point
    x: num
    y: num
  TyFnDecl add(x: num, y: num) -> num
    TyBlock: num
      Tail
        TyBinary(+): num
          TyLocal(x): num
          TyLocal(y): num
```

## Dependencies

```
cstc_tyir → cstc_ast (for UnaryOp, BinaryOp)
          → cstc_symbol
          → cstc_span
```
