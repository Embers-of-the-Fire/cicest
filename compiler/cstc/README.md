# `cstc`

CLI package for native compilation artifacts.

## Purpose

`cstc` compiles a Cicest root module through the existing frontend pipeline:

```
Source/module graph -> Parser -> AST -> TyIR -> LIR -> LLVM -> native artifacts
```

Unlike `cstc_inspect`, this tool does not print intermediate compiler data.
It emits build artifacts:

- `<stem>.s` (assembly)
- `<stem>.o` (object file)
- `<stem>` executable

Default output is the final executable (`<stem>`).
Use `--emit` to explicitly request assembly/object/executable outputs.

## Module loading

`<input-file>` is treated as the root module. From there, the CLI:

- resolves relative imports from the importing file's directory
- resolves `@std/...` imports from the configured std root
- implicitly loads `@std/prelude.cst` for every non-prelude module
- passes the flattened crate-wide AST into TyIR/LIR/codegen

## CLI

```bash
cstc <input-file>
cstc <input-file> -o build/out/program
cstc <input-file> --emit asm
cstc <input-file> --emit obj
cstc <input-file> --emit all
cstc <input-file> --module-name my_module
```

Flags:

- `-o`, `--output <output-stem>`: output path stem used for both artifacts
  (emits `<output-stem>.s` and `<output-stem>.o`)
- `--module-name <name>`: LLVM module name
- `--emit <asm|obj|exe|all>`: choose output kinds (can be repeated)
  - when omitted, defaults to `exe`
- `--linker <path>`: linker/driver executable used for `exe` output
- `-h`, `--help`: print usage

If `-o/--output` is not provided, `cstc` emits artifacts next to the input
file using the input stem.

Executable output requires an external linker toolchain.

- POSIX platforms default to `c++` (or `$CXX` when set)
- MinGW Windows defaults to `c++`; MSVC Windows defaults to `clang++`
  (both can be overridden with `$CXX` or `--linker`)
- Other non-POSIX/non-Windows platforms currently support `--emit asm` and
  `--emit obj`, but not `--emit exe`

## CMake

- Target: `cstc` (executable)
- Links: `cstc_cli_support`, `cstc_codegen`, `cstc_lir_builder`,
  `cstc_parser`, `cstc_resource_path`, `cstc_span`, `cstc_symbol`,
  `cstc_tyir_builder`
