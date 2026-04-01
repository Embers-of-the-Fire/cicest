# `cstc_resource_path`

Compiled resource path discovery helpers.

## Purpose

Provides relocatable lookup helpers for the std library directory and runtime
archive based on the current executable location, with development-layout
fallbacks.

## Public API

- Header: `include/cstc_resource_path/resource_path.hpp`
- Implementation: `src/resource_path.cpp`
- `cstc::resource_path::rt_library_filename`
- `cstc::resource_path::normalize_existing_path(...)`
- `cstc::resource_path::path_exists(...)`
- `cstc::resource_path::canonicalize_or_throw(...)`
- `cstc::resource_path::self_exe_dir()`
- `cstc::resource_path::resolve_std_dir(...)`
- `cstc::resource_path::resolve_rt_path(...)`

## Internal Design

- Platform-specific executable discovery is isolated to `src/resource_path.cpp`.
- Installed-layout probing still checks `../share/cicest/std` and
  `../lib/cicest/<archive>` relative to the compiler binaries.
- Development builds still fall back to the compile-time paths passed by the
  callers.

## CMake

- Target: `cstc_resource_path` (`STATIC`)
- Alias: `cicest::resource_path`

## Tests

- `tests/resource_path.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
