# `cstc_parser`

Compiled recursive-descent parser package.

## Purpose

Builds `cstc_ast::Program` from pre-lexed tokens or directly from source text.
Implements the scoped Cicest grammar defined in `docs/language/syntax.md`.
All textual values are interned into a shared symbol table.

The parser accepts runtime boundaries only in block form: `runtime { ... }`.
Top-level `runtime fn` and `runtime extern ... fn` declarations continue to use
their existing declaration syntax.

## Public API

- Header: `include/cstc_parser/parser.hpp`
- Implementation: `src/parser.cpp`
- Types:
  - `cstc::parser::ParseError`
- Functions:
  - `cstc::parser::parse_tokens(..., symbols)`
  - `cstc::parser::parse_source_at(..., symbols)`
  - `cstc::parser::parse_source(..., symbols)`

## CMake

- Target: `cstc_parser` (`STATIC`)
- Alias: `cicest::compiler::parser`
- Depends on: `cstc_ast`, `cstc_lexer`, `cstc_symbol`, `cstc_span`

## Tests

- `tests/parser_basic.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
