#ifndef CICEST_COMPILER_CSTC_SPAN_SPAN_HPP
#define CICEST_COMPILER_CSTC_SPAN_SPAN_HPP

#include <cstddef>

namespace cstc::span {

/// Half-open source range in bytes: `[start, end)`.
struct SourceSpan {
    /// First byte offset included in the span.
    std::size_t start = 0;
    /// First byte offset excluded from the span.
    std::size_t end = 0;

    /// Returns the number of bytes covered by this span.
    [[nodiscard]] constexpr std::size_t length() const { return end >= start ? end - start : 0; }
};

/// Returns the minimal span covering both inputs.
[[nodiscard]] constexpr SourceSpan merge(const SourceSpan& lhs, const SourceSpan& rhs) {
    return {
        lhs.start < rhs.start ? lhs.start : rhs.start,
        lhs.end > rhs.end ? lhs.end : rhs.end,
    };
}

} // namespace cstc::span

#endif // CICEST_COMPILER_CSTC_SPAN_SPAN_HPP
