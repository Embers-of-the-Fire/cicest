# Cicest Standard Library (`std`)

The cicest standard library provides built-in functions that are automatically
available in every cicest program through the **prelude**.

## Prelude

The prelude (`prelude.cst`) is automatically injected at the beginning of every
compilation. It declares extern functions that are implemented by the runtime
and linked at build time.

### Available Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `fn print(value: str)` | Prints a string to standard output. |
| `println` | `fn println(value: str)` | Prints a string followed by a newline. |
| `to_str` | `fn to_str(value: num) -> str` | Converts a number to its string representation. |
| `str_concat` | `fn str_concat(a: str, b: str) -> str` | Concatenates two strings. |
| `str_len` | `fn str_len(value: str) -> num` | Returns the length of a string. |

## Architecture

All standard library functions are declared using the `extern "lang"` syntax:

```cicest
extern "lang" fn print(value: str);
```

The `"lang"` ABI string indicates these functions are provided by the cicest
language runtime. The compiler emits LLVM `declare` instructions for these
functions, and the actual implementations are supplied at link time.

## Runtime

The actual implementations of prelude functions live in `runtime.c`. This file
is compiled into a static library (`libcicest_rt.a`) by CMake and automatically
linked into every executable produced by the compiler.

| Cicest declaration | C signature |
|--------------------|-------------|
| `fn print(value: str)` | `void print(const char*)` |
| `fn println(value: str)` | `void println(const char*)` |
| `fn to_str(value: num) -> str` | `char* to_str(double)` |
| `fn str_concat(a: str, b: str) -> str` | `char* str_concat(const char*, const char*)` |
| `fn str_len(value: str) -> num` | `double str_len(const char*)` |

Functions returning `str` allocate memory with `malloc`. There is currently no
garbage collector or ownership tracking — allocations are freed when the process
exits.

## Adding New Functions

To add a new standard library function:

1. Add the `extern "lang" fn` declaration to `prelude.cst`.
2. Implement the function in `runtime.c`.
3. Update this README with the new function's documentation.
4. Add tests in `compiler/cstc_codegen/tests/codegen_integration.cpp`.
