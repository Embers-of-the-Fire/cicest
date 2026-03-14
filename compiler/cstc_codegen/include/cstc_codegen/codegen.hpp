#ifndef CICEST_COMPILER_CSTC_CODEGEN_CODEGEN_HPP
#define CICEST_COMPILER_CSTC_CODEGEN_CODEGEN_HPP

/// @file codegen.hpp
/// @brief Public API for LLVM IR code generation from LIR.
///
/// This header intentionally does NOT include any LLVM headers.
/// All LLVM usage is confined to the `codegen.cpp` translation unit.
///
/// `cstc_codegen` is the backend bridge stage:
///
///   TyIR → LIR → LLVM IR text
///
/// The input is expected to be fully validated LIR. The generated textual IR is
/// primarily used for inspection/testing today and can also serve as input to
/// later machine-code pipelines.

#include <string>
#include <string_view>

#include <cstc_lir/lir.hpp>

namespace cstc::codegen {

/// Emits LLVM IR text for the given LIR program.
///
/// The module name defaults to "cicest_module".
/// Requires an active `cstc::symbol::SymbolSession` on the calling thread.
///
/// Behavior summary:
/// - Lowers struct/enum declarations and function definitions into an LLVM module.
/// - Applies local-to-SSA promotion (mem2reg-style pass) before printing.
/// - Returns the final textual module (`llvm::Module::print` output).
///
/// @param program Fully lowered, type-correct LIR program.
/// @return LLVM IR module text.
[[nodiscard]] std::string emit_llvm_ir(const lir::LirProgram& program);

/// Emits LLVM IR text for the given LIR program with a custom module name.
///
/// @param program Fully lowered, type-correct LIR program.
/// @param module_name Name assigned to the emitted LLVM module.
/// @return LLVM IR module text.
[[nodiscard]] std::string emit_llvm_ir(const lir::LirProgram& program,
                                       std::string_view module_name);

} // namespace cstc::codegen

#endif // CICEST_COMPILER_CSTC_CODEGEN_CODEGEN_HPP
