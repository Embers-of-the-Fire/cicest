# `cstc_ansi_color`

Header-only ANSI color helper library.

## Purpose

Provides a small, compiler-independent place for:

- Deciding whether ANSI escape sequences may be emitted
- Reading common color-related environment variables
- Wrapping text in ANSI style sequences when color is enabled

The library defaults to emitting no ANSI control characters unless color is
explicitly enabled through environment variables and the current platform is
treated as ANSI-capable.

## Public API

- Header: `include/cstc_ansi_color/ansi_color.hpp`
- `cstc::ansi_color::Emission`
- `cstc::ansi_color::Color`
- `cstc::ansi_color::Style`
- `cstc::ansi_color::Environment`
- `cstc::ansi_color::current_environment()`
- `cstc::ansi_color::platform_supports_ansi(...)`
- `cstc::ansi_color::detect_emission(...)`
- `cstc::ansi_color::paint(...)`
- `cstc::ansi_color::prefix(...)`
- `cstc::ansi_color::suffix(...)`

## Internal Design

- Environment variables are copied into an `Environment` value so callers can
  test policy without mutating process state.
- `detect_emission(...)` requires both ANSI capability and an explicit opt-in
  (`CICEST_COLOR`, `FORCE_COLOR`, `CLICOLOR_FORCE`, or `CLICOLOR`) unless a
  disabling variable such as `NO_COLOR` is present.
- Styling is intentionally small: bold + foreground color, which is enough for
  diagnostic rendering without introducing a formatting DSL.

## CMake

- Target: `cstc_ansi_color` (`INTERFACE`)
- Alias: `cicest::library::ansi_color`

## Tests

- `tests/ansi_color.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
