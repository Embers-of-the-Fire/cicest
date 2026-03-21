#include <cstc_end_to_end/end_to_end.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
# include <sys/wait.h>
#endif

namespace cstc::end_to_end {

namespace {

namespace fs = std::filesystem;

struct ExecResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
    std::string status_message;
    bool completed_normally = false;
    bool invocation_failed = false;

    [[nodiscard]] bool exited_successfully() const { return completed_normally && exit_code == 0; }
};

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view prefix)
        : path_(create(prefix)) {}

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    TemporaryDirectory(TemporaryDirectory&& other) noexcept
        : path_(std::move(other.path_)) {}

    TemporaryDirectory& operator=(TemporaryDirectory&& other) noexcept {
        if (this == &other)
            return *this;

        cleanup();
        path_ = std::move(other.path_);
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
            fs::path candidate =
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

[[nodiscard]] std::string usage(std::string_view program_name) {
    return "Usage: " + std::string(program_name)
         + " --test-dir <dir> --kind <pass|fail_compile|fail_runtime> [--report <path>] [--quiet]";
}

[[nodiscard]] std::vector<fs::path> discover_tests(const fs::path& test_dir) {
    std::vector<fs::path> tests;
    for (const auto& entry : fs::recursive_directory_iterator(test_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cst")
            tests.push_back(entry.path());
    }

    std::sort(tests.begin(), tests.end());
    return tests;
}

[[nodiscard]] std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return "";

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string normalize_line_endings(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\r') {
            normalized += '\n';
            if (index + 1 < value.size() && value[index + 1] == '\n')
                ++index;
            continue;
        }

        normalized += value[index];
    }

    return normalized;
}

[[nodiscard]] std::string escape_json(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }

    return escaped;
}

[[nodiscard]] std::string quote_shell_argument(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"')
            quoted += "\"\"";
        else
            quoted += ch;
    }
    quoted += '"';
    return quoted;
#else
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'')
            quoted += "'\\''";
        else
            quoted += ch;
    }
    quoted += '\'';
    return quoted;
#endif
}

[[nodiscard]] std::string join_command_arguments(const std::vector<std::string>& arguments) {
    std::ostringstream command;
    bool first = true;
    for (const std::string& argument : arguments) {
        if (!first)
            command << ' ';
        first = false;
        command << quote_shell_argument(argument);
    }
    return command.str();
}

[[nodiscard]] ExecResult interpret_exit_status(int raw_status) {
    ExecResult result;

    if (raw_status == -1) {
        result.invocation_failed = true;
        result.status_message = "failed to invoke the system shell";
        return result;
    }

#ifdef _WIN32
    result.completed_normally = true;
    result.exit_code = raw_status;
#else
    if (WIFEXITED(raw_status)) {
        result.completed_normally = true;
        result.exit_code = WEXITSTATUS(raw_status);
    } else if (WIFSIGNALED(raw_status)) {
        result.exit_code = 128 + WTERMSIG(raw_status);
        result.status_message = "terminated by signal " + std::to_string(WTERMSIG(raw_status));
    } else {
        result.status_message = "terminated abnormally";
    }
#endif

    return result;
}

[[nodiscard]] ExecResult execute_command(const std::vector<std::string>& arguments) {
    TemporaryDirectory temp_dir("cicest-e2e-command");
    const fs::path stdout_path = temp_dir.path() / "stdout.txt";
    const fs::path stderr_path = temp_dir.path() / "stderr.txt";

    const std::string command = join_command_arguments(arguments);
    const std::string full_command = command + " >" + quote_shell_argument(stdout_path.string())
                                   + " 2>" + quote_shell_argument(stderr_path.string());

    ExecResult result = interpret_exit_status(std::system(full_command.c_str()));
    result.stdout_output = read_file(stdout_path);
    result.stderr_output = read_file(stderr_path);
    return result;
}

[[nodiscard]] std::string format_output_difference(
    std::string_view label, const std::string& expected, const std::string& actual) {
    std::ostringstream message;
    message << label << " does not match expected output\n";
    message << "Expected:\n" << expected << "\n";
    message << "Actual:\n" << actual;
    return message.str();
}

[[nodiscard]] std::string format_exec_failure(std::string_view stage, const ExecResult& result) {
    std::ostringstream message;
    message << stage << " failed";
    if (!result.status_message.empty())
        message << " (" << result.status_message << ")";
    message << " with exit code " << result.exit_code;
    if (!result.stderr_output.empty())
        message << "\nStderr:\n" << result.stderr_output;
    return message.str();
}

