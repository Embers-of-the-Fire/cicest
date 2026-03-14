#include <cstc_codegen/codegen.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir_builder/builder.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
# include <process.h>
#elif defined(__unix__) || defined(__APPLE__)
# include <spawn.h>
# include <sys/wait.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
extern char** environ;
#endif

namespace {

enum class EmitKind {
    Asm,
    Obj,
    Exe,
};

struct Options {
    std::string input_path;
    std::optional<std::string> output_stem;
    std::string module_name = "cicest_module";
    std::optional<std::string> linker;
    std::vector<EmitKind> emits;
    bool show_help = false;
};

[[nodiscard]] std::string usage() {
    return "Usage: cstc <input-file> [-o <output-stem>] [--module-name <module>] "
           "[--emit <asm|obj|exe|all>] [--linker <linker>]";
}

void add_emit_kind(std::vector<EmitKind>& emits, EmitKind emit_kind) {
    if (std::find(emits.begin(), emits.end(), emit_kind) == emits.end())
        emits.push_back(emit_kind);
}

void parse_and_add_emit(std::vector<EmitKind>& emits, std::string_view emit_value) {
    if (emit_value == "asm") {
        add_emit_kind(emits, EmitKind::Asm);
        return;
    }

    if (emit_value == "obj") {
        add_emit_kind(emits, EmitKind::Obj);
        return;
    }

    if (emit_value == "exe") {
        add_emit_kind(emits, EmitKind::Exe);
        return;
    }

    if (emit_value == "all") {
        add_emit_kind(emits, EmitKind::Asm);
        add_emit_kind(emits, EmitKind::Obj);
        add_emit_kind(emits, EmitKind::Exe);
        return;
    }

    throw std::runtime_error(
        "invalid --emit value: " + std::string(emit_value)
        + " (expected one of: asm, obj, exe, all)");
}

[[nodiscard]] bool has_emit(const Options& options, EmitKind emit_kind) {
    if (options.emits.empty()) {
        // Default output: final executable.
        return emit_kind == EmitKind::Exe;
    }

    return std::find(options.emits.begin(), options.emits.end(), emit_kind) != options.emits.end();
}

[[nodiscard]] Options parse_options(int argc, char** argv) {
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];

        if (arg == "-h" || arg == "--help") {
            options.show_help = true;
            continue;
        }

        if (arg == "-o" || arg == "--output") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --output\n" + usage());
            options.output_stem = std::string(argv[++index]);
            continue;
        }

        if (arg == "--module-name") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --module-name\n" + usage());
            options.module_name = argv[++index];
            continue;
        }

        if (arg == "--emit") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --emit\n" + usage());
            parse_and_add_emit(options.emits, argv[++index]);
            continue;
        }

        if (arg.starts_with("--emit=")) {
            parse_and_add_emit(options.emits, arg.substr(std::string_view("--emit=").size()));
            continue;
        }

        if (arg == "--linker") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --linker\n" + usage());
            options.linker = std::string(argv[++index]);
            continue;
        }

        if (!arg.empty() && arg.front() == '-')
            throw std::runtime_error("unknown option: " + std::string(arg) + "\n" + usage());

        if (!options.input_path.empty())
            throw std::runtime_error("multiple input files are not supported\n" + usage());
        options.input_path = std::string(arg);
    }

    if (options.show_help)
        return options;

    if (options.input_path.empty())
        throw std::runtime_error("missing input file path\n" + usage());

    if (options.linker.has_value() && !has_emit(options, EmitKind::Exe)) {
        throw std::runtime_error(
            "--linker requires executable output (`--emit exe` or default output)");
    }

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

[[nodiscard]] std::string format_parse_error(
    const cstc::span::SourceMap& source_map, const cstc::parser::ParseError& error) {
    if (const auto resolved = source_map.resolve_span(error.span); resolved.has_value()) {
        return "parse error " + std::string(resolved->file_name) + ":"
             + std::to_string(resolved->start.line) + ":" + std::to_string(resolved->start.column)
             + ": " + error.message;
    }

    return "parse error [" + std::to_string(error.span.start) + ", "
         + std::to_string(error.span.end) + "): " + error.message;
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

[[nodiscard]] std::filesystem::path resolve_output_stem(const Options& options) {
    if (options.output_stem.has_value())
        return std::filesystem::path(*options.output_stem);

    const std::filesystem::path input_path(options.input_path);
    const std::filesystem::path stem = input_path.stem();
    if (stem.empty())
        throw std::runtime_error(
            "failed to derive output stem from input file: " + input_path.string());

    return input_path.parent_path() / stem;
}

void ensure_parent_directory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty())
        return;

    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error)
        throw std::runtime_error(
            "failed to create output directory '" + parent.string() + "': " + error.message());
}

