# `cstc_span`

Small header-only source span package.

## Purpose

Provides byte-range location primitives used by lexer, parser, and AST nodes.

## Public API

- Header: `include/cstc_span/span.hpp`
- `cstc::span::SourceSpan`
- `cstc::span::merge(...)`

## CMake

- Target: `cstc_span` (`INTERFACE`)
- Alias: `cicest::compiler::span`

## Tests

- `tests/span_basic.cpp`
- Built when `CICEST_BUILD_TESTS=ON`

