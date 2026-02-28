# Language Examples

This document walks through concrete code examples that demonstrate how the language features work in practice.
Each example is accompanied by an explanation of the underlying mechanisms.

---

## 1. Basic Functions and Type Annotations

All function declarations require explicit type annotations on every parameter and the return type.

```rust
fn add(x: i32, y: i32) -> i32 {
    x + y
}

fn greet(name: String) -> String {
    "Hello, " + name
}
```

**Why required annotations?**
Type inference still operates freely inside function bodies, but the signature acts as an explicit contract between the caller and the implementation.
This lets the compiler type-check each function in isolation, without needing to trace call sites, and makes generic constraint checking tractable.

Local bindings inside a body can omit annotations:

```rust
fn hypotenuse(a: f64, b: f64) -> f64 {
    let aa = a * a;   // type inferred as f64
    let bb = b * b;
    sqrt(aa + bb)
}
```

---

## 2. Algebraic Data Types

### Sum Types

`enum` declares a sum type — a value that is exactly one of its variants.
Each variant may carry different data.

```rust
enum Shape {
    | Circle { radius: f64 }
    | Rectangle { width: f64, height: f64 }
    | Point
}

enum List<T> {
    | Nil
    | Cons(T, List<T>)
}
```

**How it works:**
A `Shape` value is an abstract tagged union — the representation is chosen by the compiler and is not directly observable by the program.
Generic `List<T>` is monomorphized lazily: `List<i32>` and `List<String>` are distinct types, generated only when actually used.

### Product Types

`struct` declares a product type — all fields are always present simultaneously.
Named fields generate **accessor functions**: `struct Point { x: f64, y: f64 }` generates `Point::x : (!async Point) -> f64` and `Point::y`.
Dot-access `p.x` is sugar for calling `Point::x(p)`.

```rust
struct Point { x: f64, y: f64 }
// Point::x : (!async Point) -> f64
// Point::y : (!async Point) -> f64

struct Pair<A, B>(A, B);
// positional — no named accessors; use pattern matching
```

---

## 3. Pattern Matching

`match` deconstructs a value by its shape.
Every case must be covered — the compiler enforces exhaustiveness.

```rust
fn area(s: Shape) -> f64 {
    match s {
        Circle { radius: r }              => 3.14159265 * r * r,
        Rectangle { width: w, height: h } => w * h,
        Point                             => 0.0,
    }
}
```

**How exhaustiveness works:**
During HIR lowering the compiler builds a decision tree over the variants of `Shape`.
If any variant has no matching arm and no wildcard `_` arm is present, a compile error is emitted before code generation begins.

Patterns compose: you can match nested constructors in a single arm.

```rust
fn sum_list(list: List<i32>) -> i32 {
    match list {
        Nil           => 0,
        Cons(x, rest) => x + sum_list(rest),
    }
}
```

The `_` wildcard and `name @ pattern` (as-pattern) are also available:

```rust
fn first_or_default(list: List<i32>, default: i32) -> i32 {
    match list {
        Cons(x, _) => x,
        Nil        => default,
    }
}
```

---

## 4. Lambdas and Higher-Order Functions

`lambda` creates an anonymous function inline.
The language is block-based, so the body is always a `{ }` block.

```rust
fn apply(f: fn(i32) -> i32, x: i32) -> i32 {
    f(x)
}

fn main() -> () {
    let result = apply(lambda(x) { x * 2 }, 5);  // result = 10
}
```

Type annotations on lambda parameters are optional when the type can be inferred:

```rust
fn map_list(list: List<i32>, f: fn(i32) -> i32) -> List<i32> {
    match list {
        Nil           => Nil,
        Cons(x, rest) => Cons(f(x), map_list(rest, f)),
    }
}

fn double_all(list: List<i32>) -> List<i32> {
    map_list(list, lambda(x) { x * 2 })
}
```

**Captures:** A lambda captures immutable bindings from its enclosing scope.

```rust
fn make_adder(n: i32) -> fn(i32) -> i32 {
    lambda(x) { x + n }   // `n` is captured by value
}

fn main() -> () {
    let add5 = make_adder(5);
    let result = add5(3);   // 8
}
```

---

## 5. Generics

Generic functions use type variables in angle brackets.
Constraints are expressed with inline bounds or `where` clauses.

```rust
fn<T> identity(x: T) -> T { x }

fn<T: Printable> print_twice(x: T) -> () {
    print(x);
    print(x);
}

fn<A, B> swap(pair: Pair<A, B>) -> Pair<B, A> {
    match pair {
        Pair(a, b) => Pair(b, a),
    }
}
```

**Lazy monomorphization:**
The compiler records each distinct set of type arguments used at call sites.
Code for `identity::<i32>` and `identity::<String>` is generated separately, only when those instantiations are actually reached.
Identical instantiations are deduplicated.

---

## 6. Compile-Time Evaluation and Const Generics

All functions that do not use `runtime`-qualified values are **compile-time evaluatable**.
The compiler runs these through the built-in VM during compilation.

