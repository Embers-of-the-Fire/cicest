/// @file printer_basic.cpp
/// @brief Tests for the LIR human-readable printer.

#include <cassert>
#include <optional>
#include <string>

#include <cstc_lir/printer.hpp>
#include <cstc_symbol/symbol.hpp>

using namespace cstc::lir;
using namespace cstc::symbol;
using namespace cstc::tyir;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// ─── Empty program ────────────────────────────────────────────────────────────

static void test_print_empty_program() {
    SymbolSession session;
    const LirProgram prog;
    const std::string out = format_program(prog);
    assert(out == "LirProgram\n");
}

// ─── Struct declarations ──────────────────────────────────────────────────────

static void test_print_zst_struct() {
    SymbolSession session;
    LirProgram prog;
    LirStructDecl s;
    s.name = Symbol::intern("Marker");
    s.is_zst = true;
    prog.structs.push_back(std::move(s));
    const std::string out = format_program(prog);
    assert(contains(out, "struct Marker;"));
}

static void test_print_struct_with_fields() {
    SymbolSession session;
    LirProgram prog;
    LirStructDecl s;
    s.name = Symbol::intern("Point");
    s.is_zst = false;
    s.fields.push_back({Symbol::intern("x"), ty::num(), {}});
    s.fields.push_back({Symbol::intern("y"), ty::num(), {}});
    prog.structs.push_back(std::move(s));
    const std::string out = format_program(prog);
    assert(contains(out, "struct Point"));
    assert(contains(out, "x: num"));
    assert(contains(out, "y: num"));
}

// ─── Enum declarations ────────────────────────────────────────────────────────

static void test_print_enum() {
    SymbolSession session;
    LirProgram prog;
    LirEnumDecl e;
    e.name = Symbol::intern("Dir");
    e.variants.push_back({Symbol::intern("North"), std::nullopt, {}});
    e.variants.push_back({Symbol::intern("South"), std::nullopt, {}});
    prog.enums.push_back(std::move(e));
    const std::string out = format_program(prog);
    assert(contains(out, "enum Dir"));
    assert(contains(out, "North"));
    assert(contains(out, "South"));
}

static void test_print_enum_with_discriminants() {
    SymbolSession session;
    LirProgram prog;
    LirEnumDecl e;
    e.name = Symbol::intern("Val");
    e.variants.push_back({Symbol::intern("A"), Symbol::intern("0"), {}});
    e.variants.push_back({Symbol::intern("B"), Symbol::intern("5"), {}});
    prog.enums.push_back(std::move(e));
    const std::string out = format_program(prog);
    assert(contains(out, "A = 0"));
    assert(contains(out, "B = 5"));
}

// ─── Function bodies ──────────────────────────────────────────────────────────

static void test_print_empty_fn() {
    SymbolSession session;
    LirProgram prog;
    LirFnDef fn;
    fn.name = Symbol::intern("noop");
    fn.return_ty = ty::unit();
    fn.locals.push_back({0, ty::unit(), std::nullopt});

    LirBasicBlock entry;
    entry.id = kEntryBlock;
    entry.terminator = LirTerminator{LirReturn{std::nullopt}, {}};
    fn.blocks.push_back(std::move(entry));
    prog.fns.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "fn noop"));
    assert(contains(out, "-> Unit"));
    assert(contains(out, "bb0:"));
    assert(contains(out, "return\n"));
}

static void test_print_fn_with_params() {
    SymbolSession session;
    LirProgram prog;
    LirFnDef fn;
    fn.name = Symbol::intern("add");
    fn.return_ty = ty::num();

    fn.locals.push_back({0, ty::num(), Symbol::intern("x")});
    fn.locals.push_back({1, ty::num(), Symbol::intern("y")});
    fn.locals.push_back({2, ty::num(), std::nullopt});

    fn.params.push_back({0, Symbol::intern("x"), ty::num(), {}});
    fn.params.push_back({1, Symbol::intern("y"), ty::num(), {}});

    LirBasicBlock entry;
    entry.id = kEntryBlock;
    entry.stmts.push_back(
        LirStmt{
            LirPlace::local(2),
            LirRvalue{LirBinaryOp{
                cstc::ast::BinaryOp::Add, LirOperand::copy(LirPlace::local(0)),
                LirOperand::copy(LirPlace::local(1))}},
            {}});
    entry.terminator = LirTerminator{LirReturn{LirOperand::copy(LirPlace::local(2))}, {}};
    fn.blocks.push_back(std::move(entry));
    prog.fns.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "fn add(x: num, y: num) -> num"));
    assert(contains(out, "BinOp(+"));
    assert(contains(out, "return copy(_%2)"));
}

