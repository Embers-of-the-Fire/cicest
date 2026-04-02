#ifndef CICEST_COMPILER_CSTC_TYIR_BUILDER_BUILDER_HPP
#define CICEST_COMPILER_CSTC_TYIR_BUILDER_BUILDER_HPP

/// @file builder.hpp
/// @brief AST → TyIR lowering pass.
///
/// The lowering pass transforms a parsed `cstc::ast::Program` into a
/// fully type-annotated `cstc::tyir::TyProgram`.  It performs:
///
///  1. **Name collection** — all struct, enum, and function names are
///     gathered before any item is lowered, so forward references within the
///     same file resolve correctly.
///
///  2. **Type resolution** — `ast::TypeRef` values (which hold string-based
///     `TypeKind`) are resolved to `tyir::Ty` values; user-defined type names
///     are validated against the collected declarations.
///
///  3. **Type inference and checking** — every expression is annotated with
///     its inferred `Ty`; operator operand types, struct field types, function
///     argument types, and return types are validated.
///
///  4. **Name resolution** — `ast::PathExpr` nodes are resolved to either a
///     `tyir::LocalRef` (local binding / parameter), a `tyir::EnumVariantRef`
///     (`EnumName::Variant`), or the `fn_name` field of a `tyir::TyCall`.
///
/// ## Error handling
///
/// The pass returns `std::unexpected<LowerError>` on the first error
/// encountered.  `LowerError` contains a source span and a human-readable
/// diagnostic message.
///
/// ## Usage
///
/// ```cpp
/// cstc::symbol::SymbolSession session;
/// const auto ast = cstc::parser::parse_source(source);
/// if (!ast) { /* handle parse error */ }
///
/// const auto tyir = cstc::tyir_builder::lower_program(*ast);
/// if (!tyir) {
///     std::cerr << "lower error: " << tyir.error().message << "\n";
/// }
/// ```

#include <expected>
#include <string>

#include <cstc_ast/ast.hpp>
#include <cstc_span/span.hpp>
#include <cstc_tyir/tyir.hpp>

namespace cstc::tyir_builder {

/// Diagnostic emitted when lowering fails.
struct LowerError {
    /// Source location where the error was detected.
    cstc::span::SourceSpan span;
    /// Human-readable description of the error.
    std::string message;
};

/// Lowers an AST program to a fully type-annotated TyIR program.
///
/// Requires an active `SymbolSession` on the calling thread.
///
/// On success returns the typed program.  On failure returns the first
/// `LowerError` encountered.
[[nodiscard]] std::expected<tyir::TyProgram, LowerError> lower_program(const ast::Program& program);

} // namespace cstc::tyir_builder

#endif // CICEST_COMPILER_CSTC_TYIR_BUILDER_BUILDER_HPP