[[nodiscard]] std::string resolve_linker_program(const Options& options) {
    if (options.linker.has_value() && !options.linker->empty())
        return *options.linker;

    if (const char* cxx = std::getenv("CXX"); cxx != nullptr && cxx[0] != '\0')
        return cxx;

#if defined(_WIN32)
# if defined(__MINGW32__) || defined(__MINGW64__)
    return "c++";
# else
    return "clang++";
# endif
#else
    return "c++";
#endif
}

void link_object_to_executable(
    const std::filesystem::path& object_path, const std::filesystem::path& executable_path,
    const Options& options) {
    ensure_parent_directory(executable_path);

    std::string linker_program = resolve_linker_program(options);
    std::vector<std::string> arguments{
        linker_program,
        object_path.string(),
        "-o",
        executable_path.string(),
    };

#if defined(_WIN32)
    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments)
        argv.push_back(argument.c_str());
    argv.push_back(nullptr);

    const std::intptr_t spawn_result = _spawnvp(_P_WAIT, linker_program.c_str(), argv.data());
    if (spawn_result == -1) {
        throw std::runtime_error(
            "failed to start linker '" + linker_program + "': " + std::strerror(errno));
    }

    if (spawn_result != 0) {
        throw std::runtime_error(
            "linker '" + linker_program + "' failed with exit code "
            + std::to_string(spawn_result));
    }
#elif defined(__unix__) || defined(__APPLE__)

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (std::string& argument : arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);

    pid_t child_pid = 0;
    const int spawn_error =
        posix_spawnp(&child_pid, linker_program.c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawn_error != 0) {
        throw std::runtime_error(
            "failed to start linker '" + linker_program + "': " + std::strerror(spawn_error));
    }

    int status = 0;
    if (waitpid(child_pid, &status, 0) < 0) {
        throw std::runtime_error(
            "failed waiting for linker '" + linker_program + "': " + std::strerror(errno));
    }

    if (!WIFEXITED(status))
        throw std::runtime_error("linker '" + linker_program + "' terminated abnormally");

    const int exit_code = WEXITSTATUS(status);
    if (exit_code != 0) {
        throw std::runtime_error(
            "linker '" + linker_program + "' failed with exit code " + std::to_string(exit_code));
    }
#else
    throw std::runtime_error(
        "executable output is not supported on this platform; use `--emit asm` or `--emit obj`");
#endif
}

void compile_file(const Options& options) {
    const std::string source = read_source_file(options.input_path);

    cstc::symbol::SymbolSession session;
    cstc::span::SourceMap source_map;
    const cstc::span::SourceFileId file_id = source_map.add_file(options.input_path, source);

    const cstc::span::SourceFile* source_file = source_map.file(file_id);
    if (source_file == nullptr)
        throw std::runtime_error("invalid source file id while compiling");

    const auto parsed = cstc::parser::parse_source_at(source_file->source, source_file->start_pos);
    if (!parsed.has_value())
        throw std::runtime_error(format_parse_error(source_map, parsed.error()));

    const auto lowered = cstc::tyir_builder::lower_program(*parsed);
    if (!lowered.has_value())
        throw std::runtime_error(format_type_error(source_map, lowered.error()));

    const auto lir = cstc::lir_builder::lower_program(*lowered);

    const std::filesystem::path output_stem = resolve_output_stem(options);
    std::filesystem::path assembly_path = output_stem;
    assembly_path += ".s";
    std::filesystem::path object_path = output_stem;
    object_path += ".o";

    const bool emit_asm = has_emit(options, EmitKind::Asm);
    const bool emit_obj = has_emit(options, EmitKind::Obj);
    const bool emit_exe = has_emit(options, EmitKind::Exe);

    if (emit_asm) {
        cstc::codegen::emit_native_assembly(lir, assembly_path, options.module_name);
        std::cout << "emitted " << assembly_path.string() << '\n';
    }

    const bool needs_object_for_link = emit_exe;
    const bool produce_object = emit_obj || needs_object_for_link;
    if (produce_object) {
        cstc::codegen::emit_native_object(lir, object_path, options.module_name);
        if (emit_obj)
            std::cout << "emitted " << object_path.string() << '\n';
    }

    if (emit_exe) {
        const std::filesystem::path executable_path = output_stem;
        link_object_to_executable(object_path, executable_path, options);
        std::cout << "emitted " << executable_path.string() << '\n';

        if (!emit_obj) {
            std::error_code remove_error;
            std::filesystem::remove(object_path, remove_error);
            if (remove_error) {
                throw std::runtime_error(
                    "failed to remove intermediate object file '" + object_path.string()
                    + "': " + remove_error.message());
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        if (options.show_help) {
            std::cout << usage() << '\n';
            return 0;
        }

        compile_file(options);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
