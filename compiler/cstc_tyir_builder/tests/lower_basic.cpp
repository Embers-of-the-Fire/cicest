#include <cassert>
#include <string>

#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>
#include <cstc_tyir_builder/builder.hpp>

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

static void must_fail_with_message(const char* source, const char* expected_message_part) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(!tyir.has_value());
    assert(tyir.error().message.find(expected_message_part) != std::string::npos);
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
    assert(
        brush.fields[0].ty
        == ty::named(Symbol::intern("Color"), kInvalidSymbol, ValueSemantics::Copy));
}

static void test_struct_undefined_type_error() { must_fail("struct Foo { x: Unknown }"); }

static void test_struct_ref_field_error() {
    must_fail_with_message("struct Foo { value: &num }", "reference fields are not supported");
}

static void test_duplicate_struct_name_error() {
    must_fail_with_message(
        "struct Point { x: num }"
        "struct Point { y: num }",
        "duplicate struct name 'Point'");
}

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

static void test_duplicate_enum_name_error() {
    must_fail_with_message(
        "enum Dir { North }"
        "enum Dir { South }",
        "duplicate enum name 'Dir'");
}

static void test_enum_struct_name_collision_error() {
    must_fail_with_message(
        "enum Thing { A }"
        "struct Thing;",
        "duplicate struct name 'Thing'");
}

static void test_struct_enum_name_collision_error() {
    must_fail_with_message(
        "struct Thing;"
        "enum Thing { A }",
        "duplicate enum name 'Thing'");
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
    const auto prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn greeting() -> str { to_str(0) }");
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(fn.return_ty == ty::str());
}

static void test_fn_ref_return_rejected() {
    must_fail_with_message("fn greeting() -> &str { \"hello\" }", "reference return types");
}

static void test_runtime_fn_preserves_runtime_markers() {
    const auto prog =
        must_lower("struct Job; runtime fn dispatch(job: runtime Job) -> runtime Job { job }");
    assert(prog.items.size() == 2);
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(fn.params.size() == 1);
    assert(fn.params[0].ty.is_runtime);
    assert(fn.return_ty.is_runtime);
    assert(fn.body->ty.is_runtime);
}

static void test_runtime_fn_return_uses_runtime_sugar() {
    const auto prog = must_lower("struct Job; runtime fn dispatch(job: Job) -> Job { job }");
    assert(prog.items.size() == 2);
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(fn.params.size() == 1);
    assert(!fn.params[0].ty.is_runtime);
    assert(fn.return_ty.is_runtime);
    assert(!fn.body->ty.is_runtime);
}

