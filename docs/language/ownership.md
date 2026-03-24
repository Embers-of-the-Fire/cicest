# Ownership and Drop

This document defines Cicest's first ownership-aware execution model.

The goal is Rust-like move semantics and shared borrows for an immutable
language subset, without introducing the full Rust lifetime system. Cicest does
not have mutation, reassignment, or `&mut`, so the borrow rules are narrower
and lexical.

## Design scope

This model adds:

- move semantics for owned values
- shared borrow types `&T`
- automatic scope-exit drop for owned values
- lexical borrow checking for local scopes

This model does not add:

- `&mut T`
- explicit lifetime syntax
- reference fields in structs
- reference return types
- partial moves out of structs
- non-lexical lifetime inference

## Ownership classes

Every Cicest value falls into one of three categories:

- `Copy` values: duplicating the value does not transfer ownership
- owned values: using the value by value moves it
- borrowed values: `&T` references are non-owning and `Copy`

Current `Copy` types:

- `Unit`
- `num`
- `bool`
- fieldless enums
- `&T`
- structs whose fields are all `Copy`

Current owned, move-only types:

- `str`
- structs with at least one move-only field

## References

`&T` is an immutable shared reference.

References are allowed in:

- local `let` bindings
- function parameters
- extern function parameters

References are currently rejected in:

- struct fields
- function return types
- extern function return types

This keeps the first ownership model lexical and local. The compiler does not
yet model named lifetimes or escaping borrows across API boundaries.

## Borrows

A borrow expression uses the prefix `&` operator:

```cicest
fn show(value: &str) {
    println(value);
}

fn main() {
    let owned: str = to_str(42);
    let borrowed: &str = &owned;
    show(borrowed);
}
```

Borrowing rules:

- borrows are shared and immutable
- multiple shared borrows may coexist
- a value cannot be moved while a borrow of that value is still live
- borrow lifetimes are lexical and end when the reference binding's scope ends

Because lifetimes are lexical in this model, Cicest is intentionally more
conservative than Rust's non-lexical lifetime analysis.

## Moves

Passing or binding an owned value by value moves it:

```cicest
fn consume(value: str) {
    println(&value);
}

fn main() {
    let text: str = to_str(42);
    consume(text);
    // text is moved and cannot be used here
}
```

Move rules:

- reading a `Copy` value copies it
- reading a move-only local by value moves it
- moved locals become unusable
- moved locals are not dropped again at scope exit

Struct field access is intentionally narrower than Rust for now:

- reading a `Copy` field copies that field
- moving a move-only field out of a struct is rejected
- borrow the field instead with `&value.field`

## Drop

Owned values are dropped automatically when their scope ends, in reverse binding
order.

Current drop behavior:

- `str` lowers to a runtime `str_free` call
- structs recursively drop their fields in declaration order reversed by scope
  exit traversal
- `Copy` values and references have no drop action

Drop also runs before normal control-flow exits from a scope:

- `return`
- `break`
- `continue`

If a value has already been moved, its drop is skipped.

## Strings

String ownership is now explicit:

- string literals have type `&str`
- `to_str(value: num) -> str` returns an owned heap string
- `str_concat(a: &str, b: &str) -> str` returns an owned heap string
- `str_len(value: &str) -> num` borrows
- `print(value: &str)` and `println(value: &str)` borrow
- `str_free(value: str)` remains available as a low-level consuming primitive

Manual `str_free` is no longer required in ordinary Cicest code. Automatic
drop should be preferred.

## Temporaries

Borrowing a non-place expression may cause the compiler to materialize a hidden
temporary owned value. That temporary lives for the enclosing lexical scope and
is dropped automatically like an ordinary binding.

This makes patterns like `let r: &str = &to_str(42);` well-defined in the
current model without requiring explicit lifetime syntax.

## Current limitations

The first ownership-aware Cicest implementation is intentionally conservative:

- borrow lifetimes are lexical, not flow-sensitive
- references do not escape through returns or stored fields
- move-sensitive ownership inside loops may be rejected more often than a full
  Rust borrow checker would reject it

Those restrictions are intentional design limits for this stage, not parser
bugs.
