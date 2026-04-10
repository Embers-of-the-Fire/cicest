# Cicest Language Syntax

This document defines the complete surface syntax for the Cicest language.
Module loading, visibility, and import resolution semantics are specified in
[Module System](modules.md).

Design goals:

- Minimal subset inspired by Rust and C++.
- Pure functional core (immutable bindings, no reassignment). `extern` calls may
  perform side effects such as I/O.
- Strongly scoped user types (`struct` and enum-like `enum`).
- Syntax defined tightly for lexer, parser, token, and AST generation.

## 1. Language Scope

Supported top-level items:

- `import` declarations.
- `struct` declarations.
- `enum` declarations (scoped variants, similar in spirit to C++ `enum class`).
- `fn` declarations, including `runtime fn`, which sugar-coats a runtime-qualified
  return type.
- Generic parameter declarations on `fn`, `struct`, and `enum` items.
- Declaration-level `where` clauses with const-evaluable constraint expressions.
- `extern` declarations (external function and opaque struct declarations),
  including `runtime extern ... fn`, which likewise sugar-coats a
  runtime-qualified return type.

Explicitly out of scope:

- Async/await.
- Global `let`/`const`/`static`.
- Mutation (`mut`, reassignment, assignment expressions).
- Tuple types and tuple literals. They are intentionally unsupported to keep the
  current compiler simpler; paper examples should use currently implemented
  forms such as named structs or conditionals instead.
- Unnamed structs.
- Lambda/closure expressions.

## 2. Lexical Grammar

### 2.1 Whitespace and comments

- Whitespace separates tokens and is otherwise ignored.
- Line comment: `// ...` until end of line.
- Block comment: `/* ... */` (non-nested).

### 2.2 Identifiers

```ebnf
IDENT      = ("_" | ALPHA) , { "_" | ALPHA | DIGIT } ;
ALPHA      = "a".."z" | "A".."Z" ;
DIGIT      = "0".."9" ;
```

Rules:

- Identifiers are case-sensitive.
- A standalone `_` is allowed only as a discard binding in `let`.

### 2.3 Keywords

Reserved keywords:

```text
struct enum fn let if else for while loop break continue return
true false Unit num str bool extern runtime pub import from as where
```

### 2.4 Literals

```ebnf
NUM_LIT    = DIGIT , { DIGIT } , [ "." , DIGIT , { DIGIT } ] ;
STR_LIT    = '"' , { STR_CHAR } , '"' ;
BOOL_LIT   = "true" | "false" ;
UNIT_LIT   = "(" , ")" ;
```

String escapes (minimal set):

- `\\`, `\"`, `\n`, `\r`, `\t`.

No other literal forms are supported.

### 2.5 Punctuation and operators

Punctuation:

```text
{ } ( ) , ; : :: . -> &
```

Operators:

```text
& + - * / % ! && || == != < <= > >=
```

`=` exists only in declaration contexts (`let`, enum discriminant).
It is not an assignment expression operator.

## 3. Syntactic Grammar (EBNF)

### 3.1 Program and items

```ebnf
Program            = { Item } , EOF ;

Item               = [ "pub" ] ,
                   ( ImportDecl
                   | StructDecl
                   | EnumDecl
                   | FnDecl
                   | ExternDecl
                   | RuntimeFnDecl
                   | RuntimeExternFnDecl ) ;

ImportDecl         = "import" , "{" , [ ImportItemList ] , "}" , "from" , STR_LIT , ";" ;
ImportItemList     = ImportItem , { "," , ImportItem } , [ "," ] ;
ImportItem         = IDENT , [ "as" , IDENT ] ;

StructDecl         = "struct" , IDENT , [ GenericParamList ] , [ WhereClause ] , ( StructFields | ";" ) ;
StructFields       = "{" , [ FieldDeclList ] , "}" ;
FieldDeclList      = FieldDecl , { "," , FieldDecl } , [ "," ] ;
FieldDecl          = IDENT , ":" , Type ;

EnumDecl           = "enum" , IDENT , [ GenericParamList ] , [ WhereClause ] , "{" , [ VariantDeclList ] , "}" ;
VariantDeclList    = VariantDecl , { "," , VariantDecl } , [ "," ] ;
VariantDecl        = IDENT , [ "=" , NUM_LIT ] ;

FnDecl             = "fn" , IDENT , [ GenericParamList ] , "(" , [ ParamList ] , ")" , [ ReturnType ] , [ WhereClause ] , BlockExpr ;
ParamList          = Param , { "," , Param } , [ "," ] ;
Param              = IDENT , ":" , Type ;
ReturnType         = "->" , Type ;
RuntimeFnDecl      = "runtime" , FnDecl ;

GenericParamList   = "<" , GenericParam , { "," , GenericParam } , [ "," ] , ">" ;
GenericParam       = IDENT ;
WhereClause        = "where" , ConstraintExpr , { "," , ConstraintExpr } ;
ConstraintExpr     = Expr ;

ExternDecl         = "extern" , STR_LIT , ( ExternFnDecl | ExternStructDecl ) ;
ExternFnDecl       = "fn" , IDENT , "(" , [ ParamList ] , ")" , [ ReturnType ] , ";" ;
ExternStructDecl   = "struct" , IDENT , ";" ;
RuntimeExternFnDecl = "runtime" , "extern" , STR_LIT , ExternFnDecl ;
```

