#include <cassert>
#include <string>

#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>
#include <cstc_tyir_builder/builder.hpp>

using namespace cstc::tyir_builder;
using namespace cstc::tyir;
using namespace cstc::symbol;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static TyProgram must_lower(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(tyir.has_value());
    return *tyir;
}

static void must_fail(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    if (!ast.has_value())
        return; // parse error counts as failure
    const auto tyir = lower_program(*ast);
    assert(!tyir.has_value());
}

static void must_fail_with_message(const char* source, const char* expected_message_part) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(!tyir.has_value());
    assert(tyir.error().message.find(expected_message_part) != std::string::npos);
}

static const TyFnDecl& first_fn(const TyProgram& prog) {
    for (const auto& item : prog.items)
        if (const auto* fn = std::get_if<TyFnDecl>(&item))
            return *fn;
    assert(false && "no function found in program");
    __builtin_unreachable();
}

// ─── Literals ─────────────────────────────────────────────────────────────────

static void test_num_literal() {
    const auto prog = must_lower("fn f() -> num { 42 }");
    const auto& fn = first_fn(prog);
    const auto& tail = *fn.body->tail;
    assert(tail->ty == ty::num());
    const auto& lit = std::get<TyLiteral>(tail->node);
    assert(lit.kind == TyLiteral::Kind::Num);
    assert(lit.symbol.as_str() == "42");
}

static void test_str_literal() {
    const auto prog = must_lower("fn f() { let s: &str = \"hi\"; }");
    const auto& stmt = std::get<TyLetStmt>(first_fn(prog).body->stmts[0]);
    assert(stmt.ty == ty::ref(ty::str()));
    assert(stmt.init->ty == ty::ref(ty::str()));
    assert(std::get<TyLiteral>(stmt.init->node).kind == TyLiteral::Kind::Str);
}

static void test_bool_literals() {
    {
        const auto prog = must_lower("fn f() -> bool { true }");
        const auto& tail = *first_fn(prog).body->tail;
        assert(tail->ty == ty::bool_());
        assert(std::get<TyLiteral>(tail->node).bool_value == true);
    }
    {
        const auto prog = must_lower("fn f() -> bool { false }");
        assert(std::get<TyLiteral>((*first_fn(prog).body->tail)->node).bool_value == false);
    }
}

