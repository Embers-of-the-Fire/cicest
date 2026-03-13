#include <cassert>
#include <string>

#include <cstc_tyir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>

using namespace cstc::tyir_builder;
using namespace cstc::tyir;
using namespace cstc::symbol;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static TyProgram must_lower(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(tyir.has_value());
    return *tyir;
}

static void must_fail(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(!tyir.has_value());
}

// ─── Empty program ────────────────────────────────────────────────────────────

static void test_empty_program() {
    const auto prog = must_lower("");
    assert(prog.items.empty());
}

// ─── Struct declaration ───────────────────────────────────────────────────────

static void test_struct_decl() {
    const auto prog = must_lower("struct Point { x: num, y: num }");
    assert(prog.items.size() == 1);

    const auto& decl = std::get<TyStructDecl>(prog.items[0]);
    assert(decl.name == Symbol::intern("Point"));
    assert(!decl.is_zst);
    assert(decl.fields.size() == 2);
    assert(decl.fields[0].name == Symbol::intern("x"));
    assert(decl.fields[0].ty == ty::num());
    assert(decl.fields[1].name == Symbol::intern("y"));
    assert(decl.fields[1].ty == ty::num());
}

static void test_zst_struct() {
    const auto prog = must_lower("struct Marker;");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyStructDecl>(prog.items[0]);
    assert(decl.is_zst);
    assert(decl.fields.empty());
}

static void test_struct_with_named_field() {
    const auto prog = must_lower(
        "struct Color;"
        "struct Brush { c: Color }");
    assert(prog.items.size() == 2);
    const auto& brush = std::get<TyStructDecl>(prog.items[1]);
    assert(brush.fields[0].ty == ty::named(Symbol::intern("Color")));
}

static void test_struct_undefined_type_error() { must_fail("struct Foo { x: Unknown }"); }

// ─── Enum declaration ─────────────────────────────────────────────────────────

static void test_enum_decl() {
    const auto prog = must_lower("enum Dir { North, South, East, West }");
    assert(prog.items.size() == 1);

    const auto& decl = std::get<TyEnumDecl>(prog.items[0]);
    assert(decl.name == Symbol::intern("Dir"));
    assert(decl.variants.size() == 4);
    assert(decl.variants[0].name == Symbol::intern("North"));
    assert(decl.variants[3].name == Symbol::intern("West"));
}

// ─── Function declaration ─────────────────────────────────────────────────────

static void test_fn_no_return() {
    const auto prog = must_lower("fn noop() { }");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.name == Symbol::intern("noop"));
    assert(fn.return_ty == ty::unit());
    assert(fn.params.empty());
}

static void test_fn_with_params_and_return() {
    const auto prog = must_lower("fn add(x: num, y: num) -> num { x + y }");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.name == Symbol::intern("add"));
    assert(fn.return_ty == ty::num());
    assert(fn.params.size() == 2);
    assert(fn.params[0].name == Symbol::intern("x"));
    assert(fn.params[0].ty == ty::num());
    assert(fn.params[1].name == Symbol::intern("y"));
    assert(fn.params[1].ty == ty::num());

    // Body should have a tail of type num
    assert(fn.body->ty == ty::num());
    assert(fn.body->tail.has_value());
    assert((*fn.body->tail)->ty == ty::num());
}

static void test_fn_bool_return() {
    const auto prog = must_lower("fn yes() -> bool { true }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::bool_());
    assert(fn.body->ty == ty::bool_());
}

static void test_fn_str_return() {
    const auto prog = must_lower("fn greeting() -> str { \"hello\" }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::str());
}

// ─── Multiple items in order ──────────────────────────────────────────────────

static void test_item_order() {
    const auto prog = must_lower(
        "struct A;"
        "enum B { X }"
        "fn c() { }");
    assert(prog.items.size() == 3);
    assert(std::holds_alternative<TyStructDecl>(prog.items[0]));
    assert(std::holds_alternative<TyEnumDecl>(prog.items[1]));
    assert(std::holds_alternative<TyFnDecl>(prog.items[2]));
}

// ─── Type mismatch errors ─────────────────────────────────────────────────────

static void test_return_type_mismatch() { must_fail("fn f() -> num { true }"); }

static void test_let_type_mismatch() { must_fail("fn f() { let x: bool = 42; }"); }

int main() {
    SymbolSession session;

    test_empty_program();
    test_struct_decl();
    test_zst_struct();
    test_struct_with_named_field();
    test_struct_undefined_type_error();
    test_enum_decl();
    test_fn_no_return();
    test_fn_with_params_and_return();
    test_fn_bool_return();
    test_fn_str_return();
    test_item_order();
    test_return_type_mismatch();
    test_let_type_mismatch();

    return 0;
}