Notes:

- `struct Name;` is a valid zero-sized type (ZST).
- Enum variants are scoped and must be referenced as `EnumName::Variant`.
- `decl(expr)` is a compiler-recognized expression form that probes whether `expr`
  is type-valid in the current substituted environment.
- `decl(expr)` is compile-time only, always yields `Constraint`, and is intended
  for `where` clauses and other staged validation sites.
- In function `where` clauses, ordinary parameter references remain invalid, but
  `decl(expr)` may probe expressions that mention those parameters.
- Enums are fieldless (C++ enum-class-like), i.e., no payload variants.
- `extern` declarations use a string literal for the ABI (e.g., `"lang"`, `"c"`).
- `extern` functions have no body; `extern` structs are opaque (no fields).
- `runtime fn` and `runtime extern ... fn` are syntax sugar for wrapping the
  return type in `runtime`. For example, `runtime fn f() -> num` is equivalent
  to `fn f() -> runtime num`, and TyIR also preserves the declaration-level
  runtime bit for const-eval.
- `pub` marks a top-level item or import as exportable from its module.
- `import *` is intentionally unavailable in source code. The compiler uses an
  internal equivalent only for the std prelude.
- Relative import paths resolve from the importing module's directory; `@std/`
  paths resolve from the configured standard-library root.

### 3.2 Types

```ebnf
Type               = RuntimeType | RefType | BuiltinType | NeverType | UserType ;
RuntimeType        = "runtime" , Type ;
RefType            = "&" , Type ;
BuiltinType        = "Unit" | "num" | "str" | "bool" ;
NeverType          = "!" ;
UserType           = IDENT , [ TypeArgList ] ;
TypeArgList        = "<" , Type , { "," , Type } , [ "," ] , ">" ;
```

> **Note:** The `Never` type (`!`) is a bottom type produced by diverging
> expressions (`return`, `break`, `continue`, and body-less `loop`). It can
> also be used as an explicit return type annotation (e.g., `fn f() -> ! { loop {} }`)
> to indicate that a function never returns.

> **Note:** `runtime T` is a runtime-tagged form of `T`, conceptually close to
> a wrapper like `Runtime<T>` rather than a separate ad hoc type category.
> Values of type `T` may be promoted to `runtime T`, but the reverse demotion
> is forbidden and is reported as a hard error.
>
> Function calls add one extra lifting rule: a plain function parameter of type
> `T` may still receive a `runtime T` argument. That does not demote the
> argument. Instead, the call result is lifted to `runtime U`, where `U` is the
> declared return type, whenever the callee is runtime-qualified or any argument
> is runtime-qualified.

> **Note:** `decl(expr)` is not an ordinary function call. It is a dedicated
> compiler-recognized form that always has type `Constraint`, even when the
> probed validity/value is deferred for later checking.

Ownership notes:

- `str` is an owned, move-only string value.
- String literals have type `&str`.
- `&T` is an immutable shared reference type.
- Reference types are currently restricted to local bindings and function
  parameters; they are not yet valid in struct fields or return positions.

### 3.3 Blocks and statements

```ebnf
BlockExpr          = "{" , { Stmt } , [ Expr ] , "}" ;

Stmt               = LetStmt | ExprStmt ;
LetStmt            = "let" , LetPat , [ ":" , Type ] , "=" , Expr , ";" ;
LetPat             = IDENT | "_" ;
ExprStmt           = Expr , ";" ;
```

Rules:

- All bindings are immutable.
- Rebinding via shadowing is allowed (`let x = ...; let x = ...;`).
- No `mut` keyword exists.

