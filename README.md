# The Cicest Programming Language

## The Initial

This language grew out of exploring Rust and Zig, where I noticed powerful compiler-intrinsic operations like keyword generics and `comptime`. I wondered: could we generalize these capabilities as first-class features?

Cicest treats keywords — including `const`, `async`, and their variants — as first-class citizens alongside types. By design, operations are evaluated at compile time whenever possible, enabling early evaluation and potentially allowing asynchronously computed compile-time values. While this may introduce soundness and correctness challenges, the benefits could be substantial.

## Language

See [the language documentation](docs/language/index.md) for more information.

## Third Party Dependencies

This project depends on the following third-party libraries:

- [`llvm`](https://github.com/llvm/llvm-project) for code generation and optimization.

> **Note:** The compiler currently supports pure ASCII source input only.

## License

This project is licensed under the [Apache-2.0](LICENSE-APACHE) license or the [MIT](LICENSE-MIT) license.
