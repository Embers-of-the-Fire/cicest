#include <cstc_repl/repl.hpp>

#include <cstc/linker_selection.hpp>
#include <cstc_cli_support/support.hpp>
#include <cstc_codegen/codegen.hpp>
#include <cstc_lexer/lexer.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_module/module.hpp>
#include <cstc_resource_path/resource_path.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>
#include <cstc_tyir_builder/builder.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
# include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
# ifdef __APPLE__
#  include <crt_externs.h>
# endif
# include <fcntl.h>
# include <spawn.h>
# include <sys/wait.h>
# include <unistd.h>
#endif

namespace cstc::repl {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kReservedPrefix = "__cstc_repl_internal_";
constexpr std::string_view kGeneratedMainName = "main";
constexpr std::string_view kBodyProbeName = "__cstc_repl_internal_probe";
constexpr std::string_view kProbeValueName = "__cstc_repl_internal_probe_value";
constexpr std::string_view kPrintNumName = "__cstc_repl_internal_print_num";
constexpr std::string_view kPrintStrName = "__cstc_repl_internal_print_str";
constexpr std::string_view kPrintRefStrName = "__cstc_repl_internal_print_ref_str";
constexpr std::string_view kPrintBoolName = "__cstc_repl_internal_print_bool";
constexpr std::string_view kPrintUnitName = "__cstc_repl_internal_print_unit";
constexpr std::string_view kSessionFilePrefix = ".cstc_repl_session_";
constexpr std::string_view kSessionFileSuffix = ".cst";

constexpr std::string_view kRuntimeHelpers = R"cst(fn __cstc_repl_internal_print_num(value: num) {
    let rendered: str = to_str(value);
    println(&rendered);
}

fn __cstc_repl_internal_print_str(value: str) {
    println(&value);
}

fn __cstc_repl_internal_print_ref_str(value: &str) {
    println(value);
}

fn __cstc_repl_internal_print_bool(value: bool) {
    if value {
        println("true");
    } else {
        println("false");
    }
}

fn __cstc_repl_internal_print_unit(value: Unit) {
    println("()");
}
)cst";

enum class DisplayKind {
    None,
    Num,
    Str,
    RefStr,
    Bool,
    Unit,
    Unsupported,
    Diverges,
};

struct CommandResult {
    int exit_code = -1;
    bool completed_normally = false;
    bool invocation_failed = false;
    std::string stdout_output;
    std::string stderr_output;
    std::string status_message;

    [[nodiscard]] bool succeeded() const { return completed_normally && exit_code == 0; }
};

struct ParsedBodySnippet {
    std::vector<std::string> statement_snippets;
    std::vector<std::string> persisted_let_snippets;
    std::optional<std::string> tail_expr_snippet;
};

[[nodiscard]] SubmissionResult make_result(
    SubmissionStatus status, bool state_changed = false, bool executed = false,
    std::string stdout_output = {}, std::string stderr_output = {}, std::string info_message = {},
    std::string error_message = {}) {
    SubmissionResult result;
    result.status = status;
    result.state_changed = state_changed;
    result.executed = executed;
    result.stdout_output = std::move(stdout_output);
    result.stderr_output = std::move(stderr_output);
    result.info_message = std::move(info_message);
    result.error_message = std::move(error_message);
    return result;
}

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view prefix)
        : path_(create(prefix)) {}

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    TemporaryDirectory(TemporaryDirectory&& other) noexcept
        : path_(std::move(other.path_)) {
        other.path_.clear();
    }

    TemporaryDirectory& operator=(TemporaryDirectory&& other) noexcept {
        if (this == &other)
            return *this;

        cleanup();
        path_ = std::move(other.path_);
        other.path_.clear();
        return *this;
    }

    ~TemporaryDirectory() { cleanup(); }

    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    static fs::path create(std::string_view prefix) {
        const fs::path base = fs::temp_directory_path();
        std::random_device device;
        std::mt19937_64 generator(device());
        std::uniform_int_distribution<unsigned long long> distribution;

        for (int attempt = 0; attempt < 64; ++attempt) {
            const fs::path candidate =
                base / (std::string(prefix) + "-" + std::to_string(distribution(generator)));
            std::error_code error;
            const bool created = fs::create_directory(candidate, error);
            if (created)
                return candidate;

            if (error && error != std::errc::file_exists) {
                throw std::runtime_error(
                    "failed to create temporary directory '" + candidate.string()
                    + "': " + error.message());
            }
        }

        throw std::runtime_error("failed to allocate a unique temporary directory");
    }

    void cleanup() {
        if (path_.empty())
            return;

        std::error_code error;
        fs::remove_all(path_, error);
        path_.clear();
    }

    fs::path path_;
};

[[nodiscard]] std::string_view trim_view(std::string_view input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0)
        ++start;

    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0)
        --end;

    return input.substr(start, end - start);
}

[[nodiscard]] std::string trim_copy(std::string_view input) {
    return std::string(trim_view(input));
}

// Parse $CXX as an argv-style command so wrapper programs and extra flags survive execution.
[[nodiscard]] std::optional<std::vector<std::string>>
    split_command_arguments(std::string_view command) {
    enum class QuoteMode {
        None,
        Single,
        Double,
    };

    std::vector<std::string> arguments;
    std::string current;
    bool token_started = false;
    QuoteMode quote_mode = QuoteMode::None;

    for (std::size_t index = 0; index < command.size(); ++index) {
        const char ch = command[index];

        if (quote_mode == QuoteMode::Single) {
            if (ch == '\'') {
                quote_mode = QuoteMode::None;
                continue;
            }

            current += ch;
            continue;
        }

        if (quote_mode == QuoteMode::Double) {
            if (ch == '"') {
                quote_mode = QuoteMode::None;
                continue;
            }

            if (ch == '\\' && index + 1 < command.size()) {
                const char next = command[index + 1];
                if (next == '"' || next == '\\') {
                    current += next;
                    ++index;
                    continue;
                }
            }

            current += ch;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (token_started) {
                arguments.push_back(current);
                current.clear();
                token_started = false;
            }
            continue;
        }

        if (ch == '\'') {
            quote_mode = QuoteMode::Single;
            token_started = true;
            continue;
        }

        if (ch == '"') {
            quote_mode = QuoteMode::Double;
            token_started = true;
            continue;
        }

        if (ch == '\\' && index + 1 < command.size()) {
            const char next = command[index + 1];
            if (std::isspace(static_cast<unsigned char>(next)) != 0 || next == '\\' || next == '"'
                || next == '\'') {
                current += next;
                token_started = true;
                ++index;
                continue;
            }
        }

        current += ch;
        token_started = true;
    }

    if (quote_mode != QuoteMode::None)
        return std::nullopt;

    if (token_started)
        arguments.push_back(std::move(current));

    return arguments;
}

