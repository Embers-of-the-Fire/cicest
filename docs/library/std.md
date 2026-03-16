# std — Standard Library

The cicest standard library provides a set of built-in functions that are
automatically available in every cicest program. These functions are declared
in the **prelude** (`libraries/std/prelude.cst`) and injected into every
compilation before user source code is parsed.

## Position in the pipeline

```
libraries/std/prelude.cst
        │
        ▼
┌───────────────────────────────┐
│  SourceMap.add_file(prelude)  │   ← separate SourceFileId
│  SourceMap.add_file(user)     │   ← separate SourceFileId
└───────────────────────────────┘
        │
        ▼
┌───────────────────────────────┐
│  Parser: parse prelude        │
│  Parser: parse user source    │
└───────────────────────────────┘
        │
        ▼
┌───────────────────────────────┐
│  Merge: prelude AST + user    │   ← single ast::Program
└───────────────────────────────┘
        │
        ▼
   TyIR → LIR → LLVM IR
```

The prelude is parsed as a separate source file with its own `SourceFileId`.
Error messages for prelude declarations resolve to `prelude.cst:line:col`,
while user errors resolve to the user's file path.

## Prelude injection

Both the compiler (`cstc`) and the inspector (`cstc_inspect`) inject the
prelude automatically. The injection process is:

1. Read `prelude.cst` from the path defined by `CICEST_STD_PATH` (set at
   compile time via CMake).
2. Add it to the `SourceMap` as a separate file.
3. Parse the prelude source independently.
4. Parse the user source independently.
5. Merge the two `ast::Program` item lists (prelude items first).
6. Continue the pipeline with the merged program.

> **Note:** The `tokens` and `ast` output modes of `cstc_inspect` do not
> inject the prelude, since they display only the user's source-level
> representation. The `tyir`, `lir`, and `llvm` modes include the prelude.

## Extern declaration syntax

All standard library functions use the `extern` declaration syntax:

```cicest
extern "lang" fn print(value: str);
extern "lang" fn println(value: str);
extern "lang" struct Handle;
```

| Component | Description |
|-----------|-------------|
| `extern` | Keyword introducing an external declaration |
| `"lang"` | ABI string literal — `"lang"` denotes the cicest language runtime |
| `fn` / `struct` | Declares a function signature or opaque struct type |
| `;` | Terminator — extern declarations have no body |

The ABI string is stored as a `Symbol` and carried through the full pipeline
(AST → TyIR → LIR). The codegen layer currently ignores the ABI value and
emits all extern functions with LLVM `ExternalLinkage`. Future ABI strings
(e.g., `"c"`) can alter calling conventions without grammar changes.

### Extern functions

Extern functions participate in name resolution identically to regular
functions: their signatures are registered in `TypeEnv::fn_signatures` during
TyIR lowering. The type checker validates argument types and counts for calls
to extern functions just as it does for user-defined functions.

In LLVM IR, extern functions are emitted as `declare` (no body):

```llvm
declare void @println(ptr)
declare ptr @to_str(double)
```

### Extern structs

Extern structs are opaque named types with no visible fields. They are
registered in `TypeEnv::struct_fields` with an empty field list and lowered
as zero-sized types (ZST). They can be used as type annotations but cannot be
constructed with field initializers.

In LLVM IR, extern structs are emitted as empty struct types:

```llvm
%Handle = type {}
```

## Available functions

The prelude currently provides the following functions:

### I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `fn print(value: str)` | Prints a string to standard output without a trailing newline. |
| `println` | `fn println(value: str)` | Prints a string to standard output followed by a newline. |

### Conversion

| Function | Signature | Description |
|----------|-----------|-------------|
| `to_str` | `fn to_str(value: num) -> str` | Converts a number to its string representation. |

### String operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `str_concat` | `fn str_concat(a: str, b: str) -> str` | Concatenates two strings and returns the result. |
| `str_len` | `fn str_len(value: str) -> num` | Returns the length of a string as a number. |

## Usage examples

### Basic output

```cicest
fn main() {
    println("hello world");
}
```

Compiles to LLVM IR containing:

```llvm
declare void @println(ptr)

define i32 @main() {
bb0:
  call void @println(ptr @0)
  ret i32 0
}
```

### String operations

```cicest
fn main() {
    let greeting: str = str_concat("hello, ", "world");
    println(greeting);
    let length: num = str_len(greeting);
    println(to_str(length));
}
```

### Mixing with user-defined functions

```cicest
fn greet(name: str) {
    let message: str = str_concat("hello, ", name);
    println(message);
}

fn main() {
    greet("cicest");
}
```

## Inspecting the prelude

The `cstc_inspect` tool shows prelude declarations in its output when using
the `tyir`, `lir`, or `llvm` output modes.

```bash
cstc_inspect my_file.cst --out-type tyir
```

Example output (prelude items appear first):

```
TyProgram
  TyExternFnDecl "lang" print(value: str) -> Unit
  TyExternFnDecl "lang" println(value: str) -> Unit
  TyExternFnDecl "lang" to_str(value: num) -> str
  TyExternFnDecl "lang" str_concat(a: str, b: str) -> str
  TyExternFnDecl "lang" str_len(value: str) -> num
  TyFnDecl main() -> Unit
    ...
```

## Pipeline representation

Each pipeline stage has a corresponding node type for extern declarations:

| Stage | Node | Container |
|-------|------|-----------|
| AST | `ExternFnDecl`, `ExternStructDecl` | `Item` variant |
| TyIR | `TyExternFnDecl` | `TyItem` variant |
| LIR | `LirExternFnDecl` | `LirProgram::extern_fns` |
| LLVM IR | `declare` instruction | Module-level |

Extern structs are lowered to `TyStructDecl` (with `is_zst = true`) at the
TyIR stage and to `LirStructDecl` at the LIR stage. They do not have a
dedicated node type beyond the AST.

## Adding new standard library functions

To add a new function to the standard library:

1. Add an `extern "lang" fn` declaration to `libraries/std/prelude.cst`.
2. Implement the function in the language runtime (linked at build time).
3. Update the function tables in this document.
4. Add tests in `compiler/cstc_codegen/tests/codegen_integration.cpp` that
   verify the new function is correctly declared and callable.

## Limitations (current version)

- All extern functions use `"lang"` ABI. No other ABI strings (e.g., `"c"`)
  are semantically distinguished yet.
- Extern structs are zero-sized and cannot carry data. They serve only as
  opaque type handles.
- The prelude is always injected — there is no mechanism to suppress it.
- No module or import system exists. All prelude declarations are global.
- The runtime implementations of the prelude functions are not yet provided.
  Programs using prelude functions will compile but will fail at link time
  without a runtime library.
