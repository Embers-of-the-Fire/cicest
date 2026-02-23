# Language Syntax

- [Language Syntax](#language-syntax)
  - [Compiler Contract Marker](#compiler-contract-marker)
  - [Type Declaration](#type-declaration)
    - [Generic](#generic)
    - [Structures](#structures)
      - [Marker Structures](#marker-structures)
      - [Named Structures](#named-structures)
      - [UnNamed Structures](#unnamed-structures)
    - [Enums](#enums)
    - [Type Aliases](#type-aliases)
    - [Type Invocation](#type-invocation)
  - [Function Declaration](#function-declaration)
    - [`self` Parameter](#self-parameter)
  - [Expression](#expression)
    - [Block Expression](#block-expression)
    - [Grouped Expression](#grouped-expression)
    - [Tuple Expression](#tuple-expression)
    - [Operator Expression](#operator-expression)
    - [Constructor](#constructor)
    - [Call Expression](#call-expression)
    - [Turbofish Expression](#turbofish-expression)
    - [Control Expression](#control-expression)
  - [Statement](#statement)
    - [Expression Statement](#expression-statement)
    - [Let Statement](#let-statement)
  - [Token List](#token-list)

Note: The syntax reference is not comprehensive and is just a guide instead of rule for this language.

## Compiler Contract Marker

The language supports two type constructors: `async` and `runtime`.
To support higher level generics, the following variants are allowed: `async<A>` and `runtime<R>`, where the `A, R` are type variables, and `!async` and `!runtime`.
The negative declarations do not accept variables.

## Type Declaration

There are basically two types, structures and named enumerations.

### Generic

To declare generic variables:

< (__Name__ (: _(generic-restriction +)_+ )? ),* >

To declare standalone restrictions:

where  
&emsp;&emsp;( __Type__: _(generic-restriction +)_* )+

### Structures

Structures are plain memory skeletons.

#### Marker Structures

struct __Name__ _(generic-declaration)?_  
_(generic-restriction)?_ ;

#### Named Structures

struct __Name__ _(generic-declaration)?_  
_(generic-restriction)?_  
{  
&emsp;&emsp;(__name__: __Type__),*  
}

#### UnNamed Structures

struct __Name__ _(generic-declaration)?_  
_(generic-restriction)?_  
( (__Type__),* );

### Enums

enum __Name__ _(generic-declaration)?_  
_(generic-restriction)?_  
{(  
&emsp;| __Name__  
&emsp;| __Name__ { (__Name__: __Type__),* }  
&emsp;| __Name__ ( (__Type__),* )  
),*}

### Type Aliases

type __Name__ _(generic-declaration)?_  
= __Type__  
_(generic-restriction)?_ ;

### Type Invocation

Type invocation refers to the expression produces a type value.
This could be function calls or type variable replacements.

_contract-marker?_ __Name__ _(< __Type,+ >)?_

_contract-marker?_ _function-invocation_

_contract-marker?_ & __Type__

## Function Declaration

_contract-marker?_ fn __Name__ _(generic-declaration)?_  
( _self,?_ (__var-name__: __Type__),* ) (-> __Type__)?  
_(generic-restriction)?_  
{ _function-body_ }

_contract-marker?_ extern fn __Name__ ( (__var-name__: __Type__),* ) (-> __Type__)? ;

### `self` Parameter

The `self` parameter comes in two forms:

runtime &self

self: __Type__

**Rules**:
- `self` parameters must be marked `runtime` (methods are runtime-only)
- `&self` takes an immutable reference to the receiver
- `self: Type` allows custom receiver types

## Expression

### Block Expression

{  
&emsp;&emsp;_statements_?  
}

### Grouped Expression

( _expression_ )

### Tuple Expression

( _expression,+_ _expression_? )

### Operator Expression

**Borrow**  
& _expression_

**Dereference**  
\* _expression_

**Unary Arithmetic and Logical**  
! _expression_  
\- _expression_

**Binary Arithmetic and Logical**  
_expression_ ( + - * / % & ^ | << >>) _expression_

**Comparison**  
_expression_ ( == != > < >= <= ) _expression_

**Lazy Boolean**  
_expression_ ( || && ) _expression_

**Assignment**
_expression_ = _expression_

### Constructor

&emsp;| __Name__  
&emsp;| __Name__ { (__Name__: _expression_),* }  
&emsp;| __Name__ ( (_expression_),* )  

### Call Expression

_expression_ _turbofish_? ( (_expression_),* )
_expression_ . _path-segment_ _turbofish_? ( (_expression_),* )

### Turbofish Expression

::< __Type__,* >

### Control Expression

loop _block-expression_

if _expression_ _block-expression_  
(else if _expression_ _block-expression_)*  
(else _block-expression_)?

## Statement

| _declaration_
| _let-statement_
| _expression-statement_

### Expression Statement

| _expr-with-block_ (;)?
| _expr-without-block_ ;

### Let Statement

Binding declarations support two forms (similar to JavaScript):

const __Name__ (: __Type__)? (= _expression)?;

let __Name__ (: __Type__)? (= _expression)?;

**Rules**:
- `const` declares an immutable binding (cannot be reassigned)
- `let` declares a mutable binding (can be reassigned)
- Type constructors (`async`, `runtime`) can be used with both

## Token List

- Operator: `!` `+` `-` `*` `/` `=` `^` `|` `&` `%` `<<` `>>` `<` `>` `<=` `>=` `!=` `==` `||` `&&` `.` `::` `,` `;` `->`
- Keywords: `const` `let` `async` `fn` `where` `extern` `if` `else` `return` `loop` `struct` `enum` `type`
- Identifier
- Brackets: `( )` `[ ]` `{ }`
