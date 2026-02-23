# Language Design

This section discusses the high-level design of the language.
For detailed implementation and syntax specifications, see other documentation.

## General Architecture

The language has two execution targets: a virtual machine for dynamic, interactive execution, and a native binary compiled through LLVM.

The language operates at two levels: compile-time evaluation and runtime execution.
Compile-time evaluation resembles Zig, where values are computed during compilation.
Runtime evaluation is the standard program execution, which runs on either the VM or native platform.

## Type Constructors and Contracts

Cicest treats **two type constructors** as first-class citizens in the type system alongside regular types.
Each constructor wraps a base type and adds a semantic property:

1. **`async T`** — A deferred computation that evaluates to `T` when polled
2. **`runtime T`** — A value that must be evaluated at runtime (cannot be compile-time evaluated)

### Default Semantics

Without explicit constructors:
- Values default to **compile-time compatible** (non-`runtime`)
- Functions are **synchronous** by default (non-`async`)

Binding mutability is controlled separately via `let mut` declarations (see [Binding Mutability](#binding-mutability)).

### Type Construction Rules

These constructors can be freely combined to express complex properties:
- `async T` wraps computation suspension
- `runtime T` restricts evaluation context

Examples:
```rust
const x: i32 = 10;              // Immutable binding, compile-time, synchronous
let y: i32 = 20;                // Mutable binding, compile-time, synchronous
const z: runtime i32 = get();   // Immutable binding, runtime-only, synchronous
const f: async i32 = compute(); // Immutable binding, compile-time, deferred
```

### Contract Propagation

When a function body uses a constructor, that constructor must appear in the function's signature:

```rust
// If body awaits async values, function must be `async fn`:
async fn compute() -> async i32 { 
    let result = some_async_computation();
    result  // returns async i32
}

// If body calls runtime-only code, function must be `runtime fn`:
runtime fn use_io() -> i32 { io::read() }

// Generics work transparently with constructors:
fn<T> identity(x: T) -> T { x }

// Type widening applies in generic contexts:
fn caller() {
    let value: i32 = 42;
    identity(value);  // Works with any T
}
```

## Asynchronous Execution

The `async` contract controls **deferred execution** of computations.

### Async Type Constructor

When an `async fn` is invoked (returning type `R`), the result is a value of type `async R`, not `R` itself.
This represents a suspended computation waiting to be polled.

```rust
async fn compute() -> i32 { /* ... */ }

let future = compute();  // type: async i32, not i32
```

### Polling and Field Access

Async values are executed lazily through a single-threaded polling model:
- **Field Access = Polling**: Reading a field from `async T` automatically polls the computation once, potentially advancing it
- **Lazy Execution**: The computation only runs when explicitly polled
- **Single-threaded**: All async operations coordinate through a polling-based event loop without multithreading

For situations requiring immediate execution, compiler-intrinsic functions provide explicit control

### Contract Propagation

When a function body uses `async` operations, the function itself must be marked `async`:
```rust
async fn wrapper() -> i32 {
    let result = compute();  // result is async i32
    result  // returns async i32
}
```

## Contract Nesting

Nested contracts require careful consideration, as different contracts have fundamentally different semantics.

### Async Nesting: `async (async T)`

Nested async is semantically valid and meaningful: a computation that, when polled, returns another suspended computation.

```rust
async fn delayed_compute() -> async i32 {
    async {
        // inner async computation
        42
    }
}

let x = delayed_compute();  // type: async (async i32)
```

**Construction**: To construct `async (async T)`, wrap the inner `async` value in an outer `async` block or function.

**Resolution**: Polling `async (async T)` yields `async i32`, requiring two separate poll cycles:
```rust
let outer_future = delayed_compute();  // async (async i32)
let inner_future = access_field(outer_future);  // First poll, yields async i32
let result = access_field(inner_future);  // Second poll, yields i32
```

Alternatively, use `__await` to force complete resolution:
```rust
let result = __await(__await(delayed_compute()));  // Force both levels to completion
```

### Runtime and Async Nesting

Since `async` and `runtime` are **type constructors**, nesting is semantically distinct:

- **`async (async T)`** — A suspended computation that yields another suspended computation (valid and meaningful)
- **`runtime (runtime T)`** ≡ `runtime T` — The double `runtime` flattens to a single `runtime`
- **`async (runtime T)`** — A deferred computation yielding a runtime-exclusive value (valid)
- **`runtime (async T)`** — A runtime-only deferred computation (valid)

The compiler **automatically flattens** redundant `runtime (runtime T)` to `runtime T` during type checking.

### Mixed Type Combinations

Binding mutability and type constructors combine freely:

```rust
let x: async i32 = compute();       // mutable binding, async result
const y: runtime i32 = get_runtime_val(); // immutable binding, runtime value
let z: async (runtime i32) = delayed_io();  // mutable binding, deferred runtime op
```

## Binding Mutability

Variable bindings can be immutable (cannot be reassigned) or mutable (can be reassigned). This is controlled by the `let` vs `const` declaration style, similar to JavaScript:

```rust
const x: i32 = 10;       // x cannot be reassigned
let y: i32 = 20;         // y can be reassigned

x = 15;      // ERROR: cannot reassign immutable binding
y = 25;      // OK: can reassign mutable binding
```

That's it. No complicated rules about structural mutability or mutation permissions.

## Memory

The language currently has no memory safety guarantees; in-memory structures are kept as simple as possible without further optimization.

## Generics

The language uses lazy substitution for generics, combined with compile-time validation.

### Lazy Substitution Mechanism

When a generic function or type is instantiated:
1. **Type substitution is recorded** without immediate code generation
2. **Type checking occurs** with the substituted types, including contract validation
3. **Monomorphization happens lazily** - code is only generated when the specialized type is actually used
4. **Specializations are cached** - identical type arguments reuse the same generated code

### Generic Constraints

Generic parameters can have constraints via:
- **where clauses**: `fn<T> foo(x: T) where T: Copyable { ... }`
- **inline bounds**: `fn<T: Copyable> foo(x: T) { ... }`

Constraints are validated during HIR lowering for the abstract generic definition.
At monomorphization time, constraints are re-validated with concrete types.

### Constructor Propagation in Generics

Type constructors (`async`, `runtime`) form part of the type directly and propagate through instantiation:

**Key Rules:**

1. **Constructor Constraints**: A generic parameter can require a specific constructor:
   ```rust
   fn<T> needs_runtime(x: runtime T) { /* T must be instantiated with runtime type */ }
   fn<T> needs_sync(x: T) { /* T must be instantiated without async */ }
   ```

2. **Constructor Preservation**: Constructors don't "magically propagate" to callers:
   ```rust
   fn<T> identity(x: T) -> T { x }
   
   // If instantiated with T = async i32, returns async i32
   // If instantiated with T = runtime i32, returns runtime i32
   // The function itself doesn't become `async fn` or `runtime fn`
   ```

3. **Body Requirements**: If a generic's **implementation** uses constructor-specific operations, the function must explicitly declare them:
   ```rust
   // This function body awaits async values
   async fn<T> resolve(x: async T) -> T { __await(x) }
   
   // This function body uses runtime-only code
   runtime fn<T> io_operation(x: T) -> T { io::do_something(x) }
   ```

4. **Effect Propagation to Caller**: If a function's body uses an effect (async or runtime), the caller must also support that effect:
   ```rust
   // Example with async:
   async fn await_value(x: async i32) -> i32 { __await(x) }
   
   fn caller1() {
       let x: async i32 = compute();
       await_value(x);  // ERROR: non-async fn cannot call async fn body
   }
   
   async fn caller2() {
       let x: async i32 = compute();
       await_value(x);  // OK: async fn can use async operations
   }
   
   // Example with runtime:
   runtime fn caller3() {
       let x = some_runtime_value();
       identity(x);  // OK, caller is runtime
   }
   
   fn caller4() {
       let x = some_runtime_value();  // ERROR: non-runtime cannot use runtime values
   }
   ```

## Ownership and Reference

The language uses a simplified reference model:
- **No stored references**: References cannot be stored in struct fields or live beyond statement boundaries
- **Stack-only references**: All references are stack-allocated and scoped
- **No lifetime annotations**: Reference validity is determined statically by scope analysis
- **No ownership semantics**: Values are implicitly copied or moved based on usage context

This design avoids the complexity of Rust's borrow checker while maintaining memory safety at compile time through scope analysis.
