# LIR — Low-level Intermediate Representation

LIR is a flat, CFG-based intermediate representation positioned between TyIR and
LLVM code generation.  It is produced from TyIR by the `cstc_lir_builder` module
and consumed by `cstc_codegen`.  Its design corresponds roughly to Rust's MIR.

## Position in the pipeline

```
Source → Lexer → Tokens → Parser → AST → [TyIR Builder] → TyIR → [LIR Builder] → LIR → [Codegen] → LLVM IR
```

## Relationship to TyIR

| Aspect | TyIR (`cstc_tyir`) | LIR (`cstc_lir`) |
|---|---|---|
| Structure | Nested expression tree | Flat control-flow graph (basic blocks) |
| Control flow | Nested `if`/`loop`/`while`/`for` nodes | Explicit blocks with `Jump`/`SwitchBool` terminators |
| Variables | Named `LocalRef` symbols | Numeric `LirLocalId` slots in a dense table |
| Temporaries | Implicit (sub-expressions) | Explicit `LirLocalDecl` entries |
| Expressions | Arbitrarily nested | One assignment per statement |
| Types | Owns the `Ty` system | Re-uses `Ty` from TyIR (no new type layer) |
| Generics | Generic declarations and substitutions still exist | Only concrete, monomorphized items remain |
| Fallibility | `lower_program` returns `expected<TyProgram, Error>` | `lower_program` returns `LirProgram` directly (infallible given valid TyIR) |

## Type system

LIR re-uses the TyIR type system wholesale.  See [tyir.md](tyir.md) for the
full description.  The seven type kinds available:

| Type | Kind | Notes |
|---|---|---|
| `&T` | built-in | Shared immutable reference |
| `Unit` / `()` | built-in | Zero-information type |
| `num` | built-in | Single numeric type |
| `str` | built-in | String type |
| `bool` | built-in | Boolean type |
| `TypeName` | user-defined | Struct or enum name |
| `!` | internal | Never / bottom type (diverging expressions) |

## Generic boundary

LIR is intentionally past the language's generic boundary.

- Generic declarations are monomorphized during TyIR -> LIR lowering.
- Deferred and explicit instantiations are solidified through substitution
  before any `LirFnDef`, `LirStructDecl`, or `LirEnumDecl` is emitted.
- Instantiated items receive deterministic internal names so duplicate requests
  reuse the same lowered item and codegen symbol.
- Codegen therefore never needs to reason about unresolved type parameters.

## Core concepts

### Locals

Every variable, parameter, and compiler temporary within a function is
represented by a numeric `LirLocalId` (a `uint32_t` index).  A function's
`locals` array is a dense table of `LirLocalDecl` entries, each carrying the
slot's type and an optional debug name from the source.

Parameters always occupy the lowest IDs (`0 … params.size()-1`).  User `let`
bindings and compiler temporaries are appended after that.

### Places

A **place** is a memory location that can be read or written.  LIR supports two
place forms:

| Form | Meaning | Printed as |
|---|---|---|
| `Local(id)` | The entire local variable slot | `_%id` |
| `Field(base_local, field_path...)` | One or more named field projections rooted at a struct local | `_%id.field[.field...]` |

### Operands

An **operand** is an SSA-style read value — the "leaf" of every computation:

| Form | Meaning | Printed as |
|---|---|---|
| `Copy(place)` | Copy of the value at a place | `copy(_%id)` |
| `Move(place)` | Move the value out of a place | `move(_%id)` |
| `Const(constant)` | Compile-time constant | literal text (e.g. `42`, `"hi"`, `true`, `()`) |

### Constants

Compile-time constant values embedded directly in operands:

| Kind | Storage | Example |
|---|---|---|
| `Num` | Interned symbol text | `42`, `3.14` |
| `Str` | Interned symbol text | `"hello"` |
| `Bool` | Boolean value | `true`, `false` |
| `Unit` | (none) | `()` |

## Statements

LIR has two statement kinds:

- **assignment** for computing and storing values
- **drop** for explicit lexical destruction of owned locals

Every side-effecting computation — including calls whose return values are
discarded — is expressed as an assignment of an rvalue to a destination place:

```
dest_place = rvalue
```

Owned locals are destroyed with:

```
drop _%id
```

### Rvalues

The right-hand side of an assignment.  Seven rvalue forms exist:

| Rvalue | Description | Printed as |
|---|---|---|
| `Use(operand)` | Copy/move an operand into the destination | `copy(_%0)` or a constant |
| `Borrow(place)` | Create a shared reference to a place | `Borrow(_%0)` |
| `BinaryOp(op, lhs, rhs)` | Binary arithmetic/logical operation | `BinOp(+, copy(_%0), copy(_%1))` |
| `UnaryOp(op, operand)` | Unary arithmetic/logical operation | `UnaryOp(-, copy(_%0))` |
| `Call(fn_name, args…)` | Direct function call | `Call(add, copy(_%0), copy(_%1))` |
| `StructInit(type, fields…)` | Struct construction | `StructInit(Point, x: copy(_%0), y: copy(_%1))` |
| `EnumVariantRef(enum, variant)` | Enum variant value | `EnumVariant(Dir::North)` |

**Supported binary operators** (from `ast::BinaryOp`):
`+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`

**Supported unary operators** (from `ast::UnaryOp`):
`-` (negate), `!` (logical not)

## Terminators

Every basic block ends with exactly one terminator — the control-flow
instruction that determines where execution goes next.  There is no implicit
fall-through; all CFG edges are explicit.

| Terminator | Description | Printed as |
|---|---|---|
| `Return(value?)` | Return from function; `nullopt` for unit returns | `return copy(_%0)` or `return` |
| `Jump(target)` | Unconditional branch | `jump bb1` |
| `SwitchBool(cond, true_bb, false_bb)` | Conditional branch on a boolean | `switchBool(copy(_%0)) -> [true: bb1, false: bb2]` |
| `Unreachable` | Marks a block as unreachable (after diverging expressions) | `unreachable` |

