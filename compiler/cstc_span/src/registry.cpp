#include "cstc_span/registry.hpp"
#include <filesystem>
#include <fstream>

namespace cstc::span {

std::shared_ptr<SourceRegistry> SourceRegistry::create() {
    auto registry = std::make_shared<SourceRegistry>();
    registry->self = registry;
    return registry;
}

[[nodiscard]] std::expected<SourceFile, SourceFileError>
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

[[nodiscard]] std::size_t SourceRegistry::stored_files_count() const noexcept {
    return static_cast<std::size_t>(source_files.size());
}

[[nodiscard]] std::expected<std::weak_ptr<SourceFile>, SourceSpanError>
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

[[nodiscard]] std::size_t SourceRegistry::next_file_start_offset() const noexcept {
    const auto last = source_files.end();
    if (last == source_files.begin())
        return 0;
    return std::prev(last)->get()->source_span.end + 1;
}

SourceFile::SourceFile(
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

bool SourceFile::operator==(const SourceFile& rhs) const noexcept {
    return file_symbol.index == rhs.file_symbol.index;
}

[[nodiscard]] std::string SourceFile::filename() const noexcept {
    return absolute_path.filename().string();
}

[[nodiscard]] std::string SourceFile::filestem() const noexcept {
    return absolute_path.stem().string();
}

[[nodiscard]] const std::filesystem::path& SourceFile::path() const noexcept {
    return absolute_path;
}

} // namespace cstc::span
