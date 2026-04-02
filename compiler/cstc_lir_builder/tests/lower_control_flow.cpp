/// @file lower_control_flow.cpp
/// @brief Tests for LIR lowering of control-flow expressions:
///        if/else, loop, while, for, break, continue, return.
///
/// Note: the language uses Rust-style parenthesis-free conditions:
///   `if cond { ... }`, `while cond { ... }`, `for (init; cond; step) { ... }`

#include <cassert>
#include <optional>
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
    if (!ast.has_value()) {
        fprintf(stderr, "PARSE FAIL: %s\n", source);
        assert(false);
    }
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    if (!tyir.has_value()) {
        fprintf(stderr, "TYIR FAIL: %s\n  error: %s\n", source, tyir.error().message.c_str());
        assert(false);
    }
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

static std::size_t count_drop_stmts_for_local(const LirFnDef& fn, LirLocalId local) {
    std::size_t count = 0;
    for (const LirBasicBlock& block : fn.blocks) {
        for (const LirStmt& stmt : block.stmts) {
            const auto* drop = std::get_if<LirDrop>(&stmt.node);
            if (drop == nullptr)
                continue;
            if (drop->local == local)
                ++count;
        }
    }
    return count;
}

static std::optional<std::size_t>
    find_stmt_index_of_drop(const LirBasicBlock& block, LirLocalId local) {
    for (std::size_t index = 0; index < block.stmts.size(); ++index) {
        const auto* drop = std::get_if<LirDrop>(&block.stmts[index].node);
        if (drop == nullptr)
            continue;
        if (drop->local == local)
            return index;
    }
    return std::nullopt;
}

static std::optional<std::size_t> find_stmt_index_of_assign_from_use_operand(
    const LirBasicBlock& block, LirLocalId dest_local, const LirOperand& operand) {
    for (std::size_t index = 0; index < block.stmts.size(); ++index) {
        const auto* assign = std::get_if<LirAssign>(&block.stmts[index].node);
        if (assign == nullptr)
            continue;
        if (assign->dest != LirPlace::local(dest_local))
            continue;
        const auto* use = std::get_if<LirUse>(&assign->rhs.node);
        if (use == nullptr)
            continue;
        if (use->operand == operand)
            return index;
    }
    return std::nullopt;
}

static LirBlockId find_block_with_drop_of_local(const LirFnDef& fn, LirLocalId local) {
    for (const LirBasicBlock& block : fn.blocks) {
        if (find_stmt_index_of_drop(block, local).has_value())
            return block.id;
    }
    return kInvalidBlock;
}

static LirLocalId find_local_assigned_num(const LirFnDef& fn, const char* literal_text) {
    const Symbol literal = Symbol::intern(literal_text);
    for (const LirBasicBlock& block : fn.blocks) {
        for (const LirStmt& stmt : block.stmts) {
            const auto* assign = std::get_if<LirAssign>(&stmt.node);
            if (assign == nullptr)
                continue;
            const auto* use = std::get_if<LirUse>(&assign->rhs.node);
            if (use == nullptr)
                continue;
            if (use->operand.kind != LirOperand::Kind::Const)
                continue;
            if (use->operand.constant.kind != LirConst::Kind::Num)
                continue;
            if (use->operand.constant.symbol != literal)
                continue;
            if (assign->dest.kind != LirPlace::Kind::Local)
                continue;
            return assign->dest.local_id;
        }
    }
    return kInvalidLocal;
}

static LirLocalId find_local_assigned_bool(const LirFnDef& fn, bool value) {
    for (const LirBasicBlock& block : fn.blocks) {
        for (const LirStmt& stmt : block.stmts) {
            const auto* assign = std::get_if<LirAssign>(&stmt.node);
            if (assign == nullptr)
                continue;
            const auto* use = std::get_if<LirUse>(&assign->rhs.node);
            if (use == nullptr)
                continue;
            if (use->operand.kind != LirOperand::Kind::Const)
                continue;
            if (use->operand.constant.kind != LirConst::Kind::Bool)
                continue;
            if (use->operand.constant.bool_value != value)
                continue;
            if (assign->dest.kind != LirPlace::Kind::Local)
                continue;
            return assign->dest.local_id;
        }
    }
    return kInvalidLocal;
}

