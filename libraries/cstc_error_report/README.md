# `cstc_error_report`

Compiled Rust-like diagnostic renderer.

## Purpose

Provides a compiler-independent diagnostic model that works on file-local byte
offsets and can render:

- Point comments anchored to token offsets
- Span labels anchored to token offsets
- Nested warning/error/note/help trees
- Optional ANSI color when enabled through `cstc_ansi_color`

## Public API

- Header: `include/cstc_error_report/report.hpp`
- Implementation: `src/report.cpp`
- `cstc::error_report::SourceId`
- `cstc::error_report::Offset`
- `cstc::error_report::SourcePoint`
- `cstc::error_report::SourceSpan`
- `cstc::error_report::SourceLocation`
- `cstc::error_report::SourceFile`
- `cstc::error_report::ResolvedPoint`
- `cstc::error_report::ResolvedSpan`
- `cstc::error_report::SourceDatabase`
- `cstc::error_report::Severity`
- `cstc::error_report::LabelStyle`
- `cstc::error_report::Label`
- `cstc::error_report::Comment`
- `cstc::error_report::Diagnostic`
- `cstc::error_report::RenderOptions`
- `cstc::error_report::render(...)`

## Internal Design

- `SourceDatabase` owns source text and precomputes line starts so byte offsets
  resolve to stable `line:column` pairs without any compiler-specific types.
- The public header exposes the data model and database methods, while rendering
  internals now live in `src/report.cpp` to reduce transitive compile cost.
- Labels and comments are grouped per source file at render time, which lets a
  single diagnostic tree reference more than one file.
- Rendering is intentionally simple and deterministic: each relevant source line
  is printed once, followed by dedicated marker lines for labels and comments.
- Color is handled entirely through `cstc_ansi_color`; the renderer only
  requests styled fragments and never emits ANSI escapes on its own.

## CMake

- Target: `cstc_error_report` (`STATIC`)
- Alias: `cicest::library::error_report`

## Tests

- `tests/report_render.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
