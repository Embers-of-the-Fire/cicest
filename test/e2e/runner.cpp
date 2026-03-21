/// @file runner.cpp
/// @brief End-to-end test runner for Cicest compiler.
///
/// This runner discovers .cst test files, compiles them with cstc, runs the
/// resulting executables, and compares output against expected .stdout/.stderr
/// files.

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// Test kind determines expected behavior
enum class TestKind {
    Pass,         // Should compile and run successfully (exit 0)
    FailCompile,  // Should fail compilation
    FailRuntime   // Should compile but fail at runtime (exit != 0)
};

// Test result for a single test case
struct TestResult {
    std::string test_name;
    bool passed;
    std::string error_message;
};

// Configuration from command-line arguments
struct Config {
    fs::path test_dir;
    TestKind kind;
    fs::path cstc_binary;
};

// Parse command-line arguments
static Config parse_args(int argc, char** argv) {
    Config config;
    config.cstc_binary = CSTC_BINARY_PATH;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test-dir" && i + 1 < argc) {
            config.test_dir = argv[++i];
        } else if (arg == "--kind" && i + 1 < argc) {
            std::string kind_str = argv[++i];
            if (kind_str == "pass") {
                config.kind = TestKind::Pass;
            } else if (kind_str == "fail_compile") {
                config.kind = TestKind::FailCompile;
            } else if (kind_str == "fail_runtime") {
                config.kind = TestKind::FailRuntime;
            } else {
                std::cerr << "Unknown test kind: " << kind_str << "\n";
                std::exit(1);
            }
        }
    }

    if (config.test_dir.empty()) {
        std::cerr << "Usage: " << argv[0]
                  << " --test-dir <dir> --kind <pass|fail_compile|fail_runtime>\n";
        std::exit(1);
    }

    return config;
}

// Discover all .cst test files in a directory recursively
static std::vector<fs::path> discover_tests(const fs::path& test_dir) {
    std::vector<fs::path> tests;
    for (const auto& entry : fs::recursive_directory_iterator(test_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cst") {
            tests.push_back(entry.path());
        }
    }
    std::sort(tests.begin(), tests.end());
    return tests;
}

