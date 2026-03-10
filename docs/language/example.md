# Cicest Examples (Simplified)

This page contains examples for the currently supported language subset.

## Basic Function Body

```cicest
let x: i32 = 1;
let y = x + 2;
y;
```

## Struct and Field Access

```cicest
struct Point { x: i32, y: i32 }
let p = Point { x: 3, y: 4 };
p.x;
```

## Enum (Unit Variants Only)

```cicest
enum Color {
    Red,
    Green,
    Blue,
}
Color::Red;
```

## C-Style `for`

```cicest
for (0; true; ) {
    1;
};
```

## Empty `for` Header Slots

```cicest
let keep_running = true;
for (; keep_running; ) {
    keep_running;
};
```

## `if` and `loop`

```cicest
if true { 1 } else { 0 };
loop {
    return 1;
};
```

## `decl(TypeExpr)` Intrinsic

```cicest
decl(Vec<i32>);
```

## Turbofish Call

```cicest
identity::<i32>(1);
```

## Constraint-Oriented Generic Body

```cicest
fn validate<T>(value: T) -> T where decl(Vec<T>) {
    value
}
validate::<i32>(1);
```
