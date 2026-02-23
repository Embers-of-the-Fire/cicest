# Language Design

This section discusses the high-level design of the language.
For detailed implementation and syntax specifications, see other documentation.

## General Architecture

The language has two execution targets: a virtual machine for dynamic, interactive execution, and a native binary compiled through LLVM.

The language operates at two levels: compile-time evaluation and runtime execution.
Compile-time evaluation resembles Zig, where values are computed during compilation.
Runtime evaluation is the standard program execution, which runs on either the VM or native platform.

## Compile Time and Runtime

By default, all functions, types, and values are compile-time compatible unless explicitly marked.
The language treats compiler contracts as part of the type system.
Three basic contracts are supported: `runtime`, `async`, and `mut`.
These contracts use negative declarations: `runtime` means "not compile-time evaluated", `async` means "not synchronous", and `mut` means "not immutable".
This design choice encourages maximum generality—all items default to compile-time compatibility, with explicit marks for restrictions.

To ensure soundness, cicest attaches contracts to all types and values — for example, `runtime mut i64`.
Contracts are cascading: a function with a parameter of type `runtime T` becomes `runtime T` itself.
Marks must be explicit—if a function internally uses a `runtime`-only function, the function must itself be marked `runtime`.
However, generics do not propagate these marks to their callers, so a `fn<T>` parameterized with `T = runtime i64` does not require the function itself to be `runtime`.

## Mutation

As discussed above, contracts can be explicit markings. The `mut` contract differs slightly: it is determined by mutation operations.
To create a mutable value, a `mut` mark is required.

Consider the following rust code:
```rust
struct Bar {
    baz: i32,
}

fn foo(a1: &mut i32, mut a2: i32) {
    let mut b = 1_i32;
    // ...
}

let mut bar = Bar { baz: 42 };
foo(&mut bar.baz, bar.baz);
```

At minimum, the field `Bar.baz` must be marked `mut`.
The remaining bindings are implicitly marked: `a1 = &mut i32`, `a2 = mut i32`, and `b = mut i32`.

The type checker requires either `Bar.baz` to be marked `mut` or a `&mut i32` to be provided.
Since `bar` is `mut`, `&bar.baz` has type `& (mut Bar).i32`, which simplifies to `&mut i32`.

## Memory

The language currently has no memory safety guarantees; in-memory structures are kept as simple as possible without further optimization.

## Generics

As the language supports much compile-time support, no further generic support is provided.
The language allows to define type variables and to validate them during compile-time.

## Ownership and Reference

There is no ownership design, and storing references is not allowed.
Implementing such would be too complex for this project.
