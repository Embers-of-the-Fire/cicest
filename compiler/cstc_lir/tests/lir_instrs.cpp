/// @file lir_instrs.cpp
/// @brief Tests for LIR statement and terminator construction.

#include <cassert>
#include <optional>

#include <cstc_lir/lir.hpp>
#include <cstc_symbol/symbol.hpp>

using namespace cstc::lir;
using namespace cstc::symbol;
using namespace cstc::tyir;

static const LirAssign& as_assign(const LirStmt& stmt) { return std::get<LirAssign>(stmt.node); }

// ─── LirAssign (LirStmt) ──────────────────────────────────────────────────────

static void test_assign_use() {
    SymbolSession session;
    const LirPlace dest = LirPlace::local(0);
    const LirRvalue rhs{LirUse{LirOperand::from_const(LirConst::num(Symbol::intern("7")))}};
    const LirStmt stmt{dest, rhs, {}};
    const auto& assign = as_assign(stmt);
    assert(assign.dest == dest);
    assert(std::holds_alternative<LirUse>(assign.rhs.node));
}

static void test_assign_binary() {
    SymbolSession session;
    const LirPlace dest = LirPlace::local(2);
    const LirOperand lhs = LirOperand::copy(LirPlace::local(0));
    const LirOperand rhs_op = LirOperand::copy(LirPlace::local(1));
    const LirRvalue rhs{
        LirBinaryOp{cstc::ast::BinaryOp::Mul, lhs, rhs_op}
    };
    const LirStmt stmt{dest, rhs, {}};
    const auto& assign = as_assign(stmt);
    assert(assign.dest == LirPlace::local(2));
    const auto& bin = std::get<LirBinaryOp>(assign.rhs.node);
    assert(bin.op == cstc::ast::BinaryOp::Mul);
}

static void test_assign_call_no_args() {
    SymbolSession session;
    const Symbol fn = Symbol::intern("noop");
    const LirPlace dest = LirPlace::local(0);
    const LirRvalue rhs{
        LirCall{fn, {}}
    };
    const LirStmt stmt{dest, rhs, {}};
    const auto& call = std::get<LirCall>(as_assign(stmt).rhs.node);
    assert(call.fn_name == fn);
    assert(call.args.empty());
}

static void test_assign_call_with_args() {
    SymbolSession session;
    const Symbol fn = Symbol::intern("add");
    const LirOperand a0 = LirOperand::copy(LirPlace::local(0));
    const LirOperand a1 = LirOperand::copy(LirPlace::local(1));
    const LirRvalue rhs{
        LirCall{fn, {a0, a1}}
    };
    const LirStmt stmt{LirPlace::local(2), rhs, {}};
    const auto& call = std::get<LirCall>(as_assign(stmt).rhs.node);
    assert(call.args.size() == 2);
}

static void test_assign_unary_negate() {
    SymbolSession session;
    const LirOperand operand = LirOperand::copy(LirPlace::local(0));
    const LirRvalue rhs{
        LirUnaryOp{cstc::ast::UnaryOp::Negate, operand}
    };
    const LirStmt stmt{LirPlace::local(1), rhs, {}};
    const auto& unop = std::get<LirUnaryOp>(as_assign(stmt).rhs.node);
    assert(unop.op == cstc::ast::UnaryOp::Negate);
}

static void test_assign_borrow() {
    SymbolSession session;
    const LirStmt stmt{LirPlace::local(1), LirRvalue{LirBorrow{LirPlace::local(0)}}, {}};
    const auto& borrow = std::get<LirBorrow>(as_assign(stmt).rhs.node);
    assert(borrow.place == LirPlace::local(0));
}

static void test_assign_struct_init_empty_fields() {
    SymbolSession session;
    const Symbol type_name = Symbol::intern("Marker");
    LirStructInit si;
    si.type_name = type_name;
    const LirStmt stmt{LirPlace::local(0), LirRvalue{std::move(si)}, {}};
    const auto& init = std::get<LirStructInit>(as_assign(stmt).rhs.node);
    assert(init.type_name == type_name);
    assert(init.fields.empty());
}

