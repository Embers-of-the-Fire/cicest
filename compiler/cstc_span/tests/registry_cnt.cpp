#include <cassert>
#include <cstc_span/registry.hpp>
#include <filesystem>
#include <print>

using namespace cstc::span;

int main() {
    const auto registry = SourceRegistry::create();

    const auto file_path = "./README.md";
    const auto path = registry->push_file(file_path);

    assert(path->filename() == "README.md");
    std::println("File: {}", path->path().string());
    assert(registry->stored_files_count() == 1);

    SourceSpan span{0, 10};
    const auto file = registry->locate_source_file(span);
    if (!file.has_value()) {
        file.error();
        assert(false);
    }
    assert(file.value().lock()->filename() == "README.md");

    return 0;
}
