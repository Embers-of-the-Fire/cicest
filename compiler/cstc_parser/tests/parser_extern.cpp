#include <cassert>
#include <string>
#include <string_view>
#include <variant>

#include <cstc_ast/ast.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>

namespace {

cstc::ast::Program must_parse(std::string_view source) {
    const auto result = cstc::parser::parse_source(source);
    assert(result.has_value());
    return *result;
}

bool must_fail(std::string_view source) {
    const auto result = cstc::parser::parse_source(source);
    return !result.has_value();
}

bool must_fail_with_message(std::string_view source, std::string_view expected_substring) {
    const auto result = cstc::parser::parse_source(source);
    if (result.has_value())
        return false;
    return result.error().message.find(expected_substring) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Extern function declarations
// ---------------------------------------------------------------------------

void test_extern_fn_no_params_no_return() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(extern "lang" fn print();)");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<cstc::ast::ExternFnDecl>(prog.items[0]);
    assert(fn.abi.as_str() == "lang");
    assert(fn.name.as_str() == "print");
    assert(fn.params.empty());
    assert(!fn.return_type.has_value());
}

void test_extern_fn_with_params() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(extern "lang" fn print(value: str);)");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<cstc::ast::ExternFnDecl>(prog.items[0]);
    assert(fn.abi.as_str() == "lang");
    assert(fn.name.as_str() == "print");
    assert(fn.params.size() == 1);
    assert(fn.params[0].name.as_str() == "value");
    assert(fn.params[0].type.kind == cstc::ast::TypeKind::Str);
}

void test_extern_fn_with_return_type() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(extern "lang" fn to_str(value: num) -> str;)");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<cstc::ast::ExternFnDecl>(prog.items[0]);
    assert(fn.name.as_str() == "to_str");
    assert(fn.params.size() == 1);
    assert(fn.params[0].name.as_str() == "value");
    assert(fn.params[0].type.kind == cstc::ast::TypeKind::Num);
    assert(fn.return_type.has_value());
    assert(fn.return_type->kind == cstc::ast::TypeKind::Str);
}

void test_extern_fn_multiple_params() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(extern "lang" fn concat(a: str, b: str) -> str;)");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<cstc::ast::ExternFnDecl>(prog.items[0]);
    assert(fn.name.as_str() == "concat");
    assert(fn.params.size() == 2);
    assert(fn.params[0].name.as_str() == "a");
    assert(fn.params[1].name.as_str() == "b");
    assert(fn.return_type.has_value());
    assert(fn.return_type->kind == cstc::ast::TypeKind::Str);
}

void test_extern_fn_trailing_comma() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(extern "lang" fn f(x: num,) -> num;)");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<cstc::ast::ExternFnDecl>(prog.items[0]);
    assert(fn.params.size() == 1);
}

// ---------------------------------------------------------------------------
// Extern struct declarations
// ---------------------------------------------------------------------------

void test_extern_struct() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(extern "lang" struct Opaque;)");
    assert(prog.items.size() == 1);
    const auto& s = std::get<cstc::ast::ExternStructDecl>(prog.items[0]);
    assert(s.abi.as_str() == "lang");
    assert(s.name.as_str() == "Opaque");
}

// ---------------------------------------------------------------------------
// Multiple extern items
// ---------------------------------------------------------------------------

void test_multiple_extern_items() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(
extern "lang" fn print(value: str);
extern "lang" fn println(value: str);
extern "lang" struct Handle;
)");
    assert(prog.items.size() == 3);
    assert(std::holds_alternative<cstc::ast::ExternFnDecl>(prog.items[0]));
    assert(std::holds_alternative<cstc::ast::ExternFnDecl>(prog.items[1]));
    assert(std::holds_alternative<cstc::ast::ExternStructDecl>(prog.items[2]));
}

// ---------------------------------------------------------------------------
// Mixed extern and regular items
// ---------------------------------------------------------------------------

void test_extern_mixed_with_regular() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(
extern "lang" fn print(value: str);
struct Point { x: num, y: num }
fn main() { print("hello"); }
)");
    assert(prog.items.size() == 3);
    assert(std::holds_alternative<cstc::ast::ExternFnDecl>(prog.items[0]));
    assert(std::holds_alternative<cstc::ast::StructDecl>(prog.items[1]));
    assert(std::holds_alternative<cstc::ast::FnDecl>(prog.items[2]));
}

// ---------------------------------------------------------------------------
// Different ABI strings
// ---------------------------------------------------------------------------

void test_extern_c_abi() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(extern "c" fn puts(s: str);)");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<cstc::ast::ExternFnDecl>(prog.items[0]);
    assert(fn.abi.as_str() == "c");
}

// ---------------------------------------------------------------------------
// Span correctness
// ---------------------------------------------------------------------------

void test_extern_fn_span() {
    cstc::symbol::SymbolSession session;
    const auto prog = must_parse(R"(extern "lang" fn foo();)");
    const auto& fn = std::get<cstc::ast::ExternFnDecl>(prog.items[0]);
    // Span should cover from `extern` to the trailing `;`
    assert(fn.span.start == 0);
    assert(fn.span.end > 0);
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

void test_error_extern_without_string() {
    cstc::symbol::SymbolSession session;
    assert(must_fail_with_message("extern fn foo();", "expected ABI string"));
}

void test_error_extern_without_fn_or_struct() {
    cstc::symbol::SymbolSession session;
    assert(must_fail_with_message(R"(extern "lang" let x = 1;)", "expected `fn` or `struct`"));
}

void test_error_extern_fn_with_body() {
    cstc::symbol::SymbolSession session;
    // An extern fn should be terminated with `;`, not a body block.
    assert(must_fail(R"(extern "lang" fn foo() { })"));
}

void test_error_extern_fn_missing_semicolon() {
    cstc::symbol::SymbolSession session;
    assert(must_fail_with_message(R"(extern "lang" fn foo())", "expected `;`"));
}

void test_error_extern_struct_missing_semicolon() {
    cstc::symbol::SymbolSession session;
    assert(must_fail_with_message(R"(extern "lang" struct Foo)", "expected `;`"));
}

void test_error_extern_fn_duplicate_params() {
    cstc::symbol::SymbolSession session;
    assert(must_fail_with_message(R"(extern "lang" fn f(x: num, x: num);)", "duplicate parameter"));
}

} // namespace

int main() {
    test_extern_fn_no_params_no_return();
    test_extern_fn_with_params();
    test_extern_fn_with_return_type();
    test_extern_fn_multiple_params();
    test_extern_fn_trailing_comma();
    test_extern_struct();
    test_multiple_extern_items();
    test_extern_mixed_with_regular();
    test_extern_c_abi();
    test_extern_fn_span();
    test_error_extern_without_string();
    test_error_extern_without_fn_or_struct();
    test_error_extern_fn_with_body();
    test_error_extern_fn_missing_semicolon();
    test_error_extern_struct_missing_semicolon();
    test_error_extern_fn_duplicate_params();
    return 0;
}
