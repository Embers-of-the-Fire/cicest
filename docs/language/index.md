# Cicest Language Reference

This section documents the implemented source language and the compiler-facing
semantic layers that back it.

## Recommended Order

1. [Language Syntax](syntax.md)
2. [Module System](modules.md)
3. [Ownership and Drop](ownership.md)
4. [TyIR - Typed Intermediate Representation](tyir.md)
5. [LIR - Low-level Intermediate Representation](lir.md)

## What Each Page Covers

- `syntax.md`: supported surface grammar, type forms, `runtime` qualification,
  generics, `where` clauses, and expression forms.
- `modules.md`: root-module loading, `import` resolution, visibility, re-exports,
  and std prelude injection.
- `ownership.md`: the current owned `str` model, `&str`, move semantics, and
  compiler-inserted drop behavior.
- `tyir.md`: the typed IR used after parsing and resolution, including how the
  compiler records staged and runtime-qualified information.
- `lir.md`: the lowered control-flow-oriented IR used before LLVM codegen.

## Scope Note

These pages describe the implemented language subset, not the full long-term
design space. When a topic is intentionally unresolved, the relevant note lives
under `../unresolved/` rather than here.