[[nodiscard]] bool ends_with_newline(std::string_view input) {
    return !input.empty() && input.back() == '\n';
}

[[nodiscard]] bool starts_with_reserved_prefix(std::string_view name) {
    return name.starts_with(kReservedPrefix);
}

[[nodiscard]] bool is_command_input(std::string_view input) {
    return !input.empty() && (input.front() == ':' || input.front() == '.');
}

[[nodiscard]] std::string_view canonical_command_name(std::string_view command) {
    if (command == ":help")
        return "help";
    if (command == ":quit")
        return "quit";
    if (command == ":exit")
        return "exit";
    if (command == ":reset")
        return "reset";
    if (command == ":show")
        return "show";
    if (command == ":state")
        return "show";

    return "";
}

[[nodiscard]] std::string span_slice(std::string_view source, cstc::span::SourceSpan span) {
    if (span.end < span.start || span.end > source.size()) {
        throw std::runtime_error("invalid source span while extracting REPL snippet");
    }

    return std::string(source.substr(span.start, span.end - span.start));
}

[[nodiscard]] cstc::span::SourceSpan item_span(const cstc::ast::Item& item) {
    return std::visit([](const auto& node) { return node.span; }, item);
}

[[nodiscard]] cstc::span::SourceSpan stmt_span(const cstc::ast::Stmt& stmt) {
    return std::visit([](const auto& node) { return node.span; }, stmt);
}

void append_snippet(std::ostringstream& output, std::string_view snippet) {
    output << snippet;
    if (!ends_with_newline(snippet))
        output << '\n';
}

[[nodiscard]] CommandResult make_invocation_failed_result(std::string status_message) {
    CommandResult result;
    result.invocation_failed = true;
    result.status_message = std::move(status_message);
    return result;
}

[[nodiscard]] std::string normalize_line_endings(std::string text) {
    std::string normalized;
    normalized.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n')
                ++index;
            normalized.push_back('\n');
            continue;
        }

        normalized.push_back(ch);
    }

    return normalized;
}

[[nodiscard]] std::string read_file_or_empty(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return "";

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return normalize_line_endings(buffer.str());
}

#ifdef _WIN32

class UniqueHandle {
public:
    UniqueHandle() = default;

    explicit UniqueHandle(HANDLE handle)
        : handle_(handle) {}

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(other.release()) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this == &other)
            return *this;

        reset(other.release());
        return *this;
    }

    ~UniqueHandle() { reset(); }

    [[nodiscard]] HANDLE get() const { return handle_; }

    [[nodiscard]] HANDLE release() {
        HANDLE released = handle_;
        handle_ = nullptr;
        return released;
    }

    void reset(HANDLE handle = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
        handle_ = handle;
    }

    [[nodiscard]] explicit operator bool() const {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_ = nullptr;
};

[[nodiscard]] std::optional<std::wstring>
    decode_wide(std::string_view value, UINT code_page, DWORD flags) {
    if (value.empty())
        return std::wstring();

    const int required = MultiByteToWideChar(
        code_page, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
        return std::nullopt;

    std::wstring converted(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            code_page, flags, value.data(), static_cast<int>(value.size()), converted.data(),
            required)
        <= 0) {
        return std::nullopt;
    }

    return converted;
}

[[nodiscard]] std::wstring widen_string(std::string_view value) {
    if (auto converted = decode_wide(value, CP_UTF8, MB_ERR_INVALID_CHARS); converted.has_value())
        return *converted;
    if (auto converted = decode_wide(value, CP_ACP, 0); converted.has_value())
        return *converted;
    return std::wstring(value.begin(), value.end());
}

[[nodiscard]] std::string narrow_string(const std::wstring& value) {
    if (value.empty())
        return "";

    const int required = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        return std::string(value.begin(), value.end());

    std::string converted(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), converted.data(), required,
            nullptr, nullptr)
        <= 0) {
        return std::string(value.begin(), value.end());
    }

    return converted;
}

[[nodiscard]] std::string format_windows_error(DWORD error_code) {
    wchar_t* message_buffer = nullptr;
    const DWORD flags =
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD message_size = FormatMessageW(
        flags, nullptr, error_code, 0, reinterpret_cast<LPWSTR>(&message_buffer), 0, nullptr);
    if (message_size == 0 || message_buffer == nullptr)
        return "Win32 error " + std::to_string(error_code);

    std::wstring message(message_buffer, message_size);
    LocalFree(message_buffer);

    while (!message.empty()
           && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }

    return narrow_string(message);
}

[[nodiscard]] bool windows_argument_needs_quotes(std::wstring_view value) {
    return value.empty() || value.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
}

