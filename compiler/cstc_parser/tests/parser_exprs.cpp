#include <cassert>
#include <string_view>
#include <variant>

#include <cstc_ast/ast.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>

namespace {

// Parse a function whose body is `{ BODY_SOURCE }` and return its BlockExpr.
cstc::ast::BlockExpr parse_block(std::string_view body_source) {
    const std::string src = std::string("fn f() { ") + std::string(body_source) + " }";
    const auto result = cstc::parser::parse_source(src);
    assert(result.has_value());
    const auto& fn = std::get<cstc::ast::FnDecl>(result->items[0]);
    return *fn.body;
}

// Returns the tail expression of a block built from body_source.
const cstc::ast::Expr& tail_of(std::string_view body_source) {
    // Leak intentionally — test-only shortcut via a static.
    // We use a static block to keep the AST alive.
    static cstc::ast::BlockExpr block;
    block = parse_block(body_source);
    assert(block.tail.has_value());
    return **block.tail;
}

void test_num_literal() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("99");
    assert(std::holds_alternative<cstc::ast::LiteralExpr>(expr.node));
    const auto& lit = std::get<cstc::ast::LiteralExpr>(expr.node);
    assert(lit.kind == cstc::ast::LiteralExpr::Kind::Num);
    assert(lit.symbol.as_str() == "99");
}

void test_float_literal() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("2.71");
    const auto& lit = std::get<cstc::ast::LiteralExpr>(expr.node);
    assert(lit.kind == cstc::ast::LiteralExpr::Kind::Num);
    assert(lit.symbol.as_str() == "2.71");
}

void test_string_literal() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of(R"("hello")");
    const auto& lit = std::get<cstc::ast::LiteralExpr>(expr.node);
    assert(lit.kind == cstc::ast::LiteralExpr::Kind::Str);
    assert(lit.symbol.as_str() == "\"hello\"");
}

void test_bool_true() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("true");
    const auto& lit = std::get<cstc::ast::LiteralExpr>(expr.node);
    assert(lit.kind == cstc::ast::LiteralExpr::Kind::Bool);
    assert(lit.bool_value == true);
}

void test_bool_false() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("false");
    const auto& lit = std::get<cstc::ast::LiteralExpr>(expr.node);
    assert(lit.kind == cstc::ast::LiteralExpr::Kind::Bool);
    assert(lit.bool_value == false);
}

void test_unit_literal() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("()");
    const auto& lit = std::get<cstc::ast::LiteralExpr>(expr.node);
    assert(lit.kind == cstc::ast::LiteralExpr::Kind::Unit);
}

void test_path_simple() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("myVar");
    const auto& path = std::get<cstc::ast::PathExpr>(expr.node);
    assert(path.head.as_str() == "myVar");
    assert(!path.tail.has_value());
}

void test_path_enum_variant() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("State::Running");
    const auto& path = std::get<cstc::ast::PathExpr>(expr.node);
    assert(path.head.as_str() == "State");
    assert(path.tail.has_value());
    assert(path.tail->as_str() == "Running");
}

void test_unary_negate() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("-42");
    const auto& unary = std::get<cstc::ast::UnaryExpr>(expr.node);
    assert(unary.op == cstc::ast::UnaryOp::Negate);
    const auto& inner = std::get<cstc::ast::LiteralExpr>(unary.rhs->node);
    assert(inner.symbol.as_str() == "42");
}

void test_unary_not() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("!true");
    const auto& unary = std::get<cstc::ast::UnaryExpr>(expr.node);
    assert(unary.op == cstc::ast::UnaryOp::Not);
}

void test_unary_borrow() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("&value");
    const auto& unary = std::get<cstc::ast::UnaryExpr>(expr.node);
    assert(unary.op == cstc::ast::UnaryOp::Borrow);
    const auto& inner = std::get<cstc::ast::PathExpr>(unary.rhs->node);
    assert(inner.head.as_str() == "value");
}

void test_borrow_of_field_access() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("&person.name");
    const auto& unary = std::get<cstc::ast::UnaryExpr>(expr.node);
    assert(unary.op == cstc::ast::UnaryOp::Borrow);
    const auto& field = std::get<cstc::ast::FieldAccessExpr>(unary.rhs->node);
    assert(field.field.as_str() == "name");
}

void test_binary_add() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("1 + 2");
    const auto& bin = std::get<cstc::ast::BinaryExpr>(expr.node);
    assert(bin.op == cstc::ast::BinaryOp::Add);
}

