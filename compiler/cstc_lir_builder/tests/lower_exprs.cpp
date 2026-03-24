/// @file lower_exprs.cpp
/// @brief Tests for LIR lowering of individual expression forms.

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

static const LirFnDef& first_fn(const LirProgram& prog) {
    assert(!prog.fns.empty());
    return prog.fns[0];
}

static bool output_contains(const LirProgram& prog, const std::string& needle) {
    return format_program(prog).find(needle) != std::string::npos;
}

static LirLocalId find_named_local(const LirFnDef& fn, const char* name) {
    const Symbol symbol = Symbol::intern(name);
    for (const LirLocalDecl& local : fn.locals) {
        if (local.debug_name != symbol)
            continue;
        return local.id;
    }
    return kInvalidLocal;
}

static LirLocalId find_local_assigned_borrow_of_place(const LirFnDef& fn, const LirPlace& place) {
    for (const LirBasicBlock& block : fn.blocks) {
        for (const LirStmt& stmt : block.stmts) {
            const auto* assign = std::get_if<LirAssign>(&stmt.node);
            if (assign == nullptr)
                continue;
            const auto* borrow = std::get_if<LirBorrow>(&assign->rhs.node);
            if (borrow == nullptr)
                continue;
            if (borrow->place != place)
                continue;
            if (assign->dest.kind != LirPlace::Kind::Local)
                continue;
            return assign->dest.local_id;
        }
    }
    return kInvalidLocal;
}

static bool has_local_assign_from_use_operand(
    const LirFnDef& fn, LirLocalId dest_local, const LirOperand& operand) {
    for (const LirBasicBlock& block : fn.blocks) {
        for (const LirStmt& stmt : block.stmts) {
            const auto* assign = std::get_if<LirAssign>(&stmt.node);
            if (assign == nullptr)
                continue;
            if (assign->dest != LirPlace::local(dest_local))
                continue;
            const auto* use = std::get_if<LirUse>(&assign->rhs.node);
            if (use == nullptr)
                continue;
            if (use->operand == operand)
                return true;
        }
    }
    return false;
}

static LirLocalId
    find_local_assigned_from_use_operand(const LirFnDef& fn, const LirOperand& operand) {
    for (const LirBasicBlock& block : fn.blocks) {
        for (const LirStmt& stmt : block.stmts) {
            const auto* assign = std::get_if<LirAssign>(&stmt.node);
            if (assign == nullptr)
                continue;
            if (assign->dest.kind != LirPlace::Kind::Local)
                continue;
            const auto* use = std::get_if<LirUse>(&assign->rhs.node);
            if (use == nullptr)
                continue;
            if (use->operand == operand)
                return assign->dest.local_id;
        }
    }
    return kInvalidLocal;
}

static std::size_t count_drop_stmts_for_local(const LirFnDef& fn, LirLocalId local) {
    std::size_t count = 0;
    for (const LirBasicBlock& block : fn.blocks) {
        for (const LirStmt& stmt : block.stmts) {
            const auto* drop = std::get_if<LirDrop>(&stmt.node);
            if (drop == nullptr)
                continue;
            if (drop->local != local)
                continue;
            ++count;
        }
    }
    return count;
}

static std::size_t count_move_uses_of_local(const LirFnDef& fn, LirLocalId local) {
    std::size_t count = 0;
    for (const LirBasicBlock& block : fn.blocks) {
        for (const LirStmt& stmt : block.stmts) {
            const auto* assign = std::get_if<LirAssign>(&stmt.node);
            if (assign == nullptr)
                continue;
            const auto* use = std::get_if<LirUse>(&assign->rhs.node);
            if (use == nullptr)
                continue;
            if (use->operand.kind == LirOperand::Kind::Move)
                if (use->operand.place == LirPlace::local(local))
                    ++count;
        }
    }
    return count;
}

// ─── Numeric literal ──────────────────────────────────────────────────────────

