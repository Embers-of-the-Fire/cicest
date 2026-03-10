#include <argparse/argparse.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <cstc_ast/printer.hpp>
#include <cstc_hir/printer.hpp>
#include <cstc_hir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>

namespace {

enum class OutType {
    Ast,
    Hir,
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

[[nodiscard]] std::string render_ast_or_hir(std::string_view source, OutType out_type) {
    cstc::ast::SymbolTable symbols;
    const auto parsed = cstc::parser::parse_source(source, symbols);
    if (!parsed.has_value()) {
        const auto& error = parsed.error();
        throw std::runtime_error(
            "parse error [" + std::to_string(error.span.start) + ", "
            + std::to_string(error.span.end) + "]: " + error.message);
    }

    if (out_type == OutType::Ast)
        return cstc::ast::format_ast(parsed.value(), &symbols);

    const auto module = cstc::hir::builder::lower_ast_to_hir(parsed.value(), &symbols);
    return cstc::hir::format_hir(module);
}

} // namespace

int main(int argc, char** argv) {
    argparse::ArgumentParser program{"cstc_inspect"};
    program.add_description("Inspect Cicest source by emitting AST or HIR.");

    program.add_argument("input").help("Path to input .cst source file");
    program.add_argument("--out-type")
        .help("Output representation type")
        .required()
        .choices(std::string{"ast"}, std::string{"hir"});
    program.add_argument("-o", "--output").help("Path to output file").required();

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& error) {
        std::cerr << error.what() << '\n' << program;
        return 1;
    }

    try {
        const auto input_path = std::filesystem::path{program.get<std::string>("input")};
        const auto output_path = std::filesystem::path{program.get<std::string>("--output")};
        const auto out_type_value = program.get<std::string>("--out-type");
        const auto out_type = out_type_value == "ast" ? OutType::Ast : OutType::Hir;

        const auto source = read_source_file(input_path);
        const auto rendered = render_ast_or_hir(source, out_type);
        write_output_file(output_path, rendered);
    } catch (const std::runtime_error& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
