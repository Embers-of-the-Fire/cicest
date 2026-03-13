# `cstc_symbol`

Header-only global symbol interner package.

## Purpose

Provides rustc-style interned symbol ids shared by lexer, parser, and AST.
All textual values (identifiers, literals, etc.) are interned once and stored
as compact symbol ids.

## Public API

- Header: `include/cstc_symbol/symbol.hpp`
- Types:
  - `cstc::symbol::Symbol`
  - `cstc::symbol::SymbolHash`
  - `cstc::symbol::SymbolTable`
- Constants:
  - `cstc::symbol::kInvalidSymbol`

## CMake

- Target: `cstc_symbol` (`INTERFACE`)
- Alias: `cicest::compiler::symbol`

## Tests

- `tests/symbol_basic.cpp`
- Built when `CICEST_BUILD_TESTS=ON`

