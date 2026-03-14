# The Cicest Programming Language

Cicest is a small expression-oriented language and compiler prototype implemented in modern C++.
The current repository contains a working multi-stage compiler pipeline from source text to LLVM IR text.

## Current Implementation Scope

Implemented language surface (as of this repository state):

- Top-level items: `struct`, `enum`, `fn`
- Types: `Unit`, `num`, `str`, `bool`, and named user types
- Statements: immutable `let` and expression statements
- Expressions:
  - literals (`num`, `str`, `bool`, `()`)
  - paths (`name`, `Enum::Variant`)
  - struct initialization and field access
  - function calls
  - unary/binary operators (`+ - * / % ! && || == != < <= > >=`)
  - blocks and control flow (`if`, `loop`, `while`, C-style `for`, `break`, `continue`, `return`)

Not implemented in the current scoped subset:

- generics
- modules/import/export/visibility syntax
- mutable bindings and assignment expressions
- async/await and closures

Current semantic/lowering stages include:

- duplicate-name checks for struct fields, enum variants, and fn parameters
- name resolution (locals, functions, enum variants)
- type checking and typed IR lowering
- lowering to control-flow based LIR
- LLVM IR emission

For full syntax and grammar notes, see [docs/language/index.md](docs/language/index.md).

## Compiler Pipeline

The implementation under `compiler/` currently includes:

- `cstc_symbol`: global symbol interner
- `cstc_span`: source positions, spans, and source map
- `cstc_ast`: AST model and formatter
- `cstc_lexer`: source lexer and token model
- `cstc_parser`: recursive-descent parser (tokens/source → AST)
- `cstc_tyir`: typed IR model + formatter
- `cstc_tyir_builder`: AST → TyIR lowering + type checking
- `cstc_lir`: low-level IR model + formatter
- `cstc_lir_builder`: TyIR → LIR lowering
- `cstc_codegen`: LIR → LLVM IR text emitter
- `cstc_inspect`: CLI inspector for each stage

Pipeline overview:

```text
Source -> Lexer -> Parser -> AST -> TyIR -> LIR -> LLVM IR
```

## Build

### Option 1: Nix (recommended in this repo)

```bash
nix develop
nix run .#build
```

Run tests:

```bash
nix run .#tests
```

Run lint + format checks:

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
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCICEST_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Notes:

- The project requires C++23 (`cmake_minimum_required(VERSION 3.21)`).
- `cstc_codegen` links against LLVM and supporting libraries (`zlib`, `libxml2`, `libffi`).

## CI

For CI workflow details (test matrix, lint/format checks, and local reproduction), see [CI.md](CI.md).

## Inspector CLI

The inspector executable is built at `build/compiler/cstc_inspect/cstc_inspect`.

Usage:

```bash
./build/compiler/cstc_inspect/cstc_inspect <input-file> --out-type <tokens|ast|tyir|lir|llvm> [-o <output-file>] [--keep-trivia]
```

Examples:

```bash
./build/compiler/cstc_inspect/cstc_inspect path/to/file.cst --out-type tokens --keep-trivia
./build/compiler/cstc_inspect/cstc_inspect path/to/file.cst --out-type ast
./build/compiler/cstc_inspect/cstc_inspect path/to/file.cst --out-type tyir
./build/compiler/cstc_inspect/cstc_inspect path/to/file.cst --out-type lir
./build/compiler/cstc_inspect/cstc_inspect path/to/file.cst --out-type llvm -o output.ll
```

## License

This project is licensed under [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT).
