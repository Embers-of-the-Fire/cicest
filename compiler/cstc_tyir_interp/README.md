# cstc_tyir_interp

TyIR interpreter and const-folding pass.

## Purpose

`cstc_tyir_interp` evaluates non-runtime TyIR expressions and rewrites
const-evaluable subtrees back into TyIR before LIR lowering.

## Public API

- Header: `include/cstc_tyir_interp/interp.hpp`
- Types:
  - `cstc::tyir_interp::EvalStackFrame`
  - `cstc::tyir_interp::EvalError`
- Functions:
  - `cstc::tyir_interp::fold_program(const cstc::tyir::TyProgram&)`

## Behavior

- Executes the current TyIR surface, including blocks, control flow, loops,
  structs/enums, borrows, returns, and direct function calls.
- Treats runtime-qualified expressions as const-eval barriers.
- Evaluates supported `extern "lang"` intrinsics inside the interpreter rather
  than reusing the runtime C library.
- Returns an `EvalError` with a source span and TyIR call stack when a reached
  const-eval operation fails.

## CMake

- Target: `cstc_tyir_interp` (`INTERFACE`)
- Alias: `cicest::compiler::tyir_interp`
- Depends on: `cstc_tyir`, `cstc_ast`, `cstc_symbol`, `cstc_span`
