#include <cassert>
#include <cstdint>
#include <cstdio>
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

void cstc_std_print(const cstc_rt_str* value);
void cstc_std_println(const cstc_rt_str* value);
}

namespace {

#ifdef _WIN32
int dup_fd(int fd) { return _dup(fd); }
int dup2_fd(int src, int dst) { return _dup2(src, dst); }
int close_fd(int fd) { return _close(fd); }
int file_number(FILE* file) { return _fileno(file); }
#else
int dup_fd(int fd) { return dup(fd); }
int dup2_fd(int src, int dst) { return dup2(src, dst); }
int close_fd(int fd) { return close(fd); }
int file_number(FILE* file) { return fileno(file); }
#endif

[[nodiscard]] std::string normalize_newlines(std::string text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n')
                continue;
            normalized.push_back('\n');
            continue;
        }
        normalized.push_back(text[index]);
    }
    return normalized;
}

template <typename Fn>
[[nodiscard]] std::string capture_stdout(Fn&& fn) {
    std::fflush(stdout);

    const int stdout_fd = file_number(stdout);
    assert(stdout_fd >= 0);

    const int saved_stdout = dup_fd(stdout_fd);
    assert(saved_stdout >= 0);

    FILE* capture = std::tmpfile();
    assert(capture != nullptr);

    assert(dup2_fd(file_number(capture), stdout_fd) >= 0);

    std::forward<Fn>(fn)();

    std::fflush(stdout);
    assert(dup2_fd(saved_stdout, stdout_fd) >= 0);
    close_fd(saved_stdout);

    assert(std::fseek(capture, 0, SEEK_END) == 0);
    const long size = std::ftell(capture);
    assert(size >= 0);
    assert(std::fseek(capture, 0, SEEK_SET) == 0);

    std::string output(static_cast<size_t>(size), '\0');
    if (!output.empty()) {
        const size_t read = std::fread(output.data(), 1, output.size(), capture);
        assert(read == output.size());
    }

    std::fclose(capture);
    return normalize_newlines(std::move(output));
}

void test_print_emits_bytes_for_non_empty_strings() {
    char bytes[] = "hello";
    cstc_rt_str value{&bytes[0], 5, 0};

    const std::string output = capture_stdout([&]() { cstc_std_print(&value); });

    assert(output == "hello");
}

void test_println_appends_newline_for_non_empty_strings() {
    char bytes[] = "hello";
    cstc_rt_str value{&bytes[0], 5, 0};

    const std::string output = capture_stdout([&]() { cstc_std_println(&value); });

    assert(output == "hello\n");
}

void test_print_skips_missing_or_empty_strings() {
    char empty_bytes[] = "";
    cstc_rt_str empty{&empty_bytes[0], 0, 0};
    cstc_rt_str missing_bytes{nullptr, 3, 0};

    const std::string output = capture_stdout([&]() {
        cstc_std_print(nullptr);
        cstc_std_print(&empty);
        cstc_std_print(&missing_bytes);
    });

    assert(output.empty());
}

void test_println_skips_missing_or_empty_strings() {
    char empty_bytes[] = "";
    cstc_rt_str empty{&empty_bytes[0], 0, 0};
    cstc_rt_str missing_bytes{nullptr, 3, 0};

    const std::string output = capture_stdout([&]() {
        cstc_std_println(nullptr);
        cstc_std_println(&empty);
        cstc_std_println(&missing_bytes);
    });

    assert(output.empty());
}

} // namespace

int main() {
    test_print_emits_bytes_for_non_empty_strings();
    test_println_appends_newline_for_non_empty_strings();
    test_print_skips_missing_or_empty_strings();
    test_println_skips_missing_or_empty_strings();
    return 0;
}
