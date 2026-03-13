/// @file lower_control_flow.cpp
/// @brief Tests for LIR lowering of control-flow expressions:
///        if/else, loop, while, for, break, continue, return.
///
/// Note: the language uses Rust-style parenthesis-free conditions:
///   `if cond { ... }`, `while cond { ... }`, `for (init; cond; step) { ... }`

#include <cassert>
#include <string>

#include <cstc_lir/lir.hpp>
#include <cstc_lir/printer.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>
#include <cstc_tyir_builder/builder.hpp>

using namespace cstc::lir;
using namespace cstc::lir_builder;
using namespace cstc::symbol;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static LirProgram must_lower(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    if (!ast.has_value()) { fprintf(stderr, "PARSE FAIL: %s\n", source); assert(false); }
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    if (!tyir.has_value()) { fprintf(stderr, "TYIR FAIL: %s\n  error: %s\n", source, tyir.error().message.c_str()); assert(false); }
    return lower_program(*tyir);
}

static const LirFnDef& first_fn(const LirProgram& prog) {
    assert(!prog.fns.empty());
    return prog.fns[0];
}

static bool output_contains(const LirProgram& prog, const std::string& needle) {
    return format_program(prog).find(needle) != std::string::npos;
}

// ─── If (no else) ─────────────────────────────────────────────────────────────

static void test_if_no_else() {
    // if (cond) { }  →  SwitchBool + two blocks + merge
    const LirProgram prog = must_lower("fn f(b: bool) { if b { } }");
    const LirFnDef& fn    = first_fn(prog);
    // At least 3 blocks: entry (with SwitchBool), then-body, merge.
    assert(fn.blocks.size() >= 3);
    assert(output_contains(prog, "switchBool"));
}

// ─── If-else ──────────────────────────────────────────────────────────────────

static void test_if_else_num() {
    const LirProgram prog = must_lower(
        "fn f(b: bool) -> num { if b { 1 } else { 0 } }");
    const LirFnDef& fn = first_fn(prog);
    // At least 4 blocks: entry, then, else, merge.
    assert(fn.blocks.size() >= 4);
    assert(output_contains(prog, "switchBool"));
    assert(output_contains(prog, "1"));
    assert(output_contains(prog, "0"));
}

static void test_if_else_bool() {
    const LirProgram prog = must_lower(
        "fn f(b: bool) -> bool { if b { true } else { false } }");
    assert(output_contains(prog, "switchBool"));
    assert(output_contains(prog, "true"));
    assert(output_contains(prog, "false"));
}

// ─── If-else if chain ─────────────────────────────────────────────────────────

