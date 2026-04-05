/// @file codegen_control_flow.cpp
/// @brief LLVM IR codegen tests for control flow: if, while, for, loop,
///        break, continue, return.

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
    assert(lir.has_value());
    return cstc::codegen::emit_llvm_ir(*lir);
}

static bool ir_contains(const std::string& ir, const std::string& needle) {
    return ir.find(needle) != std::string::npos;
}

// ─── If expression ────────────────────────────────────────────────────────────

static void test_if_else() {
    const std::string ir = must_codegen("fn f(x: bool) -> num { if x { 1 } else { 2 } }");
    // Should have a conditional branch
    assert(ir_contains(ir, "br i1"));
    assert(ir_contains(ir, "ret double"));
}

static void test_if_no_else() {
    const std::string ir = must_codegen("fn f(x: bool) { if x { let _ = 1; } }");
    assert(ir_contains(ir, "br i1"));
}

// ─── While loop ───────────────────────────────────────────────────────────────

static void test_while_loop() {
    const std::string ir = must_codegen("fn f(x: bool) { while x { let _ = 1; } }");
    // Should have a back-edge (br label) and a conditional branch
    assert(ir_contains(ir, "br i1"));
    assert(ir_contains(ir, "br label"));
}

// ─── For loop ─────────────────────────────────────────────────────────────────

static void test_for_loop() {
    const std::string ir = must_codegen("fn f() { for (let i = 0; i < 10; i + 1) { let _ = i; } }");
    assert(ir_contains(ir, "br i1"));
    assert(ir_contains(ir, "fcmp olt"));
}

// ─── Loop (infinite) ──────────────────────────────────────────────────────────

static void test_infinite_loop_with_break() {
    const std::string ir = must_codegen("fn f() { loop { break; } }");
    assert(ir_contains(ir, "br label"));
    assert(ir_contains(ir, "ret void"));
}

// ─── Return statement ─────────────────────────────────────────────────────────

static void test_early_return() {
    const std::string ir = must_codegen("fn f(x: bool) -> num { if x { return 1; } 2 }");
    assert(ir_contains(ir, "ret double"));
}

// ─── Continue ─────────────────────────────────────────────────────────────────

static void test_continue_in_loop() {
    const std::string ir = must_codegen("fn f(x: bool) { loop { if x { continue; } break; } }");
    assert(ir_contains(ir, "br i1"));
    assert(ir_contains(ir, "br label"));
}

// ─── Nested control flow ─────────────────────────────────────────────────────

static void test_nested_if_in_while() {
    const std::string ir = must_codegen("fn f(a: bool, b: bool) { while a { if b { break; } } }");
    assert(ir_contains(ir, "br i1"));
}

int main() {
    SymbolSession session;

    test_if_else();
    test_if_no_else();
    test_while_loop();
    test_for_loop();
    test_infinite_loop_with_break();
    test_early_return();
    test_continue_in_loop();
    test_nested_if_in_while();

    return 0;
}
