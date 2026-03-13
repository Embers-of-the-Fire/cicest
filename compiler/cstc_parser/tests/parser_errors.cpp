#include <cassert>
#include <string_view>

#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>

namespace {

// Expect parse to fail and return an error whose message contains `needle`.
void expect_error(std::string_view source, std::string_view needle) {
    const auto result = cstc::parser::parse_source(source);
    assert(!result.has_value());
    assert(result.error().message.find(needle) != std::string::npos);
}

// ---------------------------------------------------------------------------
// Top-level item errors
// ---------------------------------------------------------------------------

void test_error_unknown_item() {
    cstc::symbol::SymbolSession session;
    expect_error("42", "expected item");
}

// ---------------------------------------------------------------------------
// Struct declaration errors
// ---------------------------------------------------------------------------

void test_error_struct_missing_name() {
    cstc::symbol::SymbolSession session;
    expect_error("struct { }", "expected struct name");
}

void test_error_struct_missing_brace_or_semi() {
    cstc::symbol::SymbolSession session;
    expect_error("struct Foo", "expected `{` or `;`");
}

void test_error_struct_missing_close_brace() {
    cstc::symbol::SymbolSession session;
    expect_error("struct Foo { x: num", "expected `}` to close struct");
}

void test_error_struct_duplicate_field() {
    cstc::symbol::SymbolSession session;
    expect_error("struct Foo { x: num, x: num }", "duplicate struct field");
}

void test_error_struct_missing_colon() {
    cstc::symbol::SymbolSession session;
    expect_error("struct Foo { x num }", "expected `:` after field name");
}

// ---------------------------------------------------------------------------
// Enum declaration errors
// ---------------------------------------------------------------------------

void test_error_enum_missing_name() {
    cstc::symbol::SymbolSession session;
    expect_error("enum { A }", "expected enum name");
}

void test_error_enum_missing_open_brace() {
    cstc::symbol::SymbolSession session;
    expect_error("enum Foo", "expected `{` after enum name");
}

void test_error_enum_missing_close_brace() {
    cstc::symbol::SymbolSession session;
    expect_error("enum Foo { A", "expected `}` to close enum");
}

void test_error_enum_duplicate_variant() {
    cstc::symbol::SymbolSession session;
    expect_error("enum Foo { A, A }", "duplicate enum variant");
}

// ---------------------------------------------------------------------------
// Function declaration errors
// ---------------------------------------------------------------------------

void test_error_fn_missing_name() {
    cstc::symbol::SymbolSession session;
    expect_error("fn () { }", "expected function name");
}

void test_error_fn_missing_open_paren() {
    cstc::symbol::SymbolSession session;
    expect_error("fn foo { }", "expected `(` after function name");
}

void test_error_fn_missing_close_paren() {
    cstc::symbol::SymbolSession session;
    expect_error("fn foo(x: num { }", "expected `)` after function parameters");
}

void test_error_fn_duplicate_param() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f(x: num, x: num) { }", "duplicate parameter");
}

void test_error_fn_missing_body() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f()", "expected `{` to start block");
}

// ---------------------------------------------------------------------------
// Block / statement errors
// ---------------------------------------------------------------------------

void test_error_block_missing_close_brace() {
    cstc::symbol::SymbolSession session;
    // Empty block without closing brace: the while loop exits at EOF,
    // then consume(RBrace) fires "expected `}` to close block".
    expect_error("fn f() {", "expected `}` to close block");
}

void test_error_let_missing_assign() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { let x: num; }", "expected `=` in let binding");
}

void test_error_let_missing_semicolon() {
    cstc::symbol::SymbolSession session;
    // After the initializer there must be `;` before the next statement.
    expect_error("fn f() { let x = 1 let y = 2; }", "expected `;` after let statement");
}

void test_error_expr_without_semicolon_before_next() {
    cstc::symbol::SymbolSession session;
    // Two non-block expressions in a row without semicolons — second one is unexpected.
    expect_error("fn f() { 1 2 }", "expected `;` or `}` after expression");
}

// ---------------------------------------------------------------------------
// Expression errors
// ---------------------------------------------------------------------------

void test_error_expected_expression() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { ; }", "expected expression");
}

void test_error_unclosed_paren() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { (1 }", "expected `)` to close parenthesized expression");
}

void test_error_call_missing_close_paren() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { foo(1, 2 }", "expected `)` after call arguments");
}

// ---------------------------------------------------------------------------
// parse_source_at: error span is offset by base_pos
// ---------------------------------------------------------------------------

void test_error_span_offset() {
    cstc::symbol::SymbolSession session;
    constexpr std::size_t base = 512;
    // Duplicate param error — span should be at/after base
    const auto result = cstc::parser::parse_source_at("fn f(x: num, x: num) { }", base);
    assert(!result.has_value());
    assert(result.error().span.start >= base);
    assert(result.error().span.end >= base);
}

} // namespace

int main() {
    test_error_unknown_item();
    test_error_struct_missing_name();
    test_error_struct_missing_brace_or_semi();
    test_error_struct_missing_close_brace();
    test_error_struct_duplicate_field();
    test_error_struct_missing_colon();
    test_error_enum_missing_name();
    test_error_enum_missing_open_brace();
    test_error_enum_missing_close_brace();
    test_error_enum_duplicate_variant();
    test_error_fn_missing_name();
    test_error_fn_missing_open_paren();
    test_error_fn_missing_close_paren();
    test_error_fn_duplicate_param();
    test_error_fn_missing_body();
    test_error_block_missing_close_brace();
    test_error_let_missing_assign();
    test_error_let_missing_semicolon();
    test_error_expr_without_semicolon_before_next();
    test_error_expected_expression();
    test_error_unclosed_paren();
    test_error_call_missing_close_paren();
    test_error_span_offset();
    return 0;
}
