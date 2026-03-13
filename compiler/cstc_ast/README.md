# `cstc_ast`

Header-only abstract syntax tree package for the scoped Cicest frontend.

## Purpose

Defines AST node structures for top-level items, statements, and expressions,
plus a readable AST formatter used by inspector tooling and tests.

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
  - `cstc::ast::format_program(...)`

## CMake

- Target: `cstc_ast` (`INTERFACE`)
- Alias: `cicest::compiler::ast`
- Depends on: `cstc_span`

## Tests

- `tests/printer_basic.cpp`
- Built when `CICEST_BUILD_TESTS=ON`