static void test_print_runtime_items() {
    SymbolSession session;
    LirProgram prog;

    LirFnDef fn;
    fn.name = Symbol::intern("dispatch");
    fn.return_ty = ty::named(Symbol::intern("Job"), kInvalidSymbol, ValueSemantics::Move, true);
    fn.locals.push_back({0, fn.return_ty, Symbol::intern("job")});
    fn.params.push_back({0, Symbol::intern("job"), fn.return_ty, {}});

    LirBasicBlock entry;
    entry.id = kEntryBlock;
    entry.terminator = LirTerminator{LirReturn{LirOperand::copy(LirPlace::local(0))}, {}};
    fn.blocks.push_back(std::move(entry));
    prog.fns.push_back(std::move(fn));

    LirExternFnDecl ext;
    ext.abi = Symbol::intern("lang");
    ext.name = Symbol::intern("poll");
    ext.return_ty = ty::unit();
    ext.params.push_back({0, Symbol::intern("value"), ty::ref(ty::str(true)), {}});
    prog.extern_fns.push_back(std::move(ext));

    const std::string out = format_program(prog);
    assert(contains(out, "fn dispatch(job: runtime Job) -> runtime Job"));
    assert(contains(out, "extern \"lang\" fn poll(value: &runtime str) -> Unit"));
}

static void test_print_fn_locals_table() {
    SymbolSession session;
    LirProgram prog;
    LirFnDef fn;
    fn.name = Symbol::intern("f");
    fn.return_ty = ty::unit();
    fn.locals.push_back({0, ty::num(), Symbol::intern("x")});
    fn.locals.push_back({1, ty::bool_(), std::nullopt});

    LirBasicBlock entry;
    entry.id = 0;
    entry.terminator = LirTerminator{LirReturn{std::nullopt}, {}};
    fn.blocks.push_back(std::move(entry));
    prog.fns.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "locals:"));
    assert(contains(out, "_%0: num"));
    assert(contains(out, "_%1: bool"));
    assert(contains(out, "/* x */"));
}

static void test_print_switch_bool() {
    SymbolSession session;
    LirProgram prog;
    LirFnDef fn;
    fn.name = Symbol::intern("choose");
    fn.return_ty = ty::num();
    fn.locals.push_back({0, ty::bool_(), Symbol::intern("b")});
    fn.locals.push_back({1, ty::num(), std::nullopt});
    fn.params.push_back({0, Symbol::intern("b"), ty::bool_(), {}});

    LirBasicBlock bb0;
    bb0.id = 0;
    bb0.terminator = LirTerminator{
        LirSwitchBool{LirOperand::copy(LirPlace::local(0)), 1, 2},
        {}
    };

    LirBasicBlock bb1;
    bb1.id = 1;
    bb1.stmts.push_back(
        LirStmt{
            LirPlace::local(1),
            LirRvalue{LirUse{LirOperand::from_const(LirConst::num(Symbol::intern("1")))}},
            {}});
    bb1.terminator = LirTerminator{LirJump{2}, {}};

    LirBasicBlock bb2;
    bb2.id = 2;
    bb2.terminator = LirTerminator{LirReturn{LirOperand::copy(LirPlace::local(1))}, {}};

    fn.blocks.push_back(std::move(bb0));
    fn.blocks.push_back(std::move(bb1));
    fn.blocks.push_back(std::move(bb2));
    prog.fns.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "switchBool"));
    assert(contains(out, "true: bb1"));
    assert(contains(out, "false: bb2"));
    assert(contains(out, "jump bb2"));
}

