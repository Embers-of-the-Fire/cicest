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

## Adding New Functions

To add a new standard library function:

1. Add the `extern "lang" fn` declaration to `prelude.cst`.
2. Implement the function in the runtime (linked at build time).
3. Update this README with the new function's documentation.