[[nodiscard]] std::wstring quote_windows_command_argument(std::wstring_view value) {
    if (!windows_argument_needs_quotes(value))
        return std::wstring(value);

    std::wstring quoted;
    quoted.push_back(L'"');

    std::size_t backslash_count = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslash_count;
            continue;
        }

        if (ch == L'"') {
            quoted.append(backslash_count * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslash_count = 0;
            continue;
        }

        quoted.append(backslash_count, L'\\');
        backslash_count = 0;
        quoted.push_back(ch);
    }

    quoted.append(backslash_count * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

[[nodiscard]] std::wstring build_windows_command_line(const std::vector<std::string>& arguments) {
    std::wstring command_line;
    bool first = true;
    for (const std::string& argument : arguments) {
        if (!first)
            command_line.push_back(L' ');
        first = false;
        command_line += quote_windows_command_argument(widen_string(argument));
    }
    return command_line;
}

[[nodiscard]] CommandResult execute_spawned_command(
    const std::vector<std::string>& arguments, const fs::path& stdout_path,
    const fs::path& stderr_path) {
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    UniqueHandle stdout_handle(CreateFileW(
        stdout_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &security_attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!stdout_handle) {
        return make_invocation_failed_result(
            "failed to open stdout capture file: " + stdout_path.string() + " ("
            + format_windows_error(GetLastError()) + ")");
    }

    UniqueHandle stderr_handle(CreateFileW(
        stderr_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &security_attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!stderr_handle) {
        return make_invocation_failed_result(
            "failed to open stderr capture file: " + stderr_path.string() + " ("
            + format_windows_error(GetLastError()) + ")");
    }

    std::wstring command_line = build_windows_command_line(arguments);
    std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = stdout_handle.get();
    startup_info.hStdError = stderr_handle.get();

    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(
            nullptr, mutable_command_line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
            &startup_info, &process_info)) {
        return make_invocation_failed_result(
            "failed to start process '" + arguments.front()
            + "': " + format_windows_error(GetLastError()));
    }

    UniqueHandle process_handle(process_info.hProcess);
    UniqueHandle thread_handle(process_info.hThread);
    (void)thread_handle;
    stdout_handle.reset();
    stderr_handle.reset();

    const DWORD wait_result = WaitForSingleObject(process_handle.get(), INFINITE);
    if (wait_result == WAIT_FAILED) {
        return make_invocation_failed_result(
            "failed waiting for process '" + arguments.front()
            + "': " + format_windows_error(GetLastError()));
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process_handle.get(), &exit_code)) {
        return make_invocation_failed_result(
            "failed reading exit code for process '" + arguments.front()
            + "': " + format_windows_error(GetLastError()));
    }

    CommandResult result;
    result.completed_normally = true;
    result.exit_code = static_cast<int>(exit_code);
    return result;
}

#elif defined(__unix__) || defined(__APPLE__)

[[nodiscard]] char** process_environment() {
# ifdef __APPLE__
    return *_NSGetEnviron();
# else
    return ::environ;
# endif
}

[[nodiscard]] CommandResult interpret_wait_status(int raw_status) {
    CommandResult result;

    if (WIFEXITED(raw_status)) {
        result.completed_normally = true;
        result.exit_code = WEXITSTATUS(raw_status);
    } else if (WIFSIGNALED(raw_status)) {
        result.exit_code = 128 + WTERMSIG(raw_status);
        result.status_message = "terminated by signal " + std::to_string(WTERMSIG(raw_status));
    } else {
        result.status_message = "terminated abnormally";
    }

    return result;
}

[[nodiscard]] CommandResult execute_spawned_command(
    const std::vector<std::string>& arguments, const fs::path& stdout_path,
    const fs::path& stderr_path) {
    posix_spawn_file_actions_t file_actions;
    const int init_error = posix_spawn_file_actions_init(&file_actions);
    if (init_error != 0) {
        return make_invocation_failed_result(
            "failed to prepare process launch for '" + arguments.front()
            + "': " + std::strerror(init_error));
    }

    const auto add_open =
        [&file_actions](int fd, const fs::path& path) -> std::optional<CommandResult> {
        const int open_error = posix_spawn_file_actions_addopen(
            &file_actions, fd, path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (open_error == 0)
            return std::nullopt;

        return make_invocation_failed_result(
            "failed to redirect fd " + std::to_string(fd) + " to '" + path.string()
            + "': " + std::strerror(open_error));
    };

    if (auto stdout_error = add_open(STDOUT_FILENO, stdout_path); stdout_error.has_value()) {
        posix_spawn_file_actions_destroy(&file_actions);
        return *stdout_error;
    }

    if (auto stderr_error = add_open(STDERR_FILENO, stderr_path); stderr_error.has_value()) {
        posix_spawn_file_actions_destroy(&file_actions);
        return *stderr_error;
    }

    std::vector<std::string> mutable_arguments = arguments;
    std::vector<char*> argv;
    argv.reserve(mutable_arguments.size() + 1);
    for (std::string& argument : mutable_arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);

    pid_t child_pid = 0;
    const int spawn_error = posix_spawnp(
        &child_pid, mutable_arguments.front().c_str(), &file_actions, nullptr, argv.data(),
        process_environment());
    posix_spawn_file_actions_destroy(&file_actions);
    if (spawn_error != 0) {
        return make_invocation_failed_result(
            "failed to start process '" + arguments.front() + "': " + std::strerror(spawn_error));
    }

    int status = 0;
    while (waitpid(child_pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;

        return make_invocation_failed_result(
            "failed waiting for process '" + arguments.front() + "': " + std::strerror(errno));
    }

    return interpret_wait_status(status);
}

#else

[[nodiscard]] CommandResult execute_spawned_command(
    const std::vector<std::string>& arguments, const fs::path&, const fs::path&) {
    return make_invocation_failed_result(
        "process execution is not supported on this platform for '" + arguments.front() + "'");
}

#endif

[[nodiscard]] CommandResult execute_command(const std::vector<std::string>& arguments) {
    if (arguments.empty())
        return make_invocation_failed_result("no process arguments were provided");

    TemporaryDirectory command_dir("cstc-repl-command");
    const fs::path stdout_path = command_dir.path() / "stdout.txt";
    const fs::path stderr_path = command_dir.path() / "stderr.txt";

    CommandResult result = execute_spawned_command(arguments, stdout_path, stderr_path);
    result.stdout_output = read_file_or_empty(stdout_path);
    result.stderr_output = read_file_or_empty(stderr_path);
    return result;
}

[[nodiscard]] bool fs_exists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error) && !error;
}

[[nodiscard]] std::vector<std::string> executable_name_candidates(std::string_view program) {
#ifdef _WIN32
    const fs::path path(program);
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
        const fs::path candidate_path(candidate);
        if ((candidate_path.is_absolute() || candidate_path.has_parent_path())
            && fs_exists(candidate_path)) {
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

    std::string_view entries = path_env;
    std::size_t start = 0;
    while (start <= entries.size()) {
        const std::size_t end = entries.find(path_separator, start);
        const std::string_view entry = entries.substr(start, end - start);
        const fs::path directory = entry.empty() ? fs::current_path() : fs::path(entry);

        for (const std::string& candidate : executable_name_candidates(program)) {
            if (fs_exists(directory / candidate))
                return true;
        }

        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    return false;
}

[[nodiscard]] std::vector<std::string>
    resolve_linker_program(const std::optional<std::string>& override_program) {
    if (override_program.has_value() && !override_program->empty())
        return {*override_program};

    if (const char* cxx = std::getenv("CXX"); cxx != nullptr && cxx[0] != '\0') {
        const auto parsed = split_command_arguments(cxx);
        if (parsed.has_value() && !parsed->empty() && is_program_available(parsed->front()))
            return *parsed;
    }

    const cstc::cli::LinkerFlavor flavor = cstc::cli::host_linker_flavor();
    for (const std::string_view candidate : cstc::cli::linker_candidates(flavor)) {
        if (is_program_available(candidate))
            return {std::string(candidate)};
    }

    return {std::string(cstc::cli::fallback_linker_program(flavor))};
}

[[nodiscard]] std::optional<fs::path> resolve_executable_path(const fs::path& output_stem) {
    if (fs_exists(output_stem))
        return output_stem;

#ifdef _WIN32
    fs::path candidate = output_stem;
    candidate += ".exe";
    if (fs_exists(candidate))
        return candidate;
#endif

    return std::nullopt;
}

void write_text_file(const fs::path& path, std::string_view source) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to open REPL session file: " + path.string());
    }

    file << source;
    if (!file) {
        throw std::runtime_error("failed to write REPL session file: " + path.string());
    }
}

[[nodiscard]] fs::path create_unique_session_path(const fs::path& root_dir) {
    std::random_device device;
    std::mt19937_64 generator(device());
    std::uniform_int_distribution<unsigned long long> distribution;

    for (int attempt = 0; attempt < 64; ++attempt) {
        const fs::path candidate =
            root_dir
            / (std::string(kSessionFilePrefix) + std::to_string(distribution(generator))
               + std::string(kSessionFileSuffix));
        if (!fs_exists(candidate))
            return candidate;
    }

    throw std::runtime_error("failed to allocate a unique REPL session source path");
}

[[nodiscard]] std::string make_body_wrapper_source(std::string_view input) {
    std::ostringstream output;
    output << "fn " << kBodyProbeName << "() {\n";
    output << input;
    if (!ends_with_newline(input))
        output << '\n';
    output << "}\n";
    return output.str();
}

[[nodiscard]] std::size_t adjusted_error_start(
    std::string_view input, const cstc::parser::ParseError& error, std::size_t prefix_len) {
    if (error.span.start <= prefix_len)
        return 0;

    const std::size_t local_start = error.span.start - prefix_len;
    return std::min(local_start, input.size());
}

[[nodiscard]] bool parse_error_at_end(
    std::string_view input, const cstc::parser::ParseError& error, std::size_t prefix_len) {
    return adjusted_error_start(input, error, prefix_len) >= input.size();
}

[[nodiscard]] std::string format_snippet_parse_error(
    std::string_view input, const cstc::parser::ParseError& error, std::size_t prefix_len) {
    std::size_t local_start = 0;
    if (error.span.start > prefix_len)
        local_start = error.span.start - prefix_len;

    std::size_t local_end = local_start;
    if (error.span.end > prefix_len)
        local_end = error.span.end - prefix_len;

    local_start = std::min(local_start, input.size());
    local_end = std::min(std::max(local_end, local_start), input.size());

    cstc::span::SourceMap source_map;
    const cstc::span::SourceFileId file_id = source_map.add_file("<repl>", std::string(input));
    const std::optional<cstc::span::SourceSpan> span =
        source_map.make_span(file_id, local_start, local_end);
    if (!span.has_value()) {
        return "parse error <repl>:1:1: " + error.message;
    }

    const auto resolved = source_map.resolve_span(*span);
    if (!resolved.has_value())
        return "parse error <repl>:1:1: " + error.message;

    return "parse error <repl>:" + std::to_string(resolved->start.line) + ":"
         + std::to_string(resolved->start.column) + ": " + error.message;
}

[[nodiscard]] std::expected<void, std::string>
    validate_reserved_name(std::string_view name, std::string_view context, bool reserve_main) {
    if (starts_with_reserved_prefix(name)) {
        return std::unexpected(
            std::string(context) + " '" + std::string(name) + "' uses reserved REPL prefix '"
            + std::string(kReservedPrefix) + "'");
    }

    if (reserve_main && name == kGeneratedMainName) {
        return std::unexpected(
            std::string(context) + " '" + std::string(name) + "' is reserved by the REPL");
    }

    return {};
}

[[nodiscard]] std::expected<void, std::string>
    validate_program_items(const cstc::ast::Program& program) {
    for (const cstc::ast::Item& item : program.items) {
        if (const auto* decl = std::get_if<cstc::ast::StructDecl>(&item)) {
            auto validated = validate_reserved_name(decl->name.as_str(), "type name", true);
            if (!validated.has_value())
                return validated;
            continue;
        }

        if (const auto* decl = std::get_if<cstc::ast::EnumDecl>(&item)) {
            auto validated = validate_reserved_name(decl->name.as_str(), "type name", true);
            if (!validated.has_value())
                return validated;
            continue;
        }

        if (const auto* decl = std::get_if<cstc::ast::FnDecl>(&item)) {
            auto validated = validate_reserved_name(decl->name.as_str(), "function name", true);
            if (!validated.has_value())
                return validated;
            continue;
        }

        if (const auto* decl = std::get_if<cstc::ast::ExternFnDecl>(&item)) {
            auto validated =
                validate_reserved_name(decl->name.as_str(), "extern function name", true);
            if (!validated.has_value())
                return validated;
            continue;
        }

        if (const auto* decl = std::get_if<cstc::ast::ExternStructDecl>(&item)) {
            auto validated =
                validate_reserved_name(decl->name.as_str(), "extern struct name", true);
            if (!validated.has_value())
                return validated;
            continue;
        }

        const auto* decl = std::get_if<cstc::ast::ImportDecl>(&item);
        if (decl == nullptr)
            continue;

        for (const cstc::ast::ImportItem& import_item : decl->items) {
            const std::string_view visible_name = import_item.alias.has_value()
                                                    ? import_item.alias->as_str()
                                                    : import_item.name.as_str();
            auto validated = validate_reserved_name(visible_name, "imported name", true);
            if (!validated.has_value())
                return validated;
        }
    }

    return {};
}

[[nodiscard]] std::expected<void, std::string>
    validate_body_bindings(const cstc::ast::BlockExpr& block) {
    for (const cstc::ast::Stmt& stmt : block.statements) {
        const auto* let_stmt = std::get_if<cstc::ast::LetStmt>(&stmt);
        if (let_stmt == nullptr || let_stmt->discard)
            continue;

        auto validated = validate_reserved_name(let_stmt->name.as_str(), "binding name", false);
        if (!validated.has_value())
            return validated;
    }

    return {};
}

[[nodiscard]] std::vector<std::string>
    extract_item_snippets(std::string_view input, const cstc::ast::Program& program) {
    std::vector<std::string> snippets;
    snippets.reserve(program.items.size());
    for (const cstc::ast::Item& item : program.items)
        snippets.push_back(span_slice(input, item_span(item)));
    return snippets;
}

[[nodiscard]] ParsedBodySnippet
    extract_body_snippet(const std::string& wrapper_source, const cstc::ast::Program& program) {
    const auto* fn_decl = std::get_if<cstc::ast::FnDecl>(&program.items.front());
    if (fn_decl == nullptr || fn_decl->body == nullptr) {
        throw std::runtime_error("invalid REPL body probe program");
    }

    ParsedBodySnippet snippet;
    snippet.statement_snippets.reserve(fn_decl->body->statements.size());
    for (const cstc::ast::Stmt& stmt : fn_decl->body->statements) {
        const std::string source = span_slice(wrapper_source, stmt_span(stmt));
        snippet.statement_snippets.push_back(source);

        const auto* let_stmt = std::get_if<cstc::ast::LetStmt>(&stmt);
        if (let_stmt != nullptr && !let_stmt->discard)
            snippet.persisted_let_snippets.push_back(source);
    }

    if (fn_decl->body->tail.has_value())
        snippet.tail_expr_snippet = span_slice(wrapper_source, (*fn_decl->body->tail)->span);

    return snippet;
}

[[nodiscard]] DisplayKind classify_display_kind(const cstc::tyir::Ty& ty) {
    using cstc::tyir::TyKind;

    switch (ty.kind) {
    case TyKind::Num: return DisplayKind::Num;
    case TyKind::Str: return DisplayKind::Str;
    case TyKind::Bool: return DisplayKind::Bool;
    case TyKind::Unit: return DisplayKind::Unit;
    case TyKind::Never: return DisplayKind::Diverges;
    case TyKind::Ref:
        if (ty.pointee != nullptr && ty.pointee->kind == TyKind::Str)
            return DisplayKind::RefStr;
        return DisplayKind::Unsupported;
    case TyKind::Named: return DisplayKind::Unsupported;
    }

    return DisplayKind::Unsupported;
}

[[nodiscard]] const cstc::tyir::TyFnDecl* find_main_fn(const cstc::tyir::TyProgram& program) {
    const cstc::symbol::Symbol main_name = cstc::symbol::Symbol::intern(kGeneratedMainName);

    for (const cstc::tyir::TyItem& item : program.items) {
        const auto* fn = std::get_if<cstc::tyir::TyFnDecl>(&item);
        if (fn != nullptr && fn->name == main_name)
            return fn;
    }

    return nullptr;
}

[[nodiscard]] const cstc::tyir::TyLetStmt*
    find_probe_value_let(const cstc::tyir::TyFnDecl& main_fn) {
    const cstc::symbol::Symbol probe_name = cstc::symbol::Symbol::intern(kProbeValueName);

    for (auto it = main_fn.body->stmts.rbegin(); it != main_fn.body->stmts.rend(); ++it) {
        const auto* let_stmt = std::get_if<cstc::tyir::TyLetStmt>(&*it);
        if (let_stmt != nullptr && !let_stmt->discard && let_stmt->name == probe_name)
            return let_stmt;
    }

    return nullptr;
}

[[nodiscard]] std::string runtime_failure_message(const CommandResult& result) {
    std::ostringstream message;
    message << "program exited with code " << result.exit_code;
    if (!result.status_message.empty())
        message << " (" << result.status_message << ")";
    return message.str();
}

[[nodiscard]] std::string format_command_for_display(const std::vector<std::string>& arguments) {
    std::ostringstream rendered;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0)
            rendered << ' ';

        const std::string_view argument = arguments[index];
        const bool needs_quotes =
            argument.empty() || std::any_of(argument.begin(), argument.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0 || ch == '"' || ch == '\\';
            });

        if (!needs_quotes) {
            rendered << argument;
            continue;
        }

        rendered << '"';
        for (const char ch : argument) {
            if (ch == '"' || ch == '\\')
                rendered << '\\';
            rendered << ch;
        }
        rendered << '"';
    }

    return rendered.str();
}

