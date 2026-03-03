#ifndef CICEST_COMPILER_CSTC_SPAN_REGISTRY_HPP
#define CICEST_COMPILER_CSTC_SPAN_REGISTRY_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
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

    static std::shared_ptr<SourceRegistry> create() {
        auto registry = std::make_shared<SourceRegistry>();
        registry->self = registry;
        return registry;
    }

    [[nodiscard]] std::expected<SourceFile, SourceFileError>
        push_file(const std::filesystem::path& any_path);

    [[nodiscard]] std::size_t stored_files_count() const noexcept {
        return static_cast<std::size_t>(source_files.size());
    }

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
        std::weak_ptr<SourceRegistry> registry)
        : file_symbol{.index = index}
        , absolute_path(absolute_path)
        , registry(registry) {
        std::ifstream file(absolute_path);
        if (!file)
            return;

        source_text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        const auto start = registry.lock()->next_file_start_offset();
        source_span = {.start = start, .end = start + source_text.size()};
    }

public:
    SourceFile() = delete;

    bool operator==(const SourceFile& rhs) const noexcept {
        return file_symbol.index == rhs.file_symbol.index;
    }

    /// Maybe an empty string if the `SourceFile` is invalid.
    [[nodiscard]] std::string filename() const noexcept {
        return absolute_path.filename().string();
    }

    /// Maybe an empty string if the `SourceFile` is invalid.
    [[nodiscard]] std::string filestem() const noexcept {
        return absolute_path.stem().string();
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return absolute_path;
    }
};

// SourceRegistry method bodies that require SourceFile to be fully defined.

inline std::expected<std::weak_ptr<SourceFile>, SourceSpanError>
    SourceRegistry::locate_source_file(SourceSpan span) const noexcept {
    for (auto it = source_files.begin(); it != source_files.end(); ++it) {
        const auto file = it->get()->source_span;
        if (file.end < span.start)
            continue;
        if (file.start <= span.start && file.end >= span.end)
            return std::weak_ptr(*it);
        if (file.start <= span.start && file.end < span.end)
            return std::unexpected(SourceSpanError::SpanInvalid);
    }

    return std::unexpected(SourceSpanError::SpanNotExists);
}

inline std::size_t SourceRegistry::next_file_start_offset() const noexcept {
    const auto last = source_files.end();
    if (last == source_files.begin())
        return 0;
    return std::prev(last)->get()->source_span.end + 1;
}

inline std::expected<SourceFile, SourceFileError>
    SourceRegistry::push_file(const std::filesystem::path& any_path) {
    namespace fs = std::filesystem;

    if (!fs::exists(any_path))
        return std::unexpected(SourceFileError::SourceFileNotExists);
    if (!fs::is_regular_file(any_path))
        return std::unexpected(SourceFileError::SourceFileIsNotFile);

    const auto absolute_path = fs::absolute(any_path);
    const auto index = source_files.size();
    SourceFile file{
        absolute_path,
        index,
        self,
    };
    source_files.push_back(std::make_shared<SourceFile>(file));

    return file;
}

} // namespace cstc::span

#endif // CICEST_COMPILER_CSTC_SPAN_REGISTRY_HPP
