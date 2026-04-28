# cstc_tyir_builder — AST to TyIR Lowering Pass

`cstc_tyir_builder` implements the **lowering pass** that transforms a parsed
`cstc::ast::Program` into a fully type-annotated `cstc::tyir::TyProgram`.

## Role in the pipeline

```
Source → Lexer → Tokens → Parser → AST → [cstc_tyir_builder] → TyIR
```

## What the lowering pass does

The pass runs in five phases over the AST:

| Phase | Description |
|---|---|
| 1 — Name collection | All struct and enum names are registered so later phases can resolve forward references |
| 2 — Type definition resolution | Struct field types and enum variants are resolved into `tyir::Ty` values |
| 3 — Signature resolution | Function parameter and return types are resolved |
| 3.5 — Main return type validation | If a `main` function exists, its return type is constrained to `Unit`, `num`, or `!` (never) |
| 4 — Function body lowering | Each function body is type-checked and all expressions are annotated with `tyir::Ty` |

## Type checking rules

### Primitive types

| Type | Literal | Source spelling |
|---|---|---|
| `Unit` | `()` | `Unit` |
| `num` | `42`, `3.14` | `num` |
| `str` | `"hello"` | `str` |
| `bool` | `true`, `false` | `bool` |
| `!` | — | never (diverging) |

### Operator rules

| Operator class | Operand type | Result type |
|---|---|---|
| Arithmetic (`+`, `-`, `*`, `/`, `%`) | `num` | `num` |
| Relational (`<`, `<=`, `>`, `>=`) | `num` | `bool` |
| Equality (`==`, `!=`) | same type on both sides | `bool` |
| Logical (`&&`, `\|\|`) | `bool` | `bool` |
| Unary `-` | `num` | `num` |
| Unary `!` | `bool` | `bool` |

### Name resolution

| AST node | Resolution |
|---|---|
| `PathExpr { head, tail: None }` | Check local scope → `LocalRef` |
| `PathExpr { head: E, tail: Some(V) }` | Check enum `E` has variant `V` → `EnumVariantRef` |
| `CallExpr { callee: PathExpr{fn}, … }` | Check fn signature → `TyCall` |

### Never / bottom type

The `Never` (display: `!`) type is produced by `break`, `continue`, and
`return` expressions.  It is compatible with any expected type (bottom type
semantics), so `break` / `return` can appear in any expression position.
It can also be used as an explicit return type annotation: `fn f() -> ! { loop {} }`.

### Runtime-tagged types

`runtime T` is modeled as a runtime-tagged form of `T`, close in spirit to a
wrapper like `Runtime<T>`. The lowering pass preserves the tag on the resolved
`tyir::Ty`, allows implicit promotion from `T` to `runtime T`, rejects the
reverse demotion, and promotes joins such as `if`/`loop` breaks to the tagged
form when either side is tagged.

Surface syntax sugar such as `runtime fn` and `runtime extern ... fn` is
normalized into a runtime-tagged return type during lowering.

Runtime block expressions use dedicated lowering: `runtime { ... }` becomes a
`TyRuntimeBlock` whose body remains an ordinary `TyBlock`. The outer expression
result is promoted to `runtime T`, but pure inner expressions keep their normal
types so later const-folding can still inspect them.

### Main function constraints

The `main` function, if present, is restricted to return one of:

- `Unit` (implicit; `fn main() { }`)
- `num` (exit code; `fn main() -> num { 0 }`)
- `!` (never returns; `fn main() -> ! { loop {} }`)

Other return types (e.g., `str`, `bool`, user-defined types) produce a compile error.

## Error model

`lower_program` returns `std::expected<tyir::TyProgram, LowerError>`.  On
failure the first error encountered is returned with its source span and a
human-readable message.

Common errors:

| Error | Message |
|---|---|
| Undefined type | `"undefined type 'Foo'"` |
| Undefined variable | `"undefined variable 'x'"` |
| Undefined function | `"undefined function 'foo'"` |
| Type mismatch in let | `"type mismatch in let binding: expected 'num', found 'bool'"` |
| Wrong argument count | `"function 'f' expects 2 argument(s), 1 provided"` |
| Argument type mismatch | `"argument 1 of 'f': expected 'num', found 'bool'"` |
| Return type mismatch | `"function 'f' body type mismatch: expected 'num', found 'bool'"` |
| Non-bool condition | `"'if' condition must have type 'bool', found 'num'"` |
| Field not found | `"no field 'z' in struct 'Point'"` |
| Duplicate top-level declaration | `"duplicate function name 'main'"` |
| Invalid main return type | `"'main' function must return 'Unit', 'num', or '!' (never), found 'str'"` |

## Public API

- Header: `include/cstc_tyir_builder/builder.hpp`
- Implementation: `src/builder.cpp`
- Types:
  - `cstc::tyir_builder::LowerError`
- Functions:
  - `cstc::tyir_builder::lower_program(...)`

## Usage

```cpp
#include <cstc_tyir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_tyir/printer.hpp>

cstc::symbol::SymbolSession session;
const auto ast = cstc::parser::parse_source(source);
if (!ast) { /* handle parse error */ }

const auto tyir = cstc::tyir_builder::lower_program(*ast);
if (!tyir) {
    std::cerr << "error: " << tyir.error().message << "\n";
    return 1;
}

std::cout << cstc::tyir::format_program(*tyir);
```

## CMake

- Target: `cstc_tyir_builder` (`STATIC`)
- Alias: `cicest::compiler::tyir_builder`
- Depends on: `cstc_tyir`, `cstc_ast`, `cstc_symbol`, `cstc_span`

## Dependencies

```
cstc_tyir_builder → cstc_tyir → cstc_ast → cstc_symbol, cstc_span
                  → cstc_ast
                  → cstc_symbol
                  → cstc_span
```
