# `cstc_ast`

Header-only abstract syntax tree package for the scoped Cicest frontend.

## Purpose

Defines AST node structures for top-level items, statements, and expressions,
plus a readable AST formatter used by inspector tooling and tests.
All textual fields in AST nodes are stored as interned symbols.

Expression forms include staged constructs such as `decl(expr)` and the forced
block runtime boundary `runtime { ... }`.

## Public API

- Headers:
  - `include/cstc_ast/ast.hpp`
  - `include/cstc_ast/printer.hpp`
- Core types:
  - `cstc::ast::Program`
  - `cstc::ast::Item`
  - `cstc::ast::Expr`
- Helpers:
  - `cstc::ast::make_expr(...)`
  - `cstc::ast::format_program(..., symbols)`

## CMake

- Target: `cstc_ast` (`INTERFACE`)
- Alias: `cicest::compiler::ast`
- Depends on: `cstc_symbol`, `cstc_span`

## Tests

- `tests/printer_basic.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