static void test_runtime_return_annotation_accepts_plain_value() {
    const auto prog = must_lower("fn promote() -> runtime num { 1 }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num(true));
    assert(fn.body->ty == ty::num());
}

static void test_runtime_return_type_mismatch_rejected() {
    must_fail_with_message(
        "struct Job; fn unwrap(job: runtime Job) -> Job { job }",
        "body has type 'runtime Job' but return type is 'Job'");
}

static void test_runtime_main_return_allowed() {
    const auto prog = must_lower("runtime fn main() -> num { 0 }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num(true));
    assert(fn.body->ty == ty::num());
}

static void test_duplicate_function_name_error() {
    must_fail_with_message("fn noop() { } fn noop() { }", "duplicate function name 'noop'");
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

static void test_non_unit_fn_fallthrough_error() { must_fail("fn f() -> num { }"); }

static void test_non_unit_fn_explicit_return_stmt() {
    const auto prog = must_lower("fn f() -> num { return 1; }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
    assert(!fn.body->tail.has_value());
}

static void test_non_unit_fn_if_else_returns_as_stmt() {
    const auto prog =
        must_lower("fn f(cond: bool) -> num { if cond { return 1; } else { return 2; }; }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
    assert(!fn.body->tail.has_value());
}

static void test_non_unit_fn_if_condition_return_no_fallthrough() {
    const auto prog = must_lower("fn f() -> num { if (return 1) { }; }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
    assert(!fn.body->tail.has_value());
}

static void test_non_unit_fn_while_condition_return_no_fallthrough() {
    const auto prog = must_lower("fn f() -> num { while (return 1) { }; }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
    assert(!fn.body->tail.has_value());
}

static void test_let_type_mismatch() { must_fail("fn f() { let x: bool = 42; }"); }

// ─── Never (!) return type ────────────────────────────────────────────────────

static void test_fn_never_return_type_valid() {
    const auto prog = must_lower("fn f() -> ! { loop {} }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::never());
}

static void test_fn_never_return_type_mismatch() {
    must_fail_with_message("fn f() -> ! { 42 }", "body has type 'num' but return type is '!'");
}

// ─── main return type constraints ─────────────────────────────────────────────

static void test_main_returns_unit() {
    const auto prog = must_lower("fn main() { }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::unit());
}

static void test_main_returns_num() {
    const auto prog = must_lower("fn main() -> num { 0 }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
}

static void test_main_returns_never() {
    const auto prog = must_lower("fn main() -> ! { loop {} }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::never());
}

static void test_main_returns_str_error() {
    must_fail_with_message(
        "extern \"lang\" fn to_str(value: num) -> str; fn main() -> str { to_str(1) }",
        "'main' function must return 'Unit', 'num', or '!' (never), found 'str'");
}

static void test_main_returns_bool_error() {
    must_fail_with_message(
        "fn main() -> bool { true }",
        "'main' function must return 'Unit', 'num', or '!' (never), found 'bool'");
}

static void test_main_returns_struct_error() {
    must_fail_with_message(
        "struct Point { x: num } fn main() -> Point { Point { x: 0 } }",
        "'main' function must return 'Unit', 'num', or '!' (never), found 'Point'");
}

static void test_main_returns_enum_error() {
    must_fail_with_message(
        "enum Dir { North } fn main() -> Dir { Dir::North }",
        "'main' function must return 'Unit', 'num', or '!' (never), found 'Dir'");
}

static void test_non_main_fn_accepts_any_return() {
    // Regression: non-main functions should still accept any return type
    const auto prog = must_lower(
        "struct Point { x: num }"
        "fn make_point() -> Point { Point { x: 1 } }");
    assert(prog.items.size() == 2);
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(
        fn.return_ty == ty::named(Symbol::intern("Point"), kInvalidSymbol, ValueSemantics::Copy));
}

int main() {
    SymbolSession session;

    test_empty_program();
    test_struct_decl();
    test_zst_struct();
    test_struct_with_named_field();
    test_struct_undefined_type_error();
    test_struct_ref_field_error();
    test_duplicate_struct_name_error();
    test_enum_decl();
    test_duplicate_enum_name_error();
    test_enum_struct_name_collision_error();
    test_struct_enum_name_collision_error();
    test_fn_no_return();
    test_fn_with_params_and_return();
    test_fn_bool_return();
    test_fn_str_return();
    test_fn_ref_return_rejected();
    test_runtime_fn_preserves_runtime_markers();
    test_runtime_fn_return_uses_runtime_sugar();
    test_runtime_return_annotation_accepts_plain_value();
    test_runtime_return_type_mismatch_rejected();
    test_runtime_main_return_allowed();
    test_duplicate_function_name_error();
    test_item_order();
    test_return_type_mismatch();
    test_non_unit_fn_fallthrough_error();
    test_non_unit_fn_explicit_return_stmt();
    test_non_unit_fn_if_else_returns_as_stmt();
    test_non_unit_fn_if_condition_return_no_fallthrough();
    test_non_unit_fn_while_condition_return_no_fallthrough();
    test_let_type_mismatch();
    test_fn_never_return_type_valid();
    test_fn_never_return_type_mismatch();
    test_main_returns_unit();
    test_main_returns_num();
    test_main_returns_never();
    test_main_returns_str_error();
    test_main_returns_bool_error();
    test_main_returns_struct_error();
    test_main_returns_enum_error();
    test_non_main_fn_accepts_any_return();

    return 0;
}
