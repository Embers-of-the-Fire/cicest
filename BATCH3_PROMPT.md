# Batch 3 Prompt — Monomorphization, Backend, Docs

Continue from branch `feat/generic-monomorphization`.

## Context

- Batch 1 already landed on `feat/generic-typeck-foundation`.
- Batch 2 is intended to land generic constraint evaluation and diagnostics on `feat/generic-constraints`.
- This batch should assume frontend generic resolution exists and constraints are enforced before backend lowering.

## Goal

Implement backend-facing generic solidification and polish:

- monomorphize generic declarations during LIR lowering
- apply substitution-based solidification for deferred and explicit instantiations
- ensure stable internal identities for instantiated items
- add end-to-end coverage and update language docs

## Main outcomes required

- Generic declarations do not leak unresolved type parameters into LIR/codegen.
- Instantiated functions/structs/enums get stable internal identities.
- Deferred and explicit instantiations are solidified through substitution before backend use.
- e2e pass/fail fixtures cover hot paths and edge cases.
- docs describe generics, constraints, and the no-generics-after-lowering boundary clearly.

## Key code areas

- `compiler/cstc_lir_builder/src/builder.cpp`
- `compiler/cstc_lir/include/cstc_lir/lir.hpp`
- `compiler/cstc_codegen/src/codegen.cpp`
- `compiler/cstc_tyir/include/cstc_tyir/tyir.hpp`
- `compiler/cstc_tyir_builder/src/builder.cpp`
- `compiler/cstc_lir_builder/tests/*.cpp`
- `compiler/cstc_codegen/tests/*.cpp`
- `test/e2e/pass/**`
- `test/e2e/fail_compile/**`
- `docs/language/syntax.md`
- `docs/language/tyir.md`
- `docs/language/lir.md`

## Recommended tiny commits

1. Introduce instantiated item identity/mangling utilities.
2. Add a monomorphization table/cache for instantiated items.
3. Lower instantiated generic structs/enums/functions into concrete LIR items.
4. Solidify deferred instantiations before LIR emission.
5. Update codegen to consume stable instantiated identities.
6. Add LIR/codegen tests for instantiated items.
7. Add e2e pass/fail fixtures for generics and constraints.
8. Update docs for syntax, TyIR, and LIR limitations/behavior.

## Important implementation notes

- LIR and codegen currently assume concrete symbols and no unresolved generics.
- Prefer eliminating generics before or during LIR lowering rather than teaching codegen to reason about generic IR.
- Internal names for instantiated items must be deterministic and stable.
- Watch for recursive instantiation and duplicate instance generation; cache aggressively.
- Preserve user-facing names in diagnostics while using stable internal identities under the hood.

## Validation requirements

- Run `nix run .#tests`
- Run `nix run .#lint`

## Commit style

Use conventional commits for each tiny task, e.g.:

- `feat(lir): monomorphize generic items during lowering`
- `feat(codegen): stabilize identities for instantiated items`
- `docs(language): document generic lowering and constraint behavior`
- `test(e2e): cover generic monomorphization paths`
