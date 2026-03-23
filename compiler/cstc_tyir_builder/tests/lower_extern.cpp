#include <cassert>
#include <string>
#include <variant>

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

// ─── Extern fn lowering ──────────────────────────────────────────────────────

static void test_extern_fn_basic() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" fn print(value: str);)");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyExternFnDecl>(prog.items[0]);
    assert(decl.abi == Symbol::intern("lang"));
    assert(decl.name == Symbol::intern("print"));
    assert(decl.params.size() == 1);
    assert(decl.params[0].name == Symbol::intern("value"));
    assert(decl.params[0].ty == ty::str());
    assert(decl.return_ty == ty::unit());
}

static void test_extern_fn_with_return() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" fn to_str(value: num) -> str;)");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyExternFnDecl>(prog.items[0]);
    assert(decl.name == Symbol::intern("to_str"));
    assert(decl.link_name == Symbol::intern("to_str"));
    assert(decl.params.size() == 1);
    assert(decl.params[0].ty == ty::num());
    assert(decl.return_ty == ty::str());
}

static void test_extern_fn_multiple_params() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" fn concat(a: str, b: str) -> str;)");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyExternFnDecl>(prog.items[0]);
    assert(decl.link_name == Symbol::intern("concat"));
    assert(decl.params.size() == 2);
    assert(decl.params[0].ty == ty::str());
    assert(decl.params[1].ty == ty::str());
    assert(decl.return_ty == ty::str());
}

static void test_extern_fn_lang_attribute_overrides_link_name() {
    SymbolSession session;
    const auto prog = must_lower(R"(
[[lang = "cstc_std_print"]]
extern "lang" fn print(value: str);
)");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyExternFnDecl>(prog.items[0]);
    assert(decl.name == Symbol::intern("print"));
    assert(decl.link_name == Symbol::intern("cstc_std_print"));
}

// ─── Extern fn is callable ───────────────────────────────────────────────────

static void test_extern_fn_callable() {
    SymbolSession session;
    const auto prog = must_lower(R"(
extern "lang" fn print(value: str);
fn main() { print("hello"); }
)");
    assert(prog.items.size() == 2);
    // The extern fn declaration
    assert(std::holds_alternative<TyExternFnDecl>(prog.items[0]));
    // The regular fn
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(fn.name == Symbol::intern("main"));
    // Body should contain a call expression
    assert(!fn.body->stmts.empty() || fn.body->tail.has_value());
}

static void test_extern_fn_call_with_return() {
    SymbolSession session;
    const auto prog = must_lower(R"(
extern "lang" fn to_str(value: num) -> str;
fn main() {
    let s: str = to_str(42);
}
)");
    assert(prog.items.size() == 2);
    assert(std::holds_alternative<TyExternFnDecl>(prog.items[0]));
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(fn.body->stmts.size() == 1);
    const auto& let_stmt = std::get<TyLetStmt>(fn.body->stmts[0]);
    assert(let_stmt.ty == ty::str());
}

// ─── Extern struct lowering ──────────────────────────────────────────────────

static void test_extern_struct_basic() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" struct Handle;)");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyExternStructDecl>(prog.items[0]);
    assert(decl.abi == Symbol::intern("lang"));
    assert(decl.name == Symbol::intern("Handle"));
}

static void test_extern_struct_usable_as_type() {
    SymbolSession session;
    const auto prog = must_lower(R"(
extern "lang" struct Handle;
extern "lang" fn create_handle() -> Handle;
fn main() {
    let h: Handle = create_handle();
}
)");
    assert(prog.items.size() == 3);
}

// ─── Error: duplicate names ──────────────────────────────────────────────────

static void test_duplicate_extern_fn_name() {
    SymbolSession session;
    must_fail_with_message(
        R"(
extern "lang" fn print(value: str);
extern "lang" fn print(value: str);
)",
        "duplicate function name");
}

static void test_duplicate_extern_and_regular_fn() {
    SymbolSession session;
    must_fail_with_message(
        R"(
extern "lang" fn foo();
fn foo() { }
)",
        "duplicate function name");
}

