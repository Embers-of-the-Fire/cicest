/// @file codegen_basic.cpp
/// @brief Basic LLVM IR codegen tests: module structure, void/typed fns, params.

#include <cassert>
#include <string>

#include <cstc_codegen/codegen.hpp>
#include <cstc_lir/lir.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir_builder/builder.hpp>

using namespace cstc::symbol;

// ─── Helpers ──────────────────────────────────────────────────────────────────

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

// ─── Empty program ────────────────────────────────────────────────────────────

static void test_empty_program() {
    const std::string ir = must_codegen("");
    // Should produce a valid module (at minimum the module header)
    assert(ir_contains(ir, "ModuleID"));
}

// ─── Custom module name ──────────────────────────────────────────────────────

static void test_custom_module_name() {
    const auto ast = cstc::parser::parse_source("");
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    const auto lir = cstc::lir_builder::lower_program(*tyir);
    const std::string ir = cstc::codegen::emit_llvm_ir(lir, "my_module");
    assert(ir_contains(ir, "my_module"));
}

// ─── Void function ────────────────────────────────────────────────────────────

static void test_void_fn() {
    const std::string ir = must_codegen("fn noop() { }");
    assert(ir_contains(ir, "define void @noop()"));
    assert(ir_contains(ir, "ret void"));
}

// ─── Function returning num ──────────────────────────────────────────────────

static void test_fn_returns_num() {
    const std::string ir = must_codegen("fn f() -> num { 42 }");
    assert(ir_contains(ir, "define double @f()"));
    assert(ir_contains(ir, "ret double"));
}

// ─── Function returning bool ─────────────────────────────────────────────────

static void test_fn_returns_bool() {
    const std::string ir = must_codegen("fn f() -> bool { true }");
    assert(ir_contains(ir, "define i1 @f()"));
    assert(ir_contains(ir, "ret i1"));
}

// ─── Function returning str ──────────────────────────────────────────────────

static void test_fn_returns_str() {
    const std::string ir =
        must_codegen("extern \"lang\" fn to_str(value: num) -> str; fn f() -> str { to_str(0) }");
    assert(ir_contains(ir, "%cstc.str = type { ptr, i64, i8 }"));
    assert(ir_contains(ir, "define %cstc.str @f()"));
    assert(ir_contains(ir, "ret %cstc.str"));
}

// ─── Function with params ────────────────────────────────────────────────────

static void test_fn_with_params() {
    const std::string ir = must_codegen("fn add(x: num, y: num) -> num { x + y }");
    assert(ir_contains(ir, "define double @add(double"));
    assert(ir_contains(ir, "fadd double"));
}

// ─── Multiple functions ──────────────────────────────────────────────────────

static void test_multiple_functions() {
    const std::string ir = must_codegen(
        "fn a() { }"
        "fn b() { }");
    assert(ir_contains(ir, "define void @a()"));
    assert(ir_contains(ir, "define void @b()"));
}

// ─── Function calling another ────────────────────────────────────────────────

static void test_fn_call() {
    const std::string ir = must_codegen(
        "fn double(x: num) -> num { x + x }"
        "fn quad(x: num) -> num { double(double(x)) }");
    assert(ir_contains(ir, "call double @double(double"));
}

// ─── main function ABI ──────────────────────────────────────────────────────

static void test_main_unit_returns_i32_zero() {
    const std::string ir = must_codegen("fn main() { }");
    assert(ir_contains(ir, "define i32 @main()"));
    assert(ir_contains(ir, "ret i32 0"));
}

static void test_main_num_returns_i32() {
    const std::string ir = must_codegen("fn main() -> num { 42 }");
    assert(ir_contains(ir, "define i32 @main()"));
    assert(ir_contains(ir, "ret i32"));
    // Must not contain ret double (it's main, returns i32)
    assert(!ir_contains(ir, "ret double"));
}

static void test_main_num_fptosi_non_constant() {
    // With a non-constant operand, fptosi should be visible
    const std::string ir = must_codegen(
        "fn get_code() -> num { 42 }"
        "fn main() -> num { get_code() }");
    assert(ir_contains(ir, "define i32 @main()"));
    assert(ir_contains(ir, "fptosi"));
    assert(ir_contains(ir, "ret i32"));
}

static void test_main_never_returns_i32() {
    const std::string ir = must_codegen("fn main() -> ! { loop {} }");
    assert(ir_contains(ir, "define i32 @main()"));
    // Never-returning main should not have ret double
    assert(!ir_contains(ir, "ret double"));
}

static void test_non_main_fn_still_uses_double_return() {
    // Regression: non-main functions must use normal return types
    const std::string ir = must_codegen(
        "fn compute() -> num { 3.14 }"
        "fn main() { }");
    assert(ir_contains(ir, "define double @compute()"));
    assert(ir_contains(ir, "define i32 @main()"));
}

int main() {
    SymbolSession session;

    test_empty_program();
    test_custom_module_name();
    test_void_fn();
    test_fn_returns_num();
    test_fn_returns_bool();
    test_fn_returns_str();
    test_fn_with_params();
    test_multiple_functions();
    test_fn_call();
    test_main_unit_returns_i32_zero();
    test_main_num_returns_i32();
    test_main_num_fptosi_non_constant();
    test_main_never_returns_i32();
    test_non_main_fn_still_uses_double_return();

    return 0;
}
