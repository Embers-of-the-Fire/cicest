# Language Design

This section discusses the high-level design of the language.
For detailed implementation and syntax specifications, see other documentation.

## General Architecture

The language has two execution targets: a virtual machine for dynamic, interactive execution, and a native binary compiled through LLVM.

The language operates at two levels: compile-time evaluation and runtime execution.
Compile-time evaluation resembles Zig, where values are computed during compilation.
Runtime evaluation is the standard program execution, which runs on either the VM or native platform.

## Type Annotations

Type annotations are **required on all function declarations** — both parameter types and the return type must be explicitly written.
This applies to named `fn` declarations at any level, including generic functions, methods, and `extern` declarations.

Local bindings inside function bodies may omit annotations when the type can be inferred from the right-hand side.

```cicest
fn add(x: i32, y: i32) -> i32 { x + y }            // OK
async fn compute() -> i32 { /* ... */ }      // OK: contracts are part of the type
fn<T> identity(x: T) -> T { x }                    // OK: generic

fn greet(name: String) { name }                    // ERROR: missing return type
fn bad(x) -> i32 { x }                             // ERROR: missing parameter type
```

Type inference still applies freely **within** function bodies.

## Type Constructors and Contracts

Cicest treats **two type constructors** as first-class citizens in the type system alongside regular types.
Each constructor wraps a base type and adds a semantic property:

1. **`async T`** — A deferred computation that evaluates to `T` when polled
2. **`runtime T`** — A value that must be evaluated at runtime (cannot be compile-time evaluated)

Both constructors also have **negative forms** that assert the *absence* of a property:

1. **`!async T`** — A value that is guaranteed **not** deferred; must be synchronously available
2. **`!runtime T`** — A value that is guaranteed **not** runtime-only; must be compile-time available

Negative declarations do not accept type variables — `!async<A>` and `!runtime<R>` are not valid.

### Aliases

The negative forms have **literal keyword aliases** for readability:

| Verbose form | Alias | Meaning |
| ------------- | ------- | ------------ |
| `!runtime T` | `const T` | Compile-time available value |
| `!async T` | `sync T` | Synchronously resolved value |

`const` and `sync` are interchangeable with `!runtime` and `!async` everywhere: in type annotations, function signatures, and contract block keywords.

```cicest
fn<T> pure_fn(x: const T) -> const T { x }   // same as fn<T>(x: !runtime T) -> !runtime T
fn poll(f: sync i32) -> sync i32 { f }       // same as fn(f: !async i32) -> !async i32
```

### Default Semantics

Without explicit constructors:

- Values default to **compile-time compatible** (`!runtime` / `const`)
- All code is **async-compatible by default**

