#include <cassert>

#include <cstc_parser/diagnostics.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>

namespace {

void test_format_parse_error_renders_source_snippet() {
    cstc::symbol::SymbolSession session;

    const std::string source = "fn main() {\n    let value = 41\n}\n";
    const auto parsed = cstc::parser::parse_source(source);
    assert(!parsed.has_value());

    cstc::span::SourceMap source_map;
    static_cast<void>(source_map.add_file("main.cst", source));

    const std::string rendered = cstc::parser::format_parse_error(source_map, parsed.error());
    assert(
        rendered.find("error: parse error: expected `;` after let statement") != std::string::npos);
    assert(rendered.find("--> main.cst:3:1") != std::string::npos);
    assert(rendered.find("2 |     let value = 41") != std::string::npos);
    assert(rendered.find("expected `;` after let statement") != std::string::npos);
}

} // namespace

int main() {
    test_format_parse_error_renders_source_snippet();
    return 0;
}
