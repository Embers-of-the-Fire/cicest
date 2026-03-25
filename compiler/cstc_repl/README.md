# `cstc_repl`

Interactive REPL support for Cicest.

This package contains:

- `cstc_repl_lib`: the session engine used by tests and the CLI frontend
- `cstc_repl`: the interactive binary

## Purpose

`cstc_repl` provides a practical REPL on top of the existing file-based Cicest
compiler pipeline.

There is no dedicated interpreter or JIT backend in the project today, so the
REPL works by:

1. materializing a temporary root module
1. reusing the normal module/type/lowering pipeline
1. emitting a native executable for the current turn
1. running that executable and surfacing its stdout/stderr back to the user

## Public API

Header: `include/cstc_repl/repl.hpp`

### Core types

- `cstc::repl::SessionOptions`
- `cstc::repl::SubmissionStatus`
- `cstc::repl::SubmissionResult`
- `cstc::repl::Session`

### Session workflow

```cpp
#include <cstc_repl/repl.hpp>

int main() {
    cstc::repl::Session session({
        .session_root_dir = std::filesystem::current_path(),
    });

    const auto first = session.submit("let x: num = 40 + 2;");
    const auto second = session.submit("x");
}
```

`Session::submit(...)` accepts:

- REPL commands: `:help`, `:show`, `:state`, `:reset`, `:quit`, `:exit`
- top-level items: `import`, `struct`, `enum`, `fn`, `extern`
- function-body fragments: statements and/or a trailing expression

`Session::needs_continuation(...)` is intended for prompt UIs that need to
decide whether to keep collecting more lines.

## Persistence model

The REPL persists:

- top-level items exactly as entered
- non-discard top-level `let` bindings entered at the prompt

It intentionally does **not** persist expression statements. That keeps
one-shot side effects such as `println("hello");` from replaying on later turns.

Persisted `let` bindings are replayed when later snippets execute so that
subsequent turns can reference earlier bindings:

```cicest
let x: num = 41 + 1;
x
```

Because bindings are replayed, any side effects inside a persisted `let`
initializer will also replay on later turns.

## REPL commands

The REPL provides a small built-in command set:

- `:help` prints the command summary
- `:show` prints persisted top-level items and replayed `let` bindings
- `:state` is an alias for `:show`
- `:reset` clears persisted state
- `:quit` / `:exit` leave the REPL

When the interactive REPL starts, it prints a short banner that points users to
`:help` and `:quit`.

## Result display

Trailing expressions are auto-rendered only for types the standard library can
already print directly:

- `num`
- `str`
- `&str`
- `bool`
- `Unit`

Other expression results are evaluated but not displayed.

## Reserved names

The REPL synthesizes helper functions using the prefix
`__cstc_repl_internal_`, and it owns the generated `main` function.

User-visible top-level names and persisted top-level `let` bindings must not
use that prefix, and top-level items must not be named `main`.

## Relative imports

The session root module is written inside `SessionOptions::session_root_dir`.
That directory is therefore the base for relative imports such as:

```cicest
import { answer } from "math.cst";
```

Using the current working directory for `session_root_dir` gives normal
file-based import behavior from the shell location where the REPL is started.

## Internal implementation

The implementation is intentionally conservative:

- Parses item submissions directly as `Program`.
- Parses body submissions by wrapping them in a temporary probe function.
- Extracts exact source slices from AST spans instead of trying to pretty-print
  reconstructed snippets.
- Reuses `cstc_module`, `cstc_tyir_builder`, `cstc_lir_builder`, and
  `cstc_codegen` for every successful turn.
- Links against the standard runtime archive the same way the compiler CLI
  does.
- Spawns the linker and generated executable directly with explicit
  stdout/stderr redirection instead of shelling out through `std::system`.

## CMake

- Library target: `cstc_repl_lib`
- Executable target: `cstc_repl`
- Alias: `cicest::compiler::repl`

## Tests

- `tests/repl_session.cpp`
- Built when `CICEST_BUILD_TESTS=ON`