static void test_unit_literal() {
    const auto prog = must_lower("fn f() { () }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::unit());
}

// ─── Variables ───────────────────────────────────────────────────────────────

static void test_param_ref() {
    const auto prog = must_lower("fn f(x: num) -> num { x }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num());
    assert(std::holds_alternative<LocalRef>(tail->node));
    assert(std::get<LocalRef>(tail->node).name == Symbol::intern("x"));
}

static void test_let_binding_ref() {
    const auto prog = must_lower("fn f() -> num { let x: num = 1; x }");
    const auto& body = *first_fn(prog).body;
    // One LetStmt then tail
    assert(body.stmts.size() == 1);
    assert(std::holds_alternative<TyLetStmt>(body.stmts[0]));
    const auto& tail = *body.tail; // TyExprPtr = shared_ptr<TyExpr>
    assert(tail->ty == ty::num());
    assert(std::holds_alternative<LocalRef>(tail->node));
}

static void test_let_infer_type() {
    // No annotation — type inferred from initializer
    const auto prog = must_lower("fn f() { let x = 42; }");
    const auto& stmt = std::get<TyLetStmt>(first_fn(prog).body->stmts[0]);
    assert(stmt.ty == ty::num());
}

static void test_undefined_variable_error() { must_fail("fn f() -> num { x }"); }

// ─── Arithmetic operators ─────────────────────────────────────────────────────

static void test_arithmetic() {
    for (const char* src : {
             "fn f(x: num, y: num) -> num { x + y }",
             "fn f(x: num, y: num) -> num { x - y }",
             "fn f(x: num, y: num) -> num { x * y }",
             "fn f(x: num, y: num) -> num { x / y }",
             "fn f(x: num, y: num) -> num { x % y }",
         }) {
        const auto prog = must_lower(src);
        assert((*first_fn(prog).body->tail)->ty == ty::num());
    }
}

static void test_arithmetic_type_error() { must_fail("fn f(x: bool) -> num { x + 1 }"); }

// ─── Comparison operators ─────────────────────────────────────────────────────

static void test_comparisons() {
    for (const char* src : {
             "fn f(x: num, y: num) -> bool { x < y }",
             "fn f(x: num, y: num) -> bool { x <= y }",
             "fn f(x: num, y: num) -> bool { x > y }",
             "fn f(x: num, y: num) -> bool { x >= y }",
         }) {
        const auto prog = must_lower(src);
        assert((*first_fn(prog).body->tail)->ty == ty::bool_());
    }
}

// ─── Equality operators ───────────────────────────────────────────────────────

static void test_equality() {
    {
        const auto prog = must_lower("fn f(x: num, y: num) -> bool { x == y }");
        assert((*first_fn(prog).body->tail)->ty == ty::bool_());
    }
    {
        const auto prog = must_lower("fn f(x: bool, y: bool) -> bool { x != y }");
        assert((*first_fn(prog).body->tail)->ty == ty::bool_());
    }
}

static void test_equality_type_mismatch_error() {
    must_fail("fn f(x: num, y: bool) -> bool { x == y }");
}

// ─── Logical operators ────────────────────────────────────────────────────────

static void test_logical() {
    for (const char* src : {
             "fn f(a: bool, b: bool) -> bool { a && b }",
             "fn f(a: bool, b: bool) -> bool { a || b }",
         }) {
        const auto prog = must_lower(src);
        assert((*first_fn(prog).body->tail)->ty == ty::bool_());
    }
}

static void test_runtime_argument_promotion() {
    const auto prog = must_lower(
        "fn main() { let value: runtime num = take(1); }"
        "fn take(value: runtime num) -> runtime num { value }");
    const auto& stmt = std::get<TyLetStmt>(first_fn(prog).body->stmts[0]);
    assert(stmt.ty == ty::num(true));
    assert(stmt.init->ty == ty::num(true));
}

static void test_runtime_argument_demotion_error() {
    must_fail_with_message(
        "runtime fn source() -> num { 1 }"
        "fn sink(value: num) -> num { value }"
        "fn main() -> num { sink(source()) }",
        "argument 1 of 'sink': expected 'num', found 'runtime num'");
}

// ─── Unary operators ─────────────────────────────────────────────────────────

static void test_unary_negate() {
    const auto prog = must_lower("fn f(x: num) -> num { -x }");
    assert((*first_fn(prog).body->tail)->ty == ty::num());
}

static void test_unary_not() {
    const auto prog = must_lower("fn f(b: bool) -> bool { !b }");
    assert((*first_fn(prog).body->tail)->ty == ty::bool_());
}

static void test_unary_type_errors() {
    must_fail("fn f(b: bool) -> num { -b }");
    must_fail("fn f(x: num) -> bool { !x }");
}

// ─── If / if-else expressions ─────────────────────────────────────────────────

static void test_if_no_else() {
    // Without else: result type is Unit; if becomes the tail expression
    const auto prog = must_lower("fn f(b: bool) { if b { } }");
    const auto& body = *first_fn(prog).body;
    assert(body.ty == ty::unit());
    // Parser emits the if as the block's tail expression
    assert(body.tail.has_value());
    assert((*body.tail)->ty == ty::unit());
    assert(std::holds_alternative<TyIf>((*body.tail)->node));
}

static void test_if_else() {
    const auto prog = must_lower("fn f(b: bool) -> num { if b { 1 } else { 2 } }");
    const auto& tail = *(*first_fn(prog).body->tail);
    assert(tail.ty == ty::num());
    assert(std::holds_alternative<TyIf>(tail.node));
}

static void test_if_else_both_bare_return_is_never() {
    const auto prog = must_lower("fn f(b: bool) { if b { return; } else { return; } }");
    const auto& tail = *(*first_fn(prog).body->tail);
    assert(tail.ty == ty::never());
    assert(std::holds_alternative<TyIf>(tail.node));
}

static void test_if_condition_must_be_bool() { must_fail("fn f(x: num) { if x { } }"); }

static void test_if_else_runtime_join_produces_runtime_type() {
    const auto prog = must_lower(
        "fn choose(flag: bool) -> runtime num { if flag { 1 } else { source() } }"
        "runtime fn source() -> num { 2 }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num(true));
}

static void test_if_else_runtime_join_prevents_demotion() {
    must_fail_with_message(
        "fn choose(flag: bool) -> num { if flag { 1 } else { source() } }"
        "runtime fn source() -> num { 2 }",
        "body has type 'runtime num' but return type is 'num'");
}

// ─── Control flow ─────────────────────────────────────────────────────────────

static void test_loop_is_unit() {
    // loop without semicolon is the tail expression of the outer block
    const auto prog = must_lower("fn f() { loop { break; } }");
    const auto& fn_body = *first_fn(prog).body;
    assert(fn_body.tail.has_value());
    const auto& loop_expr = *fn_body.tail;
    assert(loop_expr->ty == ty::unit());
    assert(std::holds_alternative<TyLoop>(loop_expr->node));
}

static void test_while() {
    const auto prog = must_lower("fn f(b: bool) { while b { } }");
    const auto& fn_body = *first_fn(prog).body;
    assert(fn_body.tail.has_value());
    const auto& while_expr = *fn_body.tail;
    assert(std::holds_alternative<TyWhile>(while_expr->node));
    assert(while_expr->ty == ty::unit());
}

static void test_for_loop() {
    const auto prog = must_lower("fn f() { for (let i: num = 0; i < 10; i + 1) { } }");
    const auto& fn_body = *first_fn(prog).body;
    assert(fn_body.tail.has_value());
    const auto& for_expr = *fn_body.tail;
    assert(std::holds_alternative<TyFor>(for_expr->node));
    assert(for_expr->ty == ty::unit());

    const auto& for_node = std::get<TyFor>(for_expr->node);
    assert(for_node.init.has_value());
    assert(for_node.init->name == Symbol::intern("i"));
    assert(for_node.init->ty == ty::num());
    assert(for_node.condition.has_value());
    assert(for_node.step.has_value());
}

static void test_break_and_continue_are_never() {
    const auto prog = must_lower("fn f() { loop { break; continue; } }");
    // loop is the tail of the outer block
    const auto& fn_body = *first_fn(prog).body;
    assert(fn_body.tail.has_value());
    const auto& loop_node = std::get<TyLoop>((*fn_body.tail)->node);
    const auto& brk_stmt = std::get<TyExprStmt>(loop_node.body->stmts[0]);
    const auto& cont_stmt = std::get<TyExprStmt>(loop_node.body->stmts[1]);
    assert(brk_stmt.expr->ty == ty::never());
    assert(cont_stmt.expr->ty == ty::never());
}

static void test_return_is_never() {
    const auto prog = must_lower("fn f() { return; }");
    const auto& stmt = std::get<TyExprStmt>(first_fn(prog).body->stmts[0]);
    assert(stmt.expr->ty == ty::never());
    assert(std::holds_alternative<TyReturn>(stmt.expr->node));
}

// ─── Loop/break type inference ────────────────────────────────────────────────

static void test_loop_break_num_type() {
    // break 42 → loop type is num
    const auto prog = must_lower("fn f() -> num { loop { break 42; } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num());
    assert(std::holds_alternative<TyLoop>(tail->node));
}

static void test_loop_break_str_type() {
    const auto prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn f() -> str { loop { break to_str(1); } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::str());
}

static void test_loop_break_bool_type() {
    const auto prog = must_lower("fn f() -> bool { loop { break true; } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::bool_());
}

static void test_loop_bare_break_is_unit() {
    const auto prog = must_lower("fn f() { loop { break; } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::unit());
    assert(std::holds_alternative<TyLoop>(tail->node));
}

static void test_loop_no_break_is_never() {
    // loop with no break diverges → type is Never
    const auto prog = must_lower("fn f() -> num { loop { return 1; } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::never());
}

static void test_loop_multiple_breaks_same_type() {
    const auto prog = must_lower(
        "fn f(b: bool) -> num {"
        "  loop {"
        "    if b { break 1; } else { break 2; }"
        "  }"
        "}");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num());
}

static void test_loop_break_type_mismatch_error() {
    must_fail_with_message(
        "fn f(b: bool) { loop { if b { break 1; } else { break true; } } }",
        "'break' value type mismatch");
}

static void test_loop_break_bare_vs_value_mismatch_error() {
    must_fail_with_message(
        "fn f(b: bool) { loop { if b { break 42; } else { break; } } }",
        "'break' value type mismatch");
}

static void test_loop_break_runtime_join_produces_runtime_type() {
    const auto prog = must_lower(
        "fn choose(flag: bool) -> runtime num {"
        "  loop {"
        "    if flag { break 1; } else { break source(); }"
        "  }"
        "}"
        "runtime fn source() -> num { 2 }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num(true));
}

static void test_loop_break_runtime_join_prevents_demotion() {
    must_fail_with_message(
        "fn choose(flag: bool) -> num {"
        "  loop {"
        "    if flag { break 1; } else { break source(); }"
        "  }"
        "}"
        "runtime fn source() -> num { 2 }",
        "body has type 'runtime num' but return type is 'num'");
}

// ─── Break/continue outside loop ─────────────────────────────────────────────

static void test_break_outside_loop_error() {
    must_fail_with_message("fn f() { break; }", "'break' outside of a loop");
}

static void test_continue_outside_loop_error() {
    must_fail_with_message("fn f() { continue; }", "'continue' outside of a loop");
}

static void test_break_outside_loop_with_value_error() {
    must_fail_with_message("fn f() { break 42; }", "'break' outside of a loop");
}

// ─── Break-with-value in while/for ───────────────────────────────────────────

static void test_break_value_in_while_error() {
    must_fail_with_message(
        "fn f(b: bool) { while b { break 42; } }",
        "'break' with a value is only allowed inside 'loop'");
}

static void test_break_value_in_for_error() {
    must_fail_with_message(
        "fn f() { for (;;) { break 42; } }", "'break' with a value is only allowed inside 'loop'");
}

static void test_bare_break_in_while_ok() {
    const auto prog = must_lower("fn f(b: bool) { while b { break; } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::unit());
}

static void test_bare_break_in_for_ok() {
    const auto prog = must_lower("fn f() { for (;;) { break; } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::unit());
}

// ─── Break/continue in loop headers ─────────────────────────────────────────

static void test_break_in_while_condition_accepted() {
    // `break` in the while condition is inside the loop context.
    // Its type is Never (bottom), which is compatible with bool,
    // so this is accepted rather than producing "'break' outside of a loop".
    const auto prog = must_lower("fn f() { while (break) { } }");
    (void)prog;
}

static void test_continue_in_while_condition_accepted() {
    // Same reasoning: `continue` is Never, compatible with bool.
    const auto prog = must_lower("fn f() { while continue { } }");
    (void)prog;
}

static void test_continue_in_for_condition_accepted() {
    // `continue` in the for condition — Never is compatible with bool.
    const auto prog = must_lower("fn f() { for (; continue; ) { } }");
    (void)prog;
}

static void test_break_in_for_step_ok() {
    // `break` in the for-step is syntactically odd but should be accepted
    // (it's inside the loop context)
    const auto prog = must_lower("fn f() { for (;; break) { } }");
    (void)prog;
}

static void test_continue_in_for_step_ok() {
    const auto prog = must_lower("fn f() { for (;; continue) { } }");
    (void)prog;
}

// ─── Nested loops ────────────────────────────────────────────────────────────

static void test_nested_loop_different_break_types() {
    // Outer loop breaks with num, inner loop breaks with bool — should be fine
    const auto prog = must_lower(
        "fn f() -> num {"
        "  loop {"
        "    loop { break true; };"
        "    break 42;"
        "  }"
        "}");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num());
}

static void test_break_value_in_loop_nested_inside_while() {
    // `loop` nested inside `while` — break-with-value should target the `loop`
    const auto prog = must_lower(
        "fn f(b: bool) -> num {"
        "  loop {"
        "    while b { break; }"
        "    break 42;"
        "  }"
        "}");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num());
}

static void test_continue_in_while_ok() {
    const auto prog = must_lower("fn f(b: bool) { while b { continue; } }");
    (void)prog;
}

static void test_continue_in_for_ok() {
    const auto prog = must_lower("fn f() { for (;;) { continue; break; } }");
    (void)prog;
}

// ─── Block divergence typing ─────────────────────────────────────────────────

static void test_block_with_return_stmt_is_never() {
    // { return 1; } has type Never (the statement diverges), not Unit
    const auto prog = must_lower("fn f() -> num { { return 1; } }");
    const auto& body = *first_fn(prog).body;
    // The inner block `{ return 1; }` is the tail of the outer block
    assert(body.tail.has_value());
    const auto& inner = std::get<TyBlockPtr>((*body.tail)->node);
    assert(inner->ty == ty::never());
}

static void test_if_else_both_return_is_never() {
    // Both if-branches end with `return;` → each block is Never →
    // if-else type is Never → compatible with any return type
    const auto prog = must_lower(
        "fn f(b: bool) -> num {"
        "  if b { return 1; } else { return 2; }"
        "}");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::never());
}

static void test_if_else_both_break_is_never() {
    // Both if-branches end with `break;` in a loop → blocks are Never
    const auto prog = must_lower(
        "fn f() -> num {"
        "  loop {"
        "    if true { break 1; } else { break 2; }"
        "  }"
        "}");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num());
}

static void test_nested_diverging_blocks() {
    // Deeply nested: return inside nested if inside nested block
    const auto prog = must_lower(
        "fn f(a: bool, b: bool) -> num {"
        "  if a {"
        "    if b { return 1; } else { return 2; }"
        "  } else {"
        "    return 3;"
        "  }"
        "}");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::never());
}

static void test_non_diverging_block_stays_unit() {
    // { let x = 5; } — no divergence, block type is Unit
    const auto prog = must_lower("fn f() { { let x = 5; } }");
    const auto& body = *first_fn(prog).body;
    assert(body.tail.has_value());
    const auto& inner = std::get<TyBlockPtr>((*body.tail)->node);
    assert(inner->ty == ty::unit());
}

static void test_diverging_stmt_followed_by_let_still_diverges() {
    // { return 1; let x = 5; } — diverges at the return
    const auto prog = must_lower("fn f() -> num { { return 1; let x = 5; } }");
    const auto& body = *first_fn(prog).body;
    assert(body.tail.has_value());
    const auto& inner = std::get<TyBlockPtr>((*body.tail)->node);
    assert(inner->ty == ty::never());
}

static void test_diverging_stmt_followed_by_tail_still_diverges() {
    // { return 1; 2 } — tail is unreachable, so block type remains Never.
    const auto prog = must_lower("fn f() -> num { { return 1; 2 } }");
    const auto& body = *first_fn(prog).body;
    assert(body.tail.has_value());
    const auto& inner = std::get<TyBlockPtr>((*body.tail)->node);
    assert(inner->tail.has_value());
    assert((*inner->tail)->ty == ty::num());
    assert(inner->ty == ty::never());
}

static void test_if_one_branch_diverges_other_unit() {
    // if b { return; } (no else) — if without else is always Unit
    const auto prog = must_lower("fn f(b: bool) { if b { return; } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::unit());
}

static void test_if_else_one_branch_diverges() {
    // if b { return 1; } else { 42 } — then is Never, else is num → result is num
    const auto prog = must_lower(
        "fn f(b: bool) -> num {"
        "  if b { return 1; } else { 42 }"
        "}");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num());
}

static void test_if_diverging_then_does_not_leak_move_state() {
    const auto prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "extern \"lang\" fn consume(value: str);"
        "fn f(b: bool) {"
        "  let s: str = to_str(1);"
        "  if b { consume(s); return; }"
        "  consume(s);"
        "}");
    (void)prog;
}

static void test_if_diverging_else_does_not_leak_move_state() {
    const auto prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "extern \"lang\" fn consume(value: str);"
        "fn f(b: bool) {"
        "  let s: str = to_str(1);"
        "  if b { } else { consume(s); return; }"
        "  consume(s);"
        "}");
    (void)prog;
}

static void test_if_breaking_then_does_not_leak_move_state() {
    const auto prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "extern \"lang\" fn consume(value: str);"
        "fn f(b: bool) {"
        "  let s: str = to_str(1);"
        "  loop {"
        "    if b { consume(s); break; }"
        "    consume(s);"
        "    break;"
        "  }"
        "}");
    (void)prog;
}

static void test_loop_body_diverges_via_return() {
    // loop { return 1; } — no break, loop type is Never (diverges via return)
    // The loop body itself diverges, but loop type comes from break_ty (no break → Never)
    const auto prog = must_lower("fn f() -> num { loop { return 1; } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::never());
}

static void test_block_only_let_stmts_is_unit() {
    // Block with only let statements → type Unit
    const auto prog = must_lower("fn f() { { let a = 1; let b = 2; } }");
    const auto& body = *first_fn(prog).body;
    assert(body.tail.has_value());
    const auto& inner = std::get<TyBlockPtr>((*body.tail)->node);
    assert(inner->ty == ty::unit());
}

static void test_empty_block_is_unit() {
    // {} — no stmts, no tail → Unit
    const auto prog = must_lower("fn f() { {} }");
    const auto& body = *first_fn(prog).body;
    assert(body.tail.has_value());
    const auto& inner = std::get<TyBlockPtr>((*body.tail)->node);
    assert(inner->ty == ty::unit());
}

static void test_block_divergence_with_continue() {
    // { continue; } inside a loop — block type is Never
    const auto prog = must_lower("fn f() { loop { { continue; } } }");
    const auto& loop_node = std::get<TyLoop>((*first_fn(prog).body->tail)->node);
    assert(loop_node.body->tail.has_value());
    const auto& inner = std::get<TyBlockPtr>((*loop_node.body->tail)->node);
    assert(inner->ty == ty::never());
}

static void test_block_divergence_with_break() {
    // { break; } inside a loop — block type is Never
    const auto prog = must_lower("fn f() { loop { { break; } } }");
    const auto& loop_node = std::get<TyLoop>((*first_fn(prog).body->tail)->node);
    assert(loop_node.body->tail.has_value());
    const auto& inner = std::get<TyBlockPtr>((*loop_node.body->tail)->node);
    assert(inner->ty == ty::never());
}

// ─── Function calls ───────────────────────────────────────────────────────────

static void test_fn_call() {
    const auto prog = must_lower(
        "fn double(x: num) -> num { x * 2 }"
        "fn main() -> num { double(21) }");
    assert(prog.items.size() == 2);
    const auto& main_fn = std::get<TyFnDecl>(prog.items[1]);
    const auto& call_expr = *main_fn.body->tail;
    assert(call_expr->ty == ty::num());
    const auto& call = std::get<TyCall>(call_expr->node);
    assert(call.fn_name == Symbol::intern("double"));
    assert(call.args.size() == 1);
    assert(call.args[0]->ty == ty::num());
}

static void test_call_undefined_fn_error() { must_fail("fn f() { unknown(); }"); }

static void test_call_arg_count_error() {
    must_fail("fn g(x: num) -> num { x } fn f() -> num { g(1, 2) }");
}

static void test_call_arg_type_error() {
    must_fail("fn g(x: num) -> num { x } fn f() -> num { g(true) }");
}

// ─── Enum variant reference ───────────────────────────────────────────────────

static void test_enum_variant_ref() {
    const auto prog = must_lower(
        "enum Color { Red, Green }"
        "fn f() -> Color { Color::Red }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::named(Symbol::intern("Color")));
    const auto& ref = std::get<EnumVariantRef>(tail->node);
    assert(ref.enum_name == Symbol::intern("Color"));
    assert(ref.variant_name == Symbol::intern("Red"));
}

static void test_unknown_variant_error() {
    must_fail("enum Color { Red } fn f() -> Color { Color::Blue }");
}

// ─── Struct init ─────────────────────────────────────────────────────────────

static void test_struct_init() {
    const auto prog = must_lower(
        "struct Point { x: num, y: num }"
        "fn origin() -> Point { Point { x: 0, y: 0 } }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::named(Symbol::intern("Point")));
    assert(std::holds_alternative<TyStructInit>(tail->node));
}

static void test_struct_init_field_type_error() {
    must_fail(
        "struct Point { x: num, y: num }"
        "fn f() -> Point { Point { x: true, y: 0 } }");
}

static void test_struct_init_unknown_field_error() {
    must_fail(
        "struct Point { x: num, y: num }"
        "fn f() -> Point { Point { x: 0, y: 0, z: 0 } }");
}

static void test_struct_init_duplicate_field_error() {
    must_fail_with_message(
        "struct Point { x: num, y: num }"
        "fn f() -> Point { Point { x: 0, x: 1 } }",
        "duplicate field 'x'");
}

static void test_struct_init_missing_field_error() {
    must_fail_with_message(
        "struct Point { x: num, y: num }"
        "fn f() -> Point { Point { x: 0 } }",
        "missing field 'y'");
}

// ─── Field access ─────────────────────────────────────────────────────────────

static void test_field_access() {
    const auto prog = must_lower(
        "struct Point { x: num, y: num }"
        "fn get_x(p: Point) -> num { p.x }");
    const auto& tail = *first_fn(prog).body->tail;
    assert(tail->ty == ty::num());
    const auto& fa = std::get<TyFieldAccess>(tail->node);
    assert(fa.field == Symbol::intern("x"));
    assert(fa.base->ty == ty::named(Symbol::intern("Point")));
}

static void test_field_access_unknown_field_error() {
    must_fail(
        "struct Point { x: num }"
        "fn f(p: Point) -> num { p.z }");
}

// ─── Ownership / borrows ─────────────────────────────────────────────────────

static void test_borrow_local_binding() {
    const auto prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn f() { let s: str = to_str(1); let r: &str = &s; }");
    const auto& body = *first_fn(prog).body;
    assert(body.stmts.size() == 2);
    const auto& stmt = std::get<TyLetStmt>(body.stmts[1]);
    assert(stmt.ty == ty::ref(ty::str()));
    assert(std::holds_alternative<TyBorrow>(stmt.init->node));
    const auto& borrow = std::get<TyBorrow>(stmt.init->node);
    assert(std::holds_alternative<LocalRef>(borrow.rhs->node));
    assert(std::get<LocalRef>(borrow.rhs->node).use_kind == ValueUseKind::Borrow);
}

static void test_move_after_move_error() {
    must_fail_with_message(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "extern \"lang\" fn consume(value: str);"
        "fn f() { let s: str = to_str(1); consume(s); consume(s); }",
        "use of moved value 's'");
}

static void test_move_while_borrowed_error() {
    must_fail_with_message(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "extern \"lang\" fn consume(value: str);"
        "fn f() { let s: str = to_str(1); let r: &str = &s; consume(s); }",
        "cannot move 's' while it is borrowed");
}

static void test_copy_ref_binding_keeps_borrow() {
    const auto prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "extern \"lang\" fn print(value: &str);"
        "fn f() { let s: str = to_str(1); let r: &str = &s; let q: &str = r; print(q); }");
    const auto& body = *first_fn(prog).body;
    const auto& copy_stmt = std::get<TyLetStmt>(body.stmts[2]);
    assert(copy_stmt.ty == ty::ref(ty::str()));
    assert(std::holds_alternative<LocalRef>(copy_stmt.init->node));
    assert(std::get<LocalRef>(copy_stmt.init->node).use_kind == ValueUseKind::Copy);
}

int main() {
    SymbolSession session;

    test_num_literal();
    test_str_literal();
    test_bool_literals();
    test_unit_literal();
    test_param_ref();
    test_let_binding_ref();
    test_let_infer_type();
    test_undefined_variable_error();
    test_arithmetic();
    test_arithmetic_type_error();
    test_comparisons();
    test_equality();
    test_equality_type_mismatch_error();
    test_logical();
    test_runtime_argument_promotion();
    test_runtime_argument_demotion_error();
    test_unary_negate();
    test_unary_not();
    test_unary_type_errors();
    test_if_no_else();
    test_if_else();
    test_if_else_both_bare_return_is_never();
    test_if_condition_must_be_bool();
    test_if_else_runtime_join_produces_runtime_type();
    test_if_else_runtime_join_prevents_demotion();
    test_loop_is_unit();
    test_while();
    test_for_loop();
    test_break_and_continue_are_never();
    test_return_is_never();

    // Loop/break type inference
    test_loop_break_num_type();
    test_loop_break_str_type();
    test_loop_break_bool_type();
    test_loop_bare_break_is_unit();
    test_loop_no_break_is_never();
    test_loop_multiple_breaks_same_type();
    test_loop_break_type_mismatch_error();
    test_loop_break_bare_vs_value_mismatch_error();
    test_loop_break_runtime_join_produces_runtime_type();
    test_loop_break_runtime_join_prevents_demotion();

    // Break/continue outside loop
    test_break_outside_loop_error();
    test_continue_outside_loop_error();
    test_break_outside_loop_with_value_error();

    // Break-with-value in while/for
    test_break_value_in_while_error();
    test_break_value_in_for_error();
    test_bare_break_in_while_ok();
    test_bare_break_in_for_ok();

    // Break/continue in loop headers
    test_break_in_while_condition_accepted();
    test_continue_in_while_condition_accepted();
    test_continue_in_for_condition_accepted();
    test_break_in_for_step_ok();
    test_continue_in_for_step_ok();

    // Nested loops
    test_nested_loop_different_break_types();
    test_break_value_in_loop_nested_inside_while();
    test_continue_in_while_ok();
    test_continue_in_for_ok();

    // Block divergence typing
    test_block_with_return_stmt_is_never();
    test_if_else_both_return_is_never();
    test_if_else_both_break_is_never();
    test_nested_diverging_blocks();
    test_non_diverging_block_stays_unit();
    test_diverging_stmt_followed_by_let_still_diverges();
    test_diverging_stmt_followed_by_tail_still_diverges();
    test_if_one_branch_diverges_other_unit();
    test_if_else_one_branch_diverges();
    test_if_diverging_then_does_not_leak_move_state();
    test_if_diverging_else_does_not_leak_move_state();
    test_if_breaking_then_does_not_leak_move_state();
    test_loop_body_diverges_via_return();
    test_block_only_let_stmts_is_unit();
    test_empty_block_is_unit();
    test_block_divergence_with_continue();
    test_block_divergence_with_break();

    test_fn_call();
    test_call_undefined_fn_error();
    test_call_arg_count_error();
    test_call_arg_type_error();
    test_enum_variant_ref();
    test_unknown_variant_error();
    test_struct_init();
    test_struct_init_field_type_error();
    test_struct_init_unknown_field_error();
    test_struct_init_duplicate_field_error();
    test_struct_init_missing_field_error();
    test_field_access();
    test_field_access_unknown_field_error();
    test_borrow_local_binding();
    test_move_after_move_error();
    test_move_while_borrowed_error();
    test_copy_ref_binding_keeps_borrow();

    return 0;
}