static void test_num_literal_return() {
    // A function returning a literal constant should have a `return 42`
    // terminator, either directly or through a materialized result local.
    const LirProgram prog = must_lower("fn f() -> num { 42 }");
    const LirFnDef& fn = first_fn(prog);
    const auto& ret = std::get<LirReturn>(fn.blocks[0].terminator.node);
    assert(ret.value.has_value());
    const LirOperand& op = *ret.value;
    assert(op.kind == LirOperand::Kind::Const || op.kind == LirOperand::Kind::Copy);
    assert(output_contains(prog, "42"));
}

// ─── String literal ───────────────────────────────────────────────────────────

static void test_str_literal_return() {
    const LirProgram prog = must_lower("fn f() { let s: &str = \"hello\"; }");
    const LirFnDef& fn = first_fn(prog);
    assert(!fn.blocks[0].stmts.empty());
    const auto& assign = std::get<LirAssign>(fn.blocks[0].stmts[0].node);
    const auto& use = std::get<LirUse>(assign.rhs.node);
    assert(use.operand.kind == LirOperand::Kind::Const);
    assert(use.operand.constant.kind == LirConst::Kind::Str);
    // The lexer stores the string token including its surrounding quotes.
    assert(use.operand.constant.symbol == Symbol::intern("\"hello\""));
}

// ─── Bool literal ─────────────────────────────────────────────────────────────

static void test_bool_literal_true() {
    const LirProgram prog = must_lower("fn f() -> bool { true }");
    const LirFnDef& fn = first_fn(prog);
    const auto& ret = std::get<LirReturn>(fn.blocks[0].terminator.node);
    assert(ret.value.has_value());
    assert(ret.value->kind == LirOperand::Kind::Const || ret.value->kind == LirOperand::Kind::Copy);
    assert(output_contains(prog, "true"));
}

static void test_bool_literal_false() {
    const LirProgram prog = must_lower("fn f() -> bool { false }");
    const LirFnDef& fn = first_fn(prog);
    const auto& ret = std::get<LirReturn>(fn.blocks[0].terminator.node);
    assert(ret.value.has_value());
    assert(ret.value->kind == LirOperand::Kind::Const || ret.value->kind == LirOperand::Kind::Copy);
    assert(output_contains(prog, "false"));
}

// ─── Local reference ──────────────────────────────────────────────────────────

static void test_local_ref_param() {
    // fn id(x: num) -> num { x }
    // The return value should preserve the parameter value, either directly or
    // through a materialized result local.
    const LirProgram prog = must_lower("fn id(x: num) -> num { x }");
    const LirFnDef& fn = first_fn(prog);
    assert(fn.params.size() == 1);
    const LirLocalId param_id = fn.params[0].local;
    const auto& ret = std::get<LirReturn>(fn.blocks[0].terminator.node);
    assert(ret.value.has_value());
    assert(ret.value->kind == LirOperand::Kind::Copy);
    if (ret.value->place != LirPlace::local(param_id))
        assert(output_contains(prog, "copy(_%" + std::to_string(param_id) + ")"));
}

// ─── Binary operations ────────────────────────────────────────────────────────

static void test_binary_add() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> num { a + b }");
    assert(output_contains(prog, "BinOp(+"));
}

static void test_binary_sub() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> num { a - b }");
    assert(output_contains(prog, "BinOp(-"));
}

static void test_binary_mul() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> num { a * b }");
    assert(output_contains(prog, "BinOp(*"));
}

static void test_binary_div() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> num { a / b }");
    assert(output_contains(prog, "BinOp(/"));
}

static void test_binary_mod() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> num { a % b }");
    assert(output_contains(prog, "BinOp(%"));
}

static void test_binary_eq() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> bool { a == b }");
    assert(output_contains(prog, "BinOp(=="));
}

static void test_binary_ne() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> bool { a != b }");
    assert(output_contains(prog, "BinOp(!="));
}

static void test_binary_lt() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> bool { a < b }");
    assert(output_contains(prog, "BinOp(<"));
}

