# Program Execution

## Execution Flow

### Before Execution

The language has mainly three phases before any code goes to the VM or native compiler.
First, the frontend parses the source file and converts them to a stream of parsed tokens.
Second, the frontend constructs a HIR, which is de-sugared. The contract checker and generic checker works in this phase.
Finally, the frontend builds a LIR, which is relatively close to native LLVM IR with some more information.

### During Execution

If the program is running on the VM, the code will end up as a LIR.
The LIR is then interpreted by the VM directly.
If the program, instead, targeting the native platform, the LIR will then be converted to LLVM IR, and therefore compiled by LLVM.

## Generic Resolution and Compile Time Execution

### Lazy Substitution (C++ Template-style)

Generics are resolved using lazy substitution, similar to C++ template instantiation.
When a generic function or type is invoked with concrete type arguments, the compiler:
1. Records the type substitution without immediate evaluation
2. Performs type checking and contract validation with substituted types
3. Generates specialized code only when the type is actually used
4. Reuses specializations across identical type arguments

### Compile Time Evaluation

Compile-time code (marked `const` or defaulting to compile-time compatible) is executed based on HIR.
The execution uses the following strategy:
1. **Lazy Evaluation**: Code is only evaluated when its result is needed
2. **Compilation to LIR**: When execution is required, code is compiled to LIR
3. **VM Execution**: LIR is transferred to the VM and interpreted
4. **Dependency Tracking**: If compile-time code depends on other compile-time code, all callers are tracked
5. **Circular Dependency Detection**: If a circular dependency is found, the compiler throws an error

Both generic resolution and compile-time evaluation follow the same lazy principle—no unnecessary work is performed.

## Contract Validation

The contract is validated during HIR lowering.

Given a keyword generic `keyword<K>`, generally there are the following rules:
1. A type is `keyword<K>` if one of the fields is `keyword` or `K` evaluates to `Contract::Success`.
2. A function is `keyword<K>` if one of the direct parameters or one of the direct statements is `keyword` or `K` evaluates to `Contract::Success`.
3. Branching expressions are always evaluated to `K: Contract::Success` if one of the branch has a contract of `keyword`.

Explicit contract narrowing is accepted.

## Asynchronous Execution

Asynchronous execution in Cicest is based on a **delayed evaluation model**.

### Async Type Construction

When an `async fn` is called with a return type `R`, the result is a value of type `async R` rather than `R` itself.
This value represents a deferred computation that has not yet been executed.

```
async fn fetch_data() -> i32 { /* ... */ }

let future = fetch_data();  // future has type `async i32`
```

### Polling Model

Asynchronous values are executed through **polling**:
- **Field Access Polling**: Accessing a field of an `async T` value polls the computation and returns the inner value
- **Single-threaded**: All polling is coordinated by a single-threaded event loop
- **No multithreading**: The language does not support concurrent execution across multiple threads

### Eager Polling

When eager execution is needed, compiler-intrinsic functions can force polling:

```
__await(future: async T) -> T    // Force immediate polling
__poll_once(future: mut async T) -> Poll<T>  // Poll once and return Poll status
```

These intrinsics allow fine-grained control over when and how async computations execute.
