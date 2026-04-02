#include <cstc_error_report/report.hpp>

#include <algorithm>
#include <ranges>
#include <set>
#include <sstream>
#include <utility>

namespace cstc::error_report {

SourceId SourceDatabase::add_source(std::string name, std::string text) {
    SourceFile file;
    file.id = files_.size();
    file.name = std::move(name);
    file.text = std::move(text);
    file.line_starts = build_line_starts(file.text);
    files_.push_back(std::move(file));
    return files_.back().id;
}

const SourceFile* SourceDatabase::source(SourceId id) const {
    if (id >= files_.size())
        return nullptr;
    return &files_[id];
}

std::optional<SourcePoint> SourceDatabase::make_point(SourceId id, Offset offset) const {
    const SourceFile* file = source(id);
    if (file == nullptr || offset > file->size())
        return std::nullopt;
    return SourcePoint{
        .source_id = id,
        .offset = offset,
    };
}

std::optional<SourceSpan> SourceDatabase::make_span(SourceId id, Offset start, Offset end) const {
    const SourceFile* file = source(id);
    if (file == nullptr || end < start || end > file->size())
        return std::nullopt;
    return SourceSpan{
        .source_id = id,
        .start = start,
        .end = end,
    };
}

std::optional<ResolvedPoint> SourceDatabase::resolve_point(SourcePoint point) const {
    const SourceFile* file = source(point.source_id);
    if (file == nullptr || point.offset > file->size())
        return std::nullopt;
    return ResolvedPoint{
        .source_id = point.source_id,
        .file_name = file->name,
        .offset = point.offset,
        .location = resolve_location(*file, point.offset),
    };
}

std::optional<ResolvedSpan> SourceDatabase::resolve_span(SourceSpan span) const {
    const SourceFile* file = source(span.source_id);
    if (file == nullptr || span.end < span.start || span.start > file->size()
        || span.end > file->size())
        return std::nullopt;
    return ResolvedSpan{
        .source_id = span.source_id,
        .file_name = file->name,
        .span = span,
        .start = resolve_location(*file, span.start),
        .end = resolve_location(*file, span.end),
    };
}

std::size_t SourceDatabase::line_count(SourceId id) const {
    const SourceFile* file = source(id);
    if (file == nullptr)
        return 0;
    return file->line_starts.size();
}

std::string_view SourceDatabase::line_text(SourceId id, std::size_t line_number) const {
    const SourceFile* file = source(id);
    if (file == nullptr)
        return {};
    return line_text(*file, line_number);
}

std::vector<Offset> SourceDatabase::build_line_starts(std::string_view text) {
    std::vector<Offset> starts;
    starts.push_back(0);

    for (Offset index = 0; index < text.size(); ++index) {
        if (text[index] == '\n')
            starts.push_back(index + 1);
    }

    return starts;
}

Offset SourceDatabase::clamp_offset(const SourceFile& file, Offset offset) {
    return offset > file.size() ? file.size() : offset;
}

SourceLocation SourceDatabase::resolve_location(const SourceFile& file, Offset local_offset) {
    const Offset clamped = clamp_offset(file, local_offset);
    const auto upper = std::upper_bound(file.line_starts.begin(), file.line_starts.end(), clamped);
    const Offset line_index = upper == file.line_starts.begin()
                                ? 0
                                : static_cast<Offset>((upper - file.line_starts.begin()) - 1);
    const Offset line_start = file.line_starts.empty() ? 0 : file.line_starts[line_index];

    return SourceLocation{
        .line = line_index + 1,
        .column = (clamped - line_start) + 1,
    };
}

std::string_view SourceDatabase::line_text(const SourceFile& file, std::size_t line_number) {
    if (line_number == 0 || line_number > file.line_starts.size())
        return {};

    const Offset start = file.line_starts[line_number - 1];
    Offset end =
        line_number < file.line_starts.size() ? file.line_starts[line_number] : file.text.size();

    if (end > start && file.text[end - 1] == '\n')
        --end;
    if (end > start && file.text[end - 1] == '\r')
        --end;

    return std::string_view(file.text).substr(start, end - start);
}

namespace detail {

struct SourceGroup {
    SourceId source_id = 0;
    std::vector<const Label*> labels;
    std::vector<const Comment*> comments;
};

struct ResolvedLabel {
    const Label* label = nullptr;
    ResolvedSpan span;
};

struct ResolvedComment {
    const Comment* comment = nullptr;
    ResolvedPoint point;
};

[[nodiscard]] static std::string pad_left(std::string text, std::size_t width) {
    if (text.size() >= width)
        return text;
    return std::string(width - text.size(), ' ') + text;
}

[[nodiscard]] static std::string styled(
    std::string_view text, const cstc::ansi_color::Style& style, const RenderOptions& options) {
    return cstc::ansi_color::paint(text, style, options.color);
}

[[nodiscard]] static std::string_view severity_name(Severity severity) {
    switch (severity) {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    case Severity::Note: return "note";
    case Severity::Help: return "help";
    }
    return "error";
}

[[nodiscard]] static cstc::ansi_color::Style severity_style(Severity severity) {
    using cstc::ansi_color::Color;
    switch (severity) {
    case Severity::Error: return {.foreground = Color::Red, .bold = true};
    case Severity::Warning: return {.foreground = Color::Yellow, .bold = true};
    case Severity::Note: return {.foreground = Color::Blue, .bold = true};
    case Severity::Help: return {.foreground = Color::Green, .bold = true};
    }
    return {.foreground = Color::Red, .bold = true};
}

[[nodiscard]] static cstc::ansi_color::Style gutter_style() {
    using cstc::ansi_color::Color;
    return {.foreground = Color::Blue, .bold = true};
}

[[nodiscard]] static cstc::ansi_color::Style
    marker_style(LabelStyle style, Severity parent_severity) {
    using cstc::ansi_color::Color;
    if (style == LabelStyle::Primary)
        return severity_style(parent_severity);
    return {.foreground = Color::Cyan, .bold = true};
}

[[nodiscard]] static cstc::ansi_color::Style comment_style() {
    using cstc::ansi_color::Color;
    return {.foreground = Color::Blue, .bold = true};
}

[[nodiscard]] static std::vector<SourceGroup> collect_source_groups(const Diagnostic& diagnostic) {
    std::vector<SourceGroup> groups;

    auto get_group = [&groups](SourceId source_id) -> SourceGroup& {
        for (SourceGroup& group : groups) {
            if (group.source_id == source_id)
                return group;
        }

        SourceGroup group;
        group.source_id = source_id;
        groups.push_back(std::move(group));
        return groups.back();
    };

    for (const Label& label : diagnostic.labels)
        get_group(label.span.source_id).labels.push_back(&label);

    for (const Comment& comment : diagnostic.comments)
        get_group(comment.point.source_id).comments.push_back(&comment);

    return groups;
}

[[nodiscard]] static std::size_t line_number_width(std::size_t line_number) {
    return std::to_string(line_number).size();
}

[[nodiscard]] static std::pair<std::size_t, std::size_t> marker_columns(
    const SourceDatabase& database, const ResolvedSpan& resolved, std::size_t line_number) {
    const std::size_t line_length = database.line_text(resolved.source_id, line_number).size();

    if (resolved.span.length() > 0 && resolved.start.line != resolved.end.line
        && line_number == resolved.end.line && resolved.end.column == 1) {
        return {0, 0};
    }

    std::size_t start_column = 1;
    std::size_t end_column = line_length + 1;

    if (resolved.span.length() == 0) {
        start_column = std::min(resolved.start.column, line_length + 1);
        end_column = start_column + 1;
        return {start_column, end_column};
    }

    if (line_number == resolved.start.line)
        start_column = std::min(resolved.start.column, line_length + 1);
    if (line_number == resolved.end.line)
        end_column = std::min(resolved.end.column, line_length + 1);

    if (end_column <= start_column)
        end_column = start_column + 1;

    return {start_column, end_column};
}

[[nodiscard]] static std::size_t rendered_end_line(const ResolvedSpan& resolved) {
    if (resolved.span.length() == 0)
        return resolved.start.line;

    if (resolved.start.line != resolved.end.line && resolved.end.column == 1)
        return resolved.end.line - 1;

    return resolved.end.line;
}

[[nodiscard]] static bool render_source_group(
    std::ostringstream& out, const SourceDatabase& database, const Diagnostic& diagnostic,
    const SourceGroup& group, const RenderOptions& options) {
    std::vector<ResolvedLabel> resolved_labels;
    resolved_labels.reserve(group.labels.size());
    for (const Label* label : group.labels) {
        const auto resolved = database.resolve_span(label->span);
        if (resolved.has_value())
            resolved_labels.push_back(ResolvedLabel{.label = label, .span = *resolved});
    }

    std::vector<ResolvedComment> resolved_comments;
    resolved_comments.reserve(group.comments.size());
    for (const Comment* comment : group.comments) {
        const auto resolved = database.resolve_point(comment->point);
        if (resolved.has_value())
            resolved_comments.push_back(ResolvedComment{.comment = comment, .point = *resolved});
    }

    if (resolved_labels.empty() && resolved_comments.empty())
        return false;

    std::ranges::sort(resolved_labels, [](const ResolvedLabel& lhs, const ResolvedLabel& rhs) {
        if (lhs.span.span.start != rhs.span.span.start)
            return lhs.span.span.start < rhs.span.span.start;
        return lhs.span.span.end < rhs.span.span.end;
    });

    std::ranges::sort(
        resolved_comments, [](const ResolvedComment& lhs, const ResolvedComment& rhs) {
            return lhs.point.offset < rhs.point.offset;
        });

    const ResolvedSpan* first_label =
        resolved_labels.empty() ? nullptr : &resolved_labels.front().span;
    const ResolvedPoint* first_comment =
        resolved_comments.empty() ? nullptr : &resolved_comments.front().point;

    std::size_t header_line = 1;
    std::size_t header_column = 1;
    std::string_view header_file_name = "<unknown>";
    if (first_label != nullptr) {
        header_line = first_label->start.line;
        header_column = first_label->start.column;
        header_file_name = first_label->file_name;
    } else if (first_comment != nullptr) {
        header_line = first_comment->location.line;
        header_column = first_comment->location.column;
        header_file_name = first_comment->file_name;
    }

    std::set<std::size_t> lines;
    const std::size_t max_line = database.line_count(group.source_id);

    auto add_context_lines = [&](std::size_t start_line, std::size_t end_line) {
        if (max_line == 0)
            return;
        const std::size_t first =
            start_line > options.context_lines ? start_line - options.context_lines : 1;
        const std::size_t last = std::min(max_line, end_line + options.context_lines);
        for (std::size_t line = first; line <= last; ++line)
            lines.insert(line);
    };

    for (const ResolvedLabel& label : resolved_labels) {
        const std::size_t end_line = rendered_end_line(label.span);
        add_context_lines(label.span.start.line, end_line);
    }

    for (const ResolvedComment& comment : resolved_comments)
        add_context_lines(comment.point.location.line, comment.point.location.line);

    if (lines.empty())
        return false;

    const std::size_t width = line_number_width(*lines.rbegin());
    out << std::string(width, ' ') << ' ' << styled("-->", gutter_style(), options) << ' '
        << header_file_name << ':' << header_line << ':' << header_column << '\n';
    out << std::string(width, ' ') << ' ' << styled("|", gutter_style(), options) << '\n';

    std::size_t previous_line = 0;
    for (const std::size_t line_number : lines) {
        if (previous_line != 0 && line_number > previous_line + 1) {
            out << std::string(width, ' ') << ' ' << styled("|", gutter_style(), options) << '\n';
        }

        out << pad_left(std::to_string(line_number), width) << ' '
            << styled("|", gutter_style(), options) << ' '
            << database.line_text(group.source_id, line_number) << '\n';

        for (const ResolvedLabel& label : resolved_labels) {
            const std::size_t end_line = rendered_end_line(label.span);
            if (line_number < label.span.start.line || line_number > end_line)
                continue;

            const auto [start_column, end_column] =
                marker_columns(database, label.span, line_number);
            if (start_column == 0 && end_column == 0)
                continue;

            std::string marker(start_column > 0 ? start_column - 1 : 0, ' ');
            marker.append(
                std::max<std::size_t>(1, end_column - start_column),
                label.label->style == LabelStyle::Primary ? '^' : '-');
            if (line_number == label.span.start.line && !label.label->message.empty())
                marker += " " + label.label->message;

            out << std::string(width, ' ') << ' ' << styled("|", gutter_style(), options) << ' '
                << styled(marker, marker_style(label.label->style, diagnostic.severity), options)
                << '\n';
        }

        for (const ResolvedComment& comment : resolved_comments) {
            if (comment.point.location.line != line_number)
                continue;

            std::string marker(
                comment.point.location.column > 0 ? comment.point.location.column - 1 : 0, ' ');
            marker += '=';
            if (!comment.comment->message.empty())
                marker += " " + comment.comment->message;

            out << std::string(width, ' ') << ' ' << styled("|", gutter_style(), options) << ' '
                << styled(marker, comment_style(), options) << '\n';
        }

        previous_line = line_number;
    }

    return true;
}

static void render_diagnostic(
    std::ostringstream& out, const SourceDatabase& database, const Diagnostic& diagnostic,
    const RenderOptions& options) {
    std::string headline = std::string(severity_name(diagnostic.severity));
    if (diagnostic.code.has_value())
        headline += "[" + *diagnostic.code + "]";

    out << styled(headline, severity_style(diagnostic.severity), options) << ": "
        << diagnostic.message << '\n';

    const std::vector<SourceGroup> groups = collect_source_groups(diagnostic);
    bool rendered_any_group = false;
    for (const SourceGroup& group : groups) {
        std::ostringstream group_out;
        if (!render_source_group(group_out, database, diagnostic, group, options))
            continue;
        if (rendered_any_group)
            out << '\n';
        out << group_out.str();
        rendered_any_group = true;
    }

    bool rendered_any_child = false;
    for (const Diagnostic& child : diagnostic.children) {
        if (rendered_any_group || rendered_any_child)
            out << '\n';
        render_diagnostic(out, database, child, options);
        rendered_any_child = true;
    }
}

} // namespace detail

std::string render(
    const SourceDatabase& database, const Diagnostic& diagnostic, const RenderOptions& options) {
    std::ostringstream out;
    detail::render_diagnostic(out, database, diagnostic, options);
    return out.str();
}

} // namespace cstc::error_report
