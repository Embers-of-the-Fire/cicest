# Cicest Module System

This document defines file-based modules, visibility, and import resolution in
Cicest.

## 1. File model

- Each `.cst` file is one module.
- Compilation starts from a root module file passed to `cstc` or
  `cstc_inspect`.
- The compiler recursively loads every module reachable through
  `import { ... } from "..."` declarations.
- After import resolution, the reachable module graph is flattened into one
  crate-wide AST before TyIR lowering.

## 2. Public items and re-exports

Any top-level item can be exported from its module by prefixing it with `pub`:

```cicest
pub struct File;
pub enum Mode { Read, Write }
pub fn open() -> File { File { } }
pub extern "lang" fn println(value: &str);
```

`pub` can also prefix an import. A public import re-exports the selected item
under its local name:

```cicest
pub import { open_file as open } from "fs.cst";
```

Only the plain `pub` keyword exists. Scoped `pub(...)` forms are not part of
the language.

## 3. Import syntax

```cicest
import { identifier } from "path/to/file.cst";
import { identifier as local_name } from "path/to/file.cst";
pub import { identifier as public_name } from "path/to/file.cst";
```

Rules:

- Imported names must be listed explicitly.
- `as` renames the binding inside the importing module.
- The import path is always a string literal.
- Source code cannot spell `import *`. The compiler reserves that behavior for
  the implicit std prelude import.

## 4. Import paths

Relative paths resolve against the importing module's directory:

```cicest
import { helper } from "util/helper.cst";
```

Paths starting with `@std/` resolve against the configured standard-library
root:

```cicest
import { println } from "@std/prelude.cst";
```

The prelude is special:

- Every non-prelude module behaves as if it had an implicit
  `import * from "@std/prelude.cst";`.
- That import is compiler-internal and cannot be written in source.
- Explicit `@std/...` imports are still available for non-prelude std modules.

For the std prelude and runtime declarations, see [std](../library/std.md).

## 5. Names and visibility

Cicest uses Rust-like type and value namespaces:

- Type namespace: `struct`, `enum`, `extern struct`
- Value namespace: `fn`, `extern fn`

Imported names enter the same namespaces as local declarations. Because of
that:

- A local item and an imported item cannot share the same name in the same
  namespace.
- Two imports cannot introduce the same visible name in the same namespace.
- Implicit prelude items participate in the same duplicate-name checks.
- Importing a non-`pub` item is an error.

If a module exports both a type binding and a value binding under the same
visible name, importing that name brings both bindings into scope.

## 6. Resolution behavior

Module resolution runs before type checking. The resolver:

- Loads modules recursively from the root module.
- Rejects cyclic imports.
- Resolves relative paths and `@std/...` paths.
- Applies the implicit std prelude to every non-prelude module.
- Validates `pub` visibility on cross-module imports.
- Applies `pub import` re-exports before the crate-wide AST reaches later
  stages.

Non-root module items are internally renamed during flattening so private helper
names from different modules can coexist. Diagnostics still report the original
source-facing names.

## 7. Example

`math.cst`

```cicest
fn helper(x: num) -> num { x + 1 }

pub fn inc(x: num) -> num {
    helper(x)
}
```

`api.cst`

```cicest
pub import { inc } from "math.cst";
```

`main.cst`

```cicest
import { inc } from "api.cst";

fn main() {
    let rendered: str = to_str(inc(41));
    println(&rendered);
}
```

`helper` stays private to `math.cst`, `inc` is re-exported through `api.cst`,
and `println` / `to_str` come from the implicit prelude.
