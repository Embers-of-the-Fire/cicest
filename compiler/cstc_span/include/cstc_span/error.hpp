#ifndef CICEST_COMPILER_CSTC_SPAN_ERROR_HPP
#define CICEST_COMPILER_CSTC_SPAN_ERROR_HPP

namespace cstc::span {

enum class SourceFileError {
    SourceFileNotExists,
    SourceFileIsNotFile,
    SourceFileInvalid,
};

enum class SourceSpanError {
    SpanInvalid,
    SpanNotExists,
};

} // namespace cstc::span

#endif // CICEST_COMPILER_CSTC_SPAN_ERROR_HPP