static void test_if_else_if() {
    const LirProgram prog = must_lower(
        "fn f(a: bool, b: bool) -> num {"
        "  if a { 1 } else { if b { 2 } else { 3 } }"
        "}");
    // Multiple SwitchBool terminators expected.
    const std::string out = format_program(prog);
    std::size_t count = 0;
    std::size_t pos   = 0;
    while ((pos = out.find("switchBool", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    assert(count >= 2);
}

// ─── Loop + break ─────────────────────────────────────────────────────────────

static void test_loop_break() {
    // loop { break; }  →  header block → body (with jump to break_target)
    const LirProgram prog = must_lower("fn f() { loop { break; } }");
    const LirFnDef& fn    = first_fn(prog);
    // At least 3 blocks: entry, header, after-loop.
    assert(fn.blocks.size() >= 3);
    assert(output_contains(prog, "jump"));
}

static void test_loop_break_value() {
    // loop { break 42; }  →  store 42 into result local then jump
    // Note: tyir always types loop as unit, so the function must return unit.
    const LirProgram prog = must_lower("fn f() { loop { break 42; } }");
    assert(output_contains(prog, "42"));
    assert(output_contains(prog, "jump"));
}

static void test_loop_infinite_with_return() {
    // loop { return; }  →  loop body block has a return terminator (no back-edge)
    // Note: tyir always types loop as unit; use void return to avoid type mismatch.
    const LirProgram prog = must_lower("fn f() { loop { return; } }");
    assert(output_contains(prog, "return"));
}

// ─── While loop ───────────────────────────────────────────────────────────────

static void test_while_simple() {
    // while cond { }  →  cond-block (SwitchBool), body-block, after-block
    const LirProgram prog = must_lower("fn f(b: bool) { while b { } }");
    const LirFnDef& fn    = first_fn(prog);
    assert(fn.blocks.size() >= 3);
    assert(output_contains(prog, "switchBool"));
}

static void test_while_with_body() {
    const LirProgram prog = must_lower(
        "fn f(b: bool) { while b { let _ = 1; } }");
    assert(output_contains(prog, "switchBool"));
}

static void test_while_break() {
    const LirProgram prog = must_lower("fn f(b: bool) { while b { break; } }");
    assert(!prog.fns.empty());
}

static void test_while_continue() {
    const LirProgram prog = must_lower("fn f(b: bool) { while b { continue; } }");
    assert(!prog.fns.empty());
}

// ─── For loop ─────────────────────────────────────────────────────────────────

static void test_for_with_init_and_condition() {
    const LirProgram prog = must_lower(
        "fn f() { for (let i: num = 0; i < 10; ) { } }");
    assert(output_contains(prog, "switchBool"));
    assert(output_contains(prog, "0"));
}

static void test_for_with_step() {
    const LirProgram prog = must_lower(
        "fn f(n: num) { for (let i: num = 0; i < n; i + 1) { } }");
    assert(output_contains(prog, "switchBool"));
    assert(output_contains(prog, "BinOp(+"));
}

static void test_for_no_condition() {
    // for (;;) {}  →  unconditional loop (like `loop`)
    const LirProgram prog = must_lower("fn f() { for (;;) { break; } }");
    assert(!prog.fns.empty());
}

static void test_for_break() {
    const LirProgram prog = must_lower(
        "fn f() { for (let i: num = 0; i < 5; ) { break; } }");
    assert(!prog.fns.empty());
}

static void test_for_continue() {
    const LirProgram prog = must_lower(
        "fn f() { for (let i: num = 0; i < 5; ) { continue; } }");
    assert(!prog.fns.empty());
}

// ─── Return ───────────────────────────────────────────────────────────────────

static void test_early_return() {
    const LirProgram prog = must_lower(
        "fn f(b: bool) -> num {"
        "  if b { return 1; }"
        "  0"
        "}");
    assert(output_contains(prog, "return 1"));
    assert(output_contains(prog, "0"));
}

static void test_return_void() {
    const LirProgram prog = must_lower("fn f() { return; }");
    assert(output_contains(prog, "return\n"));
}

static void test_return_from_nested_block() {
    // `return 42` without semicolon → tail expression with type Never,
    // which is compatible with the function's `num` return type.
    const LirProgram prog = must_lower(
        "fn f() -> num {"
        "  { return 42 }"
        "}");
    assert(output_contains(prog, "return 42"));
}

// ─── Nested control flow ──────────────────────────────────────────────────────

static void test_if_inside_while() {
    const LirProgram prog = must_lower(
        "fn f(b: bool) {"
        "  while b {"
        "    if b { break; }"
        "  }"
        "}");
    const LirFnDef& fn = first_fn(prog);
    // Both while and if generate SwitchBool terminators.
    const std::string out = format_program(prog);
    std::size_t count = 0;
    std::size_t pos   = 0;
    while ((pos = out.find("switchBool", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    assert(count >= 2);
    assert(fn.blocks.size() >= 5);
}

static void test_nested_if() {
    const LirProgram prog = must_lower(
        "fn f(a: bool, b: bool) -> num {"
        "  if a {"
        "    if b { 1 } else { 2 }"
        "  } else {"
        "    3"
        "  }"
        "}");
    assert(!prog.fns.empty());
    assert(first_fn(prog).blocks.size() >= 5);
}

// ─── Block count invariant ────────────────────────────────────────────────────

static void test_block_ids_are_dense() {
    // All block IDs should equal their position in the blocks array.
    const LirProgram prog = must_lower(
        "fn f(a: bool, b: bool) -> num {"
        "  if a { if b { 1 } else { 2 } } else { 3 }"
        "}");
    const LirFnDef& fn = first_fn(prog);
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
        assert(fn.blocks[i].id == static_cast<LirBlockId>(i));
}

int main() {
    SymbolSession session;

    test_if_no_else();
    test_if_else_num();
    test_if_else_bool();
    test_if_else_if();

    test_loop_break();
    test_loop_break_value();
    test_loop_infinite_with_return();

    test_while_simple();
    test_while_with_body();
    test_while_break();
    test_while_continue();

    test_for_with_init_and_condition();
    test_for_with_step();
    test_for_no_condition();
    test_for_break();
    test_for_continue();

    test_early_return();
    test_return_void();
    test_return_from_nested_block();

    test_if_inside_while();
    test_nested_if();
    test_block_ids_are_dense();

    return 0;
}
