#include <cassert>

#include <cstc_span/span.hpp>

int main() {
    const cstc::span::SourceSpan a{.start = 2, .end = 7};
    const cstc::span::SourceSpan b{.start = 0, .end = 3};

    assert(a.length() == 5);
    assert(b.length() == 3);

    const cstc::span::SourceSpan merged = cstc::span::merge(a, b);
    assert(merged.start == 0);
    assert(merged.end == 7);
    assert(merged.length() == 7);

    cstc::span::SourceMap source_map;
    const cstc::span::SourceFileId foo_id = source_map.add_file("foo.cst", "let x = 1;\n");
    const cstc::span::SourceFileId bar_id = source_map.add_file("bar.cst", "fn main() {}\n");

    const cstc::span::SourceFile* foo_file = source_map.file(foo_id);
    const cstc::span::SourceFile* bar_file = source_map.file(bar_id);
    assert(foo_file != nullptr);
    assert(bar_file != nullptr);
    assert(foo_file->start_pos == 0);
    assert(bar_file->start_pos > foo_file->end_pos);

    const auto foo_keyword_span = source_map.make_span(foo_id, 0, 3);
    assert(foo_keyword_span.has_value());

    const auto resolved = source_map.resolve_span(*foo_keyword_span);
    assert(resolved.has_value());
    assert(resolved->file_id == foo_id);
    assert(resolved->file_name == "foo.cst");
    assert(resolved->local.start == 0);
    assert(resolved->local.end == 3);
    assert(resolved->start.line == 1);
    assert(resolved->start.column == 1);

    const cstc::span::SourceSpan cross_file{
        .start = foo_file->end_pos - 1,
        .end = bar_file->start_pos + 1,
    };
    assert(!source_map.resolve_span(cross_file).has_value());

    return 0;
}
