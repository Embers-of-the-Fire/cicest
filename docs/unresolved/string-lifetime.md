# String Lifetime and `str_free`

This page is retained as historical context for the old manual `str_free`
convention.

The current ownership model is defined in
[docs/language/ownership.md](../language/ownership.md). `str` is now an owned
move-only type, `&str` is a shared borrow, and ordinary code relies on
automatic scope-exit drop instead of manual deallocation.

## Previous behavior

Today, the standard runtime exposes:

- `to_str(value: num) -> str`
- `str_concat(a: str, b: str) -> str`
- `str_free(value: str)`

At runtime:

- `to_str` allocates a fresh heap string with `malloc`
- `str_concat` allocates a fresh heap string with `malloc`
- `str_free` directly calls `free`

That older contract was purely by convention:

- Strings returned by `to_str` and `str_concat` are intended to be freed by the
  caller.
- String literals must not be passed to `str_free`.
- Borrowed `str` values received from elsewhere must not be assumed to be
  freeable.
- The same pointer must not be passed to `str_free` more than once.

## Why the old model was dangerous

The language currently has only one visible `str` type. It does not distinguish:

- string literals
- borrowed string references
- heap-owned strings returned by runtime helpers
- moved values versus aliased values

Because of that, the compiler cannot currently prevent code like:

```cicest
fn main() {
    str_free("hello"); // invalid, but not rejected by the type system today
}
```

or:

```cicest
fn main() {
    let value: str = to_str(42);
    str_free(value);
    str_free(value); // double free
}
```

These errors can become runtime memory corruption, crashes, or other undefined
behavior depending on the allocator and platform.

## Current guidance

For the current language rules, use
[docs/language/ownership.md](../language/ownership.md).

`str_free(value: str)` still exists, but it is now a low-level consuming
primitive instead of the primary ownership mechanism.
