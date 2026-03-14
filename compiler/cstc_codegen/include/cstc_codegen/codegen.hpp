#ifndef CICEST_COMPILER_CSTC_CODEGEN_CODEGEN_HPP
#define CICEST_COMPILER_CSTC_CODEGEN_CODEGEN_HPP

/// @file codegen.hpp
/// @brief Public API for LLVM/native code generation from LIR.
///
/// This header intentionally does NOT include any LLVM headers.
/// All LLVM usage is confined to the `codegen.cpp` translation unit.
///
/// `cstc_codegen` is the backend bridge stage:
///
///   TyIR → LIR → LLVM IR / native artifacts
///
/// The input is expected to be fully validated LIR.

#include <filesystem>
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
[[nodiscard]] std::string
    emit_llvm_ir(const lir::LirProgram& program, std::string_view module_name);

/// Emits native assembly (`.s`) for the given LIR program using the host
/// target.
///
/// The module name defaults to "cicest_module".
void emit_native_assembly(
    const lir::LirProgram& program, const std::filesystem::path& assembly_output_path);

/// Emits native assembly (`.s`) with a custom module name.
void emit_native_assembly(
    const lir::LirProgram& program, const std::filesystem::path& assembly_output_path,
    std::string_view module_name);

/// Emits a native object (`.o`) for the given LIR program using the host
/// target.
///
/// The module name defaults to "cicest_module".
void emit_native_object(
    const lir::LirProgram& program, const std::filesystem::path& object_output_path);

/// Emits a native object (`.o`) with a custom module name.
void emit_native_object(
    const lir::LirProgram& program, const std::filesystem::path& object_output_path,
    std::string_view module_name);

/// Emits native assembly (`.s`) and object (`.o`) artifacts for the given LIR
/// program using the host target.
///
/// The module name defaults to "cicest_module".
/// Requires an active `cstc::symbol::SymbolSession` on the calling thread.
///
/// @param program Fully lowered, type-correct LIR program.
/// @param assembly_output_path Path of the assembly artifact to write.
/// @param object_output_path Path of the object artifact to write.
void emit_native_artifacts(
    const lir::LirProgram& program, const std::filesystem::path& assembly_output_path,
    const std::filesystem::path& object_output_path);

/// Emits native assembly (`.s`) and object (`.o`) artifacts with a custom
/// module name.
///
/// @param program Fully lowered, type-correct LIR program.
/// @param assembly_output_path Path of the assembly artifact to write.
/// @param object_output_path Path of the object artifact to write.
/// @param module_name Name assigned to the emitted LLVM module.
void emit_native_artifacts(
    const lir::LirProgram& program, const std::filesystem::path& assembly_output_path,
    const std::filesystem::path& object_output_path, std::string_view module_name);

} // namespace cstc::codegen

#endif // CICEST_COMPILER_CSTC_CODEGEN_CODEGEN_HPP
