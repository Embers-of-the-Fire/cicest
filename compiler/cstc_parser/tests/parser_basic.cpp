#include <cassert>
#include <string>
#include <string_view>
#include <variant>

#include <cstc_ast/ast.hpp>
#include <cstc_parser/parser.hpp>

namespace {

void test_parse_program_smoke() {
    constexpr std::string_view source = R"(
struct Empty;

enum State {
    Init,
    Running,
}

fn choose(flag: bool) -> State {
    if flag {
        State::Running
    } else {
        State::Init
    }
}
)";

    const auto parsed = cstc::parser::parse_source(source);
    assert(parsed.has_value());

    const cstc::ast::Program& program = *parsed;
    assert(program.items.size() == 3);
    assert(std::holds_alternative<cstc::ast::StructDecl>(program.items[0]));
    assert(std::holds_alternative<cstc::ast::EnumDecl>(program.items[1]));
    assert(std::holds_alternative<cstc::ast::FnDecl>(program.items[2]));
}

void test_duplicate_param_rejected() {
    constexpr std::string_view source = R"(
fn bad(x: num, x: num) -> num {
    x
}
)";

    const auto parsed = cstc::parser::parse_source(source);
    assert(!parsed.has_value());
    assert(parsed.error().message.find("duplicate parameter") != std::string::npos);
}

void test_parse_source_at_uses_absolute_spans() {
    constexpr std::string_view source = R"(
fn bad(x: num, x: num) -> num {
    x
}
)";

    const auto parsed = cstc::parser::parse_source_at(source, 512);
    assert(!parsed.has_value());
    assert(parsed.error().span.start >= 512);
    assert(parsed.error().span.end >= 512);
}

} // namespace

int main() {
    test_parse_program_smoke();
    test_duplicate_param_rejected();
    test_parse_source_at_uses_absolute_spans();
    return 0;
}
