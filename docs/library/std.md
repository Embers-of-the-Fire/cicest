<!-- markdownlint-disable MD013 MD060 -->

# std — Standard Library

The cicest standard library provides a set of built-in functions that are
automatically available in every cicest program. These functions are declared
in the **prelude** (`libraries/std/prelude.cst`) and injected into every
compilation before user source code is parsed.

## Position in the pipeline

```text
libraries/std/prelude.cst
        │
        ▼
┌───────────────────────────────┐
│  SourceMap.add_file(prelude)  │   ← separate SourceFileId
│  SourceMap.add_file(user)     │   ← separate SourceFileId
└───────────────────────────────┘
        │
        ▼
┌───────────────────────────────┐
│  Parser: parse prelude        │
│  Parser: parse user source    │
└───────────────────────────────┘
        │
        ▼
┌───────────────────────────────┐
│  Merge: prelude AST + user    │   ← single ast::Program
└───────────────────────────────┘
        │
        ▼
   TyIR → LIR → LLVM IR
```

The prelude is parsed as a separate source file with its own `SourceFileId`.
Error messages for prelude declarations resolve to `prelude.cst:line:col`,
while user errors resolve to the user's file path.

## Prelude injection

Both the compiler (`cstc`) and the inspector (`cstc_inspect`) inject the
prelude automatically. The injection process is:

1. Read `prelude.cst` from the path defined by `CICEST_STD_PATH` (set at
   compile time via CMake).
1. Add it to the `SourceMap` as a separate file.
1. Parse the prelude source independently.
1. Parse the user source independently.
1. Merge the two `ast::Program` item lists (prelude items first).
1. Continue the pipeline with the merged program.

> **Note:** The `tokens` and `ast` output modes of `cstc_inspect` do not
> inject the prelude, since they display only the user's source-level
> representation. The `tyir`, `lir`, and `llvm` modes include the prelude.

## Extern declaration syntax

All standard library functions use the `extern` declaration syntax:

```cicest
[[lang = "cstc_std_print"]]
extern "lang" fn print(value: str);
[[lang = "cstc_std_println"]]
extern "lang" fn println(value: str);
extern "lang" struct Handle;
```

| Component | Description |
|-----------|-------------|
| `extern` | Keyword introducing an external declaration |
| `"lang"` | ABI string literal — `"lang"` denotes the cicest language runtime |
| `fn` / `struct` | Declares a function signature or opaque struct type |
| `;` | Terminator — extern declarations have no body |
| `[[lang = "..."]]` | Optional attribute setting the extern function's `link_name` used by codegen |

The ABI string is stored as a `Symbol` and carried through the full pipeline
(AST → TyIR → LIR). The codegen layer currently ignores the ABI value and
emits all extern functions with LLVM `ExternalLinkage`. Future ABI strings
(e.g., `"c"`) can alter calling conventions without grammar changes.

For extern functions, the compiler now preserves both the source-level
function name and the linked symbol name. Without the attribute, `link_name`
defaults to the declared function name. With the attribute, the source name
still remains available for calls and type checking, while codegen emits the
resolved `link_name`.

```cicest
[[lang = "cstc_std_print"]]
extern "lang" fn print(value: str);
```

This declaration is called in Cicest as `print("hello")`, but it lowers to an
extern declaration for `@cstc_std_print` in LLVM IR.

### Extern functions

Extern functions participate in name resolution identically to regular
functions: their signatures are registered in `TypeEnv::fn_signatures` during
TyIR lowering. The type checker validates argument types and counts for calls
to extern functions just as it does for user-defined functions.

In LLVM IR, extern functions are emitted as `declare` (no body):

```llvm
declare void @cstc_std_println(ptr)
declare ptr @cstc_std_to_str(double)
```

### Extern structs

Extern structs are opaque named types with no visible fields. They are
tracked separately from regular structs and cannot be constructed with
struct-init expressions (`ExternType {}` is a type error). They can be used
as type annotations and passed through functions that return or accept them.

In LLVM IR, extern structs are emitted as empty struct types:

```llvm
%Handle = type {}
```

## Runtime library

The actual implementations of the prelude functions live in
`libraries/std/runtime.c`. This file is compiled into a static library
(`libcicest_rt.a` on GNU-like toolchains, `cicest_rt.lib` on MSVC) by CMake and
automatically linked into every executable produced by the compiler.

The runtime path is resolved at startup: the compiler first checks for the
platform-appropriate archive under `<exe_dir>/../lib/cicest/` (installed
layout), then falls back to the `CICEST_RT_PATH` compile definition (development
builds). The filename is derived at compile time so it matches the archive name
CMake produces for the current toolchain. When the compiler invokes the linker,
it passes the runtime archive as a positional argument alongside the user's
object file:

```text
c++  user.o  /path/to/libcicest_rt.a  -o  user       # GNU / MinGW
cl   user.obj  /path/to/cicest_rt.lib  /Fe:user.exe   # MSVC
```

### Runtime function signatures

The C implementations must match the LLVM IR signatures emitted by codegen:

| Cicest declaration | LLVM IR | C signature |
|--------------------|---------|-------------|
| `fn print(value: str)` | `declare void @cstc_std_print(ptr)` | `void cstc_std_print(const char*)` |
| `fn println(value: str)` | `declare void @cstc_std_println(ptr)` | `void cstc_std_println(const char*)` |
| `fn to_str(value: num) -> str` | `declare ptr @cstc_std_to_str(double)` | `char* cstc_std_to_str(double)` |
| `fn str_concat(a: str, b: str) -> str` | `declare ptr @cstc_std_str_concat(ptr, ptr)` | `char* cstc_std_str_concat(const char*, const char*)` |
| `fn str_len(value: str) -> num` | `declare double @cstc_std_str_len(ptr)` | `double cstc_std_str_len(const char*)` |
| `fn str_free(value: str)` | `declare void @cstc_std_str_free(ptr)` | `void cstc_std_str_free(const char*)` |
| `fn assert(condition: bool)` | `declare void @cstc_std_assert(i1)` | `void cstc_std_assert(int)` |
| `fn assert_eq(a: num, b: num)` | `declare void @cstc_std_assert_eq(double, double)` | `void cstc_std_assert_eq(double, double)` |