void test_binary_all_ops() {
    cstc::symbol::SymbolSession session;
    const struct {
        const char* src;
        cstc::ast::BinaryOp op;
    } cases[] = {
        {        "1 - 2", cstc::ast::BinaryOp::Sub},
        {        "1 * 2", cstc::ast::BinaryOp::Mul},
        {        "1 / 2", cstc::ast::BinaryOp::Div},
        {        "1 % 2", cstc::ast::BinaryOp::Mod},
        {       "1 == 2",  cstc::ast::BinaryOp::Eq},
        {       "1 != 2",  cstc::ast::BinaryOp::Ne},
        {        "1 < 2",  cstc::ast::BinaryOp::Lt},
        {       "1 <= 2",  cstc::ast::BinaryOp::Le},
        {        "1 > 2",  cstc::ast::BinaryOp::Gt},
        {       "1 >= 2",  cstc::ast::BinaryOp::Ge},
        {"true && false", cstc::ast::BinaryOp::And},
        {"true || false",  cstc::ast::BinaryOp::Or},
    };
    for (const auto& [src, expected_op] : cases) {
        const auto& expr = tail_of(src);
        const auto& bin = std::get<cstc::ast::BinaryExpr>(expr.node);
        assert(bin.op == expected_op);
    }
}

void test_precedence_mul_before_add() {
    cstc::symbol::SymbolSession session;
    // "1 + 2 * 3" should parse as "1 + (2 * 3)", so the root node is Add.
    const auto& expr = tail_of("1 + 2 * 3");
    const auto& bin = std::get<cstc::ast::BinaryExpr>(expr.node);
    assert(bin.op == cstc::ast::BinaryOp::Add);
    // rhs should be 2 * 3
    const auto& rhs_bin = std::get<cstc::ast::BinaryExpr>(bin.rhs->node);
    assert(rhs_bin.op == cstc::ast::BinaryOp::Mul);
}

void test_precedence_and_before_or() {
    cstc::symbol::SymbolSession session;
    // "a || b && c" → "a || (b && c)"
    const auto& expr = tail_of("a || b && c");
    const auto& bin = std::get<cstc::ast::BinaryExpr>(expr.node);
    assert(bin.op == cstc::ast::BinaryOp::Or);
    const auto& rhs_bin = std::get<cstc::ast::BinaryExpr>(bin.rhs->node);
    assert(rhs_bin.op == cstc::ast::BinaryOp::And);
}

void test_parenthesized_expr() {
    cstc::symbol::SymbolSession session;
    // "(1 + 2) * 3" → the root is Mul
    const auto& expr = tail_of("(1 + 2) * 3");
    const auto& bin = std::get<cstc::ast::BinaryExpr>(expr.node);
    assert(bin.op == cstc::ast::BinaryOp::Mul);
}

void test_field_access() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("obj.field");
    const auto& fa = std::get<cstc::ast::FieldAccessExpr>(expr.node);
    assert(fa.field.as_str() == "field");
    assert(std::holds_alternative<cstc::ast::PathExpr>(fa.base->node));
}

void test_field_access_chain() {
    cstc::symbol::SymbolSession session;
    // "a.b.c" → FieldAccess(c, base=FieldAccess(b, base=Path(a)))
    const auto& expr = tail_of("a.b.c");
    const auto& outer = std::get<cstc::ast::FieldAccessExpr>(expr.node);
    assert(outer.field.as_str() == "c");
    const auto& inner = std::get<cstc::ast::FieldAccessExpr>(outer.base->node);
    assert(inner.field.as_str() == "b");
}

void test_call_no_args() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("foo()");
    const auto& call = std::get<cstc::ast::CallExpr>(expr.node);
    assert(call.args.empty());
    const auto& callee = std::get<cstc::ast::PathExpr>(call.callee->node);
    assert(callee.head.as_str() == "foo");
}

void test_call_multiple_args() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("add(1, 2, 3)");
    const auto& call = std::get<cstc::ast::CallExpr>(expr.node);
    assert(call.args.size() == 3);
}

void test_call_trailing_comma() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("f(1, 2,)");
    const auto& call = std::get<cstc::ast::CallExpr>(expr.node);
    assert(call.args.size() == 2);
}

void test_nested_call() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("outer(inner(1))");
    const auto& outer_call = std::get<cstc::ast::CallExpr>(expr.node);
    assert(outer_call.args.size() == 1);
    assert(std::holds_alternative<cstc::ast::CallExpr>(outer_call.args[0]->node));
}

void test_block_as_expr() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("{ 1 }");
    assert(std::holds_alternative<cstc::ast::BlockPtr>(expr.node));
    const auto& blk = std::get<cstc::ast::BlockPtr>(expr.node);
    assert(blk->tail.has_value());
}

