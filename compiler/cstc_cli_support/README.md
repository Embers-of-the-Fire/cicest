# `cstc_cli_support`

Header-only shared helpers for Cicest command-line tools.

## Purpose

Consolidates the common logic used by `cstc` and `cstc_inspect` for reading
source files, comparing canonicalized paths, loading the resolved module graph,
and formatting span-based type and module diagnostics.

## Public API

- Header: `include/cstc_cli_support/support.hpp`
- Functions:
  - `cstc::cli_support::read_source_file(path)`
  - `cstc::cli_support::paths_refer_to_same_file(lhs, rhs, ...)`
  - `cstc::cli_support::format_type_error(source_map, error)`
  - `cstc::cli_support::load_module_program(source_map, root_path, std_root_path)`

## CMake

- Target: `cstc_cli_support` (`INTERFACE`)
- Alias: `cicest::compiler::cli_support`
- Depends on: `cstc_ast`, `cstc_module`, `cstc_parser`, `cstc_resource_path`,
  `cstc_span`, `cstc_tyir_builder`