### 3.4 Expressions

```ebnf
Expr               = LogicOrExpr ;

LogicOrExpr        = LogicAndExpr , { "||" , LogicAndExpr } ;
LogicAndExpr       = EqualityExpr , { "&&" , EqualityExpr } ;
EqualityExpr       = RelExpr , { ( "==" | "!=" ) , RelExpr } ;
RelExpr            = AddExpr , { ( "<" | "<=" | ">" | ">=" ) , AddExpr } ;
AddExpr            = MulExpr , { ( "+" | "-" ) , MulExpr } ;
MulExpr            = UnaryExpr , { ( "*" | "/" | "%" ) , UnaryExpr } ;

UnaryExpr          = ( "&" | "!" | "-" ) , UnaryExpr
                   | PostfixExpr ;

PostfixExpr        = PrimaryExpr , { PostfixOp } ;
PostfixOp          = FieldAccess | GenericAppSuffix | CallSuffix ;
FieldAccess        = "." , IDENT ;
GenericAppSuffix   = "::" , "<" , Type , { "," , Type } , [ "," ] , ">" ;
CallSuffix         = "(" , [ ArgList ] , ")" ;
ArgList            = Expr , { "," , Expr } , [ "," ] ;

PrimaryExpr        = LiteralExpr
                    | DeclExpr
                    | RuntimeExpr
                    | PathExpr
                    | StructInitExpr
                    | BlockExpr
                   | IfExpr
                   | LoopExpr
                   | WhileExpr
                   | ForExpr
                   | BreakExpr
                   | ContinueExpr
                   | ReturnExpr
                   | "(" , Expr , ")" ;

LiteralExpr        = NUM_LIT | STR_LIT | BOOL_LIT | UNIT_LIT ;
DeclExpr           = "decl" , "(" , Expr , ")" ;
RuntimeExpr        = "runtime" , BlockExpr ;
PathExpr           = IDENT | IDENT , "::" , IDENT ;
StructInitExpr     = IDENT , [ TypeArgList ] , "{" , [ FieldInitList ] , "}" ;
FieldInitList      = FieldInit , { "," , FieldInit } , [ "," ] ;
FieldInit          = IDENT , ":" , Expr ;
```

Notes:

- `foo.bar` field access is valid.
- `foo::<T>(value)` is the explicit generic application form for expressions.
- Type positions accept explicit generic arguments such as `Box<num>`.
- Expression-position generic application is intentionally narrower: only direct
  function calls accept turbofish. Enum value expressions stay in the
  `EnumName::Variant` form and rely on surrounding type information.
- `EnumName::Variant` is the canonical enum value expression.
- Tuple/grouping ambiguity is avoided: `(expr)` is grouping; `()` is the unit literal.
- No tuple literals or tuple types are defined.
- `&expr` creates an immutable shared borrow.
- `runtime` is expression syntax only in the forced-block form `runtime { ... }`.
- `runtime expr` is intentionally invalid; the runtime boundary always introduces a block scope.
- Moving a non-`Copy` value out of a field is intentionally unsupported in this
  stage; borrow the field instead (`&value.field`).

### 3.5 Control-flow expressions

```ebnf
IfExpr             = "if" , Expr , BlockExpr , [ "else" , ( BlockExpr | IfExpr ) ] ;

LoopExpr           = "loop" , BlockExpr ;
WhileExpr          = "while" , Expr , BlockExpr ;

ForExpr            = "for" , "(" , [ ForInit ] , ";" , [ ForCond ] , ";" , [ ForStep ] , ")" , BlockExpr ;
ForInit            = ForInitLet | Expr ;
ForInitLet         = "let" , LetPat , [ ":" , Type ] , "=" , Expr ;
ForCond            = Expr ;
ForStep            = Expr ;

BreakExpr          = "break" , [ Expr ] ;
ContinueExpr       = "continue" ;
ReturnExpr         = "return" , [ Expr ] ;
```

Rules:

- `if`, `loop`, `while`, and `for` are expressions.
- `if` without `else` has type `Unit`.
- `while` and `for` evaluate to `Unit`.
- `loop` can evaluate via `break expr`; all `break` values must unify in type.
- For-loop is intentionally C-style only: `for (init; cond; step) { ... }`.
- No range-based or iterator-based `for` syntax exists.

## 4. Operator Precedence and Associativity

From highest to lowest:

