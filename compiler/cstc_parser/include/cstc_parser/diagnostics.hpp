#pragma once

#include <cstc_ansi_color/ansi_color.hpp>
#include <cstc_error_report/report.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_span/span.hpp>

#include <string>
#include <string_view>

namespace cstc::parser {

[[nodiscard]] inline std::string format_parse_error(
    const cstc::span::SourceMap& source_map, const ParseError& error,
    bool include_span_when_resolved = false) {
    std::string headline = "parse error: " + error.message;
    cstc::error_report::SourceDatabase database;
    cstc::error_report::Diagnostic diagnostic;
    diagnostic.severity = cstc::error_report::Severity::Error;
    diagnostic.message = headline;

    if (const auto resolved = source_map.resolve_span(error.span); resolved.has_value()) {
        if (include_span_when_resolved) {
            diagnostic.message += " [" + std::to_string(error.span.start) + ", "
                                + std::to_string(error.span.end) + ")";
        }

        if (const cstc::span::SourceFile* file = source_map.file(resolved->file_id);
            file != nullptr) {
            const cstc::error_report::SourceId source_id =
                database.add_source(file->name, file->source);
            const auto report_span =
                database.make_span(source_id, resolved->local.start, resolved->local.end);
            if (report_span.has_value()) {
                diagnostic.labels.push_back(
                    cstc::error_report::Label{
                        .span = *report_span,
                        .message = error.message,
                        .style = cstc::error_report::LabelStyle::Primary,
                    });
            }
        }
    }

    return cstc::error_report::render(
        database, diagnostic,
        cstc::error_report::RenderOptions{
            .color = cstc::ansi_color::detect_emission(),
            .context_lines = 1,
        });
}

} // namespace cstc::parser
