#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>

extern "C" {

struct cstc_rt_str {
    char* data;
    std::uint64_t len;
    std::uint8_t owns_bytes;
};

void cstc_std_str_concat(cstc_rt_str* out, const cstc_rt_str* a, const cstc_rt_str* b);
void cstc_std_str_free(const cstc_rt_str* value);
}

namespace {

void test_concat_allocates_joined_bytes() {
    char left_bytes[] = "hello ";
    char right_bytes[] = "world";
    cstc_rt_str out{};
    cstc_rt_str left{&left_bytes[0], 6, 0};
    cstc_rt_str right{&right_bytes[0], 5, 0};

    cstc_std_str_concat(&out, &left, &right);

    assert(out.data != nullptr);
    assert(out.len == 11);
    assert(out.owns_bytes == 1);
    assert(std::strcmp(out.data, "hello world") == 0);

    cstc_std_str_free(&out);
}

void test_concat_overflow_returns_borrowed_empty() {
    char left_bytes[] = "x";
    char right_bytes[] = "y";
    cstc_rt_str out{};
    cstc_rt_str left{&left_bytes[0], std::numeric_limits<std::uint64_t>::max(), 0};
    cstc_rt_str right{&right_bytes[0], 1, 0};

    cstc_std_str_concat(&out, &left, &right);

    assert(out.data != nullptr);
    assert(out.len == 0);
    assert(out.owns_bytes == 0);
    assert(out.data[0] == '\0');
}

} // namespace

int main() {
    test_concat_allocates_joined_bytes();
    test_concat_overflow_returns_borrowed_empty();
    return 0;
}
