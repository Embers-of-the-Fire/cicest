/// @file codegen_exprs.cpp
/// @brief LLVM IR codegen tests for expressions: arithmetic, comparisons,
///        unary ops, calls, let bindings.

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

// ─── Arithmetic ───────────────────────────────────────────────────────────────

static void test_add() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> num { a + b }");
    assert(ir_contains(ir, "fadd double"));
}

static void test_sub() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> num { a - b }");
    assert(ir_contains(ir, "fsub double"));
}

static void test_mul() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> num { a * b }");
    assert(ir_contains(ir, "fmul double"));
}

static void test_div() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> num { a / b }");
    assert(ir_contains(ir, "fdiv double"));
}

static void test_mod() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> num { a % b }");
    assert(ir_contains(ir, "frem double"));
}

// ─── Comparisons ──────────────────────────────────────────────────────────────

static void test_eq() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> bool { a == b }");
    assert(ir_contains(ir, "fcmp oeq"));
}

static void test_ne() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> bool { a != b }");
    assert(ir_contains(ir, "fcmp one"));
}

static void test_lt() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> bool { a < b }");
    assert(ir_contains(ir, "fcmp olt"));
}

static void test_le() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> bool { a <= b }");
    assert(ir_contains(ir, "fcmp ole"));
}

static void test_gt() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> bool { a > b }");
    assert(ir_contains(ir, "fcmp ogt"));
}

static void test_ge() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> bool { a >= b }");
    assert(ir_contains(ir, "fcmp oge"));
}

// ─── Boolean operations ──────────────────────────────────────────────────────

static void test_and() {
    const std::string ir = must_codegen("fn f(a: bool, b: bool) -> bool { a && b }");
    assert(ir_contains(ir, "and i1"));
}

static void test_or() {
    const std::string ir = must_codegen("fn f(a: bool, b: bool) -> bool { a || b }");
    assert(ir_contains(ir, "or i1"));
}

// ─── Unary operations ────────────────────────────────────────────────────────

static void test_negate() {
    const std::string ir = must_codegen("fn f(x: num) -> num { -x }");
    assert(ir_contains(ir, "fneg double"));
}

static void test_not() {
    const std::string ir = must_codegen("fn f(b: bool) -> bool { !b }");
    assert(ir_contains(ir, "xor i1"));
}

// ─── Let binding ──────────────────────────────────────────────────────────────

static void test_let_binding() {
    const std::string ir = must_codegen("fn f() -> num { let x = 10; x }");
    // After mem2reg, x should be a direct value
    assert(ir_contains(ir, "ret double"));
}

// ─── Nested expressions ──────────────────────────────────────────────────────

static void test_nested_binary() {
    const std::string ir = must_codegen("fn f(a: num, b: num) -> num { (a + b) * (a - b) }");
    assert(ir_contains(ir, "fadd double"));
    assert(ir_contains(ir, "fsub double"));
    assert(ir_contains(ir, "fmul double"));
}

// ─── Numeric constants ───────────────────────────────────────────────────────

static void test_num_constant() {
    const std::string ir = must_codegen("fn f() -> num { 3.14 }");
    assert(ir_contains(ir, "ret double"));
}

// ─── Bool constants ──────────────────────────────────────────────────────────

static void test_bool_true() {
    const std::string ir = must_codegen("fn f() -> bool { true }");
    assert(ir_contains(ir, "ret i1 true"));
}

static void test_bool_false() {
    const std::string ir = must_codegen("fn f() -> bool { false }");
    assert(ir_contains(ir, "ret i1 false"));
}

int main() {
    SymbolSession session;

    test_add();
    test_sub();
    test_mul();
    test_div();
    test_mod();

    test_eq();
    test_ne();
    test_lt();
    test_le();
    test_gt();
    test_ge();

    test_and();
    test_or();

    test_negate();
    test_not();

    test_let_binding();
    test_nested_binary();
    test_num_constant();
    test_bool_true();
    test_bool_false();

    return 0;
}
