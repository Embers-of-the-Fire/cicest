#ifndef CICEST_COMPILER_CSTC_SPAN_REGISTRY_HPP
#define CICEST_COMPILER_CSTC_SPAN_REGISTRY_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <vector>

#include <cstc_span/error.hpp>

namespace cstc::span {

class SourceFile;

/// Inclusive source span.
struct SourceSpan {
    std::size_t start;
    std::size_t end;
};

/// A index in the registry.
struct FileSymbol {
    std::size_t index;
};

class SourceRegistry {
private:
    std::vector<std::shared_ptr<SourceFile>> source_files;
    std::weak_ptr<SourceRegistry> self;

public:
    SourceRegistry() = default;
    SourceRegistry(SourceRegistry&&) = delete;
    SourceRegistry(const SourceRegistry&) = delete;

    static std::shared_ptr<SourceRegistry> create();

    [[nodiscard]] std::expected<SourceFile, SourceFileError>
        push_file(const std::filesystem::path& any_path);

    [[nodiscard]] std::size_t stored_files_count() const noexcept;
    [[nodiscard]] std::expected<std::weak_ptr<SourceFile>, SourceSpanError>
        locate_source_file(SourceSpan span) const noexcept;

    [[nodiscard]] std::size_t next_file_start_offset() const noexcept;
};

class SourceFile {
    friend class SourceRegistry;

private:
    FileSymbol file_symbol;
    std::filesystem::path absolute_path;
    std::weak_ptr<SourceRegistry> registry;

    std::string source_text;
    SourceSpan source_span;

    SourceFile(
        const std::filesystem::path& absolute_path, std::size_t index,
        std::weak_ptr<SourceRegistry> registry);

public:
    SourceFile() = delete;

    bool operator==(const SourceFile& rhs) const noexcept;

    /// Maybe an empty string if the `SourceFile` is invalid.
    [[nodiscard]] std::string filename() const noexcept;

    /// Maybe an empty string if the `SourceFile` is invalid.
    [[nodiscard]] std::string filestem() const noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
};

} // namespace cstc::span

#endif // CICEST_COMPILER_CSTC_SPAN_REGISTRY_HPP