## Basic blocks

A basic block is a straight-line sequence of assignment statements followed by
exactly one terminator:

```
bb0:
    _%2 = BinOp(+, copy(_%0), copy(_%1))
    return copy(_%2)
```

The entry block of every function is always `bb0` (block ID `0`).  The invariant
`blocks[id].id == id` is always maintained.

## Functions

A function definition contains:

- **name** — top-level function name
- **name** may be a stable internal instantiation identity for monomorphized
  generic items
- **params** — parameter metadata (in declaration order)
- **return_ty** — declared return type
- **locals** — dense array of local declarations, indexed by `LirLocalId`
- **blocks** — dense array of basic blocks, indexed by `LirBlockId`

## Type declarations

LIR keeps its own copies of struct and enum declarations so the program is
self-contained — code generation does not need to reach back into TyIR.

When a declaration originated from a generic item, the stored name is the
stable instantiated identity used internally by later stages.

### Structs

```
struct Point {
    x: num
    y: num
}
```

Zero-sized type (ZST) structs with no fields are printed with a trailing
semicolon:

```
struct Marker;
```

### Enums

```
enum Dir {
    North
    South
}
```

Variants may carry optional explicit numeric discriminant text:

```
enum Val {
    A = 0
    B = 5
}
```

## Control flow lowering

The LIR builder transforms TyIR's nested control-flow expressions into explicit
CFG structures, and also makes ownership effects explicit:

- move-only by-value uses lower to `move(place)` operands
- borrow expressions lower to `Borrow(place)` rvalues
- lexical scope exits insert `drop _%id` statements in reverse local order
- `return`, `break`, and `continue` emit the drops for every scope they exit

### If (no else)

```
current_block:
    switchBool(cond) -> [true: then_bb, false: merge_bb]
then_bb:
    <then body>
    _%result = <value>
    jump merge_bb
merge_bb:
    (result local available)
```

### If-else

```
current_block:
    switchBool(cond) -> [true: then_bb, false: else_bb]
then_bb:
    <then body>
    _%result = <value>
    jump merge_bb
else_bb:
    <else body>
    _%result = <value>
    jump merge_bb
merge_bb:
    (result local available)
```

### Loop

```
current_block:
    jump header_bb
header_bb:
    <body>
    jump header_bb          // back-edge (if body doesn't terminate)
after_bb:
    (break value available)
```

`break [value]` seals the current block with `jump after_bb` and stores the
value into the break-value local.  `continue` seals with `jump header_bb`.

### While

```
current_block:
    jump cond_bb
cond_bb:
    <evaluate condition>
    switchBool(cond) -> [true: body_bb, false: after_bb]
body_bb:
    <body>
    jump cond_bb            // if not terminated
after_bb:
    (always yields unit)
```

### For

```
current_block:
    <init>
    jump cond_bb
cond_bb:
    <evaluate condition>
    switchBool(cond) -> [true: body_bb, false: after_bb]
body_bb:
    <body>
    jump step_bb            // or cond_bb if no step
step_bb:
    <step expression>
    jump cond_bb
after_bb:
    (always yields unit)
```

The `continue` target is `step_bb` when a step expression is present, `cond_bb`
otherwise.

## Inspecting LIR

Use `cstc_inspect` to dump the LIR of a source file:

```bash
cstc_inspect input.cst --out-type lir
```

Example output for:

```cicest
struct Point { x: num, y: num }

fn add(a: num, b: num) -> num {
    a + b
}
```

```
LirProgram
  struct Point {
    x: num
    y: num
  }
  fn add(a: num, b: num) -> num
    locals: [_%0: num /* a */, _%1: num /* b */, _%2: num]
    bb0:
      _%2 = BinOp(+, copy(_%0), copy(_%1))
      return copy(_%2)
```

A more complex example with control flow:

```cicest
fn choose(b: bool) -> num {
    if (b) { 1 } else { 2 }
}
```

```
LirProgram
  fn choose(b: bool) -> num
    locals: [_%0: bool /* b */, _%1: num, _%2: num]
    bb0:
      switchBool(copy(_%0)) -> [true: bb1, false: bb2]
    bb1:
      _%1 = 1
      jump bb3
    bb2:
      _%1 = 2
      jump bb3
    bb3:
      _%2 = copy(_%1)
      return copy(_%2)
```

## LLVM mapping

LIR is designed for straightforward translation to LLVM IR:

| LIR | LLVM IR |
|---|---|
| `Ty::Num` | `double` |
| `Ty::Bool` | `i1` |
| `Ty::Str` | `ptr` (opaque pointer) |
| `Ty::Unit` | empty struct (values) / `void` (returns) |
| `LirStructDecl` | named `StructType` |
| `LirEnumDecl` | `{ i32 }` (discriminant-only) |
| `LirLocalId` | entry-block `alloca` (promoted by mem2reg) |
| `LirPlace::Field` | chained `getelementptr` + `load` / `store` |
| `LirBinaryOp` | FP arithmetic/comparison; `and`/`or` for bool ops |
| `LirUnaryOp` | `fneg` or boolean xor-not |
| `LirCall` | direct `call` |
| `LirReturn` | `ret` |
| `LirJump` | unconditional `br` |
| `LirSwitchBool` | conditional `br i1` |
| `LirUnreachable` | `unreachable` |

## Limitations (current version)

- Moves from projected places are not yet supported; only whole-local moves are
  lowered.
- No first-class functions or closures — all calls are direct.
- No generics (none in Cicest source language).
- Enum variants are fieldless (no data-carrying variants).