static void test_assign_struct_init_two_fields() {
    SymbolSession session;
    const Symbol type_name = Symbol::intern("Point");
    const Symbol fx = Symbol::intern("x");
    const Symbol fy = Symbol::intern("y");
    LirStructInit si;
    si.type_name = type_name;
    si.fields.push_back({fx, LirOperand::from_const(LirConst::num(Symbol::intern("1")))});
    si.fields.push_back({fy, LirOperand::from_const(LirConst::num(Symbol::intern("2")))});
    const LirStmt stmt{LirPlace::local(2), LirRvalue{std::move(si)}, {}};
    const auto& init = std::get<LirStructInit>(as_assign(stmt).rhs.node);
    assert(init.fields.size() == 2);
    assert(init.fields[0].name == fx);
    assert(init.fields[1].name == fy);
}

static void test_assign_field_dest() {
    SymbolSession session;
    const Symbol field = Symbol::intern("x");
    const LirPlace dest = LirPlace::field(0, field);
    const LirRvalue rhs{LirUse{LirOperand::from_const(LirConst::num(Symbol::intern("5")))}};
    const LirStmt stmt{dest, rhs, {}};
    const auto& assign = as_assign(stmt);
    assert(assign.dest.kind == LirPlace::Kind::Field);
    assert(assign.dest.field_name == field);
}

static void test_drop_stmt() {
    SymbolSession session;
    const LirStmt stmt{
        LirDrop{3, {}}
    };
    const auto& drop = std::get<LirDrop>(stmt.node);
    assert(drop.local == 3);
}

// ─── LirTerminator ────────────────────────────────────────────────────────────

static void test_terminator_return_void() {
    SymbolSession session;
    const LirTerminator term{LirReturn{std::nullopt}, {}};
    const auto& ret = std::get<LirReturn>(term.node);
    assert(!ret.value.has_value());
}

static void test_terminator_return_value() {
    SymbolSession session;
    const LirOperand val = LirOperand::from_const(LirConst::num(Symbol::intern("42")));
    const LirTerminator term{LirReturn{val}, {}};
    const auto& ret = std::get<LirReturn>(term.node);
    assert(ret.value.has_value());
    assert(*ret.value == val);
}

static void test_terminator_jump() {
    SymbolSession session;
    const LirTerminator term{LirJump{3}, {}};
    const auto& jump = std::get<LirJump>(term.node);
    assert(jump.target == 3);
}

static void test_terminator_switch_bool() {
    SymbolSession session;
    const LirOperand cond = LirOperand::copy(LirPlace::local(0));
    const LirTerminator term{
        LirSwitchBool{cond, 1, 2},
        {}
    };
    const auto& sw = std::get<LirSwitchBool>(term.node);
    assert(sw.true_target == 1);
    assert(sw.false_target == 2);
    assert(sw.condition == cond);
}

static void test_terminator_unreachable() {
    SymbolSession session;
    const LirTerminator term{LirUnreachable{}, {}};
    assert(std::holds_alternative<LirUnreachable>(term.node));
}

// ─── All binary op variants ───────────────────────────────────────────────────

static void test_all_binary_ops() {
    SymbolSession session;
    const LirOperand lhs = LirOperand::from_const(LirConst::num(Symbol::intern("1")));
    const LirOperand rhs = LirOperand::from_const(LirConst::num(Symbol::intern("2")));

    using Op = cstc::ast::BinaryOp;
    const Op ops[] = {Op::Add, Op::Sub, Op::Mul, Op::Div, Op::Mod, Op::Eq, Op::Ne,
                      Op::Lt,  Op::Le,  Op::Gt,  Op::Ge,  Op::And, Op::Or};
    for (const Op op : ops) {
        const LirRvalue rv{
            LirBinaryOp{op, lhs, rhs}
        };
        assert(std::get<LirBinaryOp>(rv.node).op == op);
    }
}

int main() {
    test_assign_use();
    test_assign_binary();
    test_assign_call_no_args();
    test_assign_call_with_args();
    test_assign_unary_negate();
    test_assign_borrow();
    test_assign_struct_init_empty_fields();
    test_assign_struct_init_two_fields();
    test_assign_field_dest();
    test_drop_stmt();

    test_terminator_return_void();
    test_terminator_return_value();
    test_terminator_jump();
    test_terminator_switch_bool();
    test_terminator_unreachable();

    test_all_binary_ops();

    return 0;
}
