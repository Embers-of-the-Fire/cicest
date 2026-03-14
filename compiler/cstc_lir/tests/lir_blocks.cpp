/// @file lir_blocks.cpp
/// @brief Tests for LirBasicBlock, LirFnDef, and LirProgram structure.

#include <cassert>
#include <optional>

#include <cstc_lir/lir.hpp>
#include <cstc_symbol/symbol.hpp>

using namespace cstc::lir;
using namespace cstc::symbol;
using namespace cstc::tyir;

// ─── LirBasicBlock ────────────────────────────────────────────────────────────

static void test_empty_block() {
    SymbolSession session;
    LirBasicBlock block;
    block.id = 0;
    block.terminator = LirTerminator{LirReturn{std::nullopt}, {}};
    assert(block.id == 0);
    assert(block.stmts.empty());
    assert(std::holds_alternative<LirReturn>(block.terminator.node));
}

static void test_block_with_stmts() {
    SymbolSession session;
    LirBasicBlock block;
    block.id = 1;
    block.stmts.push_back(
        LirStmt{
            LirPlace::local(0),
            LirRvalue{LirUse{LirOperand::from_const(LirConst::num(Symbol::intern("10")))}},
            {}});
    block.stmts.push_back(
        LirStmt{
            LirPlace::local(1),
            LirRvalue{LirBinaryOp{
                cstc::ast::BinaryOp::Add, LirOperand::copy(LirPlace::local(0)),
                LirOperand::from_const(LirConst::num(Symbol::intern("5")))}},
            {}});
    block.terminator = LirTerminator{LirReturn{LirOperand::copy(LirPlace::local(1))}, {}};
    assert(block.stmts.size() == 2);
    assert(std::holds_alternative<LirReturn>(block.terminator.node));
}

static void test_block_jump_terminator() {
    SymbolSession session;
    LirBasicBlock block;
    block.id = 0;
    block.terminator = LirTerminator{LirJump{1}, {}};
    const auto& jump = std::get<LirJump>(block.terminator.node);
    assert(jump.target == 1);
}

static void test_block_switch_bool_terminator() {
    SymbolSession session;
    LirBasicBlock block;
    block.id = 0;
    block.stmts.push_back(
        LirStmt{
            LirPlace::local(0),
            LirRvalue{LirUse{LirOperand::from_const(LirConst::bool_(true))}},
            {}});
    block.terminator = LirTerminator{
        LirSwitchBool{LirOperand::copy(LirPlace::local(0)), 1, 2},
        {}
    };
    assert(std::holds_alternative<LirSwitchBool>(block.terminator.node));
}

static void test_block_id_matches_position() {
    SymbolSession session;
    // Simulate: builder always sets block.id == position in fn.blocks
    for (LirBlockId id = 0; id < 5; ++id) {
        LirBasicBlock block;
        block.id = id;
        block.terminator = LirTerminator{LirUnreachable{}, {}};
        assert(block.id == id);
    }
}

// ─── LirFnDef ─────────────────────────────────────────────────────────────────

static void test_fn_def_empty_body() {
    SymbolSession session;
    LirFnDef fn;
    fn.name = Symbol::intern("noop");
    fn.return_ty = ty::unit();

    // Locals: just a return slot (convention: local 0 is the return value slot)
    fn.locals.push_back({0, ty::unit(), std::nullopt});

    // Entry block with a void return
    LirBasicBlock entry;
    entry.id = kEntryBlock;
    entry.terminator = LirTerminator{LirReturn{std::nullopt}, {}};
    fn.blocks.push_back(std::move(entry));

    assert(fn.name == Symbol::intern("noop"));
    assert(fn.params.empty());
    assert(fn.return_ty == ty::unit());
    assert(fn.locals.size() == 1);
    assert(fn.blocks.size() == 1);
    assert(fn.blocks[kEntryBlock].id == kEntryBlock);
}

