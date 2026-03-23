#include <cassert>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_lexer/lexer.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>

namespace {

cstc::ast::Program must_parse(std::string_view source) {
    const auto result = cstc::parser::parse_source(source);
    assert(result.has_value());
    return *result;
}

// ---------------------------------------------------------------------------
// Empty program
// ---------------------------------------------------------------------------

void test_empty_program() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("");
    assert(prog.items.empty());
}

// ---------------------------------------------------------------------------
// Struct declarations
// ---------------------------------------------------------------------------

void test_zst_struct() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("struct Empty;");
    assert(prog.items.size() == 1);
    const auto& s = std::get<cstc::ast::StructDecl>(prog.items[0]);
    assert(s.name.as_str() == "Empty");
    assert(s.is_zst);
    assert(s.fields.empty());
}

void test_struct_with_primitive_field_types() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(
struct All {
    a: num,
    b: str,
    c: bool,
    d: Unit,
    e: MyType,
}
)");
    const auto& s = std::get<cstc::ast::StructDecl>(prog.items[0]);
    assert(s.fields.size() == 5);
    assert(s.fields[0].type.kind == cstc::ast::TypeKind::Num);
    assert(s.fields[1].type.kind == cstc::ast::TypeKind::Str);
    assert(s.fields[2].type.kind == cstc::ast::TypeKind::Bool);
    assert(s.fields[3].type.kind == cstc::ast::TypeKind::Unit);
    assert(s.fields[4].type.kind == cstc::ast::TypeKind::Named);
    assert(s.fields[4].type.symbol.as_str() == "MyType");
}

void test_struct_empty_braces() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("struct EmptyBrace { }");
    const auto& s = std::get<cstc::ast::StructDecl>(prog.items[0]);
    assert(!s.is_zst);
    assert(s.fields.empty());
}

void test_struct_with_attributes() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(
[[foo]]
[[bar = "baz"]]
struct Tagged;
)");
    const auto& s = std::get<cstc::ast::StructDecl>(prog.items[0]);
    assert(s.attributes.size() == 2);
    assert(s.attributes[0].name.as_str() == "foo");
    assert(!s.attributes[0].value.has_value());
    assert(s.attributes[1].name.as_str() == "bar");
    assert(s.attributes[1].value.has_value());
    assert(s.attributes[1].value->as_str() == "baz");
    assert(s.span.start == 1);
}

void test_pub_struct() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("pub struct Public;");
    const auto& s = std::get<cstc::ast::StructDecl>(prog.items[0]);
    assert(s.is_public);
    assert(s.name.as_str() == "Public");
}

// ---------------------------------------------------------------------------
// Enum declarations
// ---------------------------------------------------------------------------

void test_enum_empty() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("enum Empty { }");
    const auto& e = std::get<cstc::ast::EnumDecl>(prog.items[0]);
    assert(e.name.as_str() == "Empty");
    assert(e.variants.empty());
}

void test_enum_plain_variants() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("enum Dir { North, South, East, West, }");
    const auto& e = std::get<cstc::ast::EnumDecl>(prog.items[0]);
    assert(e.variants.size() == 4);
    assert(e.variants[0].name.as_str() == "North");
    assert(!e.variants[0].discriminant.has_value());
    assert(e.variants[3].name.as_str() == "West");
}

void test_enum_with_discriminants() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("enum Code { Ok = 0, Fail = 255, }");
    const auto& e = std::get<cstc::ast::EnumDecl>(prog.items[0]);
    assert(e.variants.size() == 2);
    assert(e.variants[0].discriminant.has_value());
    assert(e.variants[0].discriminant->as_str() == "0");
    assert(e.variants[1].discriminant->as_str() == "255");
}

void test_enum_with_attribute() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"([[repr = "u8"]] enum Code { Ok = 0, })");
    const auto& e = std::get<cstc::ast::EnumDecl>(prog.items[0]);
    assert(e.attributes.size() == 1);
    assert(e.attributes[0].name.as_str() == "repr");
    assert(e.attributes[0].value.has_value());
    assert(e.attributes[0].value->as_str() == "u8");
}

// ---------------------------------------------------------------------------
// Function declarations
// ---------------------------------------------------------------------------

void test_fn_no_params_no_return() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("fn noop() { }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    assert(fn.name.as_str() == "noop");
    assert(fn.params.empty());
    assert(!fn.return_type.has_value());
}

void test_fn_no_params_with_return() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("fn answer() -> num { 42 }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    assert(fn.params.empty());
    assert(fn.return_type.has_value());
    assert(fn.return_type->kind == cstc::ast::TypeKind::Num);
}

void test_fn_trailing_comma_params() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("fn f(a: num, b: bool,) -> str { \"x\" }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    assert(fn.params.size() == 2);
    assert(fn.params[0].name.as_str() == "a");
    assert(fn.params[1].name.as_str() == "b");
}

void test_fn_named_return_type() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("fn make() -> MyType { MyType { } }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    assert(fn.return_type.has_value());
    assert(fn.return_type->kind == cstc::ast::TypeKind::Named);
    assert(fn.return_type->symbol.as_str() == "MyType");
}

void test_fn_with_attributes() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(
[[inline]]
[[export = "entry"]]
fn tagged() { }
)");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    assert(fn.attributes.size() == 2);
    assert(fn.attributes[0].name.as_str() == "inline");
    assert(!fn.attributes[0].value.has_value());
    assert(fn.attributes[1].name.as_str() == "export");
    assert(fn.attributes[1].value.has_value());
    assert(fn.attributes[1].value->as_str() == "entry");
}

