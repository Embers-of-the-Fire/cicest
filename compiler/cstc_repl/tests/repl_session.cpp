#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

#include <cstc_repl/repl.hpp>

namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view prefix)
        : path_(create(prefix)) {}

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

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
            if (fs::create_directory(candidate, error))
                return candidate;

            if (error && error != std::errc::file_exists) {
                throw std::runtime_error(
                    "failed to create temporary directory '" + candidate.string()
                    + "': " + error.message());
            }
        }

        throw std::runtime_error("failed to create temporary directory");
    }

    fs::path path_;
};

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(std::string name, std::string_view value)
        : name_(std::move(name))
        , previous_value_(read(name_)) {
        set(value);
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

    ~ScopedEnvironmentVariable() {
        if (previous_value_.has_value())
            set(*previous_value_);
        else
            unset();
    }

private:
    [[nodiscard]] static std::optional<std::string> read(const std::string& name) {
        if (const char* value = std::getenv(name.c_str()); value != nullptr)
            return std::string(value);
        return std::nullopt;
    }

    void set(std::string_view value) const {
        const std::string rendered(value);
#ifdef _WIN32
        const int result = _putenv_s(name_.c_str(), rendered.c_str());
#else
        const int result = setenv(name_.c_str(), rendered.c_str(), 1);
#endif
        assert(result == 0);
    }

    void unset() const {
#ifdef _WIN32
        const int result = _putenv_s(name_.c_str(), "");
#else
        const int result = unsetenv(name_.c_str());
#endif
        assert(result == 0);
    }

    std::string name_;
    std::optional<std::string> previous_value_;
};

void write_file(const fs::path& path, std::string_view contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    assert(file);
    file << contents;
    assert(file);
}

void make_executable(const fs::path& path) {
    std::error_code error;
    fs::permissions(
        path, fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
        fs::perm_options::replace, error);
    assert(!error);
}

[[nodiscard]] bool fs_exists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error) && !error;
}

[[nodiscard]] std::optional<fs::path> find_program_on_path(std::string_view program) {
    if (program.empty())
        return std::nullopt;

    const fs::path candidate_path(program);
    if ((candidate_path.is_absolute() || candidate_path.has_parent_path())
        && fs_exists(candidate_path))
        return fs::absolute(candidate_path);

    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr || path_env[0] == '\0')
        return std::nullopt;

#ifdef _WIN32
    constexpr char path_separator = ';';
#else
    constexpr char path_separator = ':';
#endif

    const std::string_view entries = path_env;
    std::size_t start = 0;
    while (start <= entries.size()) {
        const std::size_t end = entries.find(path_separator, start);
        const std::string_view entry = entries.substr(start, end - start);
        const fs::path directory = entry.empty() ? fs::current_path() : fs::path(entry);
        const fs::path candidate = directory / candidate_path;
        if (fs_exists(candidate))
            return fs::absolute(candidate);

        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    return std::nullopt;
}

[[nodiscard]] fs::path resolve_test_linker_program() {
    if (const char* cxx = std::getenv("CXX"); cxx != nullptr && cxx[0] != '\0') {
        std::string_view cxx_program = cxx;
        const std::size_t separator = cxx_program.find_first_of(" \t");
        if (separator != std::string_view::npos)
            cxx_program = cxx_program.substr(0, separator);

        if (const auto resolved = find_program_on_path(cxx_program); resolved.has_value())
            return *resolved;
    }

    for (const std::string_view candidate : {"clang++", "g++", "c++"}) {
        if (const auto resolved = find_program_on_path(candidate); resolved.has_value())
            return *resolved;
    }

    throw std::runtime_error("failed to locate a C++ linker for REPL tests");
}

[[nodiscard]] bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

[[nodiscard]] cstc::repl::SubmissionResult
    expect_success(cstc::repl::Session& session, std::string_view input) {
    const cstc::repl::SubmissionResult result = session.submit(input);
    assert(result.status == cstc::repl::SubmissionStatus::Success);
    assert(result.error_message.empty());
    return result;
}

[[nodiscard]] cstc::repl::SubmissionResult
    expect_error(cstc::repl::Session& session, std::string_view input) {
    const cstc::repl::SubmissionResult result = session.submit(input);
    assert(result.status == cstc::repl::SubmissionStatus::Error);
    assert(!result.error_message.empty());
    return result;
}

