# `cstc_lexer`

Header-only lexer package for Cicest source files.

## Purpose

Converts source text into token streams with optional trivia retention,
including keywords, literals, punctuation, operators, and EOF sentinel.

## Public API

- Headers:
  - `include/cstc_lexer/token.hpp`
  - `include/cstc_lexer/lexer.hpp`
- Core types:
  - `cstc::lexer::TokenKind`
  - `cstc::lexer::Token`
- Helpers:
  - `cstc::lexer::lex_source(...)`
  - `cstc::lexer::is_trivia(...)`
  - `cstc::lexer::token_kind_name(...)`

## CMake

- Target: `cstc_lexer` (`INTERFACE`)
- Alias: `cicest::compiler::lexer`
- Depends on: `cstc_span`

## Tests

- `tests/lexer_basic.cpp`
- Built when `CICEST_BUILD_TESTS=ON`