static void test_binary_le() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> bool { a <= b }");
    assert(output_contains(prog, "BinOp(<="));
}

static void test_binary_gt() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> bool { a > b }");
    assert(output_contains(prog, "BinOp(>"));
}

static void test_binary_ge() {
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> bool { a >= b }");
    assert(output_contains(prog, "BinOp(>="));
}

static void test_binary_and() {
    const LirProgram prog = must_lower("fn f(a: bool, b: bool) -> bool { a && b }");
    assert(output_contains(prog, "BinOp(&&"));
}

static void test_binary_or() {
    const LirProgram prog = must_lower("fn f(a: bool, b: bool) -> bool { a || b }");
    assert(output_contains(prog, "BinOp(||"));
}

// ─── Unary operations ─────────────────────────────────────────────────────────

static void test_unary_negate() {
    const LirProgram prog = must_lower("fn f(x: num) -> num { -x }");
    assert(output_contains(prog, "UnaryOp(-"));
}

static void test_unary_not() {
    const LirProgram prog = must_lower("fn f(b: bool) -> bool { !b }");
    assert(output_contains(prog, "UnaryOp(!"));
}

// ─── Let statement ────────────────────────────────────────────────────────────

static void test_let_stmt() {
    // After lowering, x is allocated a local; the tail returns it.
    const LirProgram prog = must_lower("fn f() -> num { let x = 10; x }");
    const LirFnDef& fn = first_fn(prog);
    // There must be at least one assignment statement in the entry block.
    assert(!fn.blocks[0].stmts.empty());
    assert(output_contains(prog, "10"));
}

static void test_let_discard() {
    // `let _ = expr;` — value computed but not bound to a name.
    const LirProgram prog = must_lower("fn f() { let _ = 42; }");
    // The expression is still emitted (42 is a const, so no statement).
    // There should be no crash.
    assert(!prog.fns.empty());
}

// ─── Function call ────────────────────────────────────────────────────────────

static void test_fn_call() {
    const LirProgram prog = must_lower(
        "fn square(x: num) -> num { x * x }"
        "fn double(x: num) -> num { square(x) + square(x) }");
    assert(prog.fns.size() == 2);
    assert(output_contains(prog, "Call(square"));
}

// ─── Struct initialization ────────────────────────────────────────────────────

static void test_struct_init() {
    const LirProgram prog = must_lower(
        "struct Point { x: num, y: num }"
        "fn make_point() -> Point { Point { x: 1, y: 2 } }");
    assert(output_contains(prog, "StructInit(Point"));
    assert(output_contains(prog, "x: 1"));
    assert(output_contains(prog, "y: 2"));
}

// ─── Field access ─────────────────────────────────────────────────────────────

static void test_field_access() {
    const LirProgram prog = must_lower(
        "struct Point { x: num, y: num }"
        "fn get_x(p: Point) -> num { p.x }");
    // Field access should produce a copy of the field place.
    assert(output_contains(prog, ".x"));
}

static void test_nested_field_access_reuses_projected_place() {
    const LirProgram prog = must_lower(
        "struct Inner { x: num, s: str }"
        "struct Outer { inner: Inner }"
        "fn get_x(o: Outer) -> num { o.inner.x }");
    assert(output_contains(prog, "copy(_%0.inner.x)"));
    assert(!output_contains(prog, "copy(_%0.inner)"));
}

static void test_nested_borrow_reuses_projected_place() {
    const LirProgram prog = must_lower(
        "struct Inner { s: str }"
        "struct Outer { inner: Inner }"
        "fn borrow_s(o: Outer) { let r: &str = &o.inner.s; }");
    assert(output_contains(prog, "Borrow(_%0.inner.s)"));
    assert(!output_contains(prog, "copy(_%0.inner)"));
}

// ─── Enum variant reference ───────────────────────────────────────────────────

