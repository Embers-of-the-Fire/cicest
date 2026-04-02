# `cstc_lexer`

Compiled lexer package for Cicest source files.

## Purpose

Converts source text into token streams with optional trivia retention,
including keywords, literals, punctuation, operators, and EOF sentinel.
All token text is interned into a shared symbol table.

## Public API

- Headers:
  - `include/cstc_lexer/token.hpp`
  - `include/cstc_lexer/lexer.hpp`
- Implementation:
  - `src/lexer.cpp`
- Core types:
  - `cstc::lexer::TokenKind`
  - `cstc::lexer::Token`
- Helpers:
  - `cstc::lexer::lex_source_at(..., symbols, ...)`
  - `cstc::lexer::lex_source(..., symbols, ...)`
  - `cstc::lexer::is_trivia(...)`
  - `cstc::lexer::token_kind_name(...)`

## CMake

- Target: `cstc_lexer` (`STATIC`)
- Alias: `cicest::compiler::lexer`
- Depends on: `cstc_symbol`, `cstc_span`

## Tests

- `tests/lexer_basic.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
