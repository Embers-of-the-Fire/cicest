#ifndef CICEST_LIBRARY_CSTC_ERROR_REPORT_REPORT_HPP
#define CICEST_LIBRARY_CSTC_ERROR_REPORT_REPORT_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cstc_ansi_color/ansi_color.hpp>

namespace cstc::error_report {

using Offset = std::size_t;
using SourceId = std::size_t;

struct SourcePoint {
    SourceId source_id = 0;
    Offset offset = 0;
};

struct SourceSpan {
    SourceId source_id = 0;
    Offset start = 0;
    Offset end = 0;

    [[nodiscard]] constexpr Offset length() const { return end >= start ? end - start : 0; }
};

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

struct SourceFile {
    SourceId id = 0;
    std::string name;
    std::string text;
    std::vector<Offset> line_starts;

    [[nodiscard]] constexpr Offset size() const { return text.size(); }
};

struct ResolvedPoint {
    SourceId source_id = 0;
    std::string_view file_name;
    Offset offset = 0;
    SourceLocation location;
};

struct ResolvedSpan {
    SourceId source_id = 0;
    std::string_view file_name;
    SourceSpan span;
    SourceLocation start;
    SourceLocation end;
};

class SourceDatabase {
public:
    [[nodiscard]] SourceId add_source(std::string name, std::string text);
    [[nodiscard]] const SourceFile* source(SourceId id) const;
    [[nodiscard]] std::optional<SourcePoint> make_point(SourceId id, Offset offset) const;
    [[nodiscard]] std::optional<SourceSpan> make_span(SourceId id, Offset start, Offset end) const;
    [[nodiscard]] std::optional<ResolvedPoint> resolve_point(SourcePoint point) const;
    [[nodiscard]] std::optional<ResolvedSpan> resolve_span(SourceSpan span) const;
    [[nodiscard]] std::size_t line_count(SourceId id) const;
    [[nodiscard]] std::string_view line_text(SourceId id, std::size_t line_number) const;
    [[nodiscard]] static std::vector<Offset> build_line_starts(std::string_view text);

private:
    [[nodiscard]] static Offset clamp_offset(const SourceFile& file, Offset offset);
    [[nodiscard]] static SourceLocation
        resolve_location(const SourceFile& file, Offset local_offset);
    [[nodiscard]] static std::string_view
        line_text(const SourceFile& file, std::size_t line_number);

    std::vector<SourceFile> files_;
};

enum class Severity {
    Error,
    Warning,
    Note,
    Help,
};

enum class LabelStyle {
    Primary,
    Secondary,
};

struct Label {
    SourceSpan span;
    std::string message;
    LabelStyle style = LabelStyle::Primary;
};

struct Comment {
    SourcePoint point;
    std::string message;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string message;
    std::optional<std::string> code;
    std::vector<Label> labels;
    std::vector<Comment> comments;
    std::vector<Diagnostic> children;
};

struct RenderOptions {
    cstc::ansi_color::Emission color = cstc::ansi_color::Emission::Never;
    std::size_t context_lines = 0;
};

[[nodiscard]] std::string render(
    const SourceDatabase& database, const Diagnostic& diagnostic,
    const RenderOptions& options = {});

} // namespace cstc::error_report

#endif // CICEST_LIBRARY_CSTC_ERROR_REPORT_REPORT_HPP
