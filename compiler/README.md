# Compiler Packages

This directory contains the scoped Cicest compiler packages:

- `cstc_symbol`: global symbol interner (rustc-style symbol ids)
- `cstc_span`: source span primitives
- `cstc_ast`: AST model + formatter
- `cstc_lexer`: source tokenizer
- `cstc_parser`: token/source parser to AST
- `cstc_module`: module graph loading and import resolution
- `cstc_tyir`: typed intermediate representation model + formatter
- `cstc_tyir_builder`: AST to TyIR lowering + type checking
- `cstc_codegen`: LIR to LLVM IR/native artifact backend
- `cstc_cli_support`: shared CLI helpers for module loading and diagnostics
- `cstc`: compiler CLI that emits `.s` / `.o` artifacts
- `cstc_inspect`: inspection CLI

Packages with a public developer-facing surface include a `README.md` with API
and test details.
