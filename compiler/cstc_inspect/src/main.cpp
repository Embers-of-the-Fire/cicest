#include <cstc_ast/printer.hpp>
#include <cstc_cli_support/support.hpp>
#include <cstc_codegen/codegen.hpp>
#include <cstc_lexer/lexer.hpp>
#include <cstc_lexer/token.hpp>
#include <cstc_lir/printer.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/diagnostics.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/printer.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::string input_path;
    std::string out_type;
    std::optional<std::string> output_path;
    bool keep_trivia = false;
};

[[nodiscard]] std::string usage() {
    return "Usage: cstc_inspect <input-file> --out-type <tokens|ast|tyir|lir|llvm> "
           "[-o <output-file>] [--keep-trivia]";
}

[[nodiscard]] Options parse_options(int argc, char** argv) {
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];

        if (arg == "--out-type") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --out-type\n" + usage());
            options.out_type = argv[++index];
            continue;
        }

        if (arg == "-o" || arg == "--output") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --output\n" + usage());
            options.output_path = std::string(argv[++index]);
            continue;
        }

        if (arg == "--keep-trivia") {
            options.keep_trivia = true;
            continue;
        }

        if (!arg.empty() && arg.front() == '-')
            throw std::runtime_error("unknown option: " + std::string(arg) + "\n" + usage());

        if (!options.input_path.empty())
            throw std::runtime_error("multiple input files are not supported\n" + usage());
        options.input_path = std::string(arg);
    }

    if (options.input_path.empty())
        throw std::runtime_error("missing input file path\n" + usage());
    if (options.out_type != "tokens" && options.out_type != "ast" && options.out_type != "tyir"
        && options.out_type != "lir" && options.out_type != "llvm")
        throw std::runtime_error(
            "--out-type must be one of: tokens, ast, tyir, lir, llvm\n" + usage());

    return options;
}

void write_output(std::string_view text, const std::optional<std::string>& output_path) {
    if (!output_path.has_value()) {
        std::cout << text;
        return;
    }

    std::ofstream file(*output_path, std::ios::binary | std::ios::trunc);
    if (!file)
        throw std::runtime_error("failed to open output file: " + *output_path);
    file << text;
    if (!file)
        throw std::runtime_error("failed to write output file: " + *output_path);
}

[[nodiscard]] std::string escape_lexeme(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

[[nodiscard]] std::string render_tokens(
    const cstc::span::SourceMap& source_map, cstc::span::SourceFileId file_id, bool keep_trivia) {
    const cstc::span::SourceFile* source_file = source_map.file(file_id);
    if (source_file == nullptr)
        throw std::runtime_error("invalid source file id in render_tokens");

    const std::vector<cstc::lexer::Token> tokens =
        cstc::lexer::lex_source_at(source_file->source, source_file->start_pos, keep_trivia);

    std::ostringstream output;
    for (const cstc::lexer::Token& token : tokens) {
        output << cstc::lexer::token_kind_name(token.kind) << " [" << token.span.start << ", "
               << token.span.end << ")";

        if (const auto resolved = source_map.resolve_span(token.span); resolved.has_value()) {
            output << " @" << resolved->file_name << ":" << resolved->start.line << ":"
                   << resolved->start.column;
        }

        if (token.kind != cstc::lexer::TokenKind::EndOfFile)
            output << " `" << escape_lexeme(token.symbol.as_str()) << "`";
        output << '\n';
    }
    return output.str();
}

[[nodiscard]] std::string
    render_tyir(cstc::span::SourceMap& source_map, const std::filesystem::path& input_path) {
    const auto merged =
        cstc::cli_support::load_module_program(source_map, input_path, CICEST_STD_PATH);

    const auto tyir = cstc::cli_support::lower_and_fold_program(source_map, merged);
    if (!tyir.has_value())
        throw std::runtime_error(tyir.error());

    return cstc::tyir::format_program(*tyir);
}

[[nodiscard]] std::string
    render_lir(cstc::span::SourceMap& source_map, const std::filesystem::path& input_path) {
    const auto merged =
        cstc::cli_support::load_module_program(source_map, input_path, CICEST_STD_PATH);

    const auto tyir = cstc::cli_support::lower_and_fold_program(source_map, merged);
    if (!tyir.has_value())
        throw std::runtime_error(tyir.error());

    const auto lir = cstc::lir_builder::lower_program(*tyir);
    return cstc::lir::format_program(lir);
}

[[nodiscard]] std::string
    render_llvm(cstc::span::SourceMap& source_map, const std::filesystem::path& input_path) {
    const auto merged =
        cstc::cli_support::load_module_program(source_map, input_path, CICEST_STD_PATH);

    const auto tyir = cstc::cli_support::lower_and_fold_program(source_map, merged);
    if (!tyir.has_value())
        throw std::runtime_error(tyir.error());

    const auto lir = cstc::lir_builder::lower_program(*tyir);
    return cstc::codegen::emit_llvm_ir(lir);
}

[[nodiscard]] std::string
    render_ast(const cstc::span::SourceMap& source_map, cstc::span::SourceFileId file_id) {
    const cstc::span::SourceFile* source_file = source_map.file(file_id);
    if (source_file == nullptr)
        throw std::runtime_error("invalid source file id in render_ast");

    const auto parsed = cstc::parser::parse_source_at(source_file->source, source_file->start_pos);
    if (!parsed.has_value())
        throw std::runtime_error(
            cstc::parser::format_parse_error(source_map, parsed.error(), true));

    return cstc::ast::format_program(*parsed);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        cstc::symbol::SymbolSession session;
        cstc::span::SourceMap source_map;

        std::string output;
        if (options.out_type == "tokens" || options.out_type == "ast") {
            const std::string source = cstc::cli_support::read_source_file(options.input_path);
            const cstc::span::SourceFileId file_id =
                source_map.add_file(options.input_path, source);

            if (options.out_type == "tokens")
                output = render_tokens(source_map, file_id, options.keep_trivia);
            else
                output = render_ast(source_map, file_id);
        } else if (options.out_type == "tyir") {
            output = render_tyir(source_map, options.input_path);
        } else if (options.out_type == "llvm") {
            output = render_llvm(source_map, options.input_path);
        } else {
            output = render_lir(source_map, options.input_path);
        }

        write_output(output, options.output_path);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
