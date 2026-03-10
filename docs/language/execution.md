# Program Execution

## Execution Flow

Cicest currently uses a staged pipeline:

1. Parse source into AST
2. Lower AST into HIR and validate declarations/contracts
3. Lower HIR into LIR
4. Execute with VM or lower to LLVM IR for native compilation

## Const Evaluation by Default

The language model is **compile-time first**.
Values and expressions are treated as compile-time evaluable unless explicitly marked `runtime`.

- `const` is the readable alias for `!runtime`
- `runtime T` means the value is only available at runtime

## Runtime/Const Contracts

Contract validation is performed during HIR lowering and type validation.

Core rules:

1. `runtime` is a type constructor and participates in type checking.
2. `runtime T` is distinct from `T`.
3. `runtime (runtime T)` is normalized to `runtime T`.
4. `const`/`!runtime` requirements are checked before runtime lowering.

## Generic Predicate Evaluation

`where` predicates are checked during generic instantiation.
The compiler:

1. Substitutes concrete type arguments
2. Evaluates predicate expressions in compiler-time context
3. Rejects instantiation when any predicate is false or invalid

`decl(TypeExpr)` is used to assert type-expression validity in constraints:

```cicest
fn use_vec<T>(value: T) -> T
    where decl(Vec<T>)
{
    value
}
```

## Compile-Time Execution Strategy

Compile-time execution uses lazy demand:

1. Delay evaluation until value is needed
2. Lower to LIR only when required
3. Execute LIR in compiler VM
4. Track dependencies for diagnostics and invalidation
5. Report circular compile-time dependencies as errors

## Runtime Execution

When targeting runtime execution:

- LIR is interpreted directly by the VM, or
- LIR is lowered to LLVM IR for native compilation

This keeps one semantic pipeline while supporting both fast iteration and native codegen.
