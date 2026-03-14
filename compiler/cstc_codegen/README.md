# cstc_codegen — LIR to LLVM IR Lowering

`cstc_codegen` converts `cstc::lir::LirProgram` into textual LLVM IR.

## Role in the pipeline

```
Source → Lexer → Parser → AST → TyIR → LIR → [cstc_codegen] → LLVM IR text
```

The module is intentionally the first backend-facing stage:

- Input is already type-checked (`TyIR`) and control-flow lowered (`LIR`).
- Output is a printable LLVM module string suitable for inspection, tests,
  and future machine-code pipelines.

## Public API

Header: `include/cstc_codegen/codegen.hpp`

```cpp
std::string cstc::codegen::emit_llvm_ir(const lir::LirProgram& program);
std::string cstc::codegen::emit_llvm_ir(const lir::LirProgram& program,
                                        std::string_view module_name);
```

API properties:

- LLVM types stay hidden from public headers.
- Requires a live `cstc::symbol::SymbolSession` on the caller thread.
- Produces textual IR for diagnostics/tests/tooling.
- Runs a mem2reg-style promotion pass before printing.

## Lowering model

The implementation performs these phases:

1. **Type declarations**
   - LIR structs become named LLVM struct types.
   - LIR enums become `{ i32 }` (discriminant-only representation).

2. **Function signatures**
   - Function parameter and return `Ty` values are converted to LLVM types.
   - Unit/never return types are lowered to `void`.

3. **Function body lowering**
   - Every LIR local is first represented as an entry-block `alloca`.
   - Each LIR basic block lowers to one LLVM basic block.
   - Statements lower as value computation + store into destination place.
   - Terminators lower to `ret`, `br`, conditional `br`, or `unreachable`.

4. **Cleanup/optimization**
   - A promotion pass removes most temporary allocas and yields SSA-style IR.

## LIR-to-LLVM mapping summary

| LIR concept | LLVM lowering |
|---|---|
| `Ty::Num` | `double` |
| `Ty::Bool` | `i1` |
| `Ty::Str` | opaque pointer (`ptr`) |
| `Ty::Unit` | empty struct for values, `void` for returns |
| `LirPlace::Field` | `getelementptr` for stores / `extractvalue` for loads |
| `LirBinaryOp` | FP arithmetic/comparisons, `and/or` for bool ops |
| `LirUnaryOp` | `fneg` or boolean xor-not |
| `LirCall` | direct `call` to resolved function symbol |
| `LirSwitchBool` | conditional branch (`br i1`) |

## Notes and current scope

- This stage assumes LIR is valid and type-correct.
- Enums currently use discriminant-only layout (`{ i32 }`).
- String constants are emitted as LLVM global string constants.
- Tests assert semantic patterns in emitted IR, not exact formatting.

## Quick usage

```cpp
#include <cstc_codegen/codegen.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_tyir_builder/builder.hpp>

cstc::symbol::SymbolSession session;
const auto ast = cstc::parser::parse_source("fn add(a: num, b: num) -> num { a + b }");
const auto tyir = cstc::tyir_builder::lower_program(*ast);
const auto lir = cstc::lir_builder::lower_program(*tyir);

std::string llvm_ir = cstc::codegen::emit_llvm_ir(lir, "demo_module");
```