void append_command_streams(std::ostringstream& message, const CommandResult& result) {
    if (!result.stderr_output.empty())
        message << "\nStderr:\n" << result.stderr_output;
    if (!result.stdout_output.empty())
        message << "\nStdout:\n" << result.stdout_output;
}

[[nodiscard]] std::string invocation_failure_message(const CommandResult& result) {
    std::ostringstream message;
    message << result.status_message;
    append_command_streams(message, result);
    return message.str();
}

[[nodiscard]] std::string
    linker_failure_message(std::string_view linker_program, const CommandResult& result) {
    std::ostringstream message;
    message << "linker '" << linker_program << "' failed";
    if (!result.status_message.empty())
        message << " (" << result.status_message << ")";
    message << " with exit code " << result.exit_code;
    append_command_streams(message, result);
    return message.str();
}

} // namespace

class Session::Impl {
public:
    explicit Impl(SessionOptions options)
        : options_(std::move(options))
        , scratch_dir_("cstc-repl")
        , session_root_dir_(resolve_session_root_dir(options_))
        , session_source_path_(create_unique_session_path(session_root_dir_)) {
        std::error_code error;
        fs::create_directories(session_root_dir_, error);
        if (error) {
            throw std::runtime_error(
                "failed to create REPL session root directory '" + session_root_dir_.string()
                + "': " + error.message());
        }

        write_text_file(
            session_source_path_, build_program_source(items_, persisted_lets_, std::nullopt));
    }

