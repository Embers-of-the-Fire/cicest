#include <cassert>
#include <memory>

#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/stats.hpp>
#include <cstc_tyir/tyir.hpp>

using namespace cstc::tyir;
using cstc::symbol::Symbol;

namespace {

void test_count_empty_program() {
    const TyProgram program;
    const ProgramNodeStats stats = count_program_nodes(program);
    assert(stats.total_nodes == 0);
    assert(stats.residual_calls == 0);
}

void test_count_fn_body_nodes_and_residual_calls() {
    TyFnDecl fn;
    fn.name = Symbol::intern("main");

    auto block = std::make_shared<TyBlock>();
    block->ty = ty::num();

    // let x = 1;
    auto literal = make_ty_expr({}, TyLiteral{}, ty::num());
    block->stmts.push_back(
        TyLetStmt{false, Symbol::intern("x"), ty::num(), literal, {}});

    // poll(); — a residualized runtime-barrier call
    TyCall barrier_call{Symbol::intern("poll"), {}, {}};
    barrier_call.residue = CallResidue::RuntimeBarrier;
    auto call_expr = make_ty_expr({}, std::move(barrier_call), ty::unit());
    block->stmts.push_back(TyExprStmt{call_expr, {}});

    // id(1); — a CT-eligible call with one argument
    TyCall eligible_call{Symbol::intern("id"), {}, {literal}};
    eligible_call.residue = CallResidue::CtEligible;
    auto eligible_expr = make_ty_expr({}, std::move(eligible_call), ty::num());
    block->stmts.push_back(TyExprStmt{eligible_expr, {}});

    // tail: x
    block->tail = make_ty_expr({}, LocalRef{Symbol::intern("x")}, ty::num());

    fn.body = block;

    TyProgram program;
    program.items.push_back(std::move(fn));

    const ProgramNodeStats stats = count_program_nodes(program);
    // block + let stmt + literal + expr stmt + barrier call + expr stmt +
    // eligible call + its literal argument + tail local ref
    assert(stats.total_nodes == 9);
    assert(stats.residual_calls == 1);
}

void test_count_runtime_block_and_nested_calls() {
    TyFnDecl fn;
    fn.name = Symbol::intern("main");

    auto inner = std::make_shared<TyBlock>();
    inner->ty = ty::num();
    TyCall barrier_call{Symbol::intern("poll"), {}, {}};
    barrier_call.residue = CallResidue::RuntimeBarrier;
    inner->tail = make_ty_expr({}, std::move(barrier_call), ty::num(true));

    auto runtime_block =
        make_ty_expr({}, TyRuntimeBlock{inner}, ty::num(true));
    auto outer = std::make_shared<TyBlock>();
    outer->ty = ty::num(true);
    outer->tail = runtime_block;
    fn.body = outer;

    TyProgram program;
    program.items.push_back(std::move(fn));

    const ProgramNodeStats stats = count_program_nodes(program);
    // outer block + runtime block expr + inner block + barrier call
    assert(stats.total_nodes == 4);
    assert(stats.residual_calls == 1);
}

} // namespace

int main() {
    const cstc::symbol::SymbolSession session;
    test_count_empty_program();
    test_count_fn_body_nodes_and_residual_calls();
    test_count_runtime_block_and_nested_calls();
    return 0;
}
