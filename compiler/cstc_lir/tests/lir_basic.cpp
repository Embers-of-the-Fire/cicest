/// @file lir_basic.cpp
/// @brief Basic construction tests for LIR nodes.
///
/// Tests that all core LIR node types can be constructed and their fields
/// hold the expected values.

#include <cassert>
#include <cstdint>
#include <optional>

#include <cstc_lir/lir.hpp>
#include <cstc_symbol/symbol.hpp>

using namespace cstc::lir;
using namespace cstc::symbol;
using namespace cstc::tyir;

// ─── LirConst ─────────────────────────────────────────────────────────────────

static void test_const_num() {
    SymbolSession session;
    const Symbol s = Symbol::intern("42");
    const LirConst c = LirConst::num(s);
    assert(c.kind == LirConst::Kind::Num);
    assert(c.symbol == s);
    assert(c.ty() == ty::num());
    assert(c.display() == "42");
}

static void test_const_str() {
    SymbolSession session;
    const Symbol s = Symbol::intern("hello");
    const LirConst c = LirConst::str(s);
    assert(c.kind == LirConst::Kind::Str);
    assert(c.symbol == s);
    assert(c.ty() == ty::ref(ty::str()));
    assert(c.display() == "\"hello\"");
}

static void test_const_owned_str() {
    SymbolSession session;
    const Symbol s = Symbol::intern("\"hello\"");
    const LirConst c = LirConst::owned_str(s);
    assert(c.kind == LirConst::Kind::OwnedStr);
    assert(c.symbol == s);
    assert(c.ty() == ty::str());
    assert(c.display() == "owned \"hello\"");
}

static void test_const_bool_true() {
    SymbolSession session;
    const LirConst c = LirConst::bool_(true);
    assert(c.kind == LirConst::Kind::Bool);
    assert(c.bool_value == true);
    assert(c.ty() == ty::bool_());
    assert(c.display() == "true");
}

static void test_const_bool_false() {
    SymbolSession session;
    const LirConst c = LirConst::bool_(false);
    assert(c.kind == LirConst::Kind::Bool);
    assert(c.bool_value == false);
    assert(c.display() == "false");
}

static void test_const_unit() {
    SymbolSession session;
    const LirConst c = LirConst::unit();
    assert(c.kind == LirConst::Kind::Unit);
    assert(c.ty() == ty::unit());
    assert(c.display() == "()");
}

static void test_const_equality() {
    SymbolSession session;
    const Symbol s42 = Symbol::intern("42");
    assert(LirConst::num(s42) == LirConst::num(s42));
    assert(!(LirConst::num(s42) == LirConst::bool_(true)));
    assert(LirConst::unit() == LirConst::unit());
    assert(LirConst::bool_(true) == LirConst::bool_(true));
    assert(!(LirConst::bool_(true) == LirConst::bool_(false)));
}

// ─── LirPlace ─────────────────────────────────────────────────────────────────

static void test_place_local() {
    SymbolSession session;
    const LirPlace p = LirPlace::local(3);
    assert(p.kind == LirPlace::Kind::Local);
    assert(p.local_id == 3);
    assert(p.field_path.empty());
}

static void test_place_field() {
    SymbolSession session;
    const Symbol x = Symbol::intern("x");
    const LirPlace p = LirPlace::field(0, x);
    assert(p.kind == LirPlace::Kind::Field);
    assert(p.local_id == 0);
    assert(p.field_path.size() == 1);
    assert(p.field_path[0] == x);
}

static void test_place_nested_field() {
    SymbolSession session;
    const Symbol inner = Symbol::intern("inner");
    const Symbol value = Symbol::intern("value");
    const LirPlace p = LirPlace::field(0, inner).project(value);
    assert(p.kind == LirPlace::Kind::Field);
    assert(p.local_id == 0);
    assert(p.field_path.size() == 2);
    assert(p.field_path[0] == inner);
    assert(p.field_path[1] == value);
}

static void test_place_equality() {
    SymbolSession session;
    const Symbol f = Symbol::intern("foo");
    const Symbol g = Symbol::intern("bar");
    assert(LirPlace::local(1) == LirPlace::local(1));
    assert(!(LirPlace::local(1) == LirPlace::local(2)));
    assert(LirPlace::field(0, f) == LirPlace::field(0, f));
    assert(LirPlace::field(0, f).project(g) == LirPlace::field(0, f).project(g));
    assert(!(LirPlace::field(0, f) == LirPlace::local(0)));
}

// ─── LirOperand ───────────────────────────────────────────────────────────────

static void test_operand_copy() {
    SymbolSession session;
    const LirPlace p = LirPlace::local(5);
    const LirOperand op = LirOperand::copy(p);
    assert(op.kind == LirOperand::Kind::Copy);
    assert(op.place == p);
}

static void test_operand_move() {
    SymbolSession session;
    const LirPlace p = LirPlace::local(7);
    const LirOperand op = LirOperand::move(p);
    assert(op.kind == LirOperand::Kind::Move);
    assert(op.place == p);
}

