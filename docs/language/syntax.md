# Cicest Syntax (Simplified)

This document describes the currently supported surface syntax.

## Removed Features

The following are intentionally **not supported**:

- Pattern matching (`match`) and pattern-based destructuring
- Rich enum payloads (enum variants with fields or tuple data)
- Lambda expressions (`lambda`)
- OOP-style trait/method system (`concept`, `with`, `self` receiver methods, method-call syntax)
- Type aliases (`type Name = ...`)
- Unnamed tuple types, tuple expressions, and tuple structs (`(A, B)`, `(a, b)`, `struct S(T)`)

## Declarations

### Function

```cicest
fn name<T>(arg1: Type, arg2: Type) -> Type
    where decl(Foo<T>), sizeof(T) == 4
{
    // statements
}
```

### Extern Function

```cicest
extern fn puts(v: i32) -> i32;
```

### Struct

Supported:

```cicest
struct Marker;
struct Point { x: i32, y: i32 }
```

Not supported:

```text
struct Pair(i32, i32);    // tuple struct
```

### Enum (C++ enum class style)

Only unit-like variants are supported:

```cicest
enum Color {
    Red,
    Green,
    Blue,
}
```

Variant payloads are not supported.

## Statements

### Let Binding

```cicest
let x: i32 = 1;
let y = x + 1;
let _ = y;
```

Only simple identifiers (or `_`) are allowed on the left side.

### Expression Statement

```cicest
foo(x);
```

## Expressions

Supported expression forms:

- Literals (`1`, `3.14`, `true`, `"text"`)
- Paths (`x`, `Foo::Bar`)
- Block expression (`{ ... }`)
- Grouping (`(expr)`)
- Unary / binary operators
- Function call (`f(a, b)`)
- Field access (`obj.field`)
- Named constructor (`Point { x: 1, y: 2 }`)
- `if` expression
- `loop` expression
- C-style `for` expression
- `return`
- Keyword blocks (`runtime {}`, `const {}`)
- Turbofish (`f::<T>(x)`)
- `decl(TypeExpr)` intrinsic

### C-Style `for`

```cicest
for (init; condition; step) {
    // body
}
```

Each header field is optional:

```cicest
for (; true; ) { }
for (x; x > 0; x - 1) { x; }
```

## `decl(TypeExpr)` Intrinsic

`decl` is a reserved keyword.

```cicest
fn use_vec<T>(value: T) -> T
    where decl(Vec<T>)
{
    value
}
```

- Argument is a **type expression** (parsed with type grammar, including generic arguments)
- Intended for constraint validation in `where`

## Keywords

Reserved keywords:

`let runtime const fn where extern if else match loop for struct enum type with concept return self lambda decl true false _`
