# Language Syntax

- [Language Syntax](#language-syntax)
  - [Compiler Contract Marker](#compiler-contract-marker)
    - [Contract Blocks](#contract-blocks)
  - [Type Declaration](#type-declaration)
    - [Generic](#generic)
    - [Concepts](#concepts)
    - [Structures](#structures)
      - [Marker Structures](#marker-structures)
      - [Named Structures](#named-structures)
      - [Unnamed Structures](#unnamed-structures)
    - [Enums](#enums)
    - [Type Aliases](#type-aliases)
    - [With Blocks](#with-blocks)
    - [Type Invocation](#type-invocation)
  - [Function Declaration](#function-declaration)
    - [Type Annotations](#type-annotations)
    - [`self` Parameter](#self-parameter)
  - [Expression](#expression)
    - [Block Expression](#block-expression)
    - [Grouped Expression](#grouped-expression)
    - [Tuple Expression](#tuple-expression)
    - [Operator Expression](#operator-expression)
    - [Constructor](#constructor)
    - [Lambda Expression](#lambda-expression)
    - [Match Expression](#match-expression)
    - [Call Expression](#call-expression)
    - [Turbofish Expression](#turbofish-expression)
    - [Control Expression](#control-expression)
  - [Statement](#statement)
    - [Expression Statement](#expression-statement)
    - [Let Statement](#let-statement)
  - [Patterns](#patterns)
  - [Token List](#token-list)

Note: The syntax reference is not comprehensive and is just a guide instead of a strict rule for this language.

## Compiler Contract Marker

The language supports two type constructors: `async` and `runtime`.
To support higher-level generics, the following variants are allowed: `async<A>` and `runtime<R>`, where `A` and `R` are type variables.

**Negative declarations** assert the *absence* of a contract: `!async` and `!runtime`.
Negative declarations do not accept type variables.

**Aliases** — the negative forms have readable keyword equivalents:

| Full form | Alias | Meaning |
|-----------|-------|---------|
| `async T` | — | Deferred computation; not yet resolved |
| `runtime T` | — | Only available at runtime |
| `!async T` | `sync T` | Must **not** be deferred; synchronously resolved |
| `!runtime T` | `const T` | Must **not** be runtime-only; compile-time available |

`sync` and `const` are syntactic aliases. They are interchangeable with `!async` and `!runtime` everywhere: in type positions, function signatures, and block keywords.

**`!async` / `sync` — field accessor types:**

Each named struct field generates an **accessor function**.
For `struct T { a: A }`, the compiler generates:

```
T::a : (!async T) -> A
```

The dot-access syntax `expr.field` is syntactic sugar for calling this accessor:
`r.a` desugars to `Rec::a(r)`.

When the receiver is `async T`, the compiler automatically lifts the accessor:

```
T::a : (async T) -> async A
```

```rust
struct Rec { a: i32 }
// generates: Rec::a : (!async Rec) -> i32

let r: Rec = Rec { a: 5 };
let v = r.a;               // Rec::a(r) — r: !async Rec → v: i32

let ar: async Rec = async { deferred() };
let av = ar.a;             // Rec::a(ar) — lifted: async Rec → av: async i32
let sv = sync { ar }.a;    // sync { ar }: !async Rec → sv: i32
```

**`!runtime` / `const` — compile-time type intrinsics:**

Compiler-intrinsic functions that query type information require a `!runtime` (`const`) type argument.
All plain types satisfy `!runtime` by default.

```rust
let s = sizeof(i32);        // OK: i32 is !runtime (const)
let a = alignof(Point);     // OK

// sizeof(runtime i32);       // ERROR: runtime i32 violates !runtime
```

### Contract Blocks

A `<keyword> { }` block is an **explicit type-conversion expression**.
In the common case, the compiler handles `async T` → `T` coercion implicitly; contract blocks are used when explicit control is needed.

*contract-keyword* `{` *expression* `}`

Where *contract-keyword* is one of: `async` | `sync` | `runtime` | `const` | `!async` | `!runtime`

| Block | Body type | Result type |
|-------|-----------|-------------|
| `async { expr }` | `T` | `async T` |
| `sync { expr }` | `async T` | `T` |
| `runtime { expr }` | `T` | `runtime T` |
| `const { expr }` | `T` | `const T` (`!runtime T`) |

`sync { }` and `const { }` are the preferred aliases for `!async { }` and `!runtime { }`.

```rust
let f: async i32   = async { 42 };           // i32  → async i32
let v: i32         = sync { f };             // explicit force (optional: implicit coercion also works)
let r: runtime i32 = runtime { read_int() }; // i32 → runtime i32
let c: const i32   = const { factorial(10) };// i32 → const i32 (compile-time)
```

Inside function bodies, `async T` is implicitly coerced to `T` at usage sites — `sync { }` is usually not written by hand. See [Implicit Coercion](design.md#implicit-coercion).

## Type Declaration

There are two primary types: structures (product types) and enumerations (sum types).

### Generic

To declare generic type variables:

< ( **Name** ),* >

To declare standalone restrictions:

where  
&emsp;&emsp;( *compile-time-bool-expression-over-type-params* ),*

`where` clauses accept two forms:

- **Type-level predicates**: arbitrary compile-time boolean expressions that mention only type parameters (not value parameters)
- **Concept checks** via intrinsic expression: `concept(Foo)` or `concept(Foo::<T>)`

Type parameters are compile-time values (like Zig's `comptime`), so compiler intrinsics like `sizeof(T)` and `alignof(T)` may appear inside `where`.
Any non-`runtime` function whose arguments are derived solely from type parameters is valid in a predicate.

```rust
fn<T> foo(x: T) -> T where sizeof(T) == 4 { x }
fn<T, U> compatible() -> bool where sizeof(T) == sizeof(U) { true }
fn<T> aligned() -> T where is_power_of_two(sizeof(T)) { /* ... */ }
fn<T> sortable(x: T) -> T where concept(Sortable::<T>) { x }

// ERROR — value parameter `n` is not a type parameter:
// fn bar(n: i32) -> i32 where is_positive(n) { n }
```

`concept` is a reserved keyword and is parsed as a compiler intrinsic in expression position.

### Concepts

Concepts declare compile-time interface requirements.
The concept body only allows function signatures (no function bodies).

concept **Name** *(generic-declaration)?*  
*(generic-restriction)?*  
{  
&emsp;&emsp;( *contract-marker?* fn **name** *(generic-declaration)?* ( *self,?* ( **var-name** : **Type** ),* ) `->` **Type** *(generic-restriction)?* `;` )*  
}

Use concept predicates in generic constraints with the intrinsic expression `concept(...)`:

```rust
concept Comparable<T> {
    fn compare(lhs: T, rhs: T) -> i32;
}

fn<T> max(a: T, b: T) -> T
    where concept(Comparable::<T>)
{ /* ... */ }
```

#### Const Generics

There is no separate `const N` or `const fn` generic parameter syntax.
Const-generic-style constraints are expressed entirely through type-level `where` predicates, using the fact that types are compile-time values.
The compiler evaluates all `where` predicates via the VM at each call site.

### Structures

Structures are **abstract product types**. There is no C-ABI-style memory layout — the compiler chooses a representation. Programs cannot directly observe field offsets.

#### Marker Structures

struct **Name** *(generic-declaration)?*  
*(generic-restriction)?* ;

#### Named Structures

struct **Name** *(generic-declaration)?*  
*(generic-restriction)?*  
{  
&emsp;&emsp;( **name** : **Type** ),*  
}

Each named field **name** of type **FieldType** in `struct T` generates an accessor function:

`T::name : (!async T) -> FieldType`

The dot-access syntax `expr.name` is syntactic sugar for calling this accessor.

#### Unnamed Structures

struct **Name** *(generic-declaration)?*  
*(generic-restriction)?*  
( **Type**,* );

Positional fields do not generate named accessors; they are only destructured via patterns.

### Enums

`enum` declares a sum type (algebraic data type). Each variant may carry no data, named fields, or positional fields:

enum **Name** *(generic-declaration)?*  
*(generic-restriction)?*  
{(  
&emsp;| **Name**  
&emsp;| **Name** { ( **Name** : **Type** ),*}  
&emsp;| **Name** ( **Type**,* )  
),*}

Enum variants with fields are full algebraic data type constructors and can be used in [patterns](#patterns).

### Type Aliases

type **Name** *(generic-declaration)?*  
= **Type**  
*(generic-restriction)?* ;

### With Blocks

`with` binds a set of function definitions to a target type.

with *(generic-declaration)?* **Type** *(generic-restriction)?*  
{  
&emsp;&emsp;( *contract-marker?* fn **Name** *(generic-declaration)?* ( *self,?* ( **var-name** : **Type** ),* ) `->` **Type** *(generic-restriction)?* *block-expression* )*  
}

Desugaring model (documented behavior):

- A non-static function that takes `self` is desugared to a plain function whose first parameter is the receiver value.
- Method-call syntax desugars to a plain call: `value.func(args...)` → `func(value, args...)`.

Blanket implementation rule:

- A `with` target type may not be a bare generic parameter.
- `with<T> T { ... }` is forbidden (blanket impl on all types).
- This prohibition also applies when a `where` clause is present on the `with` block.

```rust
with Point {
    fn length(self: Point) -> f64 { /* ... */ }
}

// desugars conceptually to:
// fn length(self: Point) -> f64 { ... }
// p.length()  ==> length(p)

// INVALID (forbidden blanket impl):
// with<T> T {
//     fn show(self: T) -> () { /* ... */ }
// }
```

### Type Invocation

Type invocation refers to an expression that produces a type value.
This could be function calls or type variable replacements.

*contract-marker?* **Name** *( < **Type**,+ > )?*

*contract-marker?* *function-invocation*

*contract-marker?* & **Type**

## Function Declaration

All function declarations **require explicit type annotations** on every parameter and the return type.
The return type may not be omitted.

*contract-marker?* fn **Name** *(generic-declaration)?*  
( *self,?* ( **var-name** : **Type** ),* ) `->` **Type**  
*(generic-restriction)?*  
{ *function-body* }

*contract-marker?* extern fn **Name** ( ( **var-name** : **Type** ),* ) `->` **Type** ;

### Contract Prefix Desugaring

A contract marker before `fn` is **syntactic sugar** that wraps the declared return type:

```
<contract> fn foo(...) -> T   ≡   fn foo(...) -> <contract> T
```

| Written | Desugars to | Notes |
|---------|-------------|-------|
| `async fn foo() -> i32` | `fn foo() -> async i32` | preferred form |
| `fn foo() -> async i32` | (same) | explicit form, also valid |
| `async fn foo() -> async i32` | `fn foo() -> async (async i32)` | nested async — genuinely doubly-deferred |
| `runtime fn foo() -> ()` | `fn foo() -> runtime ()` | preferred form |
| `runtime fn foo() -> runtime ()` | `fn foo() -> runtime (runtime ())` ≡ `fn foo() -> runtime ()` | idempotent — same as above |

Write the contract **once** (either as prefix or in the return type) to avoid accidental nesting.

### Type Annotations

- Parameter types: **required** (no bare `x`, must be `x: Type`)
- Return type: **required** (the `-> Type` clause is mandatory)
- Generic constraints: expressed via `where` clauses

Annotations are enforced at HIR lowering.
Type inference still applies within function bodies.

### `self` Parameter

The `self` parameter comes in two forms:

runtime &self

self: **Type**

**Rules**:

- `self` parameters must be marked `runtime` (methods are runtime-only)
- `&self` takes an immutable reference to the receiver
- `self: Type` allows custom receiver types

## Expression

### Block Expression

{  
&emsp;&emsp;*statements*?  
}

### Grouped Expression

( *expression* )

### Tuple Expression

( *expression,* *expression,+* )

### Operator Expression

**Borrow**  
& *expression*

**Dereference**  
\* *expression*

**Unary Arithmetic and Logical**  
! *expression*  
\- *expression*

**Binary Arithmetic and Logical**  
*expression* ( `+` `-` `*` `/` `%` `&` `^` `|` `<<` `>>` ) *expression*

**Comparison**  
*expression* ( `==` `!=` `>` `<` `>=` `<=` ) *expression*

**Lazy Boolean**  
*expression* ( `||` `&&` ) *expression*

**Assignment**  
*expression* `=` *expression*

### Constructor

&emsp;| **Name**  
&emsp;| **Name** { ( **Name** : *expression* ),*}  
&emsp;| **Name** ( *expression*,* )

### Lambda Expression

A lambda creates an anonymous function inline using the `lambda` keyword.
Type annotations on parameters are optional when inferable from context:

`lambda` `(` ( **var-name** (`:` **Type**)? ),* `)` *block-expression*

```rust
lambda(x: i32) { x + 1 }
lambda(x: i32, y: i32) { x + y }
lambda(x) { x * 2 }                // type inferred from context
```

Lambdas capture immutable bindings from their enclosing scope.

### Match Expression

A `match` expression deconstructs a value by pattern.
Arms are checked exhaustively. The expression evaluates to the body of the first matching arm.

`match` *expression* `{`  
&emsp;( *pattern* `=>` *expression* `,` )*  
&emsp;*pattern* `=>` *expression* `,`?  
`}`

```rust
match s {
    Circle { radius: r }              => 3.14159 * r * r,
    Rectangle { width: w, height: h } => w * h,
    Point                             => 0.0,
}
```

A wildcard arm `_ => expr` satisfies exhaustiveness for any remaining cases.

### Call Expression

*expression* *turbofish*? ( *expression*,*)  
*expression* `.` *path-segment* *turbofish*? ( *expression*,* )

### Turbofish Expression

`::`< **Type**,* >

### Control Expression

if *expression* *block-expression*  
( else if *expression* *block-expression* )*  
( else *block-expression* )?

loop *block-expression*

## Statement

| *declaration*  
| *let-statement*  
| *expression-statement*

### Expression Statement

| *expr-with-block* (`;`)?  
| *expr-without-block* `;`

### Let Statement

All bindings use `let`. Every binding is immutable — a name is bound once and cannot be reassigned.

`let` **Name** ( `:` **Type** )? ( `=` *expression* )? `;`

**Rules**:

- Type annotation is optional when the type can be inferred
- Type constructors (`async`, `runtime`, `const`) can be used in the type position

## Patterns

Patterns appear in `match` arms, lambda parameters, and `let` bindings.

| Pattern | Description |
|---------|-------------|
| `_` | Wildcard — matches anything, binds nothing |
| *name* | Variable — matches anything, binds to *name* |
| *literal* | Literal — matches an exact value (`0`, `true`, `"hi"`) |
| **Name** | Nullary constructor |
| **Name** `{` *name* `:` *pat*,* `}` | Named-field constructor |
| **Name** `(` *pat*,* `)` | Positional constructor |
| `(` *pat*,* `)` | Tuple |
| *pat* `\|` *pat* | Or-pattern — matches either branch |
| *name* `@` *pattern* | As-pattern — binds whole value to *name*, also matches inner pattern |

## Token List

- **Operators**: `!` `+` `-` `*` `/` `=` `^` `:` `|` `&` `%` `<<` `>>` `<` `>` `<=` `>=` `!=` `==` `||` `&&` `.` `::` `,` `;` `->` `=>` `@` `.` `~`
- **Keywords**: `let` `async` `runtime` `sync` `fn` `where` `extern` `if` `else` `match` `loop` `struct` `enum` `type` `with` `concept` `return` `self` `lambda`
- **Contract Aliases** (not statement keywords): `const` (≡ `!runtime`), `sync` (≡ `!async`)
- **Identifier**: starts with a lowercase letter or underscore
- **Constructor**: starts with an uppercase letter
- **Brackets**: `( )` `[ ]` `{ }`
