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

void expect_error_at(std::string_view source, std::string_view needle, std::size_t expected_start) {
    const auto result = cstc::parser::parse_source(source);
    assert(!result.has_value());
    assert(result.error().message.find(needle) != std::string::npos);
    assert(result.error().span.start == expected_start);
}

// ---------------------------------------------------------------------------
// Top-level item errors
// ---------------------------------------------------------------------------

void test_error_unknown_item() {
    cstc::symbol::SymbolSession session;
    expect_error("42", "expected item");
}

void test_error_single_bracket_attribute_start() {
    cstc::symbol::SymbolSession session;
    expect_error("[foo] fn main() { }", "expected second `[` to start attribute");
}

void test_error_attribute_missing_name() {
    cstc::symbol::SymbolSession session;
    expect_error("[[]] fn main() { }", "expected attribute name");
}

void test_error_attribute_non_string_value() {
    cstc::symbol::SymbolSession session;
    expect_error("[[foo = 1]] fn main() { }", "expected string literal after `=` in attribute");
}

void test_error_attribute_without_item() {
    cstc::symbol::SymbolSession session;
    expect_error("[[foo]]", "expected item after attributes");
}

void test_error_pub_without_item() {
    cstc::symbol::SymbolSession session;
    expect_error("pub", "expected item after `pub`");
}

void test_error_runtime_without_supported_item() {
    cstc::symbol::SymbolSession session;
    expect_error("runtime struct Foo;", "expected `fn` or `extern` after `runtime`");
}

void test_error_single_bracket_between_attributes_and_item() {
    cstc::symbol::SymbolSession session;
    expect_error("[[a]] [b] fn f() { }", "expected second `[` to start attribute");
}

void test_error_attributes_on_import() {
    cstc::symbol::SymbolSession session;
    constexpr std::string_view source = "[[a]] import { foo } from \"mod.cst\";";
    expect_error_at(source, "attributes are not supported on import", source.find("import"));
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

void test_error_struct_initializer_duplicate_field() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { Foo { x: 1, x: 2 } }", "duplicate struct field");
}

void test_error_struct_missing_colon() {
    cstc::symbol::SymbolSession session;
    expect_error("struct Foo { x num }", "expected `:` after field name");
}

void test_error_ct_required_struct_field_not_supported() {
    cstc::symbol::SymbolSession session;
    expect_error(
        "struct Foo { x: const num }", "`const`/`!runtime` is only supported for function "
                                       "parameters and explicit local annotations");
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

void test_error_duplicate_runtime_type_qualifier() {
    cstc::symbol::SymbolSession session;
    constexpr std::string_view source = "fn f(x: runtime runtime num) { }";
    expect_error_at(
        source, "duplicate `runtime` type qualifier",
        source.find("runtime runtime") + std::string_view{"runtime "}.size());
}

void test_error_nested_ct_required_ref_pointee() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f(x: &!runtime num) { }", "nested `!runtime` type qualifier is not supported");
}

void test_error_nested_const_ref_pointee() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f(x: &const num) { }", "nested `const` type qualifier is not supported");
}

void test_error_nested_ct_required_generic_argument() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f(x: Box<const num>) { }", "nested `const` type qualifier is not supported");
}

void test_error_ct_required_fn_return_not_supported() {
    cstc::symbol::SymbolSession session;
    expect_error(
        "fn f() -> !runtime num { 1 }", "`const`/`!runtime` is only supported for function "
                                        "parameters and explicit local annotations");
}

void test_error_ct_required_extern_return_not_supported() {
    cstc::symbol::SymbolSession session;
    expect_error(
        "extern \"lang\" fn f() -> const num;", "`const`/`!runtime` is only supported for function "
                                                "parameters and explicit local annotations");
}

void test_error_fn_missing_body() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f()", "expected `{` to start block");
}

void test_error_duplicate_generic_parameter() {
    cstc::symbol::SymbolSession session;
    expect_error("fn dup<T, T>() { }", "duplicate generic parameter");
}

void test_error_missing_generic_parameter_name() {
    cstc::symbol::SymbolSession session;
    expect_error("struct Box<>;", "expected generic parameter name");
}