void test_if_no_else() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("if cond { 1 }");
    const auto& if_expr = std::get<cstc::ast::IfExpr>(expr.node);
    assert(!if_expr.else_branch.has_value());
}

void test_if_else() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("if cond { 1 } else { 2 }");
    const auto& if_expr = std::get<cstc::ast::IfExpr>(expr.node);
    assert(if_expr.else_branch.has_value());
}

void test_if_else_if_chain() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("if a { 1 } else if b { 2 } else { 3 }");
    const auto& outer_if = std::get<cstc::ast::IfExpr>(expr.node);
    assert(outer_if.else_branch.has_value());
    // The else branch should itself be an IfExpr.
    assert(std::holds_alternative<cstc::ast::IfExpr>((*outer_if.else_branch)->node));
}

void test_loop() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("loop { break }");
    assert(std::holds_alternative<cstc::ast::LoopExpr>(expr.node));
    const auto& loop_expr = std::get<cstc::ast::LoopExpr>(expr.node);
    assert(loop_expr.body->tail.has_value());
    assert(std::holds_alternative<cstc::ast::BreakExpr>((*loop_expr.body->tail)->node));
}

void test_while() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("while cond { continue }");
    const auto& wh = std::get<cstc::ast::WhileExpr>(expr.node);
    assert(std::holds_alternative<cstc::ast::PathExpr>(wh.condition->node));
}

void test_for_full() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("for (let i = 0; i < 10; i) { i; }");
    const auto& for_expr = std::get<cstc::ast::ForExpr>(expr.node);
    assert(for_expr.init.has_value());
    assert(std::holds_alternative<cstc::ast::ForInitLet>(*for_expr.init));
    assert(for_expr.condition.has_value());
    assert(for_expr.step.has_value());
}

void test_for_all_parts_empty() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("for (;;) { break }");
    const auto& for_expr = std::get<cstc::ast::ForExpr>(expr.node);
    assert(!for_expr.init.has_value());
    assert(!for_expr.condition.has_value());
    assert(!for_expr.step.has_value());
}

void test_for_expr_init() {
    cstc::symbol::SymbolSession session;
    // Non-let init expression
    const auto& expr = tail_of("for (i; true; i) { }");
    const auto& for_expr = std::get<cstc::ast::ForExpr>(expr.node);
    assert(for_expr.init.has_value());
    assert(std::holds_alternative<cstc::ast::ExprPtr>(*for_expr.init));
}

void test_break_no_value() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("break");
    const auto& br = std::get<cstc::ast::BreakExpr>(expr.node);
    assert(!br.value.has_value());
}

void test_break_with_value() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("break 42");
    const auto& br = std::get<cstc::ast::BreakExpr>(expr.node);
    assert(br.value.has_value());
    const auto& val = std::get<cstc::ast::LiteralExpr>((*br.value)->node);
    assert(val.symbol.as_str() == "42");
}

void test_continue() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("continue");
    assert(std::holds_alternative<cstc::ast::ContinueExpr>(expr.node));
}

void test_return_no_value() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("return");
    const auto& ret = std::get<cstc::ast::ReturnExpr>(expr.node);
    assert(!ret.value.has_value());
}

void test_return_with_value() {
    cstc::symbol::SymbolSession session;
    const auto& expr = tail_of("return 7");
    const auto& ret = std::get<cstc::ast::ReturnExpr>(expr.node);
    assert(ret.value.has_value());
    const auto& val = std::get<cstc::ast::LiteralExpr>((*ret.value)->node);
    assert(val.symbol.as_str() == "7");
}

} // namespace

int main() {
    test_num_literal();
    test_float_literal();
    test_string_literal();
    test_bool_true();
    test_bool_false();
    test_unit_literal();
    test_path_simple();
    test_path_enum_variant();
    test_unary_negate();
    test_unary_not();
    test_unary_borrow();
    test_borrow_of_field_access();
    test_binary_add();
    test_binary_all_ops();
    test_precedence_mul_before_add();
    test_precedence_and_before_or();
    test_parenthesized_expr();
    test_field_access();
    test_field_access_chain();
    test_call_no_args();
    test_call_multiple_args();
    test_call_trailing_comma();
    test_nested_call();
    test_block_as_expr();
    test_if_no_else();
    test_if_else();
    test_if_else_if_chain();
    test_loop();
    test_while();
    test_for_full();
    test_for_all_parts_empty();
    test_for_expr_init();
    test_break_no_value();
    test_break_with_value();
    test_continue();
    test_return_no_value();
    test_return_with_value();
    return 0;
}
