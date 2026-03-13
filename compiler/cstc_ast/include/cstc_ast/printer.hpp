#ifndef CICEST_COMPILER_CSTC_AST_PRINTER_HPP
#define CICEST_COMPILER_CSTC_AST_PRINTER_HPP

#include <string>

#include <cstc_ast/ast.hpp>

namespace cstc::ast {

/// Renders a human-readable tree view of the AST program.
///
/// Symbol ids are resolved through the current session's global symbol table.
[[nodiscard]] inline std::string format_program(const Program& program);

} // namespace cstc::ast

#include <cstc_ast/printer_impl.hpp>

#endif // CICEST_COMPILER_CSTC_AST_PRINTER_HPP