void test_pub_fn() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("pub fn exported() { }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    assert(fn.is_public);
    assert(fn.name.as_str() == "exported");
}

void test_pub_extern_fn() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("pub extern \"c\" fn puts(value: str);");
    const auto& fn = std::get<cstc::ast::ExternFnDecl>(prog.items[0]);
    assert(fn.is_public);
    assert(fn.name.as_str() == "puts");
}

void test_import_decl() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("import { Value, helper as alias } from \"path/to/foo.cst\";");
    const auto& import = std::get<cstc::ast::ImportDecl>(prog.items[0]);
    assert(!import.is_public);
    assert(import.path.as_str() == "path/to/foo.cst");
    assert(import.items.size() == 2);
    assert(import.items[0].name.as_str() == "Value");
    assert(!import.items[0].alias.has_value());
    assert(import.items[1].name.as_str() == "helper");
    assert(import.items[1].alias.has_value());
    assert(import.items[1].alias->as_str() == "alias");
}

void test_pub_import_decl() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("pub import { println } from \"@std/prelude.cst\";");
    const auto& import = std::get<cstc::ast::ImportDecl>(prog.items[0]);
    assert(import.is_public);
    assert(import.items.size() == 1);
    assert(import.items[0].name.as_str() == "println");
}

// ---------------------------------------------------------------------------
// Never (!) return type
// ---------------------------------------------------------------------------

void test_fn_never_return_type() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("fn diverge() -> ! { loop {} }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    assert(fn.return_type.has_value());
    assert(fn.return_type->kind == cstc::ast::TypeKind::Never);
}

// ---------------------------------------------------------------------------
// Statements in blocks
// ---------------------------------------------------------------------------

void test_let_no_type_annotation() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("fn f() { let x = 1; x }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    const auto& let = std::get<cstc::ast::LetStmt>(fn.body->statements[0]);
    assert(let.name.as_str() == "x");
    assert(!let.type_annotation.has_value());
    assert(!let.discard);
}

void test_let_with_type_annotation() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("fn f() { let y: bool = true; y }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    const auto& let = std::get<cstc::ast::LetStmt>(fn.body->statements[0]);
    assert(let.name.as_str() == "y");
    assert(let.type_annotation.has_value());
    assert(let.type_annotation->kind == cstc::ast::TypeKind::Bool);
}

void test_let_discard() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("fn f() { let _ = 0; }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    const auto& let = std::get<cstc::ast::LetStmt>(fn.body->statements[0]);
    assert(let.discard);
    assert(!let.name.is_valid());
}

void test_expr_stmt_semicolon() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse("fn f() { 42; }");
    const auto& fn = std::get<cstc::ast::FnDecl>(prog.items[0]);
    assert(fn.body->statements.size() == 1);
    assert(std::holds_alternative<cstc::ast::ExprStmt>(fn.body->statements[0]));
    assert(!fn.body->tail.has_value());
}

void test_multiple_items() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(
struct A;
enum B { X, }
fn c() { }
)");
    assert(prog.items.size() == 3);
    assert(std::holds_alternative<cstc::ast::StructDecl>(prog.items[0]));
    assert(std::holds_alternative<cstc::ast::EnumDecl>(prog.items[1]));
    assert(std::holds_alternative<cstc::ast::FnDecl>(prog.items[2]));
}

// ---------------------------------------------------------------------------
// parse_tokens API
// ---------------------------------------------------------------------------

void test_parse_tokens_api() {
    cstc::symbol::SymbolSession session;
    // parse_tokens filters trivia automatically
    const auto tokens = cstc::lexer::lex_source("fn f() { 1 }", true);
    const auto result = cstc::parser::parse_tokens(std::span(tokens.data(), tokens.size()));
    assert(result.has_value());
    assert(result->items.size() == 1);
    assert(std::holds_alternative<cstc::ast::FnDecl>(result->items[0]));
}

// ---------------------------------------------------------------------------
// parse_source_at preserves absolute span
// ---------------------------------------------------------------------------

void test_parse_source_at_span_offset() {
    cstc::symbol::SymbolSession session;
    constexpr std::size_t base = 256;
    const auto result = cstc::parser::parse_source_at("fn f() { 1 }", base);
    assert(result.has_value());
    const auto& fn = std::get<cstc::ast::FnDecl>(result->items[0]);
    assert(fn.span.start >= base);
}

} // namespace

int main() {
    test_empty_program();
    test_zst_struct();
    test_struct_with_primitive_field_types();
    test_struct_empty_braces();
    test_struct_with_attributes();
    test_pub_struct();
    test_enum_empty();
    test_enum_plain_variants();
    test_enum_with_discriminants();
    test_enum_with_attribute();
    test_fn_no_params_no_return();
    test_fn_no_params_with_return();
    test_fn_trailing_comma_params();
    test_fn_named_return_type();
    test_fn_with_attributes();
    test_pub_fn();
    test_pub_extern_fn();
    test_import_decl();
    test_pub_import_decl();
    test_fn_never_return_type();
    test_let_no_type_annotation();
    test_let_with_type_annotation();
    test_let_discard();
    test_expr_stmt_semicolon();
    test_multiple_items();
    test_parse_tokens_api();
    test_parse_source_at_span_offset();
    return 0;
}
