<!-- markdownlint-disable MD013 MD060 -->

# Cicest Standard Library (`std`)

The cicest standard library provides built-in functions that are automatically
available in every cicest program through the **prelude**.

## Prelude

The prelude (`prelude.cst`) is compiled as a normal module whose `pub` items
are implicitly imported into every non-prelude module. Additional std modules
can be loaded explicitly through `@std/...` import paths.

### Available Functions

| Function     | Signature                              | Description                                                                                             |
| ------------ | -------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `print`      | `fn print(value: str)`                 | Prints a string to standard output.                                                                     |
| `println`    | `fn println(value: str)`               | Prints a string followed by a newline.                                                                  |
| `to_str`     | `fn to_str(value: num) -> str`         | Converts a number to its string representation.                                                         |
| `str_concat` | `fn str_concat(a: str, b: str) -> str` | Concatenates two strings.                                                                               |
| `str_len`    | `fn str_len(value: str) -> num`        | Returns the length of a string.                                                                         |
| `str_free`   | `fn str_free(value: str)`              | Frees a heap-allocated string.                                                                          |
| `assert`     | `fn assert(condition: bool)`           | Aborts the program with `assertion failed` on standard error when `condition` is `false`.               |
| `assert_eq`  | `fn assert_eq(a: num, b: num)`         | Aborts the program when two numbers differ by more than `1e-9`, printing both values on standard error. |

## Architecture

All standard library functions are declared using the `extern "lang"` syntax:

```cicest
[[lang = "cstc_std_print"]]
pub extern "lang" fn print(value: str);
```

The `"lang"` ABI string indicates these functions are provided by the cicest
language runtime. Although the source attribute is still written
`[[lang = "..."]]`, lowering now resolves that attribute into a distinct
`link_name` field on extern function declarations. The Cicest function name
stays unchanged for calls and type checking, while LLVM codegen emits the
resolved `link_name`.

```cicest
[[lang = "cstc_std_print"]]
pub extern "lang" fn print(value: str);
```

This is called in Cicest as `print("hello")`, but codegen declares and calls
`@cstc_std_print` at the LLVM level. The actual implementations are supplied
at link time.

## Runtime

The actual implementations of prelude functions live in `runtime.c`. This file
is compiled into a static library (`libcicest_rt.a` on GNU-like toolchains,
`cicest_rt.lib` on MSVC) by CMake and automatically
linked into every executable produced by the compiler.

| Cicest declaration                     | C signature                                           |
| -------------------------------------- | ----------------------------------------------------- |
| `fn print(value: str)`                 | `void cstc_std_print(const char*)`                    |
| `fn println(value: str)`               | `void cstc_std_println(const char*)`                  |
| `fn to_str(value: num) -> str`         | `char* cstc_std_to_str(double)`                       |
| `fn str_concat(a: str, b: str) -> str` | `char* cstc_std_str_concat(const char*, const char*)` |
| `fn str_len(value: str) -> num`        | `double cstc_std_str_len(const char*)`                |
| `fn str_free(value: str)`              | `void cstc_std_str_free(const char*)`                 |
| `fn assert(condition: bool)`           | `void cstc_std_assert(int)`                           |
| `fn assert_eq(a: num, b: num)`         | `void cstc_std_assert_eq(double, double)`             |

### String Ownership

Functions that return `str` (`to_str`, `str_concat`) allocate a new
heap-allocated string via `malloc`. The caller owns the returned string and is
responsible for releasing it with `str_free` when it is no longer needed.

String literals (e.g. `"hello"`) are stored in read-only memory and must **not**
be passed to `str_free`.

## Adding New Functions

To add a new standard library function:

1. Add the `pub extern "lang" fn` declaration to `prelude.cst`.
   Add `[[lang = "..."]]` when the runtime symbol name differs from the
   source-level Cicest function name.
1. Implement the function in `runtime.c`.
1. Update this README with the new function's documentation.
1. Add tests in `compiler/cstc_codegen/tests/codegen_integration.cpp`.