static void test_fn_def_with_params() {
    SymbolSession session;
    LirFnDef fn;
    fn.name = Symbol::intern("add");
    fn.return_ty = ty::num();

    // Params: x: num (%0), y: num (%1)
    fn.locals.push_back({0, ty::num(), Symbol::intern("x")});
    fn.locals.push_back({1, ty::num(), Symbol::intern("y")});
    // Temporary for result (%2)
    fn.locals.push_back({2, ty::num(), std::nullopt});

    fn.params.push_back({0, Symbol::intern("x"), ty::num(), {}});
    fn.params.push_back({1, Symbol::intern("y"), ty::num(), {}});

    // Entry block: %2 = BinOp(+, copy(%0), copy(%1)); return copy(%2)
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

    assert(fn.params.size() == 2);
    assert(fn.params[0].name == Symbol::intern("x"));
    assert(fn.params[1].name == Symbol::intern("y"));
    assert(fn.locals.size() == 3);
    assert(fn.blocks.size() == 1);
    const auto& ret = std::get<LirReturn>(fn.blocks[0].terminator.node);
    assert(ret.value.has_value());
}

static void test_fn_def_two_blocks() {
    // Represents: fn choose(b: bool) -> num { if b { 1 } else { 0 } }
    //
    // bb0: switchBool(copy(%0)) -> [true: bb1, false: bb2]
    // bb1: %1 = 1; jump bb3
    // bb2: %1 = 0; jump bb3
    // bb3: return copy(%1)
    SymbolSession session;
    LirFnDef fn;
    fn.name = Symbol::intern("choose");
    fn.return_ty = ty::num();

    fn.locals.push_back({0, ty::bool_(), Symbol::intern("b")}); // param
    fn.locals.push_back({1, ty::num(), std::nullopt});          // result temp

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
    bb1.terminator = LirTerminator{LirJump{3}, {}};

    LirBasicBlock bb2;
    bb2.id = 2;
    bb2.stmts.push_back(
        LirStmt{
            LirPlace::local(1),
            LirRvalue{LirUse{LirOperand::from_const(LirConst::num(Symbol::intern("0")))}},
            {}});
    bb2.terminator = LirTerminator{LirJump{3}, {}};

    LirBasicBlock bb3;
    bb3.id = 3;
    bb3.terminator = LirTerminator{LirReturn{LirOperand::copy(LirPlace::local(1))}, {}};

    fn.blocks.push_back(std::move(bb0));
    fn.blocks.push_back(std::move(bb1));
    fn.blocks.push_back(std::move(bb2));
    fn.blocks.push_back(std::move(bb3));

    assert(fn.blocks.size() == 4);
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
        assert(fn.blocks[i].id == static_cast<LirBlockId>(i));
}

// ─── LirProgram with declarations ─────────────────────────────────────────────

static void test_program_with_struct() {
    SymbolSession session;
    LirProgram prog;

    LirStructDecl s;
    s.name = Symbol::intern("Point");
    s.is_zst = false;
    s.fields.push_back({Symbol::intern("x"), ty::num(), {}});
    s.fields.push_back({Symbol::intern("y"), ty::num(), {}});
    prog.structs.push_back(std::move(s));

    assert(prog.structs.size() == 1);
    assert(prog.structs[0].name == Symbol::intern("Point"));
    assert(prog.structs[0].fields.size() == 2);
}

static void test_program_with_enum() {
    SymbolSession session;
    LirProgram prog;

    LirEnumDecl e;
    e.name = Symbol::intern("Dir");
    e.variants.push_back({Symbol::intern("North"), std::nullopt, {}});
    e.variants.push_back({Symbol::intern("South"), std::nullopt, {}});
    e.variants.push_back({Symbol::intern("East"), std::nullopt, {}});
    e.variants.push_back({Symbol::intern("West"), std::nullopt, {}});
    prog.enums.push_back(std::move(e));

    assert(prog.enums.size() == 1);
    assert(prog.enums[0].variants.size() == 4);
}

static void test_program_with_zst_struct() {
    SymbolSession session;
    LirProgram prog;

    LirStructDecl s;
    s.name = Symbol::intern("Marker");
    s.is_zst = true;
    prog.structs.push_back(std::move(s));

    assert(prog.structs[0].is_zst);
    assert(prog.structs[0].fields.empty());
}

int main() {
    test_empty_block();
    test_block_with_stmts();
    test_block_jump_terminator();
    test_block_switch_bool_terminator();
    test_block_id_matches_position();

    test_fn_def_empty_body();
    test_fn_def_with_params();
    test_fn_def_two_blocks();

    test_program_with_struct();
    test_program_with_enum();
    test_program_with_zst_struct();

    return 0;
}