[[nodiscard]] cstc::repl::SubmissionResult
    expect_runtime_error(cstc::repl::Session& session, std::string_view input) {
    const cstc::repl::SubmissionResult result = session.submit(input);
    assert(result.status == cstc::repl::SubmissionStatus::RuntimeError);
    assert(result.executed);
    assert(!result.error_message.empty());
    return result;
}

void test_let_bindings_persist_across_turns() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    const auto first = expect_success(session, "let x: num = 40 + 2;");
    assert(first.executed);
    assert(first.state_changed);
    assert(first.stdout_output.empty());

    const auto second = expect_success(session, "x");
    assert(second.executed);
    assert(!second.state_changed);
    assert(second.stdout_output == "42\n");
}

void test_expression_statements_are_not_persisted() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    const auto first = expect_success(session, R"(println("hello");)");
    assert(first.stdout_output == "hello\n");
    assert(!first.state_changed);

    const auto second = expect_success(session, "1");
    assert(second.stdout_output == "1\n");
}

void test_item_definitions_persist_without_running_previous_turns() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    const auto binding = expect_success(session, "let x: num = 41;");
    assert(binding.executed);

    const auto item = expect_success(session, "fn inc(value: num) -> num { value + 1 }");
    assert(item.state_changed);
    assert(!item.executed);
    assert(item.stdout_output.empty());

    const auto result = expect_success(session, "inc(x)");
    assert(result.stdout_output == "42\n");
}

void test_relative_imports_resolve_from_session_root() {
    TemporaryDirectory root("cstc-repl-test");
    write_file(root.path() / "math.cst", "pub fn answer() -> num { 42 }\n");

    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});
    const auto import_result = expect_success(session, R"(import { answer } from "math.cst";)");
    assert(import_result.state_changed);

    const auto value_result = expect_success(session, "answer()");
    assert(value_result.stdout_output == "42\n");
}

void test_supported_result_types_are_rendered() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    const auto str_result = expect_success(session, R"("hello")");
    assert(str_result.stdout_output == "hello\n");

    const auto bool_result = expect_success(session, "true");
    assert(bool_result.stdout_output == "true\n");

    const auto unit_result = expect_success(session, "()");
    assert(unit_result.stdout_output == "()\n");

    const auto owned_str = expect_success(session, R"(str_concat("he", "llo"))");
    assert(owned_str.stdout_output == "hello\n");
}

void test_unsupported_result_types_report_a_notice() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    (void)expect_success(session, "struct Point { x: num }");
    const auto result = expect_success(session, "Point { x: 1 }");
    assert(result.stdout_output.empty());
    assert(contains(result.info_message, "Point"));
    assert(contains(result.info_message, "not displayed"));
}

void test_commands_and_reset_clear_persisted_state() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    const auto help = expect_success(session, ":help");
    assert(contains(help.info_message, ":reset"));
    assert(contains(help.info_message, ":show"));
    assert(contains(help.info_message, ":state"));

    (void)expect_success(session, "let x: num = 42;");
    const auto show = expect_success(session, ":show");
    assert(contains(show.info_message, "let x: num = 42;"));
    const auto state = expect_success(session, ":state");
    assert(state.info_message == show.info_message);

    const auto reset = expect_success(session, ":reset");
    assert(contains(reset.info_message, "reset"));
    assert(!contains(session.persisted_source(), "let x: num = 42;"));

    const auto empty_show = expect_success(session, ":show");
    assert(contains(empty_show.info_message, "session is empty"));

    const auto missing = expect_error(session, "x");
    assert(contains(missing.error_message, "undefined variable"));
}

void test_quit_command_requests_exit() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    const cstc::repl::SubmissionResult quit = session.submit(":quit");
    assert(quit.status == cstc::repl::SubmissionStatus::ExitRequested);
}

void test_dot_commands_are_rejected() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    const auto result = expect_error(session, ".help");
    assert(contains(result.error_message, "unknown command"));
}

void test_startup_text_mentions_help_and_quit() {
    const std::string startup = cstc::repl::startup_text();
    assert(contains(startup, ":help"));
    assert(contains(startup, ":quit"));
}

