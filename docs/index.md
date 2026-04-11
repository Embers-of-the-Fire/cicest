# Cicest Documentation

This directory is the stable documentation hub for the language prototype.

## Reading Order

If you want the shortest path to the current design, read these pages first:

1. `language/syntax.md`
2. `language/modules.md`
3. `language/ownership.md`
4. `library/std.md`
5. `language/tyir.md`

That sequence covers the surface language, module and prelude model, ownership
rules, standard library surface, and the compiler-facing typed semantics.

## Sections

- [Language Reference](language/index.md)
- [Standard Library Reference](library/index.md)
- [Unresolved Design Notes](unresolved/index.md)

## Stability Notes

Use `language/` and `library/std.md` as the canonical reference for implemented
behavior.

Use `unresolved/` only for design questions that are intentionally not frozen
yet. Those notes are useful context, but they should not be treated as the
stable language definition.