// Read entire file into a string
static std::string read_file(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Execute a command and capture stdout, stderr, and exit code
struct ExecResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

static ExecResult execute_command(const std::string& command) {
    ExecResult result;

    // Create temporary files for stdout and stderr
    char stdout_template[] = "/tmp/e2e_stdout_XXXXXX";
    char stderr_template[] = "/tmp/e2e_stderr_XXXXXX";
    int stdout_fd = mkstemp(stdout_template);
    int stderr_fd = mkstemp(stderr_template);

    if (stdout_fd == -1 || stderr_fd == -1) {
        std::cerr << "Failed to create temporary files\n";
        result.exit_code = -1;
        return result;
    }

    close(stdout_fd);
    close(stderr_fd);

    // Build command with redirections
    std::string full_command = command + " >" + stdout_template + " 2>" + stderr_template;
    int exit_code = system(full_command.c_str());
    result.exit_code = WEXITSTATUS(exit_code);

    // Read captured output
    result.stdout_output = read_file(stdout_template);
    result.stderr_output = read_file(stderr_template);

    // Clean up temporary files
    std::remove(stdout_template);
    std::remove(stderr_template);

    return result;
}

// Compare two strings and return true if they match
static bool strings_match(const std::string& actual, const std::string& expected) {
    return actual == expected;
}

// Show a diff between expected and actual output
static void show_diff(const std::string& expected, const std::string& actual) {
    std::cerr << "Expected:\n" << expected << "\n";
    std::cerr << "Actual:\n" << actual << "\n";
}

// Run a single test case
static TestResult run_test(const fs::path& test_file, const Config& config) {
    TestResult result;
    result.test_name = test_file.stem().string();
    result.passed = false;

    // Create temporary output file
    char output_template[] = "/tmp/e2e_output_XXXXXX";
    int output_fd = mkstemp(output_template);
    if (output_fd == -1) {
        result.error_message = "Failed to create temporary output file";
        return result;
    }
    close(output_fd);

    fs::path output_path = output_template;

    // Compile the test
    std::string compile_cmd = config.cstc_binary.string() + " " + test_file.string() +
                              " -o " + output_path.string();
    ExecResult compile_result = execute_command(compile_cmd);

    if (config.kind == TestKind::FailCompile) {
        // Test should fail compilation
        if (compile_result.exit_code == 0) {
            result.error_message = "Expected compilation to fail, but it succeeded";
            std::remove(output_path.c_str());
            return result;
        }

        // Check if expected stderr file exists
        fs::path expected_stderr = test_file;
        expected_stderr.replace_extension(".stderr");
        if (fs::exists(expected_stderr)) {
            std::string expected = read_file(expected_stderr);
            if (!strings_match(compile_result.stderr_output, expected)) {
                result.error_message = "Compiler stderr does not match expected";
                show_diff(expected, compile_result.stderr_output);
                return result;
            }
        }

        result.passed = true;
        return result;
    }

    // For Pass and FailRuntime, compilation should succeed
    if (compile_result.exit_code != 0) {
        result.error_message = "Compilation failed:\n" + compile_result.stderr_output;
        std::remove(output_path.c_str());
        return result;
    }

    // Run the compiled executable
    ExecResult run_result = execute_command(output_path.string());

    if (config.kind == TestKind::Pass) {
        // Test should run successfully
        if (run_result.exit_code != 0) {
            result.error_message = "Expected exit code 0, got " +
                                   std::to_string(run_result.exit_code) + "\nStderr:\n" +
                                   run_result.stderr_output;
            std::remove(output_path.c_str());
            return result;
        }

        // Check stdout
        fs::path expected_stdout = test_file;
        expected_stdout.replace_extension(".stdout");
        if (fs::exists(expected_stdout)) {
            std::string expected = read_file(expected_stdout);
            if (!strings_match(run_result.stdout_output, expected)) {
                result.error_message = "Stdout does not match expected";
                show_diff(expected, run_result.stdout_output);
                std::remove(output_path.c_str());
                return result;
            }
        }

        result.passed = true;
    } else if (config.kind == TestKind::FailRuntime) {
        // Test should fail at runtime
        if (run_result.exit_code == 0) {
            result.error_message = "Expected runtime failure, but exit code was 0";
            std::remove(output_path.c_str());
            return result;
        }

        // Check stderr
        fs::path expected_stderr = test_file;
        expected_stderr.replace_extension(".stderr");
        if (fs::exists(expected_stderr)) {
            std::string expected = read_file(expected_stderr);
            if (!strings_match(run_result.stderr_output, expected)) {
                result.error_message = "Runtime stderr does not match expected";
                show_diff(expected, run_result.stderr_output);
                std::remove(output_path.c_str());
                return result;
            }
        }

        result.passed = true;
    }

    std::remove(output_path.c_str());
    return result;
}

int main(int argc, char** argv) {
    Config config = parse_args(argc, argv);

    if (!fs::exists(config.test_dir)) {
        std::cerr << "Test directory does not exist: " << config.test_dir << "\n";
        return 1;
    }

    if (!fs::exists(config.cstc_binary)) {
        std::cerr << "Compiler binary does not exist: " << config.cstc_binary << "\n";
        return 1;
    }

    std::vector<fs::path> tests = discover_tests(config.test_dir);
    if (tests.empty()) {
        std::cerr << "No tests found in " << config.test_dir << "\n";
        return 1;
    }

    std::cout << "Running " << tests.size() << " tests from " << config.test_dir << "\n";

    std::vector<TestResult> results;
    int passed = 0;
    int failed = 0;

    for (const auto& test_file : tests) {
        std::cout << "Running " << test_file.filename() << "... ";
        std::cout.flush();

        TestResult result = run_test(test_file, config);
        results.push_back(result);

        if (result.passed) {
            std::cout << "PASSED\n";
            ++passed;
        } else {
            std::cout << "FAILED\n";
            std::cerr << "  Error: " << result.error_message << "\n";
            ++failed;
        }
    }

    std::cout << "\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

    return failed == 0 ? 0 : 1;
}
