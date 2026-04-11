# The Cicest Programming Language

Cicest is a small expression-oriented language and compiler prototype implemented
in modern C++. The current repository contains a working frontend and backend
pipeline from source text through typed and low-level IR to LLVM IR and native
artifacts.

This README is a stable overview. The canonical language and library references
live under `docs/`.

## Stable Reading Order

- Start with `docs/language/syntax.md` for the supported surface language.
- Read `docs/language/modules.md` for file layout and import resolution.
- Read `docs/language/ownership.md` for the current `str`, `&str`, and drop
  model.
- Read `docs/language/tyir.md` and `docs/language/lir.md` for the compiler-facing
  semantic and lowering story.
- Read `docs/library/std.md` for the prelude, runtime ABI, and standard library
  surface.

## Current Implementation Scope

Implemented top-level items:

- `import`
- `struct`
- `enum`
- `fn`, including `runtime fn`
- `extern`, including `runtime extern ... fn`
- generic parameters on `fn`, `struct`, and `enum`
- declaration-level `where` clauses with constraint expressions

Implemented types and qualifiers:

- `Unit`, `num`, `str`, `bool`, and named user types
- `runtime T`
- immutable shared references `&T`
- `!` for diverging expressions

Implemented statements and expressions include:

- immutable `let`
- literals, paths, and scoped enum variants
- struct initialization and field access
- function calls
- unary and binary operators
- blocks and control flow: `if`, `loop`, `while`, C-style `for`, `break`,
  `continue`, and `return`
- `runtime { ... }` blocks
- compiler-recognized `decl(expr)` in staged checking sites such as `where`
  clauses

Explicitly out of scope in the current subset:

- mutation and assignment expressions
- async/await
- closures and lambda expressions
- tuple types and tuple literals
- global `let` / `const` / `static`

## Language Direction Reflected In The Current Docs

The public docs and tests now describe a more precise staged story than older
overview notes did.

- `runtime T` is a runtime-qualified form of `T`.
- `T` may be promoted to `runtime T`, but the reverse demotion is rejected.
- Plain function calls use call-specific lifting: a parameter of type `T` may
  receive a `runtime T` argument at a call site, and the result is lifted to
  `runtime U` when runtime dependence reaches the call result.
- `runtime { ... }` is an explicit runtime boundary in the source language.
- Pure subexpressions inside a runtime block may still be normalized ahead of
  time when they do not depend on runtime data.

For the precise surface-language statement of these rules, use
`docs/language/syntax.md`. For the implementation-facing account of how they are
tracked in the compiler, use `docs/language/tyir.md`.

## Compiler Pipeline

The implementation under `compiler/` currently includes:

- `cstc_symbol`: global symbol interner
- `cstc_span`: source positions, spans, and source map
- `cstc_ast`: AST model and formatter
- `cstc_lexer`: source lexer and token model
- `cstc_parser`: recursive-descent parser
- `cstc_module`: module graph loader and import resolver
- `cstc_tyir`: typed IR model and formatter
- `cstc_tyir_builder`: AST to TyIR lowering and type checking
- `cstc_lir`: low-level IR model and formatter
- `cstc_lir_builder`: TyIR to LIR lowering
- `cstc_codegen`: LIR to LLVM IR and native artifact backend
- `cstc_cli_support`: shared CLI helpers for module loading and diagnostics
- `cstc`: compiler CLI that emits `.s`, `.o`, and linked executables
- `cstc_inspect`: CLI inspector for intermediate stages
- `cstc_repl`: REPL support package

Pipeline overview:

```text
Root module graph -> Lexer/Parser -> Module resolution -> AST -> TyIR -> LIR
-> LLVM IR -> native .s/.o/.exe
```

## Module And Prelude Model

Compilation is rooted at a single input module file. The compiler resolves that
module graph recursively, implicitly imports `@std/prelude.cst` into every
non-prelude module, and then continues through the frontend and backend with a
crate-wide resolved AST.

This is documented in more detail in:

- `docs/language/modules.md`
- `docs/library/std.md`

## Build

### Option 1: Nix

```bash
nix develop
nix run .#build
```

Run tests:

```bash
nix run .#tests
```

Run lint and format checks:

```bash
nix run .#lint
```

### Option 2: CMake + Ninja

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

Build and run tests:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCICEST_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Notes:

- The project requires C++23.
- `cstc_codegen` links against LLVM and supporting libraries such as `zlib`,
  `libxml2`, and `libffi`.
- Windows MinGW currently skips the end-to-end suite. Use
  `-DCICEST_BUILD_E2E_TESTS=OFF` when configuring tests there, or use the CI
  helper scripts under `.github/scripts/`.

## CI

For CI workflow details, including lint and test reproduction, see `CI.md`.

## CLI Overview

The compiler executable is built at `build/compiler/cstc/cstc`.

Usage:

```bash
./build/compiler/cstc/cstc <input-file> [-o <output-stem>]
  [--module-name <module>] [--emit <asm|obj|exe|all>] [--linker <linker>]
```

The inspector executable is built at `build/compiler/cstc_inspect/cstc_inspect`.

Usage:

```bash
./build/compiler/cstc_inspect/cstc_inspect <input-file>
  --out-type <tokens|ast|tyir|lir|llvm> [-o <output-file>] [--keep-trivia]
```

## Documentation Index

For language, library, and unresolved design notes, see `docs/index.md`.

## License

This project is licensed under `Apache-2.0` or `MIT`.