[[nodiscard]] std::string relative_test_name(const fs::path& suite_dir, const fs::path& test_file) {
    std::error_code error;
    fs::path relative = fs::relative(test_file, suite_dir, error);
    if (error)
        relative = test_file.filename();

    relative.replace_extension();
    return relative.generic_string();
}

[[nodiscard]] fs::path resolve_executable_path(const fs::path& output_stem) {
    if (fs::exists(output_stem))
        return output_stem;

#ifdef _WIN32
    fs::path candidate = output_stem;
    candidate += ".exe";
    if (fs::exists(candidate))
        return candidate;
#endif

    return output_stem;
}

[[nodiscard]] TestResult run_test(const fs::path& test_file, const Config& config) {
    TestResult result;
    result.test_name = relative_test_name(config.test_dir, test_file);
    result.test_file = test_file;

    TemporaryDirectory build_dir("cicest-e2e-build");
    const fs::path output_stem = build_dir.path() / "artifact";
    const ExecResult compile_result = execute_command({
        config.cstc_binary.string(),
        test_file.string(),
        "-o",
        output_stem.string(),
        "--emit",
        "exe",
    });

    if (config.kind == TestKind::FailCompile) {
        if (compile_result.invocation_failed || !compile_result.completed_normally) {
            result.error_message = format_exec_failure("compilation", compile_result);
            return result;
        }

        if (compile_result.exit_code == 0) {
            result.error_message = "expected compilation to fail, but it succeeded";
            return result;
        }

        fs::path expected_stderr = test_file;
        expected_stderr.replace_extension(".stderr");
        if (fs::exists(expected_stderr)) {
            const std::string expected = normalize_line_endings(read_file(expected_stderr));
            const std::string actual = normalize_line_endings(compile_result.stderr_output);
            if (actual != expected) {
                result.error_message =
                    format_output_difference("compiler stderr", expected, actual);
                return result;
            }
        }

        result.status = "passed";
        return result;
    }

    if (!compile_result.exited_successfully()) {
        result.error_message = format_exec_failure("compilation", compile_result);
        return result;
    }

    const fs::path executable_path = resolve_executable_path(output_stem);
    const ExecResult run_result = execute_command({executable_path.string()});

    if (config.kind == TestKind::Pass) {
        if (!run_result.exited_successfully()) {
            result.error_message = format_exec_failure("execution", run_result);
            return result;
        }

        fs::path expected_stdout = test_file;
        expected_stdout.replace_extension(".stdout");
        if (fs::exists(expected_stdout)) {
            const std::string expected = normalize_line_endings(read_file(expected_stdout));
            const std::string actual = normalize_line_endings(run_result.stdout_output);
            if (actual != expected) {
                result.error_message = format_output_difference("stdout", expected, actual);
                return result;
            }
        }

        result.status = "passed";
        return result;
    }

    if (run_result.exited_successfully()) {
        result.error_message = "expected runtime failure, but the program exited successfully";
        return result;
    }

    if (run_result.invocation_failed) {
        result.error_message = format_exec_failure("execution", run_result);
        return result;
    }

    fs::path expected_stderr = test_file;
    expected_stderr.replace_extension(".stderr");
    if (fs::exists(expected_stderr)) {
        const std::string expected = normalize_line_endings(read_file(expected_stderr));
        const std::string actual = normalize_line_endings(run_result.stderr_output);
        if (actual != expected) {
            result.error_message = format_output_difference("runtime stderr", expected, actual);
            return result;
        }
    }

    result.status = "passed";
    return result;
}

} // namespace

std::string_view test_kind_name(const TestKind kind) {
    switch (kind) {
    case TestKind::Pass: return "pass";
    case TestKind::FailCompile: return "fail_compile";
    case TestKind::FailRuntime: return "fail_runtime";
    case TestKind::Unspecified: return "unspecified";
    }

    return "unspecified";
}

std::optional<TestKind> parse_test_kind(const std::string_view value) {
    if (value == "pass")
        return TestKind::Pass;
    if (value == "fail_compile")
        return TestKind::FailCompile;
    if (value == "fail_runtime")
        return TestKind::FailRuntime;
    return std::nullopt;
}

bool TestResult::passed() const { return status == "passed"; }

std::size_t SuiteResult::total_count() const { return tests.size(); }

std::size_t SuiteResult::passed_count() const {
    return static_cast<std::size_t>(std::count_if(
        tests.begin(), tests.end(), [](const TestResult& result) { return result.passed(); }));
}