static void test_enum_variant_ref() {
    const LirProgram prog = must_lower(
        "enum Dir { North, South }"
        "fn get_north() -> Dir { Dir::North }");
    assert(output_contains(prog, "EnumVariant(Dir::North)"));
}

// ─── Ownership lowering ──────────────────────────────────────────────────────

static void test_borrow_lowers_to_explicit_borrow() {
    const LirProgram prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn f() { let s: str = to_str(1); let r: &str = &s; }");
    const LirFnDef& fn = first_fn(prog);
    const LirLocalId s_local = find_named_local(fn, "s");
    const LirLocalId r_local = find_named_local(fn, "r");
    assert(s_local != kInvalidLocal);
    assert(r_local != kInvalidLocal);

    const LirLocalId borrow_local =
        find_local_assigned_borrow_of_place(fn, LirPlace::local(s_local));
    assert(borrow_local != kInvalidLocal);
    assert(borrow_local != r_local);
    assert(has_local_assign_from_use_operand(
        fn, r_local, LirOperand::copy(LirPlace::local(borrow_local))));
}

static void test_scope_exit_inserts_drop_for_owned_local() {
    const LirProgram prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn f() { let s: str = to_str(1); }");
    const LirFnDef& fn = first_fn(prog);
    const LirLocalId s_local = find_named_local(fn, "s");
    assert(s_local != kInvalidLocal);
    assert(count_drop_stmts_for_local(fn, s_local) == 1);
}

static void test_move_only_flow_uses_move_operands() {
    const LirProgram prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn f() -> str { let s: str = to_str(1); s }");
    const LirFnDef& fn = first_fn(prog);
    const LirLocalId s_local = find_named_local(fn, "s");
    assert(s_local != kInvalidLocal);
    assert(count_move_uses_of_local(fn, s_local) == 1);

    const auto& ret = std::get<LirReturn>(fn.blocks[0].terminator.node);
    assert(ret.value.has_value());
    assert(ret.value->kind == LirOperand::Kind::Move);
    assert(ret.value->place.kind == LirPlace::Kind::Local);
    assert(ret.value->place.local_id != s_local);

    const LirLocalId moved_from_s_local =
        find_local_assigned_from_use_operand(fn, LirOperand::move(LirPlace::local(s_local)));
    assert(moved_from_s_local != kInvalidLocal);
    assert(moved_from_s_local != s_local);
    assert(has_local_assign_from_use_operand(
        fn, ret.value->place.local_id, LirOperand::move(LirPlace::local(moved_from_s_local))));
}

// ─── Nested binary expressions ────────────────────────────────────────────────

static void test_nested_binary() {
    // (a + b) * (a - b)  — needs two temporaries
    const LirProgram prog = must_lower("fn f(a: num, b: num) -> num { (a + b) * (a - b) }");
    const LirFnDef& fn = first_fn(prog);
    // Should have at least 2 temporaries (for the two sub-expressions).
    assert(fn.locals.size() >= 4); // 2 params + at least 2 temps
}

int main() {
    SymbolSession session;

    test_num_literal_return();
    test_str_literal_return();
    test_bool_literal_true();
    test_bool_literal_false();
    test_local_ref_param();

    test_binary_add();
    test_binary_sub();
    test_binary_mul();
    test_binary_div();
    test_binary_mod();
    test_binary_eq();
    test_binary_ne();
    test_binary_lt();
    test_binary_le();
    test_binary_gt();
    test_binary_ge();
    test_binary_and();
    test_binary_or();

    test_unary_negate();
    test_unary_not();

    test_let_stmt();
    test_let_discard();

    test_fn_call();
    test_struct_init();
    test_field_access();
    test_nested_field_access_reuses_projected_place();
    test_nested_borrow_reuses_projected_place();
    test_enum_variant_ref();
    test_borrow_lowers_to_explicit_borrow();
    test_scope_exit_inserts_drop_for_owned_local();
    test_move_only_flow_uses_move_operands();
    test_nested_binary();

    return 0;
}