    ~Impl() {
        std::error_code error;
        fs::remove(session_source_path_, error);
    }

    [[nodiscard]] bool needs_continuation(std::string_view input) const {
        if (trim_view(input).empty())
            return false;

        int brace_depth = 0;
        int paren_depth = 0;
        int bracket_depth = 0;
        for (const cstc::lexer::Token& token : cstc::lexer::lex_source(input, false)) {
            switch (token.kind) {
            case cstc::lexer::TokenKind::Unknown: {
                const std::string_view text = token.symbol.as_str();
                return text.starts_with("\"") || text.starts_with("/*");
            }
            case cstc::lexer::TokenKind::LBrace: ++brace_depth; break;
            case cstc::lexer::TokenKind::RBrace: --brace_depth; break;
            case cstc::lexer::TokenKind::LParen: ++paren_depth; break;
            case cstc::lexer::TokenKind::RParen: --paren_depth; break;
            case cstc::lexer::TokenKind::LBracket: ++bracket_depth; break;
            case cstc::lexer::TokenKind::RBracket: --bracket_depth; break;
            default: break;
            }

            if (brace_depth < 0 || paren_depth < 0 || bracket_depth < 0)
                return false;
        }

        if (brace_depth > 0 || paren_depth > 0 || bracket_depth > 0)
            return true;

        const auto top_level = cstc::parser::parse_source(input);
        if (top_level.has_value())
            return false;

        const std::string wrapper_source = make_body_wrapper_source(input);
        const auto body_level = cstc::parser::parse_source(wrapper_source);
        if (body_level.has_value())
            return false;

        return parse_error_at_end(input, top_level.error(), 0)
            || parse_error_at_end(
                   input, body_level.error(),
                   std::string("fn ").size() + kBodyProbeName.size()
                       + std::string("() {\n").size());
    }