> **Note:** Functions returning `str` (`to_str`, `str_concat`) allocate memory
> with `malloc`. The caller owns the returned string and should release it with
> `str_free` when it is no longer needed. String literals must not be passed to
> `str_free`. This is a manual convention today, not a type-checked ownership
> rule. See [docs/unresolved/string-lifetime.md](../unresolved/string-lifetime.md).

## Available functions

The prelude currently provides the following functions:

### I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `fn print(value: str)` | Prints a string to standard output without a trailing newline. |
| `println` | `fn println(value: str)` | Prints a string to standard output followed by a newline. |

### Conversion

| Function | Signature | Description |
|----------|-----------|-------------|
| `to_str` | `fn to_str(value: num) -> str` | Converts a number to its string representation. |

### String operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `str_concat` | `fn str_concat(a: str, b: str) -> str` | Concatenates two strings and returns the result. |
| `str_len` | `fn str_len(value: str) -> num` | Returns the length of a string as a number. |
| `str_free` | `fn str_free(value: str)` | Frees a heap-allocated string returned by `to_str` or `str_concat`. |

### Assertions

| Function | Signature | Description |
|----------|-----------|-------------|
| `assert` | `fn assert(condition: bool)` | Terminates the program with `assertion failed` on standard error when `condition` is `false`. |
| `assert_eq` | `fn assert_eq(a: num, b: num)` | Terminates the program when `a` and `b` differ by more than `1e-9`, reporting both values on standard error. |

## Usage examples

### Basic output

```cicest
fn main() {
    println("hello world");
}
```

Compiles to LLVM IR containing:

```llvm
declare void @cstc_std_println(ptr)

define i32 @main() {
bb0:
  call void @cstc_std_println(ptr @0)
  ret i32 0
}
```

### String operations example

```cicest
fn main() {
    let greeting: str = str_concat("hello, ", "world");
    println(greeting);
    let length: num = str_len(greeting);
    println(to_str(length));
}
```

### Mixing with user-defined functions

```cicest
fn greet(name: str) {
    let message: str = str_concat("hello, ", name);
    println(message);
}

fn main() {
    greet("cicest");
}
```

### Assertions example

```cicest
fn main() {
    let answer: num = 40 + 2;
    assert(answer > 0);
    assert_eq(answer, 42);
}
```

## Inspecting the prelude

The `cstc_inspect` tool shows prelude declarations in its output when using
the `tyir`, `lir`, or `llvm` output modes.

```bash
cstc_inspect my_file.cst --out-type tyir
```

Example output (prelude items appear first):

```text
TyProgram
  TyExternFnDecl "lang" print(value: str) -> Unit
  TyExternFnDecl "lang" println(value: str) -> Unit
  TyExternFnDecl "lang" to_str(value: num) -> str
  TyExternFnDecl "lang" str_concat(a: str, b: str) -> str
  TyExternFnDecl "lang" str_len(value: str) -> num
  TyFnDecl main() -> Unit
    ...
```

## Pipeline representation

Each pipeline stage has a corresponding node type for extern declarations:

| Stage | Node | Container |
|-------|------|-----------|
| AST | `ExternFnDecl`, `ExternStructDecl` | `Item` variant |
| TyIR | `TyExternFnDecl`, `TyExternStructDecl` | `TyItem` variant |
| LIR | `LirExternFnDecl`, `LirExternStructDecl` | `LirProgram::extern_fns`, `LirProgram::extern_structs` |
| LLVM IR | `declare` instruction | Module-level |

Extern structs have their own dedicated node at every stage. At TyIR,
`TyExternStructDecl` preserves the ABI and opaque nature so that later
passes can distinguish an extern struct from a normal user-defined ZST.
At LIR, `LirExternStructDecl` is stored in `LirProgram::extern_structs`.
Extern functions preserve both `name` and `link_name` in TyIR and LIR, and
LLVM codegen emits the `link_name` while still resolving calls by the source
name inside Cicest.

## Adding new standard library functions

To add a new function to the standard library:

1. Add an `extern "lang" fn` declaration to `libraries/std/prelude.cst`.
   If the runtime symbol differs from the Cicest function name, add
   `[[lang = "..."]]` so the declaration gets the correct `link_name`.
1. Implement the function in the language runtime (linked at build time).
1. Update the function tables in this document.
1. Add tests in `compiler/cstc_codegen/tests/codegen_integration.cpp` that
   verify the new function is correctly declared and callable.

## Limitations (current version)

- All extern functions use `"lang"` ABI. No other ABI strings (e.g., `"c"`)
  are semantically distinguished yet.
- Extern structs are zero-sized and cannot carry data. They serve only as
  opaque type handles.
- The prelude is always injected — there is no mechanism to suppress it.
- No module or import system exists. All prelude declarations are global.
- Functions returning `str` allocate memory with `malloc`. Callers should use
  `str_free` to release the returned string when it is no longer needed, but
  the language does not yet define a settled lifetime/ownership model for
  `str`. See [docs/unresolved/string-lifetime.md](../unresolved/string-lifetime.md).
