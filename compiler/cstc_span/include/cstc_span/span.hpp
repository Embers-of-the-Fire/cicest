#ifndef CICEST_COMPILER_CSTC_SPAN_SPAN_HPP
#define CICEST_COMPILER_CSTC_SPAN_SPAN_HPP

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cstc::span {

/// Absolute byte position in the global source map space.
using BytePos = std::size_t;

/// Stable identifier for a source file inside a `SourceMap`.
using SourceFileId = std::size_t;

/// Half-open source range in bytes: `[start, end)`.
struct SourceSpan {
    /// First byte offset included in the span.
    std::size_t start = 0;
    /// First byte offset excluded from the span.
    std::size_t end = 0;

    /// Returns the number of bytes covered by this span.
    [[nodiscard]] constexpr std::size_t length() const { return end >= start ? end - start : 0; }
};

/// 1-based line/column location in a source file.
struct SourceLocation {
    /// 1-based line index.
    std::size_t line = 1;
    /// 1-based column index.
    std::size_t column = 1;
};

/// Registered source file metadata tracked by `SourceMap`.
struct SourceFile {
    /// Source map file identifier.
    SourceFileId id = 0;
    /// Logical file name/path.
    std::string name;
    /// Entire source text.
    std::string source;
    /// Absolute start byte position in the global source map space.
    BytePos start_pos = 0;
    /// Absolute end byte position in the global source map space.
    BytePos end_pos = 0;
    /// Local byte offsets of each line start.
    std::vector<BytePos> line_starts;

    /// Returns file source byte length.
    [[nodiscard]] constexpr BytePos size() const {
        return end_pos >= start_pos ? end_pos - start_pos : 0;
    }

    /// Returns the full absolute span for this file source text.
    [[nodiscard]] constexpr SourceSpan span() const {
        return {
            .start = start_pos,
            .end = end_pos,
        };
    }
};

/// Span resolved against a `SourceMap`.
struct ResolvedSpan {
    /// File id owning the span.
    SourceFileId file_id = 0;
    /// File name owning the span.
    std::string_view file_name;
    /// Absolute span in global source map space.
    SourceSpan absolute;
    /// Local file-relative span.
    SourceSpan local;
    /// 1-based start location.
    SourceLocation start;
    /// 1-based end location.
    SourceLocation end;
};

/// Returns the minimal span covering both inputs.
[[nodiscard]] constexpr SourceSpan merge(const SourceSpan& lhs, const SourceSpan& rhs) {
    return {
        lhs.start < rhs.start ? lhs.start : rhs.start,
        lhs.end > rhs.end ? lhs.end : rhs.end,
    };
}

/// Rustc-style source map that assigns absolute byte ranges to files.
///
/// Spans from any file can be resolved back to file + local location using only
/// a single `SourceSpan` and this map.
class SourceMap {
public:
    /// Adds a file and assigns it a unique absolute byte range.
    ///
    /// File ranges are separated by one spare byte to avoid overlap ambiguity,
    /// mirroring rustc's global position strategy.
    [[nodiscard]] SourceFileId add_file(std::string file_name, std::string source_text) {
        const SourceFileId id = files_.size();
        const BytePos start_pos = next_start_pos_;
        const BytePos end_pos = start_pos + source_text.size();

        SourceFile file;
        file.id = id;
        file.name = std::move(file_name);
        file.source = std::move(source_text);
        file.start_pos = start_pos;
        file.end_pos = end_pos;
        file.line_starts = build_line_starts(file.source);

        files_.push_back(std::move(file));
        next_start_pos_ = end_pos + 1;
        return id;
    }

    /// Returns a file by id, or `nullptr` when the id is invalid.
    [[nodiscard]] const SourceFile* file(SourceFileId id) const {
        if (id >= files_.size())
            return nullptr;
        return &files_[id];
    }

    /// Returns the file containing the given absolute span, if any.
    [[nodiscard]] const SourceFile* lookup_file(SourceSpan span) const {
        if (span.end < span.start)
            return nullptr;

        for (const SourceFile& file_value : files_) {
            if (file_value.start_pos <= span.start && span.end <= file_value.end_pos)
                return &file_value;
        }

        return nullptr;
    }

    /// Creates an absolute span from file-local byte offsets.
    [[nodiscard]] std::optional<SourceSpan>
        make_span(SourceFileId id, BytePos local_start, BytePos local_end) const {
        const SourceFile* source_file = file(id);
        if (source_file == nullptr || local_end < local_start || local_end > source_file->size())
            return std::nullopt;

        return SourceSpan{
            .start = source_file->start_pos + local_start,
            .end = source_file->start_pos + local_end,
        };
    }

    /// Resolves a span into file-relative offsets and line/column locations.
    [[nodiscard]] std::optional<ResolvedSpan> resolve_span(SourceSpan span) const {
        const SourceFile* source_file = lookup_file(span);
        if (source_file == nullptr)
            return std::nullopt;

        const SourceSpan local_span{
            .start = span.start - source_file->start_pos,
            .end = span.end - source_file->start_pos,
        };

        return ResolvedSpan{
            .file_id = source_file->id,
            .file_name = source_file->name,
            .absolute = span,
            .local = local_span,
            .start = resolve_location(*source_file, span.start),
            .end = resolve_location(*source_file, span.end),
        };
    }

private:
    [[nodiscard]] static std::vector<BytePos> build_line_starts(std::string_view source_text) {
        std::vector<BytePos> starts;
        starts.push_back(0);

        for (BytePos index = 0; index < source_text.size(); ++index) {
            if (source_text[index] == '\n')
                starts.push_back(index + 1);
        }

        return starts;
    }

    [[nodiscard]] static SourceLocation
        resolve_location(const SourceFile& source_file, BytePos absolute_pos) {
        if (absolute_pos < source_file.start_pos)
            return SourceLocation{.line = 1, .column = 1};

        const BytePos clamped =
            absolute_pos > source_file.end_pos ? source_file.end_pos : absolute_pos;
        const BytePos local = clamped - source_file.start_pos;

        const auto upper =
            std::upper_bound(source_file.line_starts.begin(), source_file.line_starts.end(), local);
        const BytePos line_index =
            upper == source_file.line_starts.begin()
                ? 0
                : static_cast<BytePos>((upper - source_file.line_starts.begin()) - 1);
        const BytePos line_start =
            source_file.line_starts.empty() ? 0 : source_file.line_starts[line_index];

        return SourceLocation{
            .line = line_index + 1,
            .column = (local - line_start) + 1,
        };
    }

private:
    std::vector<SourceFile> files_;
    BytePos next_start_pos_ = 0;
};

} // namespace cstc::span

#endif // CICEST_COMPILER_CSTC_SPAN_SPAN_HPP