    [[nodiscard]] SubmissionResult submit(std::string_view input) {
        const std::string trimmed = trim_copy(input);
        if (trimmed.empty())
            return {};

        if (is_command_input(trimmed))
            return handle_command(trimmed);

        const auto top_level = cstc::parser::parse_source(input);
        if (top_level.has_value()) {
            auto validated = validate_program_items(*top_level);
            if (!validated.has_value())
                return make_result(
                    SubmissionStatus::Error, false, false, {}, {}, {}, validated.error());

            const std::vector<std::string> new_items = extract_item_snippets(input, *top_level);
            if (new_items.empty())
                return {};

            std::vector<std::string> combined_items = items_;
            combined_items.insert(combined_items.end(), new_items.begin(), new_items.end());

            const std::string candidate_source =
                build_program_source(combined_items, persisted_lets_, std::nullopt);
            const auto validated_program = lower_to_tyir(candidate_source);
            if (!validated_program.has_value()) {
                return make_result(
                    SubmissionStatus::Error, false, false, {}, {}, {}, validated_program.error());
            }

            items_ = std::move(combined_items);
            write_text_file(
                session_source_path_, build_program_source(items_, persisted_lets_, std::nullopt));
            return make_result(SubmissionStatus::Success, true);
        }

        const std::string wrapper_source = make_body_wrapper_source(input);
        const auto body_level = cstc::parser::parse_source(wrapper_source);
        if (!body_level.has_value()) {
            const std::size_t body_prefix_len =
                std::string("fn ").size() + kBodyProbeName.size() + std::string("() {\n").size();
            const bool prefer_body =
                adjusted_error_start(input, body_level.error(), body_prefix_len)
                > adjusted_error_start(input, top_level.error(), 0);
            const cstc::parser::ParseError& selected_error =
                prefer_body ? body_level.error() : top_level.error();
            const std::size_t selected_prefix = prefer_body ? body_prefix_len : 0;

            return make_result(
                SubmissionStatus::Error, false, false, {}, {}, {},
                format_snippet_parse_error(input, selected_error, selected_prefix));
        }

        const auto* probe_fn = std::get_if<cstc::ast::FnDecl>(&body_level->items.front());
        if (probe_fn == nullptr || probe_fn->body == nullptr) {
            return make_result(
                SubmissionStatus::Error, false, false, {}, {}, {},
                "internal REPL error: invalid body probe program");
        }

        auto validated = validate_body_bindings(*probe_fn->body);
        if (!validated.has_value())
            return make_result(
                SubmissionStatus::Error, false, false, {}, {}, {}, validated.error());

        const ParsedBodySnippet snippet = extract_body_snippet(wrapper_source, *body_level);
        if (snippet.statement_snippets.empty() && !snippet.tail_expr_snippet.has_value())
            return {};

        const std::string analysis_body = build_analysis_body(snippet);
        const std::string analysis_source =
            build_program_source(items_, persisted_lets_, analysis_body);
        const auto typed_program = lower_to_tyir(analysis_source);
        if (!typed_program.has_value())
            return make_result(
                SubmissionStatus::Error, false, false, {}, {}, {}, typed_program.error());

        DisplayKind display_kind = DisplayKind::None;
        std::string tail_type_name;
        if (snippet.tail_expr_snippet.has_value()) {
            const cstc::tyir::TyFnDecl* main_fn = find_main_fn(*typed_program);
            if (main_fn == nullptr || main_fn->body == nullptr) {
                return make_result(
                    SubmissionStatus::Error, false, false, {}, {}, {},
                    "internal REPL error: generated program is missing `main`");
            }

            if (main_fn->body->ty.is_never()) {
                display_kind = DisplayKind::Diverges;
            } else {
                const cstc::tyir::TyLetStmt* probe_value = find_probe_value_let(*main_fn);
                if (probe_value == nullptr) {
                    return make_result(
                        SubmissionStatus::Error, false, false, {}, {}, {},
                        "internal REPL error: failed to locate REPL probe binding");
                }

                display_kind = classify_display_kind(probe_value->ty);
                tail_type_name = probe_value->ty.display();
            }
        }

        const auto rendered_body = build_execution_body(snippet, display_kind);
        if (!rendered_body.has_value())
            return make_result(
                SubmissionStatus::Error, false, false, {}, {}, {}, rendered_body.error());

        const std::string execution_source =
            build_program_source(items_, persisted_lets_, *rendered_body);
        const auto lir_program = lower_to_lir(execution_source);
        if (!lir_program.has_value())
            return make_result(
                SubmissionStatus::Error, false, false, {}, {}, {}, lir_program.error());

        const auto run_result = build_and_run(*lir_program);
        if (!run_result.has_value())
            return make_result(
                SubmissionStatus::Error, false, false, {}, {}, {}, run_result.error());

        const CommandResult& executed = *run_result;
        if (executed.invocation_failed) {
            return make_result(
                SubmissionStatus::Error, false, false, executed.stdout_output,
                executed.stderr_output, {}, invocation_failure_message(executed));
        }

        if (!executed.succeeded()) {
            return make_result(
                SubmissionStatus::RuntimeError, false, true, executed.stdout_output,
                executed.stderr_output, {}, runtime_failure_message(executed));
        }

        persisted_lets_.insert(
            persisted_lets_.end(), snippet.persisted_let_snippets.begin(),
            snippet.persisted_let_snippets.end());
        write_text_file(
            session_source_path_, build_program_source(items_, persisted_lets_, std::nullopt));

        SubmissionResult result = make_result(
            SubmissionStatus::Success, !snippet.persisted_let_snippets.empty(), true,
            executed.stdout_output, executed.stderr_output);

        if (display_kind == DisplayKind::Unsupported) {
            result.info_message =
                "result of type '" + tail_type_name + "' was evaluated but not displayed";
        }

        return result;
    }

