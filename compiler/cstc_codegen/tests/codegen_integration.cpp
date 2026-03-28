#include <cassert>
#include <string>

#include <cstc_codegen/codegen.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir_builder/builder.hpp>
#include <cstc_tyir_interp/interp.hpp>

using namespace cstc::symbol;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string must_codegen(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    const auto lir = cstc::lir_builder::lower_program(*tyir);
    return cstc::codegen::emit_llvm_ir(lir);
}

static std::string must_codegen_folded(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    const auto folded = cstc::tyir_interp::fold_program(*tyir);
    assert(folded.has_value());
    const auto lir = cstc::lir_builder::lower_program(*folded);
    return cstc::codegen::emit_llvm_ir(lir);
}

static bool ir_contains(const std::string& ir, const std::string& needle) {
    return ir.find(needle) != std::string::npos;
}

static std::size_t count_occurrences(const std::string& ir, const std::string& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = ir.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// ─── Integration: full std prelude usage ─────────────────────────────────────

/// Simulates the full std prelude (same declarations as libraries/std/prelude.cst)
/// combined with user code that calls prelude functions.
static void test_program_with_full_prelude() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn print(value: &str);
extern "lang" fn println(value: &str);
extern "lang" fn to_str(value: num) -> str;
extern "lang" fn str_concat(a: &str, b: &str) -> str;
extern "lang" fn str_len(value: &str) -> num;

fn main() {
    println("hello world");
    let s: str = to_str(42);
    print(&s);
    let combined: str = str_concat("a", "b");
    let length: num = str_len(&combined);
}
)");
    // All 5 extern fns should be declared
    assert(ir_contains(ir, "declare void @print(ptr)"));
    assert(ir_contains(ir, "declare void @println(ptr)"));
    assert(ir_contains(ir, "declare ptr @to_str(double)"));
    assert(ir_contains(ir, "declare ptr @str_concat(ptr, ptr)"));
    assert(ir_contains(ir, "declare double @str_len(ptr)"));

    // Calls should be emitted
    assert(ir_contains(ir, "call void @println(ptr"));
    assert(ir_contains(ir, "call ptr @to_str(double"));
    assert(ir_contains(ir, "call void @print(ptr"));
    assert(ir_contains(ir, "call ptr @str_concat(ptr"));
    assert(ir_contains(ir, "call double @str_len(ptr"));

    // main should return i32
    assert(ir_contains(ir, "define i32 @main()"));
}

/// Tests that user functions can interoperate with extern functions.
static void test_user_fn_calling_extern() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn println(value: &str);
extern "lang" fn to_str(value: num) -> str;

fn greet(name: &str) {
    println(name);
}

fn main() {
    greet("world");
    let s: str = to_str(100);
    greet(&s);
}
)");
    assert(ir_contains(ir, "declare void @println(ptr)"));
    assert(ir_contains(ir, "declare ptr @to_str(double)"));
    assert(ir_contains(ir, "define void @greet(ptr"));
    assert(ir_contains(ir, "define i32 @main()"));
}

/// Tests extern functions with opaque struct types.
static void test_extern_with_opaque_types() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" struct Handle;
extern "lang" fn open_handle() -> Handle;
extern "lang" fn close_handle(h: Handle);

fn main() {
    let h: Handle = open_handle();
    close_handle(h);
}
)");
    // Handle is a Named ZST → maps to %Handle (empty struct), not void
    assert(ir_contains(ir, "%Handle = type {}"));
    assert(ir_contains(ir, "declare %Handle @open_handle()"));
    assert(ir_contains(ir, "declare void @close_handle(%Handle)"));
}

/// Tests that extern functions coexist with regular structs and enums.
static void test_extern_with_structs_and_enums() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn println(value: &str);
extern "lang" fn to_str(value: num) -> str;

struct Point {
    x: num,
    y: num,
}

enum Color {
    Red,
    Green,
    Blue,
}

fn use_point(p: Point) -> num { p.x }
fn use_color(c: Color) -> Color { c }

fn main() {
    let p: Point = Point { x: 1, y: 2 };
    let rendered: str = to_str(use_point(p));
    println(&rendered);
    let c: Color = use_color(Color::Green);
}
)");
    assert(ir_contains(ir, "declare void @println(ptr)"));
    assert(ir_contains(ir, "declare ptr @to_str(double)"));
    assert(ir_contains(ir, "%Point = type { double, double }"));
    assert(ir_contains(ir, "%Color = type { i32 }"));
}

static void test_auto_drop_emits_runtime_str_free() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn to_str(value: num) -> str;

fn main() {
    let s: str = to_str(42);
}
)");
    assert(ir_contains(ir, "declare void @cstc_std_str_free(ptr)"));
    assert(ir_contains(ir, "call void @cstc_std_str_free(ptr"));
}

static void test_borrowed_str_literal_does_not_emit_runtime_str_free() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn print(value: &str);

fn main() {
    let literal: &str = "hi";
    let alias: &str = literal;
    print(alias);
}
)");
    assert(ir_contains(ir, "@0 = private unnamed_addr constant [3 x i8] c\"hi\\00\""));
    assert(ir_contains(ir, "call void @print(ptr @0)"));
    assert(!ir_contains(ir, "call void @cstc_std_str_free(ptr"));
}

static void test_auto_drop_recurses_through_struct_fields() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn to_str(value: num) -> str;

struct Pair {
    left: str,
    right: str,
}

fn main() {
    let p: Pair = Pair { left: to_str(1), right: to_str(2) };
}
)");
    assert(ir_contains(ir, "%Pair = type { ptr, ptr }"));
    assert(count_occurrences(ir, "call void @cstc_std_str_free(ptr") >= 2);
}

static void test_nested_field_borrow_uses_projected_geps() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn print(value: &str);
extern "lang" fn to_str(value: num) -> str;

struct Inner {
    s: str,
}

struct Outer {
    inner: Inner,
}

fn main() {
    let outer: Outer = Outer { inner: Inner { s: to_str(1) } };
    print(&outer.inner.s);
}
)");
    assert(ir_contains(ir, "declare void @print(ptr)"));
    assert(ir_contains(ir, "getelementptr inbounds nuw %Outer"));
    assert(ir_contains(ir, "getelementptr inbounds nuw %Inner"));
    assert(ir_contains(ir, "call void @print(ptr"));
}

static void test_folded_owned_str_avoids_runtime_to_str_call() {
    SymbolSession session;
    const auto ir = must_codegen_folded(R"(
[[lang = "cstc_std_to_str"]]
extern "lang" fn to_str(value: num) -> str;
[[lang = "cstc_std_print"]]
runtime extern "lang" fn print(value: &str);

fn main() {
    let rendered: str = to_str(42);
    print(&rendered);
}
)");
    assert(!ir_contains(ir, "call ptr @cstc_std_to_str(double"));
    assert(ir_contains(ir, "declare ptr @cstc_std_str_concat(ptr, ptr)"));
    assert(ir_contains(ir, "call ptr @cstc_std_str_concat(ptr"));
    assert(ir_contains(ir, "call void @cstc_std_print(ptr"));
}

int main() {
    test_program_with_full_prelude();
    test_user_fn_calling_extern();
    test_extern_with_opaque_types();
    test_extern_with_structs_and_enums();
    test_auto_drop_emits_runtime_str_free();
    test_borrowed_str_literal_does_not_emit_runtime_str_free();
    test_auto_drop_recurses_through_struct_fields();
    test_nested_field_borrow_uses_projected_geps();
    test_folded_owned_str_avoids_runtime_to_str_call();
    return 0;
}
