# Cicest Standard Library Reference

The standard library surface currently centers on the implicit std prelude and a
small runtime-backed function set.

## Main Reference

- [std - Standard Library](std.md)

## What To Expect There

`std.md` documents:

- prelude injection into every non-prelude module
- lang items such as `Constraint`
- runtime-backed extern declarations
- the runtime ABI used by codegen and `runtime.c`
- the current string runtime model and ownership-sensitive APIs
- plain std helpers that participate in the language's ordinary call-lifting rule

For the precise syntax of runtime-qualified declarations, references, and
expressions, pair this page with `../language/syntax.md`.