```rust
fn factorial(n: i32) -> i32 {
    if n <= 1 { 1 } else { n * factorial(n - 1) }
}

let FACT_10: i32 = factorial(10);   // evaluated entirely at compile time
```

**How it works:**
`factorial` has no `runtime` marker, so the compiler can execute it.
`FACT_10` is a compile-time constant; the compiler compiles `factorial` to LIR, runs it in the VM, and folds the result (`3628800`) directly into the binary.

### Const Generics via Type-Level `where` Predicates

Cicest treats **types as first-class compile-time values** (like Zig's `comptime`).
A type parameter `<T>` is a compile-time value of kind `Type` — not just a placeholder.
Compiler intrinsics like `sizeof(T)` and `alignof(T)` derive compile-time integer values from it.

Because of this, `where` clauses can express rich type-level constraints using ordinary non-`runtime` functions:

```rust
fn is_power_of_two(n: i32) -> bool {
    n > 0 && (n & (n - 1)) == 0
}

// sizeof(T) produces a compile-time i32 from the type parameter T.
// The where predicate is evaluated by the VM at each call site.
fn<T> aligned_element() -> T
    where is_power_of_two(sizeof(T))
{ /* ... */ }
```

At each call site the compiler substitutes the concrete type, evaluates the predicate, and emits a compile error if it returns `false`:

```rust
aligned_element::<i32>();    // sizeof(i32) == 4, is_power_of_two(4) == true  ✓
aligned_element::<u24>();    // sizeof(u24) == 3, is_power_of_two(3) == false ✗ COMPILE ERROR
```

Multiple predicates can be combined:

```rust
fn<T, U> reinterpret(x: T) -> U
    where sizeof(T) == sizeof(U), alignof(T) == alignof(U)
{ /* ... */ }
```

**What is NOT allowed in `where`:**
The `where` clause only accepts expressions over *type parameters*, not over value parameters.
Referencing a function's value argument is a compile error because `where` is purely type-level:

```rust
// ERROR: n is a value parameter, not a type
fn validate(n: i32) -> i32 where is_positive(n) { n }
```

---

## 7. Negative Declarations: `!async` and `!runtime`

### `!async` — Field Accessor Types

Named struct fields are **accessor functions**, not memory offsets.
For `struct T { a: A }`, the compiler generates:

```
T::a : (!async T) -> A
```

`p.a` is syntactic sugar for `T::a(p)`. Because `T::a` expects a `!async T`, the type of `p` must satisfy `!async` — exactly as for any other function call.

```rust
struct Config { timeout: i32, retries: i32 }
// Config::timeout : (!async Config) -> i32

fn use_sync(cfg: Config) -> i32 {
    cfg.timeout        // Config::timeout(cfg) — cfg: !async Config → i32
}
```

When the receiver is `async T`, the compiler automatically generates a lifted form:

```
T::a : (async T) -> async A
```

This means calling the accessor on an `async` receiver is still valid — it just returns `async A`.
With implicit coercion, this resolves naturally at the next usage site:

```rust
async fn load_config() -> Config { /* ... */ }

fn use_async() -> () {
    let ac: async Config = load_config();
    let t = ac.timeout;    // Config::timeout(ac) — lifted: async Config → async i32
    print_int(t);          // t: async i32, print_int expects i32 → implicit sync
}
```

To get the field directly (without intermediate `async`), resolve the receiver first using an explicit annotation or `sync { }`:

```rust
fn use_async_explicit() -> () {
    let ac: async Config = load_config();
    let cfg: Config = sync { ac };   // explicit force → !async Config
    let t: i32 = cfg.timeout;        // Config::timeout(cfg): i32 directly
}
```

**Why function-style accessors?**
Treating field access as function calls unifies the type-checking model — the `!async` / `async` lifting rule is just the normal rule for overloaded function application, not a special case for field access.
It also means accessor functions are first-class values that can be passed to higher-order functions.

### `!runtime` — Compile-Time Type Intrinsics

The `!runtime` marker asserts that a value or type is available at **compile time**.
All plain types satisfy `!runtime` by default. A `runtime`-qualified type does not — its value is only known at runtime and cannot be used in compile-time expressions.

Compiler intrinsics that query type information (`sizeof`, `alignof`, `typeof`) require their argument to be `!runtime`:

```rust
let s = sizeof(i32);        // i32 is !runtime → s: i32, evaluated at compile time
let a = alignof(Point);     // Point is !runtime → a: i32, evaluated at compile time
```

Attempting to query a `runtime` type is a compile error:

```rust
runtime fn bad() -> () {
    let t: runtime i32 = io::read_int();
    let s = sizeof(t);    // ERROR: typeof(t) is runtime i32, violates !runtime
}
```

**Why this matters for generic code:**
Type intrinsics in `where` predicates work because all type parameters `<T>` are `!runtime` (they are compile-time type values).
`sizeof(T)` is always valid in a `where` clause; `sizeof(runtime T)` is not.

---

## 8. Contract Blocks

A `<keyword> { }` block is an **explicit type-conversion expression**.
In common usage, the compiler handles `async T` → `T` coercion implicitly at usage sites — contract blocks are used when explicit control is desired.

| Block | Body type | Result type | Operation |
|-------|-----------|-------------|-----------|
| `async { expr }` | `T` | `async T` | Lifts into a deferred computation |
| `sync { expr }` | `async T` | `T` | Explicitly forces resolution |
| `runtime { expr }` | `T` | `runtime T` | Marks result as runtime-only |
| `const { expr }` | `T` | `const T` | Asserts/forces compile-time evaluation |

`sync` is an alias for `!async`; `const` (block form) is an alias for `!runtime`.

```rust
// async { } — lift a plain value into a deferred computation
let future: async i32 = async { 42 };

// sync { } — explicit force (the compiler would insert this implicitly at a usage site)
let value: i32 = sync { future };

// runtime { } — mark a value as runtime-only
let rt: runtime i32 = runtime { read_int() };

// const { } — force compile-time evaluation
let FACT: const i32 = const { factorial(10) };
```

---

## 9. Asynchronous Computation (`async T`)

Calling an `async fn` does not execute it immediately.
The result has type `async R` — a **suspended computation** that runs only when its value is needed.
The execution model is **async-by-default**: creating a future is a no-op; the event loop only runs when a value is actually demanded.

### Implicit coercion: no explicit await needed

The compiler automatically inserts `sync { }` whenever `async T` is used where `T` is expected.
Ordinary code looks synchronous even when it involves async operations:

```rust
async fn fetch_data(url: String) -> i32 {
    // ... deferred HTTP work ...
}

fn main() -> async () {
    let result = fetch_data("http://example.com");
    // result: async i32 — nothing has run yet

    print_int(result);
    // print_int expects i32; compiler inserts: print_int(sync { result })
    // event loop runs here, drives fetch_data to completion
}
```

**How it works:**
`fetch_data` returns a state machine (`async i32`) without starting any work.
When `result` is passed to `print_int`, the compiler inserts an implicit `sync { }`, which drives the event loop until the computation resolves.

### Composing async computations

Async functions compose naturally — each call produces a deferred value, resolved only when used:

```rust
async fn fetch_user(id: i32) -> String { /* ... */ }
async fn fetch_score(user: String) -> i32 { /* ... */ }

async fn pipeline(id: i32) -> i32 {
    let user  = fetch_user(id);    // async String — not yet run
    let score = fetch_score(user); // user auto-synced, fetch_score returns async i32
    score                          // returned as async i32 to the caller
}
```

### Explicit ordering with `sync { }`

Use explicit `sync { }` when you need to control exactly when a value is forced:

```rust
async fn pipeline_ordered(id: i32) -> i32 {
    let user  = sync { fetch_user(id) };    // force user first, then...
    let score = sync { fetch_score(user) }; // ...force score
    async { score }
}
```

### Nested Async

`async (async T)` is a valid type — a computation that, when polled, yields another computation.
Both layers must be resolved to get the inner value:

```rust
// fn outer() -> async (async i32)
// prefix-free: return type explicitly states the nesting
fn outer() -> async (async i32) {
    fetch_data("http://example.com")   // fetch_data returns async i32; outer wraps it in async
}

fn main() -> async () {
    let inner: async i32 = outer();    // outer() auto-synced (outer layer resolved)
    print_int(inner);                  // inner auto-synced (inner layer resolved)
}
```

---

## 10. Runtime Operations (`runtime T`)

The `runtime` marker means a value or function **cannot** be evaluated at compile time.
It is the opt-out from the default compile-time-eligible world.

```rust
runtime fn read_line() -> String { io::read_line() }
runtime fn print_line(s: runtime String) -> () { io::print(s) }

runtime fn greet() -> () {
    let name = read_line();        // name: runtime String
    let msg  = "Hello, " + name;   // msg: runtime String (tainted by runtime input)
    print_line(msg);
}
```

**How it works:**
`read_line` performs I/O — it must execute at runtime.
The `runtime` marker propagates: anything computed from a `runtime` value also becomes `runtime`.
The compiler rejects any attempt to use a `runtime`-qualified value in a compile-time context (e.g., as a generic predicate or as the value of a `const { }` block).

**`runtime (runtime T)` flattens to `runtime T`:**
The `runtime` constructor is idempotent.
The compiler automatically collapses redundant wrapping during type checking.

---

## 11. Combining `async` and `runtime`

`async` and `runtime` are independent type constructors that compose freely.

```rust
// A runtime-triggered async operation (e.g., network I/O):
runtime fn http_get(url: runtime String) -> async i32 {
    io::http::get(url)
}

runtime fn main() -> () {
    let url    = read_line();         // runtime String
    let result = http_get(url);       // runtime (async i32); auto-synced when used
    print_int(result);                // result: runtime i32 (async layer resolved implicitly)
}
```

**Reading the types:**

- `runtime String` — a string only known at runtime
- `runtime (async i32)` — a deferred computation that is itself only accessible at runtime (depends on runtime input to even start)
- `result` passed to `print_int` (which expects `i32`) — implicit `sync { }` resolves the `async` layer; the `runtime` taint remains

This layering lets the type system precisely describe *when* a computation can start and *when* its result is available.
