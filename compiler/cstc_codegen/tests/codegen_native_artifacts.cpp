/// @file codegen_native_artifacts.cpp
/// @brief LLVM native artifact emission test: `.s` and `.o` generation.

#include <cassert>
#include <filesystem>
#include <string>
#include <system_error>

#include <cstc_codegen/codegen.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir_builder/builder.hpp>

using namespace cstc::symbol;

static cstc::lir::LirProgram must_lower_to_lir(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    return cstc::lir_builder::lower_program(*tyir);
}

int main() {
    SymbolSession session;

    const cstc::lir::LirProgram lir = must_lower_to_lir("fn main() -> num { 1 + 2 }");

    const std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / "cstc_codegen_native_artifacts_test";
    std::filesystem::create_directories(temp_dir);

    const std::filesystem::path output_stem = temp_dir / "program";
    const std::filesystem::path assembly_path = output_stem.string() + ".s";
    const std::filesystem::path object_path = output_stem.string() + ".o";

    cstc::codegen::emit_native_assembly(lir, assembly_path, "native_artifacts_test");
    cstc::codegen::emit_native_object(lir, object_path, "native_artifacts_test");

    assert(std::filesystem::exists(assembly_path));
    assert(std::filesystem::exists(object_path));
    assert(std::filesystem::is_regular_file(assembly_path));
    assert(std::filesystem::is_regular_file(object_path));
    assert(std::filesystem::file_size(assembly_path) > 0);
    assert(std::filesystem::file_size(object_path) > 0);

    std::error_code cleanup_error;
    std::filesystem::remove(assembly_path, cleanup_error);
    std::filesystem::remove(object_path, cleanup_error);
    std::filesystem::remove(temp_dir, cleanup_error);

    return 0;
}
