#include <cassert>
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

void write_file(const fs::path& path, std::string_view contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    assert(file);
    file << contents;
    assert(file);
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

    (void)expect_success(session, "let x: num = 42;");
    const auto show = expect_success(session, ":show");
    assert(contains(show.info_message, "let x: num = 42;"));

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
    test_needs_continuation_uses_structural_heuristics();
    test_reserved_names_are_rejected();
    return 0;
}
