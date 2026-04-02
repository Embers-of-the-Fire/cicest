/// @file lower_fns.cpp
/// @brief Tests for LIR lowering of function-level structure:
///        parameters, locals table, multiple functions, call sequences.

#include <cassert>
#include <string>
#include <variant>

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
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    return lower_program(*tyir);
}

static bool output_contains(const LirProgram& prog, const std::string& needle) {
    return format_program(prog).find(needle) != std::string::npos;
}

// ─── Parameter IDs ────────────────────────────────────────────────────────────

static void test_param_ids_start_at_zero() {
    // Parameters should occupy locals 0, 1, 2, ...
    const LirProgram prog = must_lower("fn f(a: num, b: num, c: bool) -> num { a }");
    const LirFnDef& fn = prog.fns[0];
    assert(fn.params.size() == 3);
    assert(fn.params[0].local == 0);
    assert(fn.params[1].local == 1);
    assert(fn.params[2].local == 2);
}

static void test_param_names_preserved() {
    const LirProgram prog = must_lower("fn distance(x: num, y: num) -> num { x - y }");
    const LirFnDef& fn = prog.fns[0];
    assert(fn.params[0].name == Symbol::intern("x"));
    assert(fn.params[1].name == Symbol::intern("y"));
}

static void test_param_types_preserved() {
    const LirProgram prog = must_lower("fn f(a: num, b: str, c: bool) { }");
    const LirFnDef& fn = prog.fns[0];
    assert(fn.params[0].ty == cstc::tyir::ty::num());
    assert(fn.params[1].ty == cstc::tyir::ty::str());
    assert(fn.params[2].ty == cstc::tyir::ty::bool_());
}

// ─── Locals table ─────────────────────────────────────────────────────────────

static void test_param_locals_in_table() {
    const LirProgram prog = must_lower("fn f(x: num, y: num) -> num { x + y }");
    const LirFnDef& fn = prog.fns[0];
    // First two locals must be the parameters.
    assert(fn.locals.size() >= 2);
    assert(fn.locals[0].ty == cstc::tyir::ty::num());
    assert(fn.locals[1].ty == cstc::tyir::ty::num());
    assert(fn.locals[0].debug_name.has_value());
    assert(*fn.locals[0].debug_name == Symbol::intern("x"));
    assert(*fn.locals[1].debug_name == Symbol::intern("y"));
}

static void test_let_binding_in_locals() {
    // A named let creates a fresh local with a debug name.
    const LirProgram prog = must_lower("fn f() { let answer = 42; }");
    const LirFnDef& fn = prog.fns[0];
    // Should contain a local named "answer".
    bool found = false;
    for (const LirLocalDecl& loc : fn.locals) {
        if (loc.debug_name.has_value() && loc.debug_name->is_valid()
            && loc.debug_name->as_str() == "answer") {
            found = true;
            break;
        }
    }
    assert(found);
}

static void test_let_type_in_locals() {
    const LirProgram prog = must_lower("fn f() { let x: bool = true; }");
    const LirFnDef& fn = prog.fns[0];
    bool found = false;
    for (const LirLocalDecl& loc : fn.locals) {
        if (loc.ty == cstc::tyir::ty::bool_()) {
            found = true;
            break;
        }
    }
    assert(found);
}

static void test_temporaries_are_unnamed() {
    // The result of a binary op is a temporary with no debug name.
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> num { a + b }");
    const LirFnDef& fn = prog.fns[0];
    // Look for a local without a debug name that is num-typed.
    bool found_temp = false;
    for (const LirLocalDecl& loc : fn.locals) {
        if (!loc.debug_name.has_value() && loc.ty == cstc::tyir::ty::num()) {
            found_temp = true;
            break;
        }
    }
    assert(found_temp);
}

// ─── Block structure ──────────────────────────────────────────────────────────

static void test_single_block_simple_fn() {
    // A function with only a literal return should have exactly one block.
    const LirProgram prog = must_lower("fn f() -> num { 42 }");
    const LirFnDef& fn = prog.fns[0];
    assert(fn.blocks.size() == 1);
}

static void test_return_ty_unit_no_value() {
    // Unit-returning function should have `return` with no operand.
    const LirProgram prog = must_lower("fn f() { }");
    const LirFnDef& fn = prog.fns[0];
    const auto& ret = std::get<LirReturn>(fn.blocks.back().terminator.node);
    assert(!ret.value.has_value());
}

