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
    const auto prog = must_lower("fn f() -> str { \"hi\" }");
    assert((*first_fn(prog).body->tail)->ty == ty::str());
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

static void test_if_condition_must_be_bool() { must_fail("fn f(x: num) { if x { } }"); }

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
    const auto prog = must_lower("fn f() -> str { loop { break \"hi\"; } }");
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
    test_unary_negate();
    test_unary_not();
    test_unary_type_errors();
    test_if_no_else();
    test_if_else();
    test_if_condition_must_be_bool();
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

    // Break/continue outside loop
    test_break_outside_loop_error();
    test_continue_outside_loop_error();
    test_break_outside_loop_with_value_error();

    // Break-with-value in while/for
    test_break_value_in_while_error();
    test_break_value_in_for_error();
    test_bare_break_in_while_ok();
    test_bare_break_in_for_ok();

    // Nested loops
    test_nested_loop_different_break_types();
    test_break_value_in_loop_nested_inside_while();
    test_continue_in_while_ok();
    test_continue_in_for_ok();

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

    return 0;
}