void test_runtime_errors_do_not_commit_new_state() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    (void)expect_success(session, "let x: num = 1;");
    const auto failure = expect_runtime_error(session, "assert(false);");
    assert(contains(failure.stderr_output, "assertion failed"));

    const auto value = expect_success(session, "x");
    assert(value.stdout_output == "1\n");
}

void test_missing_linker_reports_process_start_failure() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({
        .session_root_dir = root.path(),
        .linker = std::string("cstc-repl-test-missing-linker"),
    });

    const auto result = expect_error(session, "1");
    assert(contains(result.error_message, "failed to start process"));
    assert(contains(result.error_message, "cstc-repl-test-missing-linker"));
}

void test_custom_linker_path_with_spaces_is_supported() {
#if defined(__unix__) || defined(__APPLE__)
    TemporaryDirectory root("cstc-repl-test");
    TemporaryDirectory tools("cstc-repl-tools");
    const fs::path linker_path = resolve_test_linker_program();
    const fs::path spaced_linker = tools.path() / "linker with spaces";

    std::error_code error;
    fs::create_symlink(linker_path, spaced_linker, error);
    assert(!error);

    cstc::repl::Session session({
        .session_root_dir = root.path(),
        .linker = spaced_linker.string(),
    });

    const auto result = expect_success(session, "1");
    assert(result.stdout_output == "1\n");
#endif
}

void test_cxx_environment_command_with_wrapper_and_flags_is_supported() {
#if defined(__unix__) || defined(__APPLE__)
    TemporaryDirectory root("cstc-repl-test");
    TemporaryDirectory tools("cstc-repl-tools");
    const fs::path linker_path = resolve_test_linker_program();
    const fs::path spaced_linker = tools.path() / "linker with spaces";
    const fs::path wrapper = tools.path() / "linker-wrapper";

    std::error_code error;
    fs::create_symlink(linker_path, spaced_linker, error);
    assert(!error);

    write_file(wrapper, "#!/bin/sh\nexec \"$@\"\n");
    make_executable(wrapper);

    const std::string cxx = "\"" + wrapper.string() + "\" \"" + spaced_linker.string() + "\" -v";
    const ScopedEnvironmentVariable scoped_cxx("CXX", cxx);

    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});
    const auto result = expect_success(session, "1");
    assert(result.stdout_output == "1\n");
#endif
}

void test_invalid_cxx_environment_command_falls_back_to_discovered_linker() {
    TemporaryDirectory root("cstc-repl-test");
    const ScopedEnvironmentVariable scoped_cxx("CXX", "cstc-repl-test-missing-linker -bad-flag");

    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});
    const auto result = expect_success(session, "1");
    assert(result.stdout_output == "1\n");
}

void test_needs_continuation_uses_structural_heuristics() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    assert(session.needs_continuation("fn inc(value: num) -> num {\n"));
    assert(session.needs_continuation("1 +"));
    assert(!session.needs_continuation("1 + 2"));
    assert(!session.needs_continuation("struct Point { x: num }"));
}

void test_reserved_names_are_rejected() {
    TemporaryDirectory root("cstc-repl-test");
    cstc::repl::Session session({.session_root_dir = root.path(), .linker = std::nullopt});

    const auto bad_main = expect_error(session, "fn main() { }");
    assert(contains(bad_main.error_message, "reserved"));

    const auto bad_binding = expect_error(session, "let __cstc_repl_internal_shadow: num = 1;");
    assert(contains(bad_binding.error_message, "reserved"));
}

} // namespace

int main() {
    test_let_bindings_persist_across_turns();
    test_expression_statements_are_not_persisted();
    test_item_definitions_persist_without_running_previous_turns();
    test_relative_imports_resolve_from_session_root();
    test_supported_result_types_are_rendered();
    test_unsupported_result_types_report_a_notice();
    test_commands_and_reset_clear_persisted_state();
    test_quit_command_requests_exit();
    test_dot_commands_are_rejected();
    test_startup_text_mentions_help_and_quit();
    test_runtime_errors_do_not_commit_new_state();
    test_missing_linker_reports_process_start_failure();
    test_custom_linker_path_with_spaces_is_supported();
    test_cxx_environment_command_with_wrapper_and_flags_is_supported();
    test_invalid_cxx_environment_command_falls_back_to_discovered_linker();
    test_needs_continuation_uses_structural_heuristics();
    test_reserved_names_are_rejected();
    return 0;
}
