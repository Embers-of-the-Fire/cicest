#include <cassert>

#include <cstc_span/span.hpp>

namespace {

void test_lookup_file() {
    cstc::span::SourceMap map;
    const auto foo_id = map.add_file("foo.cst", "hello");   // size 5, start=0, end=5
    const auto bar_id = map.add_file("bar.cst", "world!");  // size 6, start=6, end=12

    const cstc::span::SourceFile* foo = map.file(foo_id);
    const cstc::span::SourceFile* bar = map.file(bar_id);

    // A span entirely inside foo
    const cstc::span::SourceSpan in_foo{.start = foo->start_pos + 1, .end = foo->start_pos + 3};
    assert(map.lookup_file(in_foo) == foo);

    // A span entirely inside bar
    const cstc::span::SourceSpan in_bar{.start = bar->start_pos, .end = bar->start_pos + 2};
    assert(map.lookup_file(in_bar) == bar);

    // A span that straddles the gap between files returns nullptr
    const cstc::span::SourceSpan cross{.start = foo->end_pos - 1, .end = bar->start_pos + 1};
    assert(map.lookup_file(cross) == nullptr);
}

void test_file_not_found() {
    cstc::span::SourceMap map;
    assert(map.file(0)   == nullptr);
    assert(map.file(999) == nullptr);
}

void test_file_size_and_span() {
    cstc::span::SourceMap map;
    const auto id = map.add_file("a.cst", "abcde");
    const cstc::span::SourceFile* f = map.file(id);

    assert(f != nullptr);
    assert(f->size() == 5);
    assert(f->span().length() == 5);
    assert(f->span().start == f->start_pos);
    assert(f->span().end   == f->end_pos);
    assert(f->name == "a.cst");
}

void test_resolve_span_line_column() {
    // "abc\ndef\n" — 8 bytes
    //  line 1 starts at local byte 0
    //  line 2 starts at local byte 4 (after '\n' at byte 3)
    //  line 3 starts at local byte 8 (after '\n' at byte 7)
    cstc::span::SourceMap map;
    const auto id = map.add_file("src.cst", "abc\ndef\n");

    // Span covering "abc" — local [0, 3)
    const auto span_abc = map.make_span(id, 0, 3);
    assert(span_abc.has_value());
    const auto r1 = map.resolve_span(*span_abc);
    assert(r1.has_value());
    assert(r1->file_id   == id);
    assert(r1->file_name == "src.cst");
    assert(r1->local.start == 0);
    assert(r1->local.end   == 3);
    assert(r1->start.line   == 1);
    assert(r1->start.column == 1);

    // Span covering "d" — local [4, 5)
    const auto span_d = map.make_span(id, 4, 5);
    assert(span_d.has_value());
    const auto r2 = map.resolve_span(*span_d);
    assert(r2.has_value());
    assert(r2->start.line   == 2);
    assert(r2->start.column == 1);

    // Span covering "ef" — local [5, 7)
    const auto span_ef = map.make_span(id, 5, 7);
    assert(span_ef.has_value());
    const auto r3 = map.resolve_span(*span_ef);
    assert(r3.has_value());
    assert(r3->start.line   == 2);
    assert(r3->start.column == 2);
}

void test_make_span_out_of_bounds() {
    cstc::span::SourceMap map;
    const auto id = map.add_file("f.cst", "hello");  // size 5

    // local_end > file size -> nullopt
    assert(!map.make_span(id, 0, 100).has_value());
    // local_start > local_end -> nullopt
    assert(!map.make_span(id, 3, 1).has_value());
    // bad file id -> nullopt
    assert(!map.make_span(999, 0, 3).has_value());

    // Exactly the full file is valid
    assert(map.make_span(id, 0, 5).has_value());
    // Empty span (zero length) is valid
    assert(map.make_span(id, 2, 2).has_value());
}

void test_multiple_files_ordering() {
    cstc::span::SourceMap map;
    const auto a = map.add_file("a.cst", "AAA");
    const auto b = map.add_file("b.cst", "BBBBB");
    const auto c = map.add_file("c.cst", "CC");

    const cstc::span::SourceFile* fa = map.file(a);
    const cstc::span::SourceFile* fb = map.file(b);
    const cstc::span::SourceFile* fc = map.file(c);

    // Files are laid out in order with at least one spare byte between them.
    assert(fa->end_pos <  fb->start_pos);
    assert(fb->end_pos <  fc->start_pos);
    assert(fa->size() == 3);
    assert(fb->size() == 5);
    assert(fc->size() == 2);
}

void test_resolve_span_not_in_any_file() {
    cstc::span::SourceMap map;
    // Empty map
    const cstc::span::SourceSpan nowhere{.start = 100, .end = 200};
    assert(!map.resolve_span(nowhere).has_value());

    // After adding a file, a span clearly outside it still returns nullopt
    static_cast<void>(map.add_file("x.cst", "hi"));
    const cstc::span::SourceSpan far{.start = 9000, .end = 9010};
    assert(!map.resolve_span(far).has_value());
}

void test_absolute_span_absolute_fields() {
    cstc::span::SourceMap map;
    static_cast<void>(map.add_file("pad.cst", "PADDING"));  // size 7, occupies [0,7); next start = 8
    const auto id = map.add_file("real.cst", "hello");  // start = 8

    const cstc::span::SourceFile* f = map.file(id);
    assert(f->start_pos == 8);

    const auto span = map.make_span(id, 1, 3);  // local [1,3) -> absolute [9,11)
    assert(span.has_value());
    assert(span->start == 9);
    assert(span->end   == 11);

    const auto resolved = map.resolve_span(*span);
    assert(resolved.has_value());
    assert(resolved->absolute.start == 9);
    assert(resolved->absolute.end   == 11);
    assert(resolved->local.start == 1);
    assert(resolved->local.end   == 3);
}

} // namespace

int main() {
    test_lookup_file();
    test_file_not_found();
    test_file_size_and_span();
    test_resolve_span_line_column();
    test_make_span_out_of_bounds();
    test_multiple_files_ordering();
    test_resolve_span_not_in_any_file();
    test_absolute_span_absolute_fields();
    return 0;
}