All bindings use `let` and are immutable by default in the functional core (see [Binding Mutability](#binding-mutability)).

### Implicit Coercion

Inside function bodies, when `async T` appears where `T` is expected (as a function argument, in an expression, or as a return value), the compiler **automatically inserts `sync { }`** at that site. This keeps ordinary code concise — async boundaries are implicit in surface syntax:

```cicest
async fn fetch(url: String) -> i32 { /* ... */ }
fn double(x: i32) -> i32 { x * 2 }

fn main() -> () {
    let r = fetch("http://example.com");  // r: async i32 — nothing runs yet
    let d = double(r);                    // compiler inserts: double(sync { r })
}
```

Explicit `sync { }` is still valid and can be used for documentation, disambiguation, or controlling evaluation order.

The insertion is a frontend-only convenience: lowered representations make forcing explicit.
Typed HIR records each inserted force, and LIR uses explicit sync operations rather than implicit coercion.

### Contract Blocks

A `<keyword> { }` block performs an **explicit type-level conversion**.

| Block | Body type | Result type | What happens |
|---|---|---|---|
| `async { expr }` | `T` | `async T` | Lifts `T` into a deferred computation |
| `sync { expr }` | `async T` | `T` | Explicitly forces resolution |
| `runtime { expr }` | `T` | `runtime T` | Marks the result as runtime-only |
| `const { expr }` | `T` | `const T` (`!runtime T`) | Asserts compile-time availability; evaluates via the VM |

`sync { }` and `const { }` are the canonical aliases for `!async { }` and `!runtime { }` blocks.

```cicest
let deferred: async i32 = async { 42 };        // explicit lift into async i32
let resolved: i32       = sync { deferred };   // explicit force (same as implicit coercion)
let ct: const i32       = const { factorial(10) };  // explicit compile-time assertion
```

`__await` remains available as a lower-level function for fine-grained polling control.

### Type Construction Rules

Constructors and their aliases combine freely:

```cicest
let x: i32 = 10;                    // !runtime, !async (plain value, compile-time available)
let y: i32 = 20;                    // same: !runtime by default
let z: runtime i32 = get();         // runtime
let f: async i32 = async { 1 };     // !runtime, async
let g: sync i32 = 0;                // same as: i32 that is !async — i.e. plain i32
```

### Negative Declarations in Practice

**`!async` / `sync` — field accessor types:**

Each named struct field generates an accessor function.
For `struct T { a: A }`, the generated accessor has type `(!async T) -> A`.
Because `p.a` is sugar for `T::a(p)`, the type of `p` must satisfy `!async` — the same rule as any function call.

If the receiver is `async T`, the compiler automatically lifts the accessor to type `(async T) -> async A`, performing one poll step:

```cicest
struct Point { x: f64, y: f64 }
// generates: Point::x : (!async Point) -> f64
//            Point::y : (!async Point) -> f64

let p: Point = Point { x: 1.0, y: 2.0 };
let ax = p.x;                    // Point::x(p) — p: !async Point → ax: f64

let ap: async Point = async { compute_point() };
let ay = ap.x;                   // Point::x(ap) — lifted to (async Point) → ay: async f64
let z  = sync { ap }.x;          // sync { ap }: !async Point → z: f64
```

The two accessor forms for `T { a: A }`:

- `T::a : (!async T) -> A`
- `T::a : (async T) -> async A` (compiler-generated lifting)

**`!runtime` / `const` — compile-time type intrinsics:**

Functions that query type information (`sizeof`, `alignof`, `typeof`) require their type argument to be `!runtime` (`const`).
Since types are always compile-time values, this is naturally satisfied for all plain types.

```cicest
let s: i32 = sizeof(i32);         // OK: i32 is !runtime
let a: i32 = alignof(Point);      // OK

// sizeof(runtime i32);           // ERROR: runtime i32 violates !runtime
```

### Function Contract Prefix

A contract marker before `fn` is **syntactic sugar** that wraps the return type in that contract constructor:

```
<contract> fn foo(...) -> T   ===   fn foo(...) -> <contract> T
```

This applies to both `async` and `runtime`:

```cicest
// These pairs are exactly equivalent:
async fn   compute() -> i32       // === fn compute() -> async i32
runtime fn use_io() -> ()         // === fn use_io() -> runtime ()

// Writing the contract in both places wraps twice:
async fn   fetch() -> async i32   // === fn fetch() -> async (async i32)
runtime fn io()   -> runtime ()   // === fn io()   -> runtime (runtime ())
                                  //              flattens to runtime ()  (idempotent)
```

Because `runtime` is idempotent (`runtime (runtime T)` ≡ `runtime T`), `runtime fn -> runtime T` and `runtime fn -> T` produce the same type.
Because `async` nesting is **not** idempotent, `async fn -> async T` produces `async (async T)` — a genuinely doubly-deferred computation requiring two resolution steps.

The convention is to write the contract only **once** to avoid confusion:

- `async fn foo() -> i32` — preferred (prefix provides the wrapper)
- `fn foo() -> async i32` — equivalent, also valid

## Asynchronous Execution

The `async` contract controls **deferred execution** of computations.

### Async Type Constructor

When an `async fn` is invoked, the result is `async R` (the prefix wraps the return type).
This value represents a suspended computation waiting to be polled.

```cicest
async fn compute() -> i32 { /* ... */ }
// desugars to: fn compute() -> async i32

let future = compute();  // type: async i32
```

## Contract Nesting

Nested contracts require careful consideration, as different contracts have fundamentally different semantics.

### Async Nesting: `async (async T)`

Nested async is semantically valid and meaningful: a computation that, when polled, returns another suspended computation.

```cicest
// No prefix: return type explicitly states the nesting
fn delayed_compute() -> async (async i32) {
    async {
        42  // inner async computation
    }
}

let x = delayed_compute();  // type: async (async i32)
```

**Resolution**: Polling `async (async T)` yields `async i32`, requiring two separate poll cycles:

```cicest
let outer_future = delayed_compute();          // async (async i32)
let inner_future = outer_future.poll();        // First poll, yields async i32
let result       = inner_future.poll();        // Second poll, yields i32
```

Alternatively, use `__await` to force complete resolution:

```cicest
let result = __await(__await(delayed_compute()));
```

### Runtime and Async Nesting

Since `async` and `runtime` are **type constructors**, nesting is semantically distinct:

- **`async (async T)`** — A suspended computation that yields another suspended computation (valid and meaningful)
- **`runtime (runtime T)`** ≡ `runtime T` — The double `runtime` flattens to a single `runtime`
- **`async (runtime T)`** — A deferred computation yielding a runtime-exclusive value (valid)
- **`runtime (async T)`** — A runtime-only deferred computation (valid)

The compiler **automatically flattens** redundant `runtime (runtime T)` to `runtime T` during type checking.

### Constructor Introspection and Construction Intrinsics

The compiler provides type-level intrinsics for constructor inspection and construction:

- `is_async(Type) -> bool`
- `is_runtime(Type) -> bool`
- `make_async(Type) -> Type`
- `make_runtime(Type) -> Type`

These intrinsics are compile-time operations and are primarily used in generic constraints.
`make_async(T)` and `make_runtime(T)` preserve constructor semantics (`runtime` remains idempotent, `async` nesting remains meaningful).

### Mixed Type Combinations

```cicest
let x: async i32           = compute();    // async result
let y: runtime i32         = get_val();    // runtime value
let z: async (runtime i32) = delayed_io(); // deferred runtime operation
```

## Algebraic Data Types

Cicest supports algebraic data types (ADTs) through `enum` (sum types) and `struct` (product types).

### Sum Types

`enum` declares a **sum type** — a value that is exactly one of several named variants.
Variants may carry no data, named fields, or positional fields:

```cicest
enum Shape {
    | Circle { radius: f64 }
    | Rectangle { width: f64, height: f64 }
    | Point
}

enum Result<T, E> {
    | Ok(T)
    | Err(E)
}

enum List<T> {
    | Nil
    | Cons(T, List<T>)
}
```

### Product Types

`struct` declares a **product type** — a value that bundles multiple named or positional fields.

Named fields are **accessor functions**, not memory offsets.
For a `struct T { a: A, b: B }`, the compiler generates:

```
T::a : (!async T) -> A
T::b : (!async T) -> B
```

The dot-access syntax `expr.field` is syntactic sugar for calling the accessor:
`p.x` desugars to `Point::x(p)`.

This makes field access a first-class, type-checkable operation — the accessor's type signature fully determines what contracts apply (see [Negative Declarations in Practice](#negative-declarations-in-practice)).

```cicest
struct Point { x: f64, y: f64 }  // generates: Point::x, Point::y

struct Pair<A, B>(A, B);          // positional — no named accessors
```

### Recursive Types

Sum types may be recursive to describe inductive data structures:

```cicest
enum Tree<T> {
    | Leaf
    | Node { value: T, left: Tree<T>, right: Tree<T> }
}
```

## Pattern Matching

Pattern matching deconstructs values by shape. The primary mechanism is the `match` expression.

### Match Expression

A `match` expression evaluates to the body of the first matching arm.
Patterns are **exhaustiveness-checked** at compile time — all possible variants must be covered, or a wildcard `_` arm must be present.

```cicest
fn area(s: Shape) -> f64 {
    match s {
        Circle { radius: r }              => 3.14159 * r * r,
        Rectangle { width: w, height: h } => w * h,
        Point                             => 0.0,
    }
}
```

```cicest
fn head<T>(list: List<T>) -> runtime T {
    match list {
        Cons(x, _) => x,
        Nil        => runtime_panic("empty list"),
    }
}
```

### Nested Patterns

Patterns may be arbitrarily nested to match deep structure:

```cicest
match result {
    Ok(Cons(x, _)) => x,
    Ok(Nil)        => 0,
    Err(_)         => -1,
}
```

### Pattern Bindings

A pattern variable binds the matched value to a name in the arm body:

- `_` — wildcard, discards the value
- `name` — binds the value to `name`
- `name @ pattern` — binds the whole value to `name` and also matches the inner pattern

## Lambda Expressions

Lambda expressions create anonymous functions inline using the `lambda` keyword.
The syntax is block-based, consistent with the rest of the language:

```cicest
lambda(x: i32) { x + 1 }
lambda(x: i32, y: i32) { x + y }
lambda(s: Shape) {
    match s {
        Circle { radius: r } => 3.14159 * r * r,
        _                    => 0.0,
    }
}
```

Type annotations on lambda parameters are optional when the type can be inferred from context:

```cicest
let double = lambda(x) { x * 2 };    // type inferred as fn(i32) -> i32
```

Lambdas may capture immutable bindings from their enclosing scope:

```cicest
fn make_adder(n: i32) -> fn(i32) -> i32 {
    lambda(x) { x + n }   // n is captured from the enclosing scope
}
```

## Binding Mutability

All bindings use `let`. In the functional core every `let` binding is immutable — a name is bound once and cannot be reassigned. This is consistent with value semantics and compile-time evaluation.

```cicest
let x: i32 = 10;
let y: runtime i32 = get_val();

// x = 15;   ERROR: cannot reassign an immutable binding
```

## Memory

The language has no C-ABI-style native value types.
Structs and enums are **abstract values** — the compiler is free to choose any internal representation, and programs cannot directly observe memory layout (no pointer arithmetic into fields, no `offsetof`).
`sizeof` and `alignof` are available as type intrinsics but measure the abstract storage size, not a C-compatible layout.

## Generics

The language uses lazy substitution for generics, combined with compile-time validation.

### Lazy Substitution Mechanism

When a generic function or type is instantiated:

1. **Type substitution is recorded** without immediate code generation
2. **Type checking occurs** with the substituted types, including contract validation
3. **Monomorphization happens lazily** — code is only generated when the specialized type is actually used
4. **Specializations are cached** — identical type arguments reuse the same generated code

### Generic Constraints

Generic parameters can have constraints via:

- **type-level `where` predicates**: `fn<T> foo(x: T) -> T where sizeof(T) == 4 { ... }`
- **concept predicates** via intrinsic expression: `fn<T> foo(x: T) -> T where concept(Foo::<T>) { ... }`

Constraints are validated during HIR lowering for the abstract generic definition.
At monomorphization time, constraints are re-validated with concrete types.

## Concepts and With Blocks

Cicest supports a C++-style concept surface, expressed through two declarations:

1. **`concept`** — declares function-signature requirements
2. **`with`** — attaches function definitions to a target type

Concept checking in generic constraints is expressed with a compiler intrinsic expression in `where` clauses:

```cicest
concept Comparable<T> {
    fn compare(lhs: T, rhs: T) -> i32;
}

fn<T> max(a: T, b: T) -> T
    where concept(Comparable::<T>)
{ /* ... */ }
```

`with` blocks define associated functions for a type:

```cicest
with Point {
    fn length(self: Point) -> f64 { /* ... */ }
}
```

Blanket impls over all types are forbidden:

- `with<T> T { ... }` is not allowed.
- A bare type-parameter target is rejected even if the block has a `where` clause.

This keeps concept resolution coherent and avoids overlap/ambiguity at the interface level.

For non-static functions that take `self`, the call model is defined by desugaring:

- A method definition is treated as a plain function whose first parameter is the receiver value.
- `value.func(args...)` desugars to `func(value, args...)`.

This keeps method syntax as surface sugar while preserving the core function-based model.

### Types as Values

Cicest treats **types as first-class compile-time values**, inspired by Zig's `comptime`.
A type parameter `T` in `<T>` is not just a placeholder — it is a compile-time value of kind `Type` that can be passed to functions and inspected by compiler-intrinsic operations.

This enables functions that compute over types:

```cicest
let s = sizeof(i32);          // sizeof takes a Type value, returns i32
let a = alignof(Point);       // same pattern
```

The `where` clause exploits this: it accepts **compile-time boolean expressions**.
Because type parameters are compile-time values, any non-`runtime` function that accepts a type (or a value derived from a type, like `sizeof(T)`) can appear in a `where` clause.

Normatively, each `where` predicate is evaluated to `bool` at compile time by the HIR interpreter running on the VM.
All predicates must evaluate to `true`, or substitution fails.

```cicest
fn is_power_of_two(n: i32) -> bool { n > 0 && (n & (n - 1)) == 0 }

fn<T> aligned_element() -> T
    where is_power_of_two(sizeof(T))   // sizeof(T) derives a value from the type T
{ /* ... */ }

fn<T, U> same_size() -> bool
    where sizeof(T) == sizeof(U)
{ sizeof(T) == sizeof(U) }
```

At each call site the compiler substitutes the concrete type for `T`, evaluates the `where` expressions through the VM, and emits a compile error if any evaluate to `false`.

```cicest
aligned_element::<i32>();    // sizeof(i32) == 4 → is_power_of_two(4) == true ✓
aligned_element::<u24>();    // sizeof(u24) == 3 → is_power_of_two(3) == false ✗ COMPILE ERROR
```

**What is not allowed in `where`:** calling `runtime` functions or reading `runtime` values. Every predicate must be fully compile-time evaluatable.

```cicest
fn foo(n: i32) -> i32
    where runtime_check(n)  // ERROR: depends on runtime evaluation
{ n }
```

### Constructor Propagation in Generics

Type constructors (`async`, `runtime`) form part of the type directly and propagate through instantiation:

1. **Constructor Constraints**: A generic parameter can require a specific constructor:

   ```cicest
   fn<T> needs_runtime(x: runtime T) -> () { /* T must be instantiated with runtime type */ }
   fn<T> needs_sync(x: T) -> () { /* T must be instantiated without async */ }
   ```

2. **Constructor Preservation**: Constructors don't "magically propagate" to callers:

   ```cicest
   fn<T> identity(x: T) -> T { x }
   // If instantiated with T = async i32, returns async i32
   // The function itself doesn't become async fn or runtime fn
   ```

3. **Body Requirements**: If a generic's implementation uses constructor-specific operations, the function must declare the appropriate prefix:

   ```cicest
   async fn<T> resolve(x: async T) -> T { __await(x) }
   // desugars to: fn<T> resolve(x: async T) -> async T  (prefix wraps return)
   // BUT: __await(x) returns T, not async T — so the return type must match

   // Correct way to express "takes async T, returns T" without prefix:
   fn<T> resolve(x: async T) -> T { __await(x) }
   ```

4. **Effect Propagation to Caller**: Calling an `async fn` returns `async T`; the caller receives a future, which is then resolved by implicit coercion or explicit `sync { }`:

   ```cicest
   async fn compute() -> i32 { /* ... */ }
   // compute() returns async i32

   fn use_it() -> () {
       let x = compute();   // x: async i32
       print_int(x);        // implicit sync: print_int(sync { x })
   }
   ```

### Const Generics

Because all functions without a `runtime` marker are compile-time evaluatable, and because types are first-class compile-time values, Cicest achieves const generics without a separate `const N` parameter syntax.

The mechanism is entirely through **type-level `where` predicates**:

- Pass a type as `<T>` — it is already a compile-time value
- Use `sizeof(T)`, `alignof(T)`, or any compile-time function on `T` inside `where`
- The compiler evaluates these at each call site via the VM

There is no special `const fn` keyword — the `runtime` marker is the only distinction.

```cicest
fn is_power_of_two(n: i32) -> bool { n > 0 && (n & (n - 1)) == 0 }
fn is_aligned(size: i32, align: i32) -> bool { size % align == 0 }

fn<T> safe_cast_target() -> T
    where is_power_of_two(sizeof(T)), is_aligned(sizeof(T), alignof(T))
{ /* ... */ }
```

## Ownership and Reference

References follow a C++-style model with a single built-in guarantee:

- **Non-null reference guarantee**: a reference value is never null
- **No lifetime safety guarantee**: dangling or otherwise invalid references are possible if the program constructs them
- **No ownership system**: values are implicitly copied or moved by usage context
- **No borrow checker**: aliasing and validity are programmer-managed

This keeps reference semantics simple and explicit, while leaving temporal safety as a language-level non-goal.
