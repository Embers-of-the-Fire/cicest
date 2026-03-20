# String Lifetime and `str_free`

This page documents an unresolved part of the current language/runtime design:
the `str` type has manual deallocation via `str_free`, but the language does
not yet define a lifetime, ownership, or scope-based resource-management model.

## Current behavior

Today, the standard runtime exposes:

- `to_str(value: num) -> str`
- `str_concat(a: str, b: str) -> str`
- `str_free(value: str)`

At runtime:

- `to_str` allocates a fresh heap string with `malloc`
- `str_concat` allocates a fresh heap string with `malloc`
- `str_free` directly calls `free`

That means the current contract is purely by convention:

- Strings returned by `to_str` and `str_concat` are intended to be freed by the
  caller.
- String literals must not be passed to `str_free`.
- Borrowed `str` values received from elsewhere must not be assumed to be
  freeable.
- The same pointer must not be passed to `str_free` more than once.

## Why this is dangerous

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

Until the language defines a proper ownership/lifetime model:

- Treat `str_free` as a low-level manual escape hatch.
- Only call `str_free` on values returned by runtime functions that explicitly
  document heap allocation, currently `to_str` and `str_concat`.
- Never call `str_free` on string literals.
- Never call `str_free` twice on the same logical value.
- Be conservative when passing `str` values between functions, because the type
  system does not encode who owns the storage.

## What is unresolved

The project has not yet decided what the long-term model should be called or
how far it should go. Possible directions include:

- explicit ownership and move semantics for heap-backed `str`
- a lifetime model that distinguishes borrowed versus owned strings
- scope-based automatic cleanup
- RAII-like destruction at the end of a binding or block
- keeping manual `str_free` as an unsafe or low-level primitive even if higher
  level lifetime rules are added

## Documentation status

This page is intentionally descriptive, not normative. It records the current
runtime behavior and the risks around `str_free`, but it does not establish the
final language design for lifetime, scope, ownership, or RAII semantics.