    void reset() {
        items_.clear();
        persisted_lets_.clear();
        write_text_file(
            session_source_path_, build_program_source(items_, persisted_lets_, std::nullopt));
    }

    [[nodiscard]] std::string persisted_source() const {
        return build_program_source(items_, persisted_lets_, std::nullopt);
    }

private:
    [[nodiscard]] static fs::path resolve_session_root_dir(const SessionOptions& options) {
        if (!options.session_root_dir.empty())
            return fs::absolute(options.session_root_dir);
        return fs::current_path();
    }

    [[nodiscard]] SubmissionResult handle_command(std::string_view command) {
        const std::string_view canonical = canonical_command_name(command);
        if (canonical == "help") {
            return make_result(SubmissionStatus::Success, false, false, {}, {}, help_text());
        }

        if (canonical == "quit" || canonical == "exit")
            return make_result(SubmissionStatus::ExitRequested);

        if (canonical == "reset") {
            const bool had_state = !items_.empty() || !persisted_lets_.empty();
            reset();
            return make_result(
                SubmissionStatus::Success, had_state, false, {}, {}, "session reset");
        }

        if (canonical == "show")
            return make_result(
                SubmissionStatus::Success, false, false, {}, {}, user_state_source());

        return make_result(
            SubmissionStatus::Error, false, false, {}, {}, {},
            "unknown command: " + std::string(command));
    }

    [[nodiscard]] std::string user_state_source() const {
        if (items_.empty() && persisted_lets_.empty())
            return "session is empty";

        std::ostringstream output;
        bool needs_gap = false;

        for (const std::string& item : items_) {
            if (needs_gap)
                output << '\n';
            append_snippet(output, item);
            needs_gap = true;
        }

        if (!persisted_lets_.empty()) {
            if (needs_gap)
                output << '\n';
            output << "// persisted let bindings\n";
            for (const std::string& let_snippet : persisted_lets_)
                append_snippet(output, let_snippet);
        }

        std::string rendered = output.str();
        if (!rendered.empty() && rendered.back() == '\n')
            rendered.pop_back();
        return rendered;
    }

    [[nodiscard]] std::string build_analysis_body(const ParsedBodySnippet& snippet) const {
        std::ostringstream body;
        for (const std::string& statement : snippet.statement_snippets)
            append_snippet(body, statement);

        if (snippet.tail_expr_snippet.has_value()) {
            body << "let " << kProbeValueName << " = " << *snippet.tail_expr_snippet << ";\n";
        }

        return body.str();
    }

