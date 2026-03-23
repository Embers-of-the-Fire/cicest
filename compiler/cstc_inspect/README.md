# `cstc_inspect`

CLI package for frontend inspection output.

## Purpose

Reads a source file and emits either:

- lexer token stream (`tokens`)
- parsed AST tree (`ast`)

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
cstc_inspect <input-file> --out-type ast -o output.txt
```

Flags:

- `--out-type <tokens|ast>` (required)
- `-o, --output <path>`
- `--keep-trivia` (tokens mode)

## CMake

- Target: `cstc_inspect` (executable)
- Links: `cstc_ast`, `cstc_lexer`, `cstc_parser`
