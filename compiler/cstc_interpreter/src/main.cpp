#include <argparse/argparse.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <cstc_hir/printer.hpp>
#include <cstc_hir_builder/builder.hpp>
#include <cstc_hir_interpreter/interpreter.hpp>
#include <cstc_parser/parser.hpp>

namespace {

enum class Mode {
    Validate,
    Materialize,
    Run,
};

[[nodiscard]] std::string read_source_file(const std::filesystem::path& input_path) {
    std::ifstream input_file(input_path, std::ios::binary);
    if (!input_file)
        throw std::runtime_error("failed to open input file: " + input_path.string());

    return {
        std::istreambuf_iterator<char>(input_file),
        std::istreambuf_iterator<char>(),
    };
}

void write_output_file(const std::filesystem::path& output_path, const std::string& content) {
    std::ofstream output_file(output_path, std::ios::binary | std::ios::trunc);
    if (!output_file)
        throw std::runtime_error("failed to open output file: " + output_path.string());

    output_file << content;
    if (!output_file)
        throw std::runtime_error("failed to write output file: " + output_path.string());
}

[[nodiscard]] cstc::hir::Module lower_source_to_hir(std::string_view source) {
    cstc::ast::SymbolTable symbols;
    const auto parsed = cstc::parser::parse_source(source, symbols);
    if (!parsed.has_value()) {
        const auto& error = parsed.error();
        throw std::runtime_error(
            "parse error [" + std::to_string(error.span.start) + ", "
            + std::to_string(error.span.end) + "]: " + error.message);
    }

    return cstc::hir::builder::lower_ast_to_hir(parsed.value(), &symbols);
}

void print_diagnostics(const std::vector<cstc::hir::interpreter::Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        std::cerr << (diagnostic.severity == cstc::hir::interpreter::DiagnosticSeverity::Error ? "error"
                                                                                                 : "warning");
        if (!diagnostic.scope.empty())
            std::cerr << "[" << diagnostic.scope << "]";
        std::cerr << ": " << diagnostic.message << '\n';
    }
}

[[nodiscard]] Mode parse_mode(const std::string& mode_text) {
    if (mode_text == "validate")
        return Mode::Validate;
    if (mode_text == "materialize")
        return Mode::Materialize;
    return Mode::Run;
}

} // namespace

int main(int argc, char** argv) {
    argparse::ArgumentParser program{"cstc_interpreter"};
    program.add_description("File-based HIR interpreter CLI.");

    program.add_argument("input").help("Path to input .cst source file");
    program.add_argument("--mode")
        .help("Interpreter mode")
        .default_value(std::string{"run"})
        .choices(
            std::string{"validate"}, std::string{"materialize"}, std::string{"run"});
    program.add_argument("--entry")
        .help("Entry function for run mode")
        .default_value(std::string{"main"});
    program.add_argument("--emit-hir")
        .help("Output file for materialized HIR (materialize mode)")
        .default_value(std::string{});

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& error) {
        std::cerr << error.what() << '\n' << program;
        return 1;
    }

    try {
        const auto input_path = std::filesystem::path{program.get<std::string>("input")};
        const std::string mode_text = program.get<std::string>("--mode");
        const std::string entry = program.get<std::string>("--entry");
        const std::string emit_hir_path = program.get<std::string>("--emit-hir");

        const auto source = read_source_file(input_path);
        auto module = lower_source_to_hir(source);

        cstc::hir::interpreter::HirInterpreter interpreter{module};
        const Mode mode = parse_mode(mode_text);

        if (mode == Mode::Validate) {
            const auto validation = interpreter.validate();
            print_diagnostics(validation.diagnostics);
            return validation.ok ? 0 : 1;
        }

        if (mode == Mode::Materialize) {
            const auto materialized = interpreter.materialize_const_types();
            print_diagnostics(materialized.diagnostics);
            if (!materialized.ok)
                return 1;

            for (const auto& [name, value] : materialized.lifted_constants)
                std::cout << name << " = " << cstc::hir::interpreter::format_value(value) << '\n';

            if (!emit_hir_path.empty()) {
                write_output_file(
                    std::filesystem::path{emit_hir_path}, cstc::hir::format_hir(module));
            }

            return 0;
        }

        const auto materialized = interpreter.materialize_const_types();
        print_diagnostics(materialized.diagnostics);
        if (!materialized.ok)
            return 1;

        const auto execution = interpreter.run(cstc::hir::interpreter::RunOptions{
            .entry = entry,
            .mode = cstc::hir::interpreter::ExecutionMode::Runtime,
            .call_depth_limit = 256,
        });
        print_diagnostics(execution.diagnostics);
        if (!execution.ok)
            return 1;

        if (execution.value.has_value())
            std::cout << cstc::hir::interpreter::format_value(*execution.value) << '\n';
        return 0;
    } catch (const std::runtime_error& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