1. Postfix: `.`, function call `(...)` (left-associative)
1. Unary prefix: `&`, `!`, unary `-` (right-associative)
1. Multiplicative: `*`, `/`, `%` (left-associative)
1. Additive: `+`, `-` (left-associative)
1. Relational: `<`, `<=`, `>`, `>=` (left-associative)
1. Equality: `==`, `!=` (left-associative)
1. Logical AND: `&&` (left-associative)
1. Logical OR: `||` (left-associative)

No assignment-expression precedence tier exists.

## 5. Static Constraints (Parser + Early Semantics)

Mandatory constraints for frontend validation:

- No top-level expressions or global bindings.
- No duplicate field names inside one struct.
- No duplicate variant names inside one enum.
- No duplicate parameter names within one function signature.
- Condition expressions in `if`/`while`/`for` must type-check as `bool`.
- `break`/`continue` allowed only inside loop forms (`loop`, `while`, `for`).
- `break` with a value (`break expr`) is allowed only inside `loop`, not inside `while` or `for`.
- `return` allowed only inside function bodies.
- Enum variant usage must be fully scoped (`E::V`), not bare `V`.
- Only named struct declarations are permitted; unnamed structs are invalid.
- Generic inference is attempted before explicit failure for omitted turbofish.
  If inference succeeds, later lowering stages only see concrete instantiations.
- `where` clauses are checked after generic substitution and must remain
  const-evaluable in the substituted environment.
- Recursive generic declarations are only accepted through named items and are
  rejected when their structural expansion is non-productive.
- Generic type checking, const-eval, and monomorphization all consume from the
  compiler's fixed recursion budget; hitting the limit reports the instantiation chain.
- Backend-facing IR is past the generic boundary: generic declarations are
  monomorphized before LIR/codegen use.

### 5.1 Loop and Break Typing Rules

#### `loop` expression typing

The type of a `loop` expression is inferred from its `break` values:

- If the loop contains no `break` statements (e.g., it diverges via `return`), the loop has type `Never` (bottom type, `!`). See §3.2 for the type grammar.
- If the loop contains only bare `break;` (no value), the loop has type `Unit`.
- If the loop contains `break expr;`, the loop has the type of `expr`.
- All `break` values within a single `loop` must have the same type. Conflicting break types produce a type error.

#### `while` and `for` expression typing

`while` and `for` expressions always have type `Unit`, regardless of their body.
`break` with a value is rejected inside `while` and `for`; only bare `break;` is permitted.

#### Break type unification

When multiple `break` statements exist in the same `loop`:

- The first `break` establishes the expected break type.
- Subsequent `break` values must be compatible with the established type.
- `Never`-typed values (e.g., from nested `return`) are compatible with any type.

#### Nested loops

Each loop form maintains its own break-type context. A `break` targets the innermost enclosing loop. Inner and outer loops may have different break types independently.

### 5.2 Block and Statement Typing Rules

#### Statement types

Every statement has an implicit type used for block type computation:

- `LetStmt` (`let x = expr;`) — type is `Unit`.
- `ExprStmt` (`expr;`) — type is the inner expression's type. When the expression diverges (type `Never`, e.g., `return;`, `break;`, `continue;`), the statement type is `Never`.

#### Block type computation

A block `{ stmt1; stmt2; ... [tail] }` has its type computed as follows:

1. If a **tail expression** is present (final expression without `;`), the block type is the tail expression's type.
1. If **no tail expression** is present:
   - If any statement in the block has type `Never` (i.e., an `ExprStmt` wrapping a diverging expression), the block type is `Never`.
   - Otherwise, the block type is `Unit`.

This means `{ return 1; }` has type `Never` (not `Unit`), which cascades correctly through `if`/`else` branches:

```text
// Both branches diverge → if-else type is Never → compatible with num
fn f(b: bool) -> num {
    if b { return 1; } else { return 2; }
}
```

## 7. Valid Syntax Examples

```text
runtime extern "lang" fn print(value: &str);
extern "lang" fn to_str(value: num) -> str;
extern "lang" struct Handle;

struct Empty;

struct User {
    id: num,
    name: str,
}

enum State {
    Init,
    Running,
    Done,
}

fn choose(flag: bool) -> State {
    if flag {
        State::Running
    } else {
        State::Init
    }
}

fn count_down(start: num) -> num {
    loop {
        if start <= 0 {
            break 0;
        } else {
            break start - 1;
        }
    }
}

fn spin_once() -> Unit {
    for (; false; ()) {
        continue;
    }
    ()
}
```

This specification is intentionally minimal and fully closed over the features listed above.
