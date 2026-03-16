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
    return cstc::codegen::emit_llvm_ir(lir);
}

static bool ir_contains(const std::string& ir, const std::string& needle) {
    return ir.find(needle) != std::string::npos;
}

// ─── Integration: full std prelude usage ─────────────────────────────────────

/// Simulates the full std prelude (same declarations as libraries/std/prelude.cst)
/// combined with user code that calls prelude functions.
static void test_program_with_full_prelude() {
    SymbolSession session;
    const auto ir = must_codegen(R"(
extern "lang" fn print(value: str);
extern "lang" fn println(value: str);
extern "lang" fn to_str(value: num) -> str;
extern "lang" fn str_concat(a: str, b: str) -> str;
extern "lang" fn str_len(value: str) -> num;

fn main() {
    println("hello world");
    let s: str = to_str(42);
    print(s);
    let combined: str = str_concat("a", "b");
    let length: num = str_len(combined);
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
extern "lang" fn println(value: str);
extern "lang" fn to_str(value: num) -> str;

fn greet(name: str) {
    println(name);
}

fn main() {
    greet("world");
    let s: str = to_str(100);
    greet(s);
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
extern "lang" fn println(value: str);
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
    println(to_str(use_point(p)));
    let c: Color = use_color(Color::Green);
}
)");
    assert(ir_contains(ir, "declare void @println(ptr)"));
    assert(ir_contains(ir, "declare ptr @to_str(double)"));
    assert(ir_contains(ir, "%Point = type { double, double }"));
    assert(ir_contains(ir, "%Color = type { i32 }"));
}

int main() {
    test_program_with_full_prelude();
    test_user_fn_calling_extern();
    test_extern_with_opaque_types();
    test_extern_with_structs_and_enums();
    return 0;
}
