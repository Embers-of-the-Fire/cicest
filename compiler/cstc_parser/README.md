# `cstc_parser`

Header-only recursive-descent parser package.

## Purpose

Builds `cstc_ast::Program` from pre-lexed tokens or directly from source text.
Implements the scoped Cicest grammar defined in `docs/language/syntax.md`.

## Public API

- Header: `include/cstc_parser/parser.hpp`
- Types:
  - `cstc::parser::ParseError`
- Functions:
  - `cstc::parser::parse_tokens(...)`
  - `cstc::parser::parse_source_at(...)`
  - `cstc::parser::parse_source(...)`

## CMake

- Target: `cstc_parser` (`INTERFACE`)
- Alias: `cicest::compiler::parser`
- Depends on: `cstc_ast`, `cstc_lexer`, `cstc_span`

## Tests

- `tests/parser_basic.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