void test_error_missing_generic_argument_close() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f(value: Box<num) { }", "expected `>` to close generic argument list");
}

void test_error_where_clause_missing_constraint() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f<T>() where { }", "expected constraint expression after `where`");
}

void test_error_where_clause_trailing_comma() {
    cstc::symbol::SymbolSession session;
    expect_error(
        "struct Box<T> where ready, { value: T }", "expected constraint expression after `,`");
}

void test_error_decl_wrong_arity() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { decl(1, 2) }", "`decl` expects exactly 1 argument");
}

void test_error_decl_call_postfix() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { decl(1)() }", "expected `;` or `}` after expression");
}

void test_error_decl_field_postfix() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { decl(1).field }", "expected `;` or `}` after expression");
}

void test_error_decl_turbofish_postfix() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { decl(1)::<num> }", "expected `;` or `}` after expression");
}

void test_error_import_missing_brace() {
    cstc::symbol::SymbolSession session;
    expect_error("import foo from \"mod.cst\";", "expected `{` after `import`");
}

void test_error_import_star_not_supported() {
    cstc::symbol::SymbolSession session;
    expect_error("import { * } from \"mod.cst\";", "`import *` is not supported");
}

void test_error_import_missing_from() {
    cstc::symbol::SymbolSession session;
    expect_error("import { foo } \"mod.cst\";", "expected `from` after import item list");
}

void test_error_import_missing_path() {
    cstc::symbol::SymbolSession session;
    expect_error("import { foo } from bar;", "expected import path string after `from`");
}

void test_error_import_missing_semicolon() {
    cstc::symbol::SymbolSession session;
    expect_error("import { foo } from \"mod.cst\"", "expected `;` after import declaration");
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

void test_error_runtime_expr_requires_block() {
    cstc::symbol::SymbolSession session;
    expect_error("fn f() { runtime x }", "expected `{` to start runtime block after `runtime`");
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
    test_error_single_bracket_attribute_start();
    test_error_attribute_missing_name();
    test_error_attribute_non_string_value();
    test_error_attribute_without_item();
    test_error_pub_without_item();
    test_error_runtime_without_supported_item();
    test_error_single_bracket_between_attributes_and_item();
    test_error_attributes_on_import();
    test_error_struct_missing_name();
    test_error_struct_missing_brace_or_semi();
    test_error_struct_missing_close_brace();
    test_error_struct_duplicate_field();
    test_error_struct_initializer_duplicate_field();
    test_error_struct_missing_colon();
    test_error_ct_required_struct_field_not_supported();
    test_error_enum_missing_name();
    test_error_enum_missing_open_brace();
    test_error_enum_missing_close_brace();
    test_error_enum_duplicate_variant();
    test_error_fn_missing_name();
    test_error_fn_missing_open_paren();
    test_error_fn_missing_close_paren();
    test_error_fn_duplicate_param();
    test_error_duplicate_runtime_type_qualifier();
    test_error_nested_ct_required_ref_pointee();
    test_error_nested_const_ref_pointee();
    test_error_nested_ct_required_generic_argument();
    test_error_ct_required_fn_return_not_supported();
    test_error_ct_required_extern_return_not_supported();
    test_error_fn_missing_body();
    test_error_duplicate_generic_parameter();
    test_error_missing_generic_parameter_name();
    test_error_missing_generic_argument_close();
    test_error_where_clause_missing_constraint();
    test_error_where_clause_trailing_comma();
    test_error_decl_wrong_arity();
    test_error_decl_call_postfix();
    test_error_decl_field_postfix();
    test_error_decl_turbofish_postfix();
    test_error_import_missing_brace();
    test_error_import_star_not_supported();
    test_error_import_missing_from();
    test_error_import_missing_path();
    test_error_import_missing_semicolon();
    test_error_block_missing_close_brace();
    test_error_runtime_expr_requires_block();
    test_error_let_missing_assign();
    test_error_let_missing_semicolon();
    test_error_expr_without_semicolon_before_next();
    test_error_expected_expression();
    test_error_unclosed_paren();
    test_error_call_missing_close_paren();
    test_error_span_offset();
    return 0;
}
