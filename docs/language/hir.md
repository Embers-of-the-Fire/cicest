# HIR and De-Sugared Design

This document defines the canonical HIR form of Cicest.
This document is HIR-only. LIR representation and backend execution details are out of scope.

## Scope and Position

Frontend semantic phases are:

1. Parse source into AST
2. Lower AST into HIR
3. Validate HIR (name resolution, type resolution, type inference, constraint validation)
4. Execute compile-time evaluation required by HIR validation

After these phases, the compiler has fully validated typed HIR.

---

## HIR Declaration Shape

Each declaration is represented with explicit runtime and compile-time sections.

```text
<declaration-header>
<decl>::runtime {
    <runtime expressions>
}
<decl>::const {
    <compile-time expressions>
}
```

Rules:

- `<decl>::runtime` contains runtime behavior.
- `<decl>::const` contains compile-time behavior used by typing, constraints, and compile-time constants.
- Both sections exist for every declaration; either section may be empty.

---

## Constraint Normalization

Any `where` constraints are lowered into the declaration const section.

Canonical form:

```text
<declaration>
<decl>::constraint {
    <compile-time-bool-expr-1>
    <compile-time-bool-expr-2>
    ...
}
```

Example:

```rust
fn max<T>(a: T, b: T) -> T
    where sizeof(T) == 4, concept(Comparable::<T>)
{ a }
```

HIR form:

```text
fn max<T>(a: T, b: T) -> T { a }
max::constraint {
    sizeof(T) == 4
    concept(Comparable::<T>)
}
```

Constraint semantics:

- Every constraint expression has compile-time boolean type.
- Constraints are conjunctive; all expressions must evaluate to `true`.
- Constraint expressions are evaluated lazily at concrete generic instantiation.

---

## Member Access and Member Call Normalization

Surface forms:

- `t.p`
- `t.a(arg1, arg2)`

Canonical forms:

- `T::p(t)`
- `T::a(t, arg1, arg2)`

`T` is the resolved static receiver type.

HIR carries explicit member-operation nodes until receiver type is known, then rewrites to canonical static-call form.

---

## Contract Normalization

HIR canonicalizes contract syntax into one form:

1. `sync T` becomes `!async T`
2. `const T` becomes `!runtime T`
3. Function-prefix contracts are rewritten into return-type constructors
4. Contract blocks (`async {}`, `sync {}`, `runtime {}`, `const {}`) are explicit HIR nodes
5. `runtime(runtime T)` is flattened to `runtime T`

After normalization, contract semantics are represented only by canonical constructors and explicit HIR nodes.

---

## Async Forcing Representation

Implicit `async T -> T` conversions are explicit in typed HIR.

When expected type is `T` and actual type is `async T`, typed HIR inserts:

```text
sync(expr)
```

---

## Type Inference in HIR Validation

### Inference Scope

- Named function parameter types and return types are explicit in source and are not inferred.
- Type inference applies to local bindings, lambda parameters without annotations, and intermediate expressions.

### Inference Variables

During validation, each unknown type is assigned a fresh type variable.

```text
let x = expr        => x : αx
lambda(y) { ... }   => y : αy
```

### Constraint Generation

Validation generates constraints over HIR expressions.

Constraint classes:

1. **Type equality constraints**
   - `τ1 == τ2`
2. **Call constraints**
   - callee type must match `fn(τ_args...) -> τ_ret`
3. **Constructor/contract constraints**
   - `async`, `runtime`, `!async`, `!runtime` structure must match required positions
4. **Qualifier constraints**
   - qualification (`const` / `runtime`) is solved as part of type constraints
5. **Constraint-block obligations**
   - each `<decl>::constraint` expression must type-check as compile-time boolean

### Generic Calls

At each generic call site:

1. Instantiate generic parameters with fresh variables
2. Unify argument types with instantiated parameter types
3. Derive concrete substitution
4. Type-check and evaluate `::constraint` block under that substitution

This process is lazy and only triggered when the instantiation is required.

### Solving

Constraint solving is deterministic:

1. Unify structural type equations
2. Solve constructor and qualifier equations
3. Reject on conflict
4. Reject if any type variable remains unresolved after solving

There is no fallback qualifier assignment. Unresolved qualification is a hard type error.

### Typed HIR Output

After solving:

- every expression has a concrete type
- every qualifier state is concrete
- implicit async forcing sites are explicit (`sync()`)
- constraint expressions are typed and ready for compile-time evaluation

---

## Compile-Time Evaluation in Type Validation

Type validation may require compile-time values to determine types or qualifiers.

Rules:

- required compile-time expressions are read from `<decl>::const`
- evaluation is on-demand and lazy
- values produced here may participate in type and qualifier decisions
- if compile-time evaluation cannot produce a valid compile-time value, validation fails

This allows type validation and compile-time evaluation to interact without eager whole-program evaluation.

---

## Circular Dependency Detection

Circular dependency detection for compile-time evaluation uses keys:

```text
Key = (declaration-id, concrete-generic-substitution)
```

Evaluation state per key:

- `Unvisited`
- `Active`
- `Done`

Algorithm:

1. Mark key `Active` when evaluation starts
2. If evaluating an `Active` key, report cycle
3. Mark key `Done` when evaluation finishes

Rules:

- any detected cycle is a hard compile error
- diagnostics include full dependency chain with source locations
