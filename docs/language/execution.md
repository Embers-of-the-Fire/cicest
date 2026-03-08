# Program Execution

## Execution Flow

### Before Execution

The language has mainly three phases before any code goes to the VM or native compiler.
First, the frontend parses the source file and converts them to a stream of parsed tokens.
Second, the frontend constructs a HIR, which is de-sugared. The contract checker, type annotation checker, and generic checker work in this phase.
Finally, the frontend builds a LIR, which is relatively close to native LLVM IR with some more information.

### During Execution

If the program is running on the VM, the code will end up as a LIR.
The LIR is then interpreted by the VM directly.
If the program, instead, targets the native platform, the LIR will then be converted to LLVM IR, and therefore compiled by LLVM.

## Async-by-Default Execution Model

Cicest treats **all operations as async by default**.
The VM and native runtime operate on a cooperative, single-threaded event loop.
Values may be deferred (`async T`) or immediately resolved (`!async T` / `sync T`).

### Lazy creation, implicit resolution

Creating an `async T` value is a **no-op** — it packages a computation without running it:

```rust
let a = fetch(url1);  // type: async i32 — nothing executed
let b = fetch(url2);  // type: async i32 — another deferred computation
```

**Implicit coercion:** when `async T` is used where `T` is expected (passed as a function argument, returned from a non-`async` function, used in an expression), the compiler automatically inserts a `sync { }` at that site. This means ordinary code looks synchronous even when it involves async operations:

```rust
async fn fetch(url: String) -> i32 { /* ... */ }
fn print_int(x: i32) -> () { /* ... */ }

runtime fn main() -> () {
    let result = fetch("http://example.com");  // async i32, nothing runs
    print_int(result);  // result: async i32, print_int expects i32
                        // compiler inserts: print_int(sync { result })
}
```

The `sync { }` is inserted by the compiler in the compiled output; you do not write it unless you want explicit control.

### Explicit `sync { }` (optional)

`sync { expr: async T }` is still valid and forces resolution at a specific point.
Use it when order of evaluation matters or for documentation clarity:

```rust
let a = fetch(url1);
let b = fetch(url2);
let ra = sync { a };  // force a explicitly before b
let rb = sync { b };
```

### Program entry point

`main` cannot literally return `async ()` — the OS expects a plain `()`.
Writing `fn main() -> async ()` is syntactic sugar that is desugared to `fn main() -> ()`.
The body remains an async context; all implicit coercions inside apply as normal.
The event loop is bootstrapped at the first forced value in `main`'s body.

```rust
// Written:
fn main() -> async () {
    let result = fetch("http://example.com");
    print_int(result);  // implicit sync here
}

// Equivalent after desugaring + coercion:
fn main() -> () {
    let result = fetch("http://example.com");
    print_int(sync { result });
}
```

`__await` and `__poll_once` remain available as lower-level functions for fine-grained polling.

## Generic Resolution and Compile-Time Execution

### Lazy Substitution (C++ Template-style)

Generics are resolved using lazy substitution, similar to C++ template instantiation.
When a generic function or type is invoked with concrete type arguments, the compiler:

1. Records the type substitution without immediate evaluation
2. Performs type checking and contract validation with substituted types
3. Generates specialized code only when the type is actually used
4. Reuses specializations across identical type arguments

### Const Generic Resolution

All functions without a `runtime` marker are compile-time evaluatable.
The compiler's VM is invoked during compilation to run these functions, which enables using them as **compile-time predicates** in `where` clauses.

When a generic function with a predicate constraint is instantiated, the compiler:

1. Substitutes the concrete type argument
2. Evaluates the predicate function(s) in the `where` clause via the VM
3. If any predicate returns `false`, a compile error is emitted at the call site
4. Otherwise, proceeds with monomorphization as normal

```rust
fn is_small(n: i32) -> bool { n < 256 }

// sizeof(T) derives a compile-time integer from the type parameter T — valid in where.
fn<T> small_element() -> T
    where is_small(sizeof(T))
{ /* ... */ }
```

There is no separate `const` parameter syntax — the `runtime` distinction already separates what is and is not usable at compile time.

### Compile-Time Evaluation

Compile-time code (values not qualified `runtime`) is executed based on HIR.
The execution uses the following strategy:

1. **Lazy Evaluation**: Code is only evaluated when its result is needed
2. **Compilation to LIR**: When execution is required, code is compiled to LIR
3. **VM Execution**: LIR is transferred to the VM and interpreted
4. **Dependency Tracking**: If compile-time code depends on other compile-time code, all callers are tracked
5. **Circular Dependency Detection**: If a circular dependency is found, the compiler throws an error

Both generic resolution and compile-time evaluation follow the same lazy principle — no unnecessary work is performed.

## Contract Validation

The contract is validated during HIR lowering.

Given a keyword generic `keyword<K>`, generally there are the following rules:

1. A type is `keyword<K>` if one of the fields is `keyword` or `K` evaluates to `Contract::Success`.
2. A function is `keyword<K>` if one of the direct parameters or one of the direct statements is `keyword` or `K` evaluates to `Contract::Success`.
3. Branching expressions are always evaluated to `K: Contract::Success` if one of the branches has a contract of `keyword`.

Explicit contract narrowing is accepted.

## Type Annotation Validation

Type annotation completeness is enforced during HIR lowering, before type inference runs.
If a function declaration is missing a parameter type or a return type, the compiler reports a hard error at that declaration.
This check does not apply to lambda expressions or local `let` bindings.

## Asynchronous Execution

Asynchronous execution in Cicest is based on a **delayed evaluation model**.

### Async Type Construction

When an `async fn` is called with a return type `R`, the result is a value of type `async R` rather than `R` itself.
This value represents a deferred computation that has not yet been executed.

```rust
async fn fetch_data() -> i32 { /* ... */ }

let future = fetch_data();  // future has type `async i32`
```

### Polling Model

Asynchronous values are executed through **polling**:

- **Field Access Polling**: Accessing a field of an `async T` value polls the computation and returns the inner value
- **Single-threaded**: All polling is coordinated by a single-threaded event loop
- **No multithreading**: The language does not support concurrent execution across multiple threads

### Eager Resolution

When immediate resolution is needed, the `sync { }` block is the standard mechanism:

```rust
let v: i32 = sync { future };     // drives the event loop until the future resolves
```

## Pattern Match Compilation

`match` expressions are compiled during HIR lowering into a decision tree.
Exhaustiveness is checked against the known variants of the matched type — a non-exhaustive match is a compile error.
Or-patterns (`p1 | p2`) are expanded before the decision tree is constructed.
The compiled decision tree feeds directly into LIR without additional passes.