static void test_return_ty_num_has_value() {
    // num-returning function should have `return <operand>`.
    const LirProgram prog = must_lower("fn f() -> num { 0 }");
    const LirFnDef& fn = prog.fns[0];
    const auto& ret = std::get<LirReturn>(fn.blocks.back().terminator.node);
    assert(ret.value.has_value());
}

static void test_runtime_fn_preserved() {
    const LirProgram prog =
        must_lower("struct Job; runtime fn dispatch(job: runtime Job) -> runtime Job { job }");
    const LirFnDef& fn = prog.fns[0];
    assert(fn.params.size() == 1);
    assert(fn.params[0].ty.is_runtime);
    assert(fn.return_ty.is_runtime);
    assert(output_contains(prog, "fn dispatch(job: runtime Job) -> runtime Job"));
}

// ─── Multiple functions ───────────────────────────────────────────────────────

static void test_two_functions() {
    const LirProgram prog = must_lower(
        "fn add(x: num, y: num) -> num { x + y }"
        "fn sub(x: num, y: num) -> num { x - y }");
    assert(prog.fns.size() == 2);
    assert(prog.fns[0].name == Symbol::intern("add"));
    assert(prog.fns[1].name == Symbol::intern("sub"));
}

static void test_mutually_calling_fns() {
    // Both functions calling each other (TyIR forward-resolves names).
    const LirProgram prog = must_lower(
        "fn double(x: num) -> num { x + x }"
        "fn quadruple(x: num) -> num { double(double(x)) }");
    assert(prog.fns.size() == 2);
    assert(output_contains(prog, "Call(double"));
}

// ─── Struct-typed parameter ───────────────────────────────────────────────────

static void test_struct_param() {
    const LirProgram prog = must_lower(
        "struct Point { x: num, y: num }"
        "fn get_x(p: Point) -> num { p.x }");
    const LirFnDef& fn = prog.fns[0];
    assert(fn.params.size() == 1);
    assert(fn.params[0].ty.is_named());
    assert(fn.params[0].ty.name == Symbol::intern("Point"));
}

// ─── Named-type return value ──────────────────────────────────────────────────

static void test_struct_return_ty() {
    const LirProgram prog = must_lower(
        "struct Pt { x: num, y: num }"
        "fn origin() -> Pt { Pt { x: 0, y: 0 } }");
    const LirFnDef& fn = prog.fns[0];
    assert(fn.return_ty.is_named());
    assert(fn.return_ty.name == Symbol::intern("Pt"));
}

// ─── Expression statement side effects ────────────────────────────────────────

static void test_expr_stmt_side_effects() {
    // fn f() { 1 + 2; }  — the result is discarded but the instruction is emitted.
    const LirProgram prog = must_lower("fn f() { 1 + 2; }");
    assert(output_contains(prog, "BinOp(+"));
}

static void test_struct_init_stops_after_returning_field() {
    const LirProgram prog = must_lower(
        "struct Pair { x: num, y: num }"
        "fn f() -> num { let pair = Pair { x: { return 7; }, y: 2 }; 0 }");
    const std::string out = format_program(prog);
    assert(out.find("StructInit(Pair") == std::string::npos);
    assert(out.find("return 7") != std::string::npos);
}

static void test_call_stops_after_returning_argument() {
    const LirProgram prog = must_lower(
        "fn id(x: num) -> num { x }"
        "fn f() -> num { id({ return 7; }); 0 }");
    const std::string out = format_program(prog);
    assert(out.find("Call(id") == std::string::npos);
    assert(out.find("return 7") != std::string::npos);
}

int main() {
    SymbolSession session;

    test_param_ids_start_at_zero();
    test_param_names_preserved();
    test_param_types_preserved();

    test_param_locals_in_table();
    test_let_binding_in_locals();
    test_let_type_in_locals();
    test_temporaries_are_unnamed();

    test_single_block_simple_fn();
    test_return_ty_unit_no_value();
    test_return_ty_num_has_value();
    test_runtime_fn_preserved();

    test_two_functions();
    test_mutually_calling_fns();

    test_struct_param();
    test_struct_return_ty();
    test_expr_stmt_side_effects();
    test_struct_init_stops_after_returning_field();
    test_call_stops_after_returning_argument();

    return 0;
}
