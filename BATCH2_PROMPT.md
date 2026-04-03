# Batch 2 Prompt — Generic Constraints

Continue from branch `feat/generic-constraints`.

## Context

- Batch 1 already landed on `feat/generic-typeck-foundation`.
- Implemented there:
  - generic metadata in TyIR
  - generic parameter environments for declaration/signature lowering
  - explicit and inferred generic call resolution
  - generic struct instantiation with substitution
- Validation for batch 1 passed with:
  - `nix run .#tests`
  - `nix run .#lint`

## Goal

Implement the constraint-evaluation batch for generics and `where` clauses.

## Main outcomes required

- Evaluate constraint expressions under generic substitutions/environments.
- Implicitly convert boolean constraint results into `Constraint` semantics.
- Reject runtime-only or otherwise non-const-evaluable operations in constraint position.
- Produce clear compile-time diagnostics for failed constraints.
- Add strong unit and e2e coverage for valid and invalid constraints.

## Key code areas

- `compiler/cstc_tyir_builder/src/builder.cpp`
- `compiler/cstc_tyir_interp/src/interp.cpp`
- `compiler/cstc_tyir_interp/include/cstc_tyir_interp/interp.hpp`
- `compiler/cstc_tyir_builder/tests/*.cpp`
- `compiler/cstc_tyir_interp/tests/folding.cpp`
- `test/e2e/fail_compile/**`
- `test/e2e/pass/**`

## Recommended tiny commits

1. Add a constraint evaluation model and result type for generic environments.
2. Wire generic substitutions into constraint evaluation.
3. Treat `bool` results as valid/invalid constraints.
4. Reject runtime-tagged or impure/non-const operations in constraint position.
5. Enforce constraints during generic instantiation and emit diagnostics.
6. Add unit tests for passing/failing constraint evaluation.
7. Add e2e fixtures for clear compile-time diagnostics.

## Important implementation notes

- Reuse the TyIR interpreter where possible instead of inventing a separate evaluator.
- Constraint evaluation should run with substitution-applied generic environments.
- Failed constraints must produce compile-time errors, not deferred backend failures.
- Diagnostics should distinguish:
  - constraint evaluated to invalid
  - constraint could not be const-evaluated
  - constraint used runtime-only behavior
- Keep changes scoped to frontend/const-eval semantics; do not start monomorphization here.

## Validation requirements

- Run `nix run .#tests`
- Run `nix run .#lint`

## Commit style

Use conventional commits for each tiny task, e.g.:

- `feat(const-eval): evaluate generic where constraints`
- `fix(typeck): reject runtime-only operations in constraints`
- `test(const-eval): cover valid and invalid generic constraints`
