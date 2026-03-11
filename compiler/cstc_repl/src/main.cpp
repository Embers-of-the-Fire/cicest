#include <argparse/argparse.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <cstc_hir_builder/builder.hpp>
#include <cstc_hir_interpreter/interpreter.hpp>
#include <cstc_parser/parser.hpp>

namespace {

[[nodiscard]] std::string read_source_file(const std::filesystem::path& input_path) {
    std::ifstream input_file(input_path, std::ios::binary);
    if (!input_file)
        throw std::runtime_error("failed to open input file: " + input_path.string());

    return {
        std::istreambuf_iterator<char>(input_file),
        std::istreambuf_iterator<char>(),
    };
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

int run_repl(cstc::hir::interpreter::HirInterpreter& interpreter, const std::string& entry) {
    std::cout << "Cicest REPL ready. Commands: :run, :quit\n";

    for (;;) {
        std::cout << ">>> ";
        std::string line;
        if (!std::getline(std::cin, line))
            break;

        if (line == ":quit" || line == ":exit")
            break;

        if (line == ":run") {
            const auto execution = interpreter.run(cstc::hir::interpreter::RunOptions{
                .entry = entry,
                .mode = cstc::hir::interpreter::ExecutionMode::Runtime,
                .call_depth_limit = 256,
            });
            print_diagnostics(execution.diagnostics);
            if (execution.ok && execution.value.has_value())
                std::cout << cstc::hir::interpreter::format_value(*execution.value) << '\n';
            continue;
        }

        const auto evaluation = interpreter.eval_repl_line(line);
        print_diagnostics(evaluation.diagnostics);
        if (evaluation.ok && evaluation.value.has_value())
            std::cout << cstc::hir::interpreter::format_value(*evaluation.value) << '\n';
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    argparse::ArgumentParser program{"cstc_repl"};
    program.add_description("Interactive Cicest REPL powered by HIR interpreter.");

    program.add_argument("--input").help("Optional .cst file to preload declarations").default_value(
        std::string{});
    program.add_argument("--entry")
        .help("Entry function used by :run")
        .default_value(std::string{"main"});

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& error) {
        std::cerr << error.what() << '\n' << program;
        return 1;
    }

    try {
        const std::string input_path = program.get<std::string>("--input");
        const std::string entry = program.get<std::string>("--entry");

        cstc::hir::Module module;
        if (input_path.empty()) {
            module = lower_source_to_hir("");
        } else {
            const auto source = read_source_file(std::filesystem::path{input_path});
            module = lower_source_to_hir(source);
        }

        cstc::hir::interpreter::HirInterpreter interpreter{module};
        const auto materialized = interpreter.materialize_const_types();
        print_diagnostics(materialized.diagnostics);
        if (!materialized.ok)
            return 1;

        return run_repl(interpreter, entry);
    } catch (const std::runtime_error& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