static std::size_t count_unit_const_assignments(const LirFnDef& fn) {
    std::size_t count = 0;
    for (const LirBasicBlock& block : fn.blocks) {
        for (const LirStmt& stmt : block.stmts) {
            const auto* assign = std::get_if<LirAssign>(&stmt.node);
            if (assign == nullptr)
                continue;
            const auto* use = std::get_if<LirUse>(&assign->rhs.node);
            if (use == nullptr)
                continue;
            if (use->operand.kind != LirOperand::Kind::Const)
                continue;
            if (use->operand.constant.kind == LirConst::Kind::Unit)
                ++count;
        }
    }
    return count;
}

static std::size_t count_num_literal_returns(const LirFnDef& fn, const char* literal_text) {
    const Symbol literal = Symbol::intern(literal_text);
    std::size_t count = 0;
    for (const LirBasicBlock& block : fn.blocks) {
        const auto* ret = std::get_if<LirReturn>(&block.terminator.node);
        if (ret == nullptr || !ret->value.has_value())
            continue;
        if (ret->value->kind != LirOperand::Kind::Const)
            continue;
        if (ret->value->constant.kind != LirConst::Kind::Num)
            continue;
        if (ret->value->constant.symbol == literal)
            ++count;
    }
    return count;
}

static std::size_t count_return_terminators(const LirFnDef& fn) {
    std::size_t count = 0;
    for (const LirBasicBlock& block : fn.blocks) {
        if (std::holds_alternative<LirReturn>(block.terminator.node))
            ++count;
    }
    return count;
}

static std::size_t count_unreachable_terminators(const LirFnDef& fn) {
    std::size_t count = 0;
    for (const LirBasicBlock& block : fn.blocks) {
        if (std::holds_alternative<LirUnreachable>(block.terminator.node))
            ++count;
    }
    return count;
}

// ─── If (no else) ─────────────────────────────────────────────────────────────

static void test_if_no_else() {
    // if (cond) { }  →  SwitchBool + two blocks + merge
    const LirProgram prog = must_lower("fn f(b: bool) { if b { } }");
    const LirFnDef& fn = first_fn(prog);
    // At least 3 blocks: entry (with SwitchBool), then-body, merge.
    assert(fn.blocks.size() >= 3);
    assert(output_contains(prog, "switchBool"));
}

// ─── If-else ──────────────────────────────────────────────────────────────────

static void test_if_else_num() {
    const LirProgram prog = must_lower("fn f(b: bool) -> num { if b { 1 } else { 0 } }");
    const LirFnDef& fn = first_fn(prog);
    // At least 4 blocks: entry, then, else, merge.
    assert(fn.blocks.size() >= 4);
    assert(output_contains(prog, "switchBool"));
    assert(output_contains(prog, "1"));
    assert(output_contains(prog, "0"));
}

static void test_if_else_bool() {
    const LirProgram prog = must_lower("fn f(b: bool) -> bool { if b { true } else { false } }");
    assert(output_contains(prog, "switchBool"));
    assert(output_contains(prog, "true"));
    assert(output_contains(prog, "false"));
}

static void test_if_never_typed_skips_following_code() {
    const LirProgram prog = must_lower(
        "fn f(b: bool) -> num {"
        "  if b { return 1; } else { return 2; }"
        "  3"
        "}");
    const LirFnDef& fn = first_fn(prog);
    assert(output_contains(prog, "return 1"));
    assert(output_contains(prog, "return 2"));
    assert(find_local_assigned_num(fn, "3") == kInvalidLocal);
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
    std::size_t pos = 0;
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
    const LirFnDef& fn = first_fn(prog);
    // At least 3 blocks: entry, header, after-loop.
    assert(fn.blocks.size() >= 3);
    assert(output_contains(prog, "jump"));
}

static void test_loop_break_value() {
    // loop { break 42; }  →  store 42 into result local then jump
    // The loop is typed as num since break carries a value.
    const LirProgram prog = must_lower("fn f() -> num { loop { break 42; } }");
    assert(output_contains(prog, "42"));
    assert(output_contains(prog, "jump"));
}

static void test_loop_infinite_with_return() {
    // loop { return; }  →  loop body block has a return terminator (no back-edge)
    // Loop with no break has type Never, which is compatible with unit return.
    const LirProgram prog = must_lower("fn f() { loop { return; } }");
    assert(output_contains(prog, "return"));
}

static void test_infinite_loop_skips_following_code() {
    const LirProgram prog = must_lower(
        "fn f() {"
        "  loop {};"
        "  1 + 2;"
        "}");
    assert(!output_contains(prog, "BinOp(+"));
}