static void test_operand_const() {
    SymbolSession session;
    const LirConst c = LirConst::bool_(false);
    const LirOperand op = LirOperand::from_const(c);
    assert(op.kind == LirOperand::Kind::Const);
    assert(op.constant == c);
}

static void test_operand_equality() {
    SymbolSession session;
    const LirOperand a = LirOperand::copy(LirPlace::local(0));
    const LirOperand b = LirOperand::copy(LirPlace::local(0));
    const LirOperand c = LirOperand::from_const(LirConst::unit());
    assert(a == b);
    assert(!(a == c));
}

// ─── LirRvalue ────────────────────────────────────────────────────────────────

static void test_rvalue_use() {
    SymbolSession session;
    const LirRvalue rv{LirUse{LirOperand::from_const(LirConst::unit())}};
    assert(std::holds_alternative<LirUse>(rv.node));
}

static void test_rvalue_borrow() {
    SymbolSession session;
    const LirRvalue rv{LirBorrow{LirPlace::local(1)}};
    assert(std::holds_alternative<LirBorrow>(rv.node));
    assert(std::get<LirBorrow>(rv.node).place == LirPlace::local(1));
}

static void test_rvalue_binary_op() {
    SymbolSession session;
    const LirOperand lhs = LirOperand::from_const(LirConst::num(Symbol::intern("1")));
    const LirOperand rhs = LirOperand::from_const(LirConst::num(Symbol::intern("2")));
    const LirRvalue rv{
        LirBinaryOp{cstc::ast::BinaryOp::Add, lhs, rhs}
    };
    assert(std::holds_alternative<LirBinaryOp>(rv.node));
    const auto& binop = std::get<LirBinaryOp>(rv.node);
    assert(binop.op == cstc::ast::BinaryOp::Add);
}

static void test_rvalue_unary_op() {
    SymbolSession session;
    const LirOperand operand = LirOperand::from_const(LirConst::bool_(true));
    const LirRvalue rv{
        LirUnaryOp{cstc::ast::UnaryOp::Not, operand}
    };
    assert(std::holds_alternative<LirUnaryOp>(rv.node));
    const auto& unop = std::get<LirUnaryOp>(rv.node);
    assert(unop.op == cstc::ast::UnaryOp::Not);
}

static void test_rvalue_call() {
    SymbolSession session;
    const Symbol fn = Symbol::intern("add");
    const LirRvalue rv{
        LirCall{fn, {}}
    };
    assert(std::holds_alternative<LirCall>(rv.node));
    assert(std::get<LirCall>(rv.node).fn_name == fn);
}

static void test_rvalue_struct_init() {
    SymbolSession session;
    const Symbol type_name = Symbol::intern("Point");
    const Symbol field_x = Symbol::intern("x");
    LirStructInit si;
    si.type_name = type_name;
    si.fields.push_back({field_x, LirOperand::from_const(LirConst::num(Symbol::intern("1")))});
    const LirRvalue rv{std::move(si)};
    assert(std::holds_alternative<LirStructInit>(rv.node));
}

static void test_rvalue_enum_variant() {
    SymbolSession session;
    const Symbol en = Symbol::intern("Dir");
    const Symbol var = Symbol::intern("North");
    const LirRvalue rv{
        LirEnumVariantRef{en, var}
    };
    assert(std::holds_alternative<LirEnumVariantRef>(rv.node));
    const auto& evr = std::get<LirEnumVariantRef>(rv.node);
    assert(evr.enum_name == en);
    assert(evr.variant_name == var);
}

// ─── LirLocalDecl ─────────────────────────────────────────────────────────────

static void test_local_decl() {
    SymbolSession session;
    const Symbol name = Symbol::intern("x");
    LirLocalDecl loc;
    loc.id = 0;
    loc.ty = ty::num();
    loc.debug_name = name;
    assert(loc.id == 0);
    assert(loc.ty == ty::num());
    assert(loc.debug_name.has_value());
    assert(*loc.debug_name == name);
}

static void test_local_decl_no_debug_name() {
    SymbolSession session;
    LirLocalDecl loc;
    loc.id = 1;
    loc.ty = ty::bool_();
    assert(!loc.debug_name.has_value());
}

// ─── LirProgram empty ─────────────────────────────────────────────────────────

static void test_empty_program() {
    SymbolSession session;
    LirProgram prog;
    assert(prog.structs.empty());
    assert(prog.enums.empty());
    assert(prog.fns.empty());
}

int main() {
    test_const_num();
    test_const_str();
    test_const_owned_str();
    test_const_bool_true();
    test_const_bool_false();
    test_const_unit();
    test_const_equality();

    test_place_local();
    test_place_field();
    test_place_nested_field();
    test_place_equality();

    test_operand_copy();
    test_operand_move();
    test_operand_const();
    test_operand_equality();

    test_rvalue_use();
    test_rvalue_borrow();
    test_rvalue_binary_op();
    test_rvalue_unary_op();
    test_rvalue_call();
    test_rvalue_struct_init();
    test_rvalue_enum_variant();

    test_local_decl();
    test_local_decl_no_debug_name();

    test_empty_program();

    return 0;
}
