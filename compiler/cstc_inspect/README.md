# `cstc_inspect`

CLI package for frontend inspection output.

## Purpose

Reads a source file and emits either:

- lexer token stream (`tokens`)
- parsed AST tree (`ast`)
- typed IR (`tyir`)
- low-level IR (`lir`)
- LLVM IR (`llvm`)

Output can be written to stdout or a file.

Token and parse diagnostics are span-resolved through `cstc_span::SourceMap`,
so emitted positions can be mapped to concrete files and line/column locations.
Token text and AST names are resolved from a shared global symbol table. The
`tyir`, `lir`, and `llvm` modes resolve module imports and the implicit std
prelude before printing.

## CLI

```bash
cstc_inspect <input-file> --out-type tokens
cstc_inspect <input-file> --out-type ast
cstc_inspect <input-file> --out-type tyir
cstc_inspect <input-file> --out-type lir
cstc_inspect <input-file> --out-type llvm
cstc_inspect <input-file> --out-type llvm -o output.ll
```

Flags:

- `--out-type <tokens|ast|tyir|lir|llvm>` (required)
- `-o, --output <path>`
- `--keep-trivia` (tokens mode)

Output kinds:

- `tokens`: lexer token stream for the root source file; optionally keeps trivia
  with `--keep-trivia`.
- `ast`: parsed AST for the root source file only.
- `tyir`: typed IR after import resolution and implicit std prelude injection.
- `lir`: low-level IR lowered from the resolved typed program.
- `llvm`: LLVM IR emitted for the resolved module graph and injected std
  prelude.

The `ast` and `tyir` outputs render explicit runtime block nodes for
`runtime { ... }`, which helps inspect how the runtime boundary survives
lowering before LIR/codegen.

`tokens` and `ast` operate on the root source file only. `tyir`, `lir`, and
`llvm` first resolve the full module graph and inject the std prelude.

## CMake

- Target: `cstc_inspect` (executable)
- Links: `cstc_ast`, `cstc_cli_support`, `cstc_codegen`, `cstc_lexer`,
  `cstc_lir`, `cstc_lir_builder`, `cstc_parser`, `cstc_resource_path`,
  `cstc_tyir`, `cstc_tyir_builder`
