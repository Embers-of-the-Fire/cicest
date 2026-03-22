#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace cstc::end_to_end {

enum class TestKind {
    Unspecified,
    Pass,
    FailCompile,
    FailRuntime,
};

[[nodiscard]] std::string_view test_kind_name(TestKind kind);
[[nodiscard]] std::optional<TestKind> parse_test_kind(std::string_view value);

struct TestResult {
    std::string test_name;
    std::filesystem::path test_file;
    std::string status = "failed";
    std::string error_message;

    [[nodiscard]] bool passed() const;
};

struct SuiteResult {
    std::filesystem::path test_dir;
    TestKind kind = TestKind::Unspecified;
    std::vector<TestResult> tests;

    [[nodiscard]] std::size_t total_count() const;
    [[nodiscard]] std::size_t passed_count() const;
    [[nodiscard]] std::size_t failed_count() const;
    [[nodiscard]] bool all_passed() const;
};

struct Config {
    std::filesystem::path test_dir;
    TestKind kind = TestKind::Unspecified;
    std::filesystem::path cstc_binary;
    std::optional<std::filesystem::path> report_path;
    bool quiet = false;
};

[[nodiscard]] Config parse_args(int argc, char** argv, std::string_view cstc_binary_path);
[[nodiscard]] SuiteResult run_suite(const Config& config);
void print_suite_summary(const SuiteResult& result, std::ostream& out, std::ostream& err);
void write_json_report(const SuiteResult& result, const std::filesystem::path& report_path);

} // namespace cstc::end_to_end
