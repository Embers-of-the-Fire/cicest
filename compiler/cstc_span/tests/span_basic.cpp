#include <cassert>

#include <cstc_span/span.hpp>

int main() {
    const cstc::span::SourceSpan a{.start = 2, .end = 7};
    const cstc::span::SourceSpan b{.start = 0, .end = 3};

    assert(a.length() == 5);
    assert(b.length() == 3);

    const cstc::span::SourceSpan merged = cstc::span::merge(a, b);
    assert(merged.start == 0);
    assert(merged.end == 7);
    assert(merged.length() == 7);

    return 0;
}