std::size_t SuiteResult::failed_count() const { return total_count() - passed_count(); }

bool SuiteResult::all_passed() const { return failed_count() == 0; }

Config parse_args(int argc, char** argv, const std::string_view cstc_binary_path) {
    Config config;
    config.cstc_binary = cstc_binary_path;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];

        if (argument == "--test-dir") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --test-dir\n" + usage(argv[0]));
            config.test_dir = argv[++index];
            continue;
        }

        if (argument == "--kind") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --kind\n" + usage(argv[0]));

            const std::string_view kind_value = argv[++index];
            const std::optional<TestKind> kind = parse_test_kind(kind_value);
            if (!kind.has_value()) {
                throw std::runtime_error(
                    "unknown test kind: " + std::string(kind_value) + "\n" + usage(argv[0]));
            }

            config.kind = *kind;
            continue;
        }

        if (argument == "--report") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --report\n" + usage(argv[0]));
            config.report_path = fs::path(argv[++index]);
            continue;
        }

        if (argument == "--quiet") {
            config.quiet = true;
            continue;
        }

        throw std::runtime_error(
            "unknown option: " + std::string(argument) + "\n" + usage(argv[0]));
    }

    if (config.test_dir.empty())
        throw std::runtime_error("missing --test-dir\n" + usage(argv[0]));

    if (config.kind == TestKind::Unspecified)
        throw std::runtime_error("missing --kind\n" + usage(argv[0]));

    return config;
}

SuiteResult run_suite(const Config& config) {
    if (!fs::exists(config.test_dir)) {
        throw std::runtime_error("test directory does not exist: " + config.test_dir.string());
    }

    if (!fs::exists(config.cstc_binary)) {
        throw std::runtime_error("compiler binary does not exist: " + config.cstc_binary.string());
    }

    SuiteResult suite;
    suite.test_dir = config.test_dir;
    suite.kind = config.kind;
    suite.tests.reserve(16);

    const std::vector<fs::path> tests = discover_tests(config.test_dir);
    if (tests.empty()) {
        throw std::runtime_error("no tests found in " + config.test_dir.string());
    }

    suite.tests.reserve(tests.size());
    for (const fs::path& test_file : tests)
        suite.tests.push_back(run_test(test_file, config));

    return suite;
}

void print_suite_summary(const SuiteResult& result, std::ostream& out, std::ostream& err) {
    out << "Suite " << test_kind_name(result.kind) << " (" << result.test_dir.string()
        << "): " << result.total_count() << " total, " << result.passed_count() << " passed, "
        << result.failed_count() << " failed\n";

    if (result.all_passed())
        return;

    err << "Failures:\n";
    for (const TestResult& test : result.tests) {
        if (test.passed())
            continue;

        err << "  [" << test_kind_name(result.kind) << "] " << test.test_name << "\n";
        err << "    " << test.test_file.string() << "\n";
        if (!test.error_message.empty())
            err << test.error_message << "\n";
    }
}

void write_json_report(const SuiteResult& result, const fs::path& report_path) {
    const fs::path parent = report_path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        fs::create_directories(parent, error);
        if (error) {
            throw std::runtime_error(
                "failed to create report directory '" + parent.string() + "': " + error.message());
        }
    }

    std::ofstream report(report_path, std::ios::binary);
    if (!report) {
        throw std::runtime_error("failed to open report file: " + report_path.string());
    }

    report << "{\n";
    report << R"(  "kind": ")" << escape_json(test_kind_name(result.kind)) << "\",\n";
    report << R"(  "test_dir": ")" << escape_json(result.test_dir.generic_string()) << "\",\n";
    report << "  \"total\": " << result.total_count() << ",\n";
    report << "  \"passed\": " << result.passed_count() << ",\n";
    report << "  \"failed\": " << result.failed_count() << ",\n";
    report << "  \"tests\": [\n";

    for (std::size_t index = 0; index < result.tests.size(); ++index) {
        const TestResult& test = result.tests[index];
        report << "    {\n";
        report << R"(      "name": ")" << escape_json(test.test_name) << "\",\n";
        report << R"(      "path": ")" << escape_json(test.test_file.generic_string()) << "\",\n";
        report << R"(      "status": ")" << escape_json(test.status) << "\",\n";
        report << R"(      "error": ")" << escape_json(test.error_message) << "\"\n";
        report << "    }";
        if (index + 1 < result.tests.size())
            report << ",";
        report << "\n";
    }

    report << "  ]\n";
    report << "}\n";
}

} // namespace cstc::end_to_end
