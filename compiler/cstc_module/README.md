# `cstc_module`

Header-only module graph loader and import resolver.

## Purpose

Builds a crate-wide `cstc::ast::Program` from a root module path by parsing
every reachable file, resolving named imports, applying the implicit std
prelude, and rewriting internal names so private helpers from different modules
do not collide.

## Public API

- Header: `include/cstc_module/module.hpp`
- Types:
  - `cstc::module::ModuleError`
- Functions:
  - `cstc::module::format_module_error(..., error)`
  - `cstc::module::load_program(..., root_path, std_root_path)`

## Behavior

- Resolves relative imports from the importing module's directory.
- Resolves `@std/...` imports from the configured std root.
- Applies the implicit std prelude to every non-prelude module.
- Enforces `pub` visibility and supports `pub import` re-exports.
- Rejects duplicate imported/local names and cyclic imports.
- Returns a flattened AST that later compiler stages can consume unchanged.

## CMake

- Target: `cstc_module` (`INTERFACE`)
- Alias: `cicest::compiler::module`
- Depends on: `cstc_ast`, `cstc_parser`, `cstc_resource_path`, `cstc_span`,
  `cstc_symbol`

## Tests

- `tests/module_resolution.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
