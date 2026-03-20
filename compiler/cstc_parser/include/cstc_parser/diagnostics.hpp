#pragma once

#include <cstc_parser/parser.hpp>
#include <cstc_span/span.hpp>

#include <string>
#include <string_view>

namespace cstc::parser {

[[nodiscard]] inline std::string format_parse_error(
    const cstc::span::SourceMap& source_map, const ParseError& error,
    bool include_span_when_resolved = false) {
    if (const auto resolved = source_map.resolve_span(error.span); resolved.has_value()) {
        std::string message = "parse error " + std::string(resolved->file_name) + ":"
                            + std::to_string(resolved->start.line) + ":"
                            + std::to_string(resolved->start.column);
        if (include_span_when_resolved) {
            message += " [" + std::to_string(error.span.start) + ", "
                     + std::to_string(error.span.end) + ")";
        }
        return message + ": " + error.message;
    }

    return "parse error [" + std::to_string(error.span.start) + ", "
         + std::to_string(error.span.end) + "): " + error.message;
}

} // namespace cstc::parser
