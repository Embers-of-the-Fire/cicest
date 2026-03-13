# `cstc_span`

Small header-only source span package.

## Purpose

Provides byte-range location primitives used by lexer, parser, and AST nodes.

## Public API

- Header: `include/cstc_span/span.hpp`
- `cstc::span::BytePos`
- `cstc::span::SourceSpan`
- `cstc::span::SourceLocation`
- `cstc::span::SourceFile`
- `cstc::span::ResolvedSpan`
- `cstc::span::SourceMap`
- `cstc::span::merge(...)`

### Rustc-style source tracking

`SourceMap` assigns each file a unique absolute byte range (with a one-byte gap
between files), so a single `SourceSpan` can be resolved back to its owning
file via `resolve_span(...)`.

## CMake

- Target: `cstc_span` (`INTERFACE`)
- Alias: `cicest::compiler::span`

## Tests

- `tests/span_basic.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