static void test_print_call_rvalue() {
    SymbolSession session;
    LirProgram prog;
    LirFnDef fn;
    fn.name = Symbol::intern("caller");
    fn.return_ty = ty::unit();
    fn.locals.push_back({0, ty::unit(), std::nullopt});

    LirBasicBlock entry;
    entry.id = 0;
    entry.stmts.push_back(
        LirStmt{LirPlace::local(0), LirRvalue{LirCall{Symbol::intern("noop"), {}}}, {}});
    entry.terminator = LirTerminator{LirReturn{std::nullopt}, {}};
    fn.blocks.push_back(std::move(entry));
    prog.fns.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "Call(noop)"));
}

static void test_print_borrow_move_and_drop() {
    SymbolSession session;
    LirProgram prog;
    LirFnDef fn;
    fn.name = Symbol::intern("own");
    fn.return_ty = ty::unit();
    fn.locals.push_back({0, ty::str(), Symbol::intern("s")});
    fn.locals.push_back({1, ty::ref(ty::str()), Symbol::intern("r")});

    LirBasicBlock entry;
    entry.id = 0;
    entry.stmts.push_back(
        LirStmt{LirPlace::local(1), LirRvalue{LirBorrow{LirPlace::local(0)}}, {}});
    entry.stmts.push_back(
        LirStmt{
            LirDrop{0, {}}
    });
    entry.terminator = LirTerminator{LirReturn{LirOperand::move(LirPlace::local(0))}, {}};
    fn.blocks.push_back(std::move(entry));
    prog.fns.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "Borrow(_%0)"));
    assert(contains(out, "drop _%0"));
    assert(contains(out, "return move(_%0)"));
}

static void test_print_nested_projection() {
    SymbolSession session;
    LirProgram prog;
    LirFnDef fn;
    fn.name = Symbol::intern("nested");
    fn.return_ty = ty::unit();
    fn.locals.push_back({0, ty::named(Symbol::intern("Outer")), Symbol::intern("outer")});
    fn.locals.push_back({1, ty::ref(ty::str()), Symbol::intern("r")});

    LirBasicBlock entry;
    entry.id = 0;
    entry.stmts.push_back(
        LirStmt{
            LirPlace::local(1),
            LirRvalue{LirBorrow{
                LirPlace::field(0, Symbol::intern("inner")).project(Symbol::intern("s"))}},
            {}});
    entry.terminator = LirTerminator{LirReturn{std::nullopt}, {}};
    fn.blocks.push_back(std::move(entry));
    prog.fns.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "Borrow(_%0.inner.s)"));
}

static void test_print_enum_variant_rvalue() {
    SymbolSession session;
    LirProgram prog;
    LirFnDef fn;
    fn.name = Symbol::intern("get_dir");
    fn.return_ty = ty::named(Symbol::intern("Dir"));
    fn.locals.push_back({0, ty::named(Symbol::intern("Dir")), std::nullopt});

    LirBasicBlock entry;
    entry.id = 0;
    entry.stmts.push_back(
        LirStmt{
            LirPlace::local(0),
            LirRvalue{LirEnumVariantRef{Symbol::intern("Dir"), Symbol::intern("North")}},
            {}});
    entry.terminator = LirTerminator{LirReturn{LirOperand::copy(LirPlace::local(0))}, {}};
    fn.blocks.push_back(std::move(entry));
    prog.fns.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "EnumVariant(Dir::North)"));
}

static void test_print_unreachable() {
    SymbolSession session;
    LirProgram prog;
    LirFnDef fn;
    fn.name = Symbol::intern("diverge");
    fn.return_ty = ty::never();
    fn.locals.push_back({0, ty::unit(), std::nullopt});

    LirBasicBlock entry;
    entry.id = 0;
    entry.terminator = LirTerminator{LirUnreachable{}, {}};
    fn.blocks.push_back(std::move(entry));
    prog.fns.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "unreachable"));
}

int main() {
    test_print_empty_program();

    test_print_zst_struct();
    test_print_struct_with_fields();

    test_print_enum();
    test_print_enum_with_discriminants();

    test_print_empty_fn();
    test_print_fn_with_params();
    test_print_runtime_items();
    test_print_fn_locals_table();
    test_print_switch_bool();
    test_print_call_rvalue();
    test_print_borrow_move_and_drop();
    test_print_nested_projection();
    test_print_enum_variant_rvalue();
    test_print_unreachable();

    return 0;
}
