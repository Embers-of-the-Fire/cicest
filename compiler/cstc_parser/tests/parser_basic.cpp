#include <cassert>
#include <string>
#include <string_view>
#include <variant>

#include <cstc_ast/ast.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>

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

void test_struct_literal_basic() {
    constexpr std::string_view source = R"(
struct Point {
    x: num,
    y: num,
}

fn make(a: num, b: num) -> Point {
    Point { x: a, y: b }
}
)";

    const auto parsed = cstc::parser::parse_source(source);
    assert(parsed.has_value());

    const auto& fn_decl = std::get<cstc::ast::FnDecl>(parsed->items[1]);
    assert(fn_decl.body->tail.has_value());
    const auto& tail = *fn_decl.body->tail;
    assert(std::holds_alternative<cstc::ast::StructInitExpr>(tail->node));

    const auto& init = std::get<cstc::ast::StructInitExpr>(tail->node);
    assert(init.type_name.as_str() == std::string_view{"Point"});
    assert(init.fields.size() == 2);
    assert(init.fields[0].name.as_str() == std::string_view{"x"});
    assert(init.fields[1].name.as_str() == std::string_view{"y"});
}

void test_struct_literal_empty() {
    constexpr std::string_view source = R"(
struct Empty;

fn make() -> Empty {
    Empty { }
}
)";

    const auto parsed = cstc::parser::parse_source(source);
    assert(parsed.has_value());

    const auto& fn_decl = std::get<cstc::ast::FnDecl>(parsed->items[1]);
    assert(fn_decl.body->tail.has_value());
    const auto& init = std::get<cstc::ast::StructInitExpr>((*fn_decl.body->tail)->node);
    assert(init.type_name.as_str() == std::string_view{"Empty"});
    assert(init.fields.empty());
}

void test_struct_literal_trailing_comma() {
    constexpr std::string_view source = R"(
struct Pair { a: num, b: num, }

fn make() -> Pair {
    Pair { a: 1, b: 2, }
}
)";

    const auto parsed = cstc::parser::parse_source(source);
    assert(parsed.has_value());

    const auto& fn_decl = std::get<cstc::ast::FnDecl>(parsed->items[1]);
    const auto& init =
        std::get<cstc::ast::StructInitExpr>((*fn_decl.body->tail)->node);
    assert(init.fields.size() == 2);
}

void test_struct_literal_nested() {
    constexpr std::string_view source = R"(
struct Inner { v: num, }
struct Outer { inner: Inner, w: num, }

fn make() -> Outer {
    Outer { inner: Inner { v: 10 }, w: 20 }
}
)";

    const auto parsed = cstc::parser::parse_source(source);
    assert(parsed.has_value());

    const auto& fn_decl = std::get<cstc::ast::FnDecl>(parsed->items[2]);
    const auto& outer =
        std::get<cstc::ast::StructInitExpr>((*fn_decl.body->tail)->node);
    assert(outer.type_name.as_str() == std::string_view{"Outer"});
    assert(outer.fields.size() == 2);

    const auto& inner_field_value = outer.fields[0].value;
    assert(std::holds_alternative<cstc::ast::StructInitExpr>(inner_field_value->node));
    const auto& inner = std::get<cstc::ast::StructInitExpr>(inner_field_value->node);
    assert(inner.type_name.as_str() == std::string_view{"Inner"});
}

void test_struct_literal_field_access() {
    // Foo { x: 1 }.x — struct literal as base of field access
    constexpr std::string_view source = R"(
struct Foo { x: num, }

fn get() -> num {
    Foo { x: 42 }.x
}
)";

    const auto parsed = cstc::parser::parse_source(source);
    assert(parsed.has_value());

    const auto& fn_decl = std::get<cstc::ast::FnDecl>(parsed->items[1]);
    assert(fn_decl.body->tail.has_value());
    assert(std::holds_alternative<cstc::ast::FieldAccessExpr>((*fn_decl.body->tail)->node));
}

void test_struct_literal_as_argument() {
    constexpr std::string_view source = R"(
struct Foo { v: num, }

fn consume(f: Foo) -> num { f.v }

fn test() -> num {
    consume(Foo { v: 7 })
}
)";

    const auto parsed = cstc::parser::parse_source(source);
    assert(parsed.has_value());

    const auto& fn_decl = std::get<cstc::ast::FnDecl>(parsed->items[2]);
    const auto& call = std::get<cstc::ast::CallExpr>((*fn_decl.body->tail)->node);
    assert(call.args.size() == 1);
    assert(std::holds_alternative<cstc::ast::StructInitExpr>(call.args[0]->node));
}

void test_struct_literal_let_binding() {
    constexpr std::string_view source = R"(
struct Vec2 { x: num, y: num, }

fn test() -> num {
    let v: Vec2 = Vec2 { x: 3, y: 4 };
    v.x
}
)";

    const auto parsed = cstc::parser::parse_source(source);
    assert(parsed.has_value());

    const auto& fn_decl = std::get<cstc::ast::FnDecl>(parsed->items[1]);
    const auto& let_stmt =
        std::get<cstc::ast::LetStmt>(fn_decl.body->statements[0]);
    assert(let_stmt.name.as_str() == std::string_view{"v"});
    assert(std::holds_alternative<cstc::ast::StructInitExpr>(let_stmt.initializer->node));
}

} // namespace

int main() {
    cstc::symbol::SymbolSession session;
    test_parse_program_smoke();
    test_duplicate_param_rejected();
    test_parse_source_at_uses_absolute_spans();
    test_struct_literal_basic();
    test_struct_literal_empty();
    test_struct_literal_trailing_comma();
    test_struct_literal_nested();
    test_struct_literal_field_access();
    test_struct_literal_as_argument();
    test_struct_literal_let_binding();
    return 0;
}
