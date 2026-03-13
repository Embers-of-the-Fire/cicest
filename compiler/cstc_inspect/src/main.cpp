#include <cstc_ast/printer.hpp>
#include <cstc_lexer/lexer.hpp>
#include <cstc_lexer/token.hpp>
#include <cstc_tyir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/printer.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
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
    return "Usage: cstc_inspect <input-file> --out-type <tokens|ast|tyir> "
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
    if (options.out_type != "tokens" && options.out_type != "ast" && options.out_type != "tyir")
        throw std::runtime_error("--out-type must be one of: tokens, ast, tyir\n" + usage());

    return options;
}

[[nodiscard]] std::string read_source_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("failed to open input file: " + path.string());

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof())
        throw std::runtime_error("failed while reading input file: " + path.string());
    return buffer.str();
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
    render_tyir(const cstc::span::SourceMap& source_map, cstc::span::SourceFileId file_id) {
    const cstc::span::SourceFile* source_file = source_map.file(file_id);
    if (source_file == nullptr)
        throw std::runtime_error("invalid source file id in render_tyir");

    const auto parsed = cstc::parser::parse_source_at(source_file->source, source_file->start_pos);
    if (!parsed.has_value()) {
        const cstc::parser::ParseError& error = parsed.error();
        if (const auto resolved = source_map.resolve_span(error.span); resolved.has_value()) {
            throw std::runtime_error(
                "parse error " + std::string(resolved->file_name) + ":"
                + std::to_string(resolved->start.line) + ":"
                + std::to_string(resolved->start.column) + ": " + error.message);
        }
        throw std::runtime_error(
            "parse error [" + std::to_string(error.span.start) + ", "
            + std::to_string(error.span.end) + "): " + error.message);
    }

    const auto lowered = cstc::tyir_builder::lower_program(*parsed);
    if (!lowered.has_value()) {
        const cstc::tyir_builder::LowerError& error = lowered.error();
        if (const auto resolved = source_map.resolve_span(error.span); resolved.has_value()) {
            throw std::runtime_error(
                "type error " + std::string(resolved->file_name) + ":"
                + std::to_string(resolved->start.line) + ":"
                + std::to_string(resolved->start.column) + ": " + error.message);
        }
        throw std::runtime_error("type error: " + error.message);
    }

    return cstc::tyir::format_program(*lowered);
}

[[nodiscard]] std::string
    render_ast(const cstc::span::SourceMap& source_map, cstc::span::SourceFileId file_id) {
    const cstc::span::SourceFile* source_file = source_map.file(file_id);
    if (source_file == nullptr)
        throw std::runtime_error("invalid source file id in render_ast");

    const auto parsed = cstc::parser::parse_source_at(source_file->source, source_file->start_pos);
    if (!parsed.has_value()) {
        const cstc::parser::ParseError& error = parsed.error();

        if (const auto resolved = source_map.resolve_span(error.span); resolved.has_value()) {
            throw std::runtime_error(
                "parse error " + std::string(resolved->file_name) + ":"
                + std::to_string(resolved->start.line) + ":"
                + std::to_string(resolved->start.column) + " [" + std::to_string(error.span.start)
                + ", " + std::to_string(error.span.end) + "): " + error.message);
        }

        throw std::runtime_error(
            "parse error [" + std::to_string(error.span.start) + ", "
            + std::to_string(error.span.end) + "): " + error.message);
    }

    return cstc::ast::format_program(*parsed);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const std::string source = read_source_file(options.input_path);

        cstc::symbol::SymbolSession session;
        cstc::span::SourceMap source_map;
        const cstc::span::SourceFileId file_id = source_map.add_file(options.input_path, source);

        std::string output;
        if (options.out_type == "tokens")
            output = render_tokens(source_map, file_id, options.keep_trivia);
        else if (options.out_type == "ast")
            output = render_ast(source_map, file_id);
        else
            output = render_tyir(source_map, file_id);

        write_output(output, options.output_path);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