static void test_loop_break_value_returned() {
    // loop { break 42; } with -> num return → codegen stores 42 and returns it
    const LirProgram prog = must_lower("fn f() -> num { loop { break 42; } }");
    assert(output_contains(prog, "42"));
    assert(output_contains(prog, "return"));
}

// ─── While loop ───────────────────────────────────────────────────────────────

static void test_while_simple() {
    // while cond { }  →  cond-block (SwitchBool), body-block, after-block
    const LirProgram prog = must_lower("fn f(b: bool) { while b { } }");
    const LirFnDef& fn = first_fn(prog);
    assert(fn.blocks.size() >= 3);
    assert(output_contains(prog, "switchBool"));
}

static void test_while_with_body() {
    const LirProgram prog = must_lower("fn f(b: bool) { while b { let _ = 1; } }");
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

static void test_while_never_condition_skips_following_code() {
    const LirProgram prog = must_lower(
        "fn f() -> bool {"
        "  while return true { }"
        "  false"
        "}");
    const LirFnDef& fn = first_fn(prog);
    assert(!output_contains(prog, "switchBool"));
    assert(find_local_assigned_bool(fn, false) == kInvalidLocal);
}

// ─── For loop ─────────────────────────────────────────────────────────────────

static void test_for_with_init_and_condition() {
    const LirProgram prog = must_lower("fn f() { for (let i: num = 0; i < 10; ) { } }");
    assert(output_contains(prog, "switchBool"));
    assert(output_contains(prog, "0"));
}

static void test_for_with_step() {
    const LirProgram prog = must_lower("fn f(n: num) { for (let i: num = 0; i < n; i + 1) { } }");
    assert(output_contains(prog, "switchBool"));
    assert(output_contains(prog, "BinOp(+"));
}

static void test_for_no_condition() {
    // for (;;) {}  →  unconditional loop (like `loop`)
    const LirProgram prog = must_lower("fn f() { for (;;) { break; } }");
    assert(!prog.fns.empty());
}

static void test_for_break() {
    const LirProgram prog = must_lower("fn f() { for (let i: num = 0; i < 5; ) { break; } }");
    assert(!prog.fns.empty());
}

static void test_for_continue() {
    const LirProgram prog = must_lower("fn f() { for (let i: num = 0; i < 5; ) { continue; } }");
    assert(!prog.fns.empty());
}

static void test_for_never_condition_skips_following_code() {
    const LirProgram prog = must_lower(
        "fn f() -> bool {"
        "  for (; return false; ) { }"
        "  true"
        "}");
    const LirFnDef& fn = first_fn(prog);
    assert(!output_contains(prog, "switchBool"));
    assert(find_local_assigned_bool(fn, true) == kInvalidLocal);
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

static void test_return_drops_owned_locals() {
    const LirProgram prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn f() -> str {"
        "  let a: str = to_str(1);"
        "  let b: str = to_str(2);"
        "  return b;"
        "}");
    const LirFnDef& fn = first_fn(prog);
    const LirLocalId a_local = find_named_local(fn, "a");
    const LirLocalId b_local = find_named_local(fn, "b");
    assert(a_local != kInvalidLocal);
    assert(b_local != kInvalidLocal);
    assert(count_drop_stmts_for_local(fn, a_local) == 1);
    assert(count_drop_stmts_for_local(fn, b_local) == 1);

    const LirBasicBlock& block = fn.blocks[0];
    const auto& ret = std::get<LirReturn>(block.terminator.node);
    assert(ret.value.has_value());
    assert(ret.value->kind == LirOperand::Kind::Move);
    assert(ret.value->place.kind == LirPlace::Kind::Local);

    const LirLocalId returned_local = ret.value->place.local_id;
    assert(returned_local != a_local);
    assert(returned_local != b_local);
    assert(!find_stmt_index_of_drop(block, returned_local).has_value());

    const auto return_move_index = find_stmt_index_of_assign_from_use_operand(
        block, returned_local, LirOperand::move(LirPlace::local(b_local)));
    const auto b_drop_index = find_stmt_index_of_drop(block, b_local);
    const auto a_drop_index = find_stmt_index_of_drop(block, a_local);
    assert(return_move_index.has_value());
    assert(b_drop_index.has_value());
    assert(a_drop_index.has_value());
    assert(*return_move_index < *b_drop_index);
    assert(*b_drop_index < *a_drop_index);
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

static void test_return_never_payload_preserves_inner_returns() {
    const LirProgram prog = must_lower(
        "fn f(b: bool) -> num {"
        "  return if b { return 1 } else { return 2 };"
        "}");
    const LirFnDef& fn = first_fn(prog);

    assert(std::holds_alternative<LirSwitchBool>(fn.blocks[0].terminator.node));
    assert(count_return_terminators(fn) == 2);
    assert(count_num_literal_returns(fn, "1") == 1);
    assert(count_num_literal_returns(fn, "2") == 1);
}

static void test_never_let_initializer_skips_binding() {
    const LirProgram prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn f() -> str {"
        "  let x: str = return to_str(1);"
        "}");
    const LirFnDef& fn = first_fn(prog);

    assert(find_named_local(fn, "x") == kInvalidLocal);
    assert(std::holds_alternative<LirReturn>(fn.blocks[0].terminator.node));

    const auto& ret = std::get<LirReturn>(fn.blocks[0].terminator.node);
    assert(ret.value.has_value());
    assert(ret.value->kind == LirOperand::Kind::Move);
    assert(!output_contains(prog, " = ()"));
}

static void test_break_never_payload_skips_outer_break_lowering() {
    const LirProgram prog = must_lower(
        "fn f(b: bool) -> num {"
        "  loop {"
        "    break if b { break 1 } else { break 2 };"
        "  }"
        "}");
    const LirFnDef& fn = first_fn(prog);

    assert(count_unit_const_assignments(fn) == 0);
    assert(find_local_assigned_num(fn, "1") != kInvalidLocal);
    assert(find_local_assigned_num(fn, "2") != kInvalidLocal);
}

static void test_never_call_seals_block() {
    const Symbol panic_now = Symbol::intern("panic_now");
    const Symbol b = Symbol::intern("b");
    const cstc::span::SourceSpan span{};

    const auto cond =
        cstc::tyir::make_ty_expr(span, cstc::tyir::LocalRef{b}, cstc::tyir::ty::bool_());
    const auto call =
        cstc::tyir::make_ty_expr(span, cstc::tyir::TyCall{panic_now, {}}, cstc::tyir::ty::never());
    const auto then_block = std::make_shared<cstc::tyir::TyBlock>(cstc::tyir::TyBlock{
        {cstc::tyir::TyExprStmt{call, span}},
        std::nullopt,
        cstc::tyir::ty::never(),
        span,
    });
    const auto if_expr = cstc::tyir::make_ty_expr(
        span, cstc::tyir::TyIf{cond, then_block, std::nullopt}, cstc::tyir::ty::unit());
    const auto tail = cstc::tyir::make_ty_expr(
        span, cstc::tyir::TyLiteral{cstc::tyir::TyLiteral::Kind::Num, Symbol::intern("1")},
        cstc::tyir::ty::num());

    cstc::tyir::TyProgram program;
    program.items.push_back(
        cstc::tyir::TyFnDecl{
            Symbol::intern("f"),
            {cstc::tyir::TyParam{b, cstc::tyir::ty::bool_(), span}},
            cstc::tyir::ty::num(),
            std::make_shared<cstc::tyir::TyBlock>(cstc::tyir::TyBlock{
                {cstc::tyir::TyExprStmt{if_expr, span}},
                tail,
                cstc::tyir::ty::num(),
                span,
            }),
            span,
            false,
        });

    const LirProgram prog = lower_program(program);
    const LirFnDef& fn = first_fn(prog);

    assert(count_unreachable_terminators(fn) == 1);
    assert(count_return_terminators(fn) == 1);
    assert(output_contains(prog, "Call(panic_now"));
    assert(output_contains(prog, "unreachable"));
    assert(output_contains(prog, "switchBool"));
}

static void test_param_shadowing_in_fn_body() {
    const LirProgram prog = must_lower("fn f(x: num) -> num { let x = 2; x }");
    const LirFnDef& fn = first_fn(prog);
    assert(fn.params.size() == 1);

    const LirLocalId param_local = fn.params[0].local;
    const auto& ret = std::get<LirReturn>(fn.blocks[0].terminator.node);
    assert(ret.value.has_value());
    assert(ret.value->kind == LirOperand::Kind::Copy);
    assert(ret.value->place.kind == LirPlace::Kind::Local);
    assert(ret.value->place.local_id != param_local);
}

static void test_nested_block_shadowing_prefers_inner_binding() {
    const LirProgram prog = must_lower("fn f() -> num { let x = 1; { let x = 2; x } }");
    const LirFnDef& fn = first_fn(prog);

    const LirLocalId outer_x = find_local_assigned_num(fn, "1");
    const LirLocalId inner_x = find_local_assigned_num(fn, "2");
    assert(outer_x != kInvalidLocal);
    assert(inner_x != kInvalidLocal);
    assert(outer_x != inner_x);

    const auto& ret = std::get<LirReturn>(fn.blocks[0].terminator.node);
    assert(ret.value.has_value());
    assert(ret.value->kind == LirOperand::Kind::Copy);
    assert(ret.value->place.kind == LirPlace::Kind::Local);
    assert(ret.value->place.local_id != outer_x);
    assert(output_contains(prog, "copy(_%" + std::to_string(inner_x) + ")"));
}

static void test_terminated_block_skips_tail_expr() {
    const LirProgram prog = must_lower(
        "fn f() -> num {"
        "  return 1;"
        "  2 + 3"
        "}");
    const LirFnDef& fn = first_fn(prog);

    assert(std::holds_alternative<LirReturn>(fn.blocks[0].terminator.node));
    assert(fn.blocks[0].stmts.empty());
    assert(!output_contains(prog, "BinOp(+"));
}

static void test_terminated_loop_body_skips_following_stmt() {
    const LirProgram prog = must_lower("fn f() { loop { break; 1 + 2; } }");
    assert(!output_contains(prog, "BinOp(+"));
}

static void test_break_drops_owned_locals() {
    const LirProgram prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn f() { loop { let s: str = to_str(1); break; } }");
    const LirFnDef& fn = first_fn(prog);
    const LirLocalId s_local = find_named_local(fn, "s");
    assert(s_local != kInvalidLocal);
    assert(count_drop_stmts_for_local(fn, s_local) == 1);

    const LirBlockId drop_block_id = find_block_with_drop_of_local(fn, s_local);
    assert(drop_block_id != kInvalidBlock);
    const LirBasicBlock& drop_block = fn.blocks[drop_block_id];
    assert(find_stmt_index_of_drop(drop_block, s_local).has_value());

    const auto* jump = std::get_if<LirJump>(&drop_block.terminator.node);
    assert(jump != nullptr);
    assert(jump->target != drop_block_id);
    assert(std::holds_alternative<LirReturn>(fn.blocks[jump->target].terminator.node));
}

static void test_continue_drops_owned_locals() {
    const LirProgram prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn f(b: bool) { while b { let s: str = to_str(1); continue; } }");
    const LirFnDef& fn = first_fn(prog);
    const LirLocalId s_local = find_named_local(fn, "s");
    assert(s_local != kInvalidLocal);
    assert(count_drop_stmts_for_local(fn, s_local) == 1);

    const LirBlockId drop_block_id = find_block_with_drop_of_local(fn, s_local);
    assert(drop_block_id != kInvalidBlock);
    const LirBasicBlock& drop_block = fn.blocks[drop_block_id];
    assert(find_stmt_index_of_drop(drop_block, s_local).has_value());

    const auto* jump = std::get_if<LirJump>(&drop_block.terminator.node);
    assert(jump != nullptr);
    assert(jump->target != drop_block_id);
    assert(std::holds_alternative<LirSwitchBool>(fn.blocks[jump->target].terminator.node));
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
    std::size_t pos = 0;
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
    test_if_never_typed_skips_following_code();
    test_if_else_if();

    test_loop_break();
    test_loop_break_value();
    test_loop_break_value_returned();
    test_loop_infinite_with_return();
    test_infinite_loop_skips_following_code();

    test_while_simple();
    test_while_with_body();
    test_while_break();
    test_while_continue();
    test_while_never_condition_skips_following_code();

    test_for_with_init_and_condition();
    test_for_with_step();
    test_for_no_condition();
    test_for_break();
    test_for_continue();
    test_for_never_condition_skips_following_code();

    test_early_return();
    test_return_void();
    test_return_drops_owned_locals();
    test_return_from_nested_block();
    test_return_never_payload_preserves_inner_returns();
    test_never_let_initializer_skips_binding();
    test_break_never_payload_skips_outer_break_lowering();
    test_never_call_seals_block();
    test_param_shadowing_in_fn_body();
    test_nested_block_shadowing_prefers_inner_binding();
    test_terminated_block_skips_tail_expr();
    test_terminated_loop_body_skips_following_stmt();
    test_break_drops_owned_locals();
    test_continue_drops_owned_locals();

    test_if_inside_while();
    test_nested_if();
    test_block_ids_are_dense();

    return 0;
}
