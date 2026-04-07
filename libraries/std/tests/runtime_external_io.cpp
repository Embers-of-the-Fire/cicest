#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

#ifdef _WIN32
# include <io.h>
#else
# include <unistd.h>
#endif

extern "C" {

struct cstc_rt_str {
    char* data;
    std::uint64_t len;
    std::uint8_t owns_bytes;
};

void cstc_std_read_file(cstc_rt_str* out, const cstc_rt_str* path);
void cstc_std_read_line(cstc_rt_str* out);
double cstc_std_rand(void);
double cstc_std_time(void);
void cstc_std_env(cstc_rt_str* out, const cstc_rt_str* name);
void cstc_std_str_free(const cstc_rt_str* value);
}

namespace {

#ifdef _WIN32
int dup_fd(int fd) { return _dup(fd); }
int dup2_fd(int src, int dst) { return _dup2(src, dst); }
int close_fd(int fd) { return _close(fd); }
int file_number(FILE* file) { return _fileno(file); }
int set_env_var(const char* name, const char* value) { return _putenv_s(name, value); }
int unset_env_var(const char* name) { return _putenv_s(name, ""); }
#else
int dup_fd(int fd) { return dup(fd); }
int dup2_fd(int src, int dst) { return dup2(src, dst); }
int close_fd(int fd) { return close(fd); }
int file_number(FILE* file) { return fileno(file); }
int set_env_var(const char* name, const char* value) { return setenv(name, value, 1); }
int unset_env_var(const char* name) { return unsetenv(name); }
#endif

[[nodiscard]] cstc_rt_str borrowed_string(std::string& text) {
    return cstc_rt_str{text.data(), static_cast<std::uint64_t>(text.size()), 0};
}

[[nodiscard]] std::filesystem::path make_temp_path(const std::string& stem) {
    const auto base = std::filesystem::temp_directory_path();
    const auto unique = stem + "-" + std::to_string(std::rand()) + ".txt";
    return base / unique;
}

void test_read_file_reads_owned_bytes() {
    const std::string contents = "hello\nworld";
    const std::filesystem::path path = make_temp_path("cicest-read-file");

    FILE* file = std::fopen(path.string().c_str(), "wb");
    assert(file != nullptr);
    const size_t written = std::fwrite(contents.data(), 1, contents.size(), file);
    assert(written == contents.size());
    assert(std::fclose(file) == 0);

    std::string path_text = path.string();
    const cstc_rt_str path_value = borrowed_string(path_text);
    cstc_rt_str out{};

    cstc_std_read_file(&out, &path_value);

    assert(out.data != nullptr);
    assert(out.len == contents.size());
    assert(out.owns_bytes == 1);
    assert(std::memcmp(out.data, contents.data(), contents.size()) == 0);

    cstc_std_str_free(&out);
    std::filesystem::remove(path);
}

void test_read_file_missing_path_returns_borrowed_empty() {
    const std::filesystem::path path = make_temp_path("cicest-read-file-missing");
    std::string path_text = path.string();
    const cstc_rt_str path_value = borrowed_string(path_text);
    cstc_rt_str out{};

    cstc_std_read_file(&out, &path_value);

    assert(out.data != nullptr);
    assert(out.len == 0);
    assert(out.owns_bytes == 0);
}

template <typename Fn>
auto with_stdin_contents(const std::string& input, Fn&& fn) {
    static bool stdin_unbuffered = []() { return std::setvbuf(stdin, nullptr, _IONBF, 0) == 0; }();
    assert(stdin_unbuffered);

    const int stdin_fd = file_number(stdin);
    assert(stdin_fd >= 0);

    const int saved_stdin = dup_fd(stdin_fd);
    assert(saved_stdin >= 0);

    FILE* capture = std::tmpfile();
    assert(capture != nullptr);
    const size_t written = std::fwrite(input.data(), 1, input.size(), capture);
    assert(written == input.size());
    assert(std::fflush(capture) == 0);
    assert(std::fseek(capture, 0, SEEK_SET) == 0);
    assert(dup2_fd(file_number(capture), stdin_fd) >= 0);
    std::clearerr(stdin);

    auto result = std::forward<Fn>(fn)();

    assert(dup2_fd(saved_stdin, stdin_fd) >= 0);
    std::clearerr(stdin);
    close_fd(saved_stdin);
    std::fclose(capture);
    return result;
}

void test_read_line_reads_single_line_without_newline() {
    cstc_rt_str out = with_stdin_contents("hello\nworld\n", []() {
        cstc_rt_str value{};
        cstc_std_read_line(&value);
        return value;
    });

    assert(out.data != nullptr);
    assert(out.len == 5);
    assert(out.owns_bytes == 1);
    assert(std::memcmp(out.data, "hello", 5) == 0);

    cstc_std_str_free(&out);
}

void test_read_line_eof_returns_borrowed_empty() {
    cstc_rt_str out = with_stdin_contents("", []() {
        cstc_rt_str value{};
        cstc_std_read_line(&value);
        return value;
    });

    assert(out.data != nullptr);
    assert(out.len == 0);
    assert(out.owns_bytes == 0);
}

void test_rand_returns_value_in_unit_interval() {
    for (int index = 0; index < 32; ++index) {
        const double value = cstc_std_rand();
        assert(value >= 0.0);
        assert(value < 1.0);
    }
}

void test_time_returns_positive_timestamp() {
    const double first = cstc_std_time();
    const double second = cstc_std_time();

    assert(first > 0.0);
    assert(second >= first);
}

void test_env_copies_variable_value() {
    const char* name = "CICEST_RUNTIME_TEST_ENV";
    assert(set_env_var(name, "hello-runtime") == 0);

    std::string env_name = name;
    const cstc_rt_str env_key = borrowed_string(env_name);
    cstc_rt_str out{};

    cstc_std_env(&out, &env_key);
    assert(set_env_var(name, "updated") == 0);

    assert(out.data != nullptr);
    assert(out.len == std::strlen("hello-runtime"));
    assert(out.owns_bytes == 1);
    assert(std::memcmp(out.data, "hello-runtime", out.len) == 0);

    cstc_std_str_free(&out);
    assert(unset_env_var(name) == 0);
}

void test_env_missing_variable_returns_borrowed_empty() {
    const char* name = "CICEST_RUNTIME_TEST_ENV_MISSING";
    assert(unset_env_var(name) == 0);

    std::string env_name = name;
    const cstc_rt_str env_key = borrowed_string(env_name);
    cstc_rt_str out{};

    cstc_std_env(&out, &env_key);

    assert(out.data != nullptr);
    assert(out.len == 0);
    assert(out.owns_bytes == 0);
}

} // namespace

int main() {
    test_read_file_reads_owned_bytes();
    test_read_file_missing_path_returns_borrowed_empty();
    test_read_line_reads_single_line_without_newline();
    test_read_line_eof_returns_borrowed_empty();
    test_rand_returns_value_in_unit_interval();
    test_time_returns_positive_timestamp();
    test_env_copies_variable_value();
    test_env_missing_variable_returns_borrowed_empty();
    return 0;
}
