#include <cstc/linker_selection.hpp>
#include <cstc_cli_support/support.hpp>
#include <cstc_codegen/codegen.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/diagnostics.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_resource_path/resource_path.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
# include <process.h>
#elif defined(__unix__) || defined(__APPLE__)
# ifdef __APPLE__
#  include <crt_externs.h>
# else
#  include <unistd.h>
# endif
# include <spawn.h>
# include <sys/wait.h>
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

[[nodiscard]] std::filesystem::path resolve_output_stem(const Options& options) {
    if (options.output_stem.has_value())
        return {*options.output_stem};

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

[[nodiscard]] bool path_exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

[[nodiscard]] std::vector<std::string> executable_name_candidates(std::string_view program) {
#ifdef _WIN32
    const std::filesystem::path path(program);
    if (path.has_extension())
        return {std::string(program)};

    std::vector<std::string> candidates;
    const char* pathext = std::getenv("PATHEXT");
    std::string_view extensions = ".COM;.EXE;.BAT;.CMD";
    if (pathext != nullptr && pathext[0] != '\0')
        extensions = pathext;

    std::size_t start = 0;
    while (start <= extensions.size()) {
        const std::size_t end = extensions.find(';', start);
        const std::string_view extension = extensions.substr(start, end - start);
        if (!extension.empty())
            candidates.push_back(std::string(program) + std::string(extension));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    candidates.push_back(std::string(program));
    return candidates;
#else
    return {std::string(program)};
#endif
}

[[nodiscard]] bool is_program_available(std::string_view program) {
    if (program.empty())
        return false;

    for (const std::string& candidate : executable_name_candidates(program)) {
        const std::filesystem::path candidate_path(candidate);
        if (candidate_path.is_absolute() || candidate_path.has_parent_path()) {
            if (path_exists(candidate_path))
                return true;
        }
    }

    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr || path_env[0] == '\0')
        return false;

#ifdef _WIN32
    constexpr char path_separator = ';';
#else
    constexpr char path_separator = ':';
#endif

    std::string_view path_entries = path_env;
    std::size_t start = 0;
    while (start <= path_entries.size()) {
        const std::size_t end = path_entries.find(path_separator, start);
        const std::string_view entry = path_entries.substr(start, end - start);
        const std::filesystem::path directory =
            entry.empty() ? std::filesystem::current_path() : std::filesystem::path(entry);

        for (const std::string& candidate : executable_name_candidates(program)) {
            if (path_exists(directory / candidate))
                return true;
        }

        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    return false;
}

[[nodiscard]] std::string
    find_available_program(std::initializer_list<std::string_view> candidates) {
    for (const std::string_view candidate : candidates) {
        if (is_program_available(candidate))
            return std::string(candidate);
    }

    return "";
}

[[nodiscard]] std::string resolve_linker_program(const Options& options) {
    if (options.linker.has_value() && !options.linker->empty())
        return *options.linker;

    if (const char* cxx = std::getenv("CXX"); cxx != nullptr && cxx[0] != '\0')
        return cxx;

    const cstc::cli::LinkerFlavor flavor = cstc::cli::host_linker_flavor();
    for (const std::string_view candidate : cstc::cli::linker_candidates(flavor)) {
        if (std::string linker = find_available_program({candidate}); !linker.empty())
            return linker;
    }

    return std::string(cstc::cli::fallback_linker_program(flavor));
}

#if defined(__unix__) || defined(__APPLE__)
[[nodiscard]] char** process_environment() {
# ifdef __APPLE__
    return *_NSGetEnviron();
# else
    return ::environ;
# endif
}
#endif

void link_object_to_executable(
    const std::filesystem::path& object_path, const std::filesystem::path& executable_path,
    const Options& options) {
    ensure_parent_directory(executable_path);

    std::string linker_program = resolve_linker_program(options);
    std::vector<std::string> arguments{
        linker_program,
        object_path.string(),
        cstc::resource_path::resolve_rt_path(CICEST_RT_PATH).string(),
        "-o",
        executable_path.string(),
    };

#ifdef _WIN32
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
    const int spawn_error = posix_spawnp(
        &child_pid, linker_program.c_str(), nullptr, nullptr, argv.data(), process_environment());
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
    cstc::symbol::SymbolSession session;
    cstc::span::SourceMap source_map;
    const cstc::ast::Program merged =
        cstc::cli_support::load_module_program(source_map, options.input_path, CICEST_STD_PATH);

    const auto tyir = cstc::cli_support::lower_and_fold_program(source_map, merged);
    if (!tyir.has_value())
        throw std::runtime_error(tyir.error());

    const auto lir = cstc::lir_builder::lower_program(*tyir);

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
        const std::filesystem::path& executable_path = output_stem;
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