    [[nodiscard]] std::expected<std::string, std::string>
        build_execution_body(const ParsedBodySnippet& snippet, DisplayKind display_kind) const {
        std::ostringstream body;
        for (const std::string& statement : snippet.statement_snippets)
            append_snippet(body, statement);

        if (!snippet.tail_expr_snippet.has_value())
            return body.str();

        const std::string_view tail = *snippet.tail_expr_snippet;
        switch (display_kind) {
        case DisplayKind::None: break;
        case DisplayKind::Num: body << kPrintNumName << "(" << tail << ");\n"; break;
        case DisplayKind::Str: body << kPrintStrName << "(" << tail << ");\n"; break;
        case DisplayKind::RefStr: body << kPrintRefStrName << "(" << tail << ");\n"; break;
        case DisplayKind::Bool: body << kPrintBoolName << "(" << tail << ");\n"; break;
        case DisplayKind::Unit: body << kPrintUnitName << "(" << tail << ");\n"; break;
        case DisplayKind::Unsupported: body << tail << ";\n"; break;
        case DisplayKind::Diverges: append_snippet(body, tail); break;
        }

        return body.str();
    }

    [[nodiscard]] std::string build_program_source(
        const std::vector<std::string>& items, const std::vector<std::string>& lets,
        std::optional<std::string_view> current_body) const {
        std::ostringstream program;
        program << "// generated by cstc_repl\n\n";

        for (const std::string& item : items) {
            append_snippet(program, item);
            program << '\n';
        }

        program << kRuntimeHelpers;
        if (!ends_with_newline(kRuntimeHelpers))
            program << '\n';
        program << '\n';

        program << "fn " << kGeneratedMainName << "() {\n";
        if (!lets.empty()) {
            program << "    // replayed let bindings\n";
            for (const std::string& let_snippet : lets)
                append_snippet(program, let_snippet);
        }

        if (current_body.has_value() && !trim_view(*current_body).empty()) {
            if (!lets.empty())
                program << '\n';
            program << "    // current input\n";
            append_snippet(program, *current_body);
        }

        program << "}\n";
        return program.str();
    }

    [[nodiscard]] std::expected<cstc::tyir::TyProgram, std::string>
        lower_to_tyir(std::string_view source) const {
        try {
            write_text_file(session_source_path_, source);

            cstc::span::SourceMap source_map;
            const auto loaded =
                cstc::module::load_program(source_map, session_source_path_, CICEST_STD_PATH);
            if (!loaded.has_value())
                return std::unexpected(
                    cstc::module::format_module_error(source_map, loaded.error()));

            const auto typed = cstc::tyir_builder::lower_program(*loaded);
            if (!typed.has_value())
                return std::unexpected(
                    cstc::cli_support::format_type_error(source_map, typed.error()));

            return *typed;
        } catch (const std::exception& error) {
            return std::unexpected(std::string(error.what()));
        }
    }

    [[nodiscard]] std::expected<cstc::lir::LirProgram, std::string>
        lower_to_lir(std::string_view source) const {
        const auto typed = lower_to_tyir(source);
        if (!typed.has_value())
            return std::unexpected(typed.error());

        return cstc::lir_builder::lower_program(*typed);
    }

    [[nodiscard]] std::expected<CommandResult, std::string>
        build_and_run(const cstc::lir::LirProgram& program) const {
        try {
            const fs::path object_path = scratch_dir_.path() / "repl_step.o";
            const fs::path output_stem = scratch_dir_.path() / "repl_step";
            cstc::codegen::emit_native_object(program, object_path, "cstc_repl_session");

            std::vector<std::string> linker_command = resolve_linker_program(options_.linker);
            linker_command.push_back(object_path.string());
            linker_command.push_back(cstc::resource_path::resolve_rt_path(CICEST_RT_PATH).string());
            linker_command.push_back("-o");
            linker_command.push_back(output_stem.string());

            const CommandResult linker_result = execute_command(linker_command);
            if (linker_result.invocation_failed)
                return std::unexpected(invocation_failure_message(linker_result));

            if (!linker_result.succeeded()) {
                return std::unexpected(linker_failure_message(
                    format_command_for_display(linker_command), linker_result));
            }

            const auto executable_path = resolve_executable_path(output_stem);
            if (!executable_path.has_value()) {
                return std::unexpected(
                    "failed to locate generated executable: " + output_stem.string());
            }

            return execute_command({executable_path->string()});
        } catch (const std::exception& error) {
            return std::unexpected(std::string(error.what()));
        }
    }

    // Keeps the thread-local symbol interner active for parsing and IR lowering across the
    // REPL session lifetime.
    cstc::symbol::SymbolSession symbols_;
    SessionOptions options_;
    TemporaryDirectory scratch_dir_;
    fs::path session_root_dir_;
    fs::path session_source_path_;
    std::vector<std::string> items_;
    std::vector<std::string> persisted_lets_;
};

Session::Session(SessionOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

Session::~Session() = default;

bool Session::needs_continuation(std::string_view input) const {
    return impl_->needs_continuation(input);
}

SubmissionResult Session::submit(std::string_view input) { return impl_->submit(input); }

void Session::reset() { (*impl_).reset(); }

std::string Session::persisted_source() const { return impl_->persisted_source(); }

std::string help_text() {
    return "Commands:\n"
           "  :help   show this help\n"
           "  :show   print persisted items and let bindings\n"
           "  :state  alias for :show\n"
           "  :reset  clear persisted items and bindings\n"
           "  :quit   exit the REPL\n"
           "  :exit   exit the REPL\n"
           "\n"
           "Persistence:\n"
           "  top-level items persist across turns\n"
           "  non-discard top-level let bindings persist via replay\n"
           "  expression statements run once and are not persisted";
}

std::string startup_text() {
    return "Cicest REPL\n"
           "Type :help for help and :quit to exit.";
}

} // namespace cstc::repl
