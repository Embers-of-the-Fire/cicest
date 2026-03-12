# The Cicest Programming Language

## The Initial

This language grew out of exploring Rust and Zig, where I noticed powerful compiler-intrinsic operations like keyword generics and `comptime`. I wondered: could we generalize these capabilities as first-class features?

Cicest treats keywords — especially `const`/`runtime` contracts — as first-class citizens alongside types. The current direction is const-evaluation by default, so code is compile-time oriented unless explicitly marked runtime.

## Language

See [the language documentation](docs/language/index.md) for more information.

## License

This project is licensed under the [Apache-2.0](LICENSE-APACHE) license or the [MIT](LICENSE-MIT) license.