static void test_duplicate_extern_struct_name() {
    SymbolSession session;
    must_fail_with_message(
        R"(
extern "lang" struct Foo;
struct Foo;
)",
        "duplicate");
}

// ─── Error: wrong arg types ─────────────────────────────────────────────────

static void test_extern_fn_wrong_arg_type() {
    SymbolSession session;
    must_fail(R"(
extern "lang" fn print(value: str);
fn main() { print(42); }
)");
}

static void test_extern_fn_wrong_arg_count() {
    SymbolSession session;
    must_fail(R"(
extern "lang" fn print(value: str);
fn main() { print(); }
)");
}

// ─── Multiple extern items ───────────────────────────────────────────────────

static void test_multiple_extern_items() {
    SymbolSession session;
    const auto prog = must_lower(R"(
extern "lang" fn print(value: str);
extern "lang" fn println(value: str);
extern "lang" fn to_str(value: num) -> str;
extern "lang" struct Handle;
)");
    assert(prog.items.size() == 4);
    assert(std::holds_alternative<TyExternFnDecl>(prog.items[0]));
    assert(std::holds_alternative<TyExternFnDecl>(prog.items[1]));
    assert(std::holds_alternative<TyExternFnDecl>(prog.items[2]));
    assert(std::holds_alternative<TyExternStructDecl>(prog.items[3]));
}

// ─── Error: unsupported ABI ─────────────────────────────────────────────────

static void test_unsupported_abi_fn() {
    SymbolSession session;
    must_fail_with_message(R"(extern "bogus" fn foo();)", "unsupported ABI");
}

static void test_unsupported_abi_struct() {
    SymbolSession session;
    must_fail_with_message(R"(extern "bogus" struct Foo;)", "unsupported ABI");
}

static void test_c_abi_accepted() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "c" fn puts(s: str);)");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyExternFnDecl>(prog.items[0]);
    assert(decl.abi == Symbol::intern("c"));
    assert(decl.link_name == Symbol::intern("puts"));
}

static void test_lang_attribute_requires_lang_abi() {
    SymbolSession session;
    must_fail_with_message(
        R"(
[[lang = "puts"]]
extern "c" fn puts(s: str);
)",
        "attribute `lang` is only supported");
}

static void test_lang_attribute_requires_value() {
    SymbolSession session;
    must_fail_with_message(
        R"(
[[lang]]
extern "lang" fn print(value: str);
)",
        "attribute `lang` requires a string value");
}

static void test_lang_attribute_rejects_duplicates() {
    SymbolSession session;
    must_fail_with_message(
        R"(
[[lang = "print"]]
[[lang = "println"]]
extern "lang" fn print(value: str);
)",
        "duplicate `lang` attribute");
}

// ─── Error: constructing opaque extern struct ───────────────────────────────

static void test_extern_struct_init_rejected() {
    SymbolSession session;
    must_fail_with_message(
        R"(
extern "lang" struct Handle;
fn main() { let h: Handle = Handle {}; }
)",
        "cannot construct extern type");
}

static void test_extern_struct_init_with_fields_rejected() {
    SymbolSession session;
    must_fail_with_message(
        R"(
extern "lang" struct Handle;
fn main() { let h: Handle = Handle { x: 1 }; }
)",
        "cannot construct extern type");
}

int main() {
    test_extern_fn_basic();
    test_extern_fn_with_return();
    test_extern_fn_multiple_params();
    test_extern_fn_lang_attribute_overrides_link_name();
    test_extern_fn_callable();
    test_extern_fn_call_with_return();
    test_extern_struct_basic();
    test_extern_struct_usable_as_type();
    test_duplicate_extern_fn_name();
    test_duplicate_extern_and_regular_fn();
    test_duplicate_extern_struct_name();
    test_extern_fn_wrong_arg_type();
    test_extern_fn_wrong_arg_count();
    test_multiple_extern_items();
    test_unsupported_abi_fn();
    test_unsupported_abi_struct();
    test_c_abi_accepted();
    test_lang_attribute_requires_lang_abi();
    test_lang_attribute_requires_value();
    test_lang_attribute_rejects_duplicates();
    test_extern_struct_init_rejected();
    test_extern_struct_init_with_fields_rejected();
    return 0;
}
