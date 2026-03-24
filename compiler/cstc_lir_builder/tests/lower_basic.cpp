/// @file lower_basic.cpp
/// @brief Basic TyIR → LIR lowering tests: program structure, declarations,
///        and simple function bodies.

#include <cassert>
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

// ─── Empty program ────────────────────────────────────────────────────────────

static void test_empty_program() {
    const LirProgram prog = must_lower("");
    assert(prog.structs.empty());
    assert(prog.enums.empty());
    assert(prog.fns.empty());
}

// ─── Struct forwarding ────────────────────────────────────────────────────────

static void test_struct_forwarded() {
    const LirProgram prog = must_lower("struct Point { x: num, y: num }");
    assert(prog.structs.size() == 1);
    const LirStructDecl& s = prog.structs[0];
    assert(s.name == Symbol::intern("Point"));
    assert(!s.is_zst);
    assert(s.fields.size() == 2);
    assert(s.fields[0].name == Symbol::intern("x"));
    assert(s.fields[0].ty == cstc::tyir::ty::num());
    assert(s.fields[1].name == Symbol::intern("y"));
    assert(s.fields[1].ty == cstc::tyir::ty::num());
}

static void test_zst_struct_forwarded() {
    const LirProgram prog = must_lower("struct Marker;");
    assert(prog.structs.size() == 1);
    assert(prog.structs[0].is_zst);
    assert(prog.structs[0].fields.empty());
}

// ─── Enum forwarding ──────────────────────────────────────────────────────────

static void test_enum_forwarded() {
    const LirProgram prog = must_lower("enum Dir { North, South, East, West }");
    assert(prog.enums.size() == 1);
    const LirEnumDecl& e = prog.enums[0];
    assert(e.name == Symbol::intern("Dir"));
    assert(e.variants.size() == 4);
    assert(e.variants[0].name == Symbol::intern("North"));
    assert(e.variants[3].name == Symbol::intern("West"));
}

// ─── Simple function: no params, void return ─────────────────────────────────

static void test_fn_noop() {
    const LirProgram prog = must_lower("fn noop() { }");
    assert(prog.fns.size() == 1);
    const LirFnDef& fn = prog.fns[0];
    assert(fn.name == Symbol::intern("noop"));
    assert(fn.params.empty());
    assert(fn.return_ty == cstc::tyir::ty::unit());
    assert(!fn.blocks.empty());
    // Entry block should have a void return terminator.
    assert(std::holds_alternative<LirReturn>(fn.blocks[kEntryBlock].terminator.node));
    const auto& ret = std::get<LirReturn>(fn.blocks[kEntryBlock].terminator.node);
    assert(!ret.value.has_value());
}

// ─── Function with params and numeric return ──────────────────────────────────

static void test_fn_add_params() {
    const LirProgram prog = must_lower("fn add(x: num, y: num) -> num { x + y }");
    assert(prog.fns.size() == 1);
    const LirFnDef& fn = prog.fns[0];
    assert(fn.name == Symbol::intern("add"));
    assert(fn.params.size() == 2);
    assert(fn.params[0].name == Symbol::intern("x"));
    assert(fn.params[0].ty == cstc::tyir::ty::num());
    assert(fn.params[1].name == Symbol::intern("y"));
    assert(fn.params[1].ty == cstc::tyir::ty::num());
    assert(fn.return_ty == cstc::tyir::ty::num());
    // Params occupy the first two locals.
    assert(fn.locals.size() >= 2);
    assert(fn.locals[0].ty == cstc::tyir::ty::num());
    assert(fn.locals[1].ty == cstc::tyir::ty::num());
}

// ─── Function with bool return ───────────────────────────────────────────────

static void test_fn_bool_return() {
    const LirProgram prog = must_lower("fn yes() -> bool { true }");
    assert(prog.fns.size() == 1);
    const LirFnDef& fn = prog.fns[0];
    assert(fn.return_ty == cstc::tyir::ty::bool_());
}

// ─── Function with str return ────────────────────────────────────────────────

static void test_fn_str_return() {
    const LirProgram prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn greeting() -> str { to_str(0) }");
    assert(prog.fns.size() == 1);
    assert(prog.fns[0].return_ty == cstc::tyir::ty::str());
}

// ─── Multiple items: order preserved ─────────────────────────────────────────

static void test_item_order() {
    const LirProgram prog = must_lower(
        "struct A;"
        "enum B { X }"
        "fn c() { }");
    assert(prog.structs.size() == 1);
    assert(prog.enums.size() == 1);
    assert(prog.fns.size() == 1);
    assert(prog.structs[0].name == Symbol::intern("A"));
    assert(prog.enums[0].name == Symbol::intern("B"));
    assert(prog.fns[0].name == Symbol::intern("c"));
}

// ─── Printer smoke test ───────────────────────────────────────────────────────

static void test_printer_smoke() {
    const LirProgram prog = must_lower("fn add(x: num, y: num) -> num { x + y }");
    const std::string out = format_program(prog);
    assert(out.find("LirProgram") != std::string::npos);
    assert(out.find("fn add") != std::string::npos);
    assert(out.find("bb0") != std::string::npos);
}

// ─── Entry block invariant ────────────────────────────────────────────────────

static void test_entry_block_is_zero() {
    const LirProgram prog = must_lower("fn f() { }");
    assert(!prog.fns.empty());
    const LirFnDef& fn = prog.fns[0];
    assert(!fn.blocks.empty());
    assert(fn.blocks[0].id == kEntryBlock);
}

int main() {
    SymbolSession session;

    test_empty_program();
    test_struct_forwarded();
    test_zst_struct_forwarded();
    test_enum_forwarded();
    test_fn_noop();
    test_fn_add_params();
    test_fn_bool_return();
    test_fn_str_return();
    test_item_order();
    test_printer_smoke();
    test_entry_block_is_zero();

    return 0;
}
