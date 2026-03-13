#ifndef CICEST_COMPILER_CSTC_TYIR_PRINTER_HPP
#define CICEST_COMPILER_CSTC_TYIR_PRINTER_HPP

/// @file printer.hpp
/// @brief Human-readable tree formatter for TyIR programs.
///
/// Usage:
/// @code
///   cstc::symbol::SymbolSession session;
///   const auto result = cstc::lower::lower_program(ast_program);
///   if (result) {
///       std::cout << cstc::tyir::format_program(*result);
///   }
/// @endcode
///
/// The output format mirrors the AST printer but annotates every expression
/// and block node with its resolved `Ty` (e.g. `TyBinary(+): num`).

#include <string>

#include <cstc_tyir/tyir.hpp>

namespace cstc::tyir {

/// Renders a human-readable indented tree view of the typed program.
///
/// Symbol ids are resolved through the current session's global symbol table.
[[nodiscard]] inline std::string format_program(const TyProgram& program);

} // namespace cstc::tyir

#include <cstc_tyir/printer_impl.hpp>

#endif // CICEST_COMPILER_CSTC_TYIR_PRINTER_HPP
