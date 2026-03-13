#ifndef CICEST_COMPILER_CSTC_LIR_PRINTER_HPP
#define CICEST_COMPILER_CSTC_LIR_PRINTER_HPP

/// @file printer.hpp
/// @brief Human-readable CFG formatter for LIR programs.
///
/// Usage:
/// @code
///   cstc::symbol::SymbolSession session;
///   const auto lir = cstc::lir_builder::lower_program(tyir_program);
///   std::cout << cstc::lir::format_program(lir);
/// @endcode
///
/// The output shows each function as a sequence of numbered basic blocks,
/// with each block containing its assignments and its terminator.

#include <string>

#include <cstc_lir/lir.hpp>

namespace cstc::lir {

/// Renders a human-readable indented view of the LIR program.
///
/// Symbol ids are resolved through the current session's global symbol table.
[[nodiscard]] inline std::string format_program(const LirProgram& program);

} // namespace cstc::lir

#include <cstc_lir/printer_impl.hpp>

#endif // CICEST_COMPILER_CSTC_LIR_PRINTER_HPP
