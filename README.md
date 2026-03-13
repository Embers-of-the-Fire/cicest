# The Cicest Programming Language

## The Initial

This language grew out of exploring Rust and Zig, where I noticed powerful compiler-intrinsic operations like keyword generics and `comptime`. I wondered: could we generalize these capabilities as first-class features?

Cicest treats keywords — especially `const`/`runtime` contracts — as first-class citizens alongside types. The current direction is const-evaluation by default, so code is compile-time oriented unless explicitly marked runtime.

## Language

See [the language documentation](docs/language/index.md) for more information.

## Frontend Components

This repository now includes a minimal compiler frontend stack under `compiler/`:

- `cstc_ast`: header-only AST data model and formatter.
- `cstc_lexer`: header-only source lexer and token definitions.
- `cstc_parser`: header-only recursive-descent parser from token stream to AST.
- `cstc_inspect`: inspector CLI to dump `tokens` or `ast`.

When `-DCICEST_BUILD_TESTS=ON` is enabled, each package builds its own local `tests/*.cpp` executables.

Inspector usage:

```bash
cstc_inspect <input-file> --out-type tokens
cstc_inspect <input-file> --out-type ast -o output.txt
```

## License

This project is licensed under the [Apache-2.0](LICENSE-APACHE) license or the [MIT](LICENSE-MIT) license.
