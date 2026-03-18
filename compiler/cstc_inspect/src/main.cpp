#include <cstc_ast/printer.hpp>
#include <cstc_codegen/codegen.hpp>
#include <cstc_lexer/lexer.hpp>
#include <cstc_lexer/token.hpp>
#include <cstc_lir/printer.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/printer.hpp>
#include <cstc_tyir_builder/builder.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#endif

#if defined(__APPLE__)
# include <mach-o/dyld.h>
#endif

namespace {

// ─── Resource path resolution ────────────────────────────────────────────────

[[nodiscard]] std::filesystem::path normalize_existing_path(const std::filesystem::path& path) {
    std::error_code ec;
    const auto normalized = std::filesystem::weakly_canonical(path, ec);
    if (!ec)
        return normalized;

    return path.lexically_normal();
}

#if defined(__unix__) && !defined(__APPLE__)
[[nodiscard]] std::filesystem::path procfs_self_exe_dir() {
# if defined(__sun)
    constexpr const char* proc_candidates[] = {
        "/proc/self/path/a.out",
        "/proc/self/exe",
        "/proc/curproc/file",
        "/proc/curproc/exe",
    };
# else
    constexpr const char* proc_candidates[] = {
        "/proc/self/exe",
        "/proc/curproc/file",
        "/proc/curproc/exe",
    };
# endif

    for (const char* candidate : proc_candidates) {
        std::error_code ec;
        const auto target = std::filesystem::read_symlink(candidate, ec);
        if (ec || target.empty())
            continue;

        const auto link_path = std::filesystem::path(candidate);
        const auto exe_path = target.is_absolute() ? target : link_path.parent_path() / target;
        return normalize_existing_path(exe_path).parent_path();
    }

    return {};
}
#endif

/// Returns the directory containing the currently running binary.
///
/// Uses platform APIs on Windows/macOS and procfs symlinks on supported Unix
/// variants so installed layouts remain relocatable across those targets.
[[nodiscard]] std::filesystem::path self_exe_dir() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};

        if (length < buffer.size())
            return normalize_existing_path(
                       std::filesystem::path(std::wstring(buffer.data(), length)))
                .parent_path();

        if (buffer.size() >= 32768)
            return {};

        std::size_t next_size = buffer.size() * 2;
        if (next_size > 32768)
            next_size = 32768;
        buffer.resize(next_size);
    }
#elif defined(__APPLE__)
    std::vector<char> buffer(4096);
    while (true) {
        uint32_t size = static_cast<uint32_t>(buffer.size());
        if (_NSGetExecutablePath(buffer.data(), &size) == 0)
            return normalize_existing_path(std::filesystem::path(buffer.data())).parent_path();

        if (size <= buffer.size())
            return {};

        buffer.resize(size);
    }
#elif defined(__unix__)
    return procfs_self_exe_dir();
#endif

    return {};
}

/// Returns the path to the std library directory.
[[nodiscard]] std::filesystem::path resolve_std_dir() {
    const auto bin_dir = self_exe_dir();
    if (!bin_dir.empty()) {
        auto installed = bin_dir / ".." / "share" / "cicest" / "std";
        std::error_code ec;
        if (std::filesystem::exists(installed / "prelude.cst", ec))
            return std::filesystem::canonical(installed);
    }
    return std::filesystem::path(CICEST_STD_PATH);
}

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

[[nodiscard]] std::string format_type_error(
    const cstc::span::SourceMap& source_map, const cstc::tyir_builder::LowerError& error) {
    if (const auto resolved = source_map.resolve_span(error.span); resolved.has_value()) {
        return "type error " + std::string(resolved->file_name) + ":"
             + std::to_string(resolved->start.line) + ":" + std::to_string(resolved->start.column)
             + ": " + error.message;
    }

    return "type error: " + error.message;
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

/// Parses the user source and merges it with the std prelude AST.
///
/// The prelude is loaded from `CICEST_STD_PATH/prelude.cst`, added to the
/// source map as a separate file, parsed, and its items are prepended to the
/// user program items.
[[nodiscard]] cstc::ast::Program
    parse_with_prelude(cstc::span::SourceMap& source_map, cstc::span::SourceFileId user_file_id) {
    const std::filesystem::path prelude_path = resolve_std_dir() / "prelude.cst";

    // Skip prelude injection when inspecting the prelude itself to avoid
    // merging every declaration twice.
    const cstc::span::SourceFile* user_source = source_map.file(user_file_id);
    if (user_source == nullptr)
        throw std::runtime_error("invalid source file id");

    const bool inject_prelude = [&] {
        std::error_code ec;
        return !std::filesystem::equivalent(user_source->name, prelude_path, ec);
    }();

    // Parse prelude
    cstc::ast::Program prelude_program;
    if (inject_prelude) {
        const std::string prelude_source = read_source_file(prelude_path);
        const cstc::span::SourceFileId prelude_file_id =
            source_map.add_file(prelude_path.string(), prelude_source);
        const cstc::span::SourceFile* prelude_file = source_map.file(prelude_file_id);
        if (prelude_file == nullptr)
            throw std::runtime_error("invalid source file id for std prelude");

        const auto prelude_parsed =
            cstc::parser::parse_source_at(prelude_file->source, prelude_file->start_pos);
        if (!prelude_parsed.has_value()) {
            const cstc::parser::ParseError& error = prelude_parsed.error();
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

        prelude_program = *prelude_parsed;
    }

    // Parse user source
    const cstc::span::SourceFile* source_file = source_map.file(user_file_id);
    if (source_file == nullptr)
        throw std::runtime_error("invalid source file id");

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

    // Merge
    cstc::ast::Program merged;
    merged.items.reserve(prelude_program.items.size() + parsed->items.size());
    for (const auto& item : prelude_program.items)
        merged.items.push_back(item);
    for (const auto& item : parsed->items)
        merged.items.push_back(item);
    return merged;
}

[[nodiscard]] std::string
    render_tyir(cstc::span::SourceMap& source_map, cstc::span::SourceFileId file_id) {
    const auto merged = parse_with_prelude(source_map, file_id);

    const auto lowered = cstc::tyir_builder::lower_program(merged);
    if (!lowered.has_value())
        throw std::runtime_error(format_type_error(source_map, lowered.error()));

    return cstc::tyir::format_program(*lowered);
}

[[nodiscard]] std::string
    render_lir(cstc::span::SourceMap& source_map, cstc::span::SourceFileId file_id) {
    const auto merged = parse_with_prelude(source_map, file_id);

    const auto lowered = cstc::tyir_builder::lower_program(merged);
    if (!lowered.has_value())
        throw std::runtime_error(format_type_error(source_map, lowered.error()));

    const auto lir = cstc::lir_builder::lower_program(*lowered);
    return cstc::lir::format_program(lir);
}

[[nodiscard]] std::string
    render_llvm(cstc::span::SourceMap& source_map, cstc::span::SourceFileId file_id) {
    const auto merged = parse_with_prelude(source_map, file_id);

    const auto lowered = cstc::tyir_builder::lower_program(merged);
    if (!lowered.has_value())
        throw std::runtime_error(format_type_error(source_map, lowered.error()));

    const auto lir = cstc::lir_builder::lower_program(*lowered);
    return cstc::codegen::emit_llvm_ir(lir);
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
        else if (options.out_type == "tyir")
            output = render_tyir(source_map, file_id);
        else if (options.out_type == "llvm")
            output = render_llvm(source_map, file_id);
        else
            output = render_lir(source_map, file_id);

        write_output(output, options.output_path);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
