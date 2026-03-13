# cstc_tyir_builder — AST to TyIR Lowering Pass

`cstc_tyir_builder` implements the **lowering pass** that transforms a parsed
`cstc::ast::Program` into a fully type-annotated `cstc::tyir::TyProgram`.

## Role in the pipeline

```
Source → Lexer → Tokens → Parser → AST → [cstc_tyir_builder] → TyIR
```

## What the lowering pass does

The pass runs in four phases over the AST:

| Phase | Description |
|---|---|
| 1 — Name collection | All struct and enum names are registered so later phases can resolve forward references |
| 2 — Type definition resolution | Struct field types and enum variants are resolved into `tyir::Ty` values |
| 3 — Signature resolution | Function parameter and return types are resolved |
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
| Return type mismatch | `"function 'f' body has type 'bool' but return type is 'num'"` |
| Non-bool condition | `"'if' condition must have type 'bool', found 'num'"` |
| Field not found | `"no field 'z' in struct 'Point'"` |

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

## Dependencies

```
cstc_tyir_builder → cstc_tyir → cstc_ast → cstc_symbol, cstc_span
                  → cstc_ast
                  → cstc_symbol
                  → cstc_span
```
