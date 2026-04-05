#include <cassert>
#include <string>

#include <cstc_codegen/codegen.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir_builder/builder.hpp>

using namespace cstc::symbol;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string must_codegen(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    const auto lir = cstc::lir_builder::lower_program(*tyir);
    assert(lir.has_value());
    return cstc::codegen::emit_llvm_ir(*lir);
}

static bool ir_contains(const std::string& ir, const std::string& needle) {
    return ir.find(needle) != std::string::npos;
}

// ─── Extern fn declarations ──────────────────────────────────────────────────

static void test_extern_fn_void_no_params() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn do_nothing();
fn main() { }
)");
    assert(ir_contains(ir, "declare void @do_nothing()"));
}

static void test_extern_fn_with_str_param() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn print(value: &str);
fn main() { }
)");
    assert(ir_contains(ir, "declare void @print(ptr)"));
}

static void test_extern_fn_with_return_type() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn to_str(value: num) -> str;
fn main() { }
)");
    assert(ir_contains(ir, "declare void @to_str(ptr, double)"));
}

static void test_extern_fn_multiple_params() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn concat(a: &str, b: &str) -> str;
fn main() { }
)");
    assert(ir_contains(ir, "declare void @concat(ptr, ptr, ptr)"));
}

static void test_extern_fn_with_owned_str_param() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn consume(value: str);
fn main() { }
)");
    assert(ir_contains(ir, "declare void @consume(ptr)"));
}

static void test_extern_fn_num_return() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn str_len(value: &str) -> num;
fn main() { }
)");
    assert(ir_contains(ir, "declare double @str_len(ptr)"));
}

static void test_extern_fn_bool_param_and_return() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn negate(value: bool) -> bool;
fn main() { }
)");
    assert(ir_contains(ir, "declare i1 @negate(i1)"));
}

// ─── Extern fn calls ────────────────────────────────────────────────────────

static void test_call_extern_void_fn() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn print(value: &str);
fn main() { print("hello"); }
)");
    assert(ir_contains(ir, "declare void @print(ptr)"));
    assert(ir_contains(ir, "call void @print(ptr"));
}

static void test_call_extern_fn_with_return() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn to_str(value: num) -> str;
fn main() {
    let s: str = to_str(42);
}
)");
    assert(ir_contains(ir, "declare void @to_str(ptr, double)"));
    assert(ir_contains(ir, "call void @to_str(ptr"));
}

static void test_call_chain_through_extern() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn to_str(value: num) -> str;
extern "lang" fn print(value: &str);
fn main() {
    let s: str = to_str(42);
    print(&s);
}
)");
    assert(ir_contains(ir, "declare void @to_str(ptr, double)"));
    assert(ir_contains(ir, "declare void @print(ptr)"));
    assert(ir_contains(ir, "call void @to_str(ptr"));
    assert(ir_contains(ir, "call void @print(ptr"));
}

static void test_call_extern_fn_with_owned_str_param() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn to_str(value: num) -> str;
extern "lang" fn consume(value: str);
fn main() {
    let s: str = to_str(42);
    consume(s);
}
)");
    assert(ir_contains(ir, "declare void @consume(ptr)"));
    assert(ir_contains(ir, "call void @consume(ptr"));
}

static void test_extern_fn_custom_lang_link_name() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
[[lang = "cstc_std_print"]]
extern "lang" fn print(value: &str);
fn main() { print("hello"); }
)");
    assert(ir_contains(ir, "declare void @cstc_std_print(ptr)"));
    assert(ir_contains(ir, "call void @cstc_std_print(ptr"));
    assert(!ir_contains(ir, "declare void @print(ptr)"));
}

static void test_extern_link_name_reuses_matching_function_definition() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
[[lang = "foo"]]
extern "lang" fn bar();
fn foo() { }
fn main() { bar(); }
)");
    assert(ir_contains(ir, "define void @foo()"));
    assert(ir_contains(ir, "call void @foo()"));
    assert(!ir_contains(ir, "declare void @foo()"));
}

// ─── Multiple extern declarations ───────────────────────────────────────────

static void test_multiple_extern_declarations() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn print(value: &str);
extern "lang" fn println(value: &str);
extern "lang" fn to_str(value: num) -> str;
fn main() { }
)");
    assert(ir_contains(ir, "declare void @print(ptr)"));
    assert(ir_contains(ir, "declare void @println(ptr)"));
    assert(ir_contains(ir, "declare void @to_str(ptr, double)"));
}

// ─── Extern struct + fn interaction ──────────────────────────────────────────

static void test_extern_struct_as_fn_return() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" struct Handle;
extern "lang" fn create_handle() -> Handle;
fn main() {
    let h: Handle = create_handle();
}
)");
    // Handle is a Named ZST type → maps to %Handle (empty struct), not void
    assert(ir_contains(ir, "declare %Handle @create_handle()"));
}

int main() {
    test_extern_fn_void_no_params();
    test_extern_fn_with_str_param();
    test_extern_fn_with_return_type();
    test_extern_fn_multiple_params();
    test_extern_fn_with_owned_str_param();
    test_extern_fn_num_return();
    test_extern_fn_bool_param_and_return();
    test_call_extern_void_fn();
    test_call_extern_fn_with_return();
    test_call_chain_through_extern();
    test_call_extern_fn_with_owned_str_param();
    test_extern_fn_custom_lang_link_name();
    test_extern_link_name_reuses_matching_function_definition();
    test_multiple_extern_declarations();
    test_extern_struct_as_fn_return();
    return 0;
}
