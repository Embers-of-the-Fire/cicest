#include <cassert>
#include <cstdlib>
#include <string>
#include <string_view>
#include <variant>

#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>
#include <cstc_tyir_builder/builder.hpp>
#include <cstc_tyir_interp/interp.hpp>

using namespace cstc::symbol;
using namespace cstc::tyir;

static TyProgram must_fold(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    const auto folded = cstc::tyir_interp::fold_program(*tyir);
    assert(folded.has_value());
    return *folded;
}

static cstc::tyir_interp::EvalError must_fail_to_fold(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    const auto folded = cstc::tyir_interp::fold_program(*tyir);
    assert(!folded.has_value());
    return folded.error();
}

static const TyFnDecl& find_fn(const TyProgram& program, std::string_view name) {
    const Symbol fn_name = Symbol::intern(name);
    for (const TyItem& item : program.items) {
        if (const auto* fn = std::get_if<TyFnDecl>(&item)) {
            if (fn->name == fn_name)
                return *fn;
        }
    }

    assert(false && "function not found");
    std::abort();
}

static const TyExprPtr& require_tail(const TyFnDecl& fn) {
    assert(fn.body != nullptr);
    assert(fn.body->tail.has_value());
    return *fn.body->tail;
}

static const TyLiteral& require_literal(const TyExprPtr& expr) {
    assert(std::holds_alternative<TyLiteral>(expr->node));
    return std::get<TyLiteral>(expr->node);
}

static void test_const_function_call_folds_to_literal() {
    SymbolSession session;
    const auto program = must_fold(R"(
fn add(a: num, b: num) -> num {
    a + b
}

fn main() -> num {
    add(1, 2)
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Num);
    assert(literal.symbol.as_str() == std::string_view{"3"});
}

static void test_runtime_call_remains_in_tyir() {
    SymbolSession session;
    const auto program = must_fold(R"(
runtime fn source() -> num {
    41
}

fn main() -> runtime num {
    source() + 1
}
)");

    const TyExprPtr& tail = require_tail(find_fn(program, "main"));
    assert(std::holds_alternative<TyBinary>(tail->node));
    const auto& binary = std::get<TyBinary>(tail->node);
    assert(std::holds_alternative<TyCall>(binary.lhs->node));
    assert(std::get<TyCall>(binary.lhs->node).fn_name == Symbol::intern("source"));
}

static void test_short_circuit_boolean_ops_do_not_eval_dead_rhs() {
    SymbolSession session;
    const auto program = must_fold(R"(
fn lhs_false() -> bool {
    false && (1 / 0 == 0)
}

fn lhs_true() -> bool {
    true || (1 / 0 == 0)
}
)");

    const TyLiteral& lhs_false = require_literal(require_tail(find_fn(program, "lhs_false")));
    assert(lhs_false.kind == TyLiteral::Kind::Bool);
    assert(!lhs_false.bool_value);

    const TyLiteral& lhs_true = require_literal(require_tail(find_fn(program, "lhs_true")));
    assert(lhs_true.kind == TyLiteral::Kind::Bool);
    assert(lhs_true.bool_value);
}

static void test_move_only_local_and_borrow_can_fold() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn to_str(value: num) -> str;
extern "lang" fn str_len(value: &str) -> num;

fn main() -> num {
    let text: str = to_str(42);
    str_len(&text)
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Num);
    assert(literal.symbol.as_str() == std::string_view{"2"});
}

static void test_move_only_return_materializes_owned_string() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn to_str(value: num) -> str;

fn render() -> str {
    let text: str = to_str(42);
    text
}

fn main() {
    let rendered: str = render();
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "render")));
    assert(literal.kind == TyLiteral::Kind::OwnedStr);
    assert(literal.symbol.as_str() == std::string_view{"\"42\""});
}

static void test_string_intrinsics_fold_to_owned_string() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn str_concat(a: &str, b: &str) -> str;

fn render() -> str {
    str_concat("he", "llo")
}

fn main() {
    let rendered: str = render();
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "render")));
    assert(literal.kind == TyLiteral::Kind::OwnedStr);
    assert(literal.symbol.as_str() == std::string_view{"\"hello\""});
}

static void test_assert_intrinsics_fold_to_unit() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn assert(condition: bool);
extern "lang" fn assert_eq(a: num, b: num);

fn main() {
    assert(true);
    assert_eq(1, 1);
}
)");

    const TyFnDecl& main_fn = find_fn(program, "main");
    assert(main_fn.body != nullptr);
    assert(main_fn.body->stmts.size() == 2);

    for (const TyStmt& stmt : main_fn.body->stmts) {
        assert(std::holds_alternative<TyExprStmt>(stmt));
        const TyLiteral& literal = require_literal(std::get<TyExprStmt>(stmt).expr);
        assert(literal.kind == TyLiteral::Kind::Unit);
    }
}

static void test_runtime_intrinsic_call_is_preserved() {
    SymbolSession session;
    const auto program = must_fold(R"(
runtime extern "lang" fn println(value: &str);

fn main() {
    println("hello");
}
)");

    const TyFnDecl& main_fn = find_fn(program, "main");
    assert(main_fn.body != nullptr);
    assert(main_fn.body->stmts.size() == 1);
    assert(std::holds_alternative<TyExprStmt>(main_fn.body->stmts[0]));
    const auto& expr = std::get<TyExprStmt>(main_fn.body->stmts[0]).expr;
    assert(std::holds_alternative<TyCall>(expr->node));
    assert(std::get<TyCall>(expr->node).fn_name == Symbol::intern("println"));
}

static void test_dead_if_body_is_not_folded() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn assert(condition: bool);

fn main() {
    if false { assert(false); }
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Unit);
}

static void test_dead_while_body_is_not_folded() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn assert(condition: bool);

fn main() {
    while false { assert(false); }
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Unit);
}

static void test_dead_for_body_and_step_are_not_folded() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn assert(condition: bool);

fn main() {
    for (; false; assert(false)) { assert(false); }
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Unit);
}

static void test_dead_for_still_folds_reachable_init() {
    SymbolSession session;
    const auto error = must_fail_to_fold(R"(
extern "lang" fn assert(condition: bool);

fn main() {
    for (let _ = assert(false); false; assert(true)) { assert(true); }
}
)");

    assert(error.message.find("compile-time assertion failed") != std::string::npos);
}

static void test_reached_assertion_failure_reports_error() {
    SymbolSession session;
    const auto error = must_fail_to_fold(R"(
extern "lang" fn assert(condition: bool);

fn main() {
    assert(false);
}
)");

    assert(error.message.find("compile-time assertion failed") != std::string::npos);
    assert(error.stack.empty());
}

static void test_error_stack_records_called_function() {
    SymbolSession session;
    const auto error = must_fail_to_fold(R"(
extern "lang" fn assert_eq(a: num, b: num);

fn check(value: num) {
    assert_eq(value, 1);
}

fn main() {
    check(2);
}
)");

    assert(error.message.find("compile-time assert_eq failed") != std::string::npos);
    assert(error.stack.size() == 1);
    assert(error.stack[0].fn_name == Symbol::intern("check"));
}

static void test_recursive_const_eval_reports_call_depth_error() {
    SymbolSession session;
    const auto error = must_fail_to_fold(R"(
fn recur() -> num {
    recur()
}
)");

    assert(error.message.find("const-eval call depth exhausted") != std::string::npos);
    assert(error.message.find("recur") != std::string::npos);
    assert(!error.stack.empty());
    assert(error.stack.back().fn_name == Symbol::intern("recur"));
}

static void test_infinite_loop_reports_step_budget_error() {
    SymbolSession session;
    const auto error = must_fail_to_fold(R"(
fn main() -> ! {
    loop {}
}
)");

    assert(error.message.find("const-eval step budget exhausted") != std::string::npos);
    assert(error.stack.empty());
}

static void test_infinite_while_reports_step_budget_error() {
    SymbolSession session;
    const auto error = must_fail_to_fold(R"(
fn main() {
    while true {}
}
)");

    assert(error.message.find("const-eval step budget exhausted") != std::string::npos);
    assert(error.stack.empty());
}

static void test_infinite_for_reports_step_budget_error() {
    SymbolSession session;
    const auto error = must_fail_to_fold(R"(
fn main() {
    for (;;) {}
}
)");

    assert(error.message.find("const-eval step budget exhausted") != std::string::npos);
    assert(error.stack.empty());
}

static void test_bare_break_in_for_still_folds_to_unit() {
    SymbolSession session;
    const auto program = must_fold(R"(
fn main() {
    for (;;) { break; }
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Unit);
}

static void test_break_value_in_for_reports_const_eval_error() {
    SymbolSession session;
    const auto ast = cstc::parser::parse_source(R"(
fn main() {
    for (;;) { break; }
}
)");
    assert(ast.has_value());
    auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());

    bool mutated = false;
    for (TyItem& item : tyir->items) {
        auto* fn = std::get_if<TyFnDecl>(&item);
        if (fn == nullptr || fn->name != Symbol::intern("main"))
            continue;
        assert(fn->body != nullptr);
        assert(fn->body->tail.has_value());
        auto* for_expr = std::get_if<TyFor>(&(*fn->body->tail)->node);
        assert(for_expr != nullptr);
        assert(for_expr->body != nullptr);
        assert(for_expr->body->stmts.size() == 1);
        assert(std::holds_alternative<TyExprStmt>(for_expr->body->stmts[0]));
        TyExprPtr& break_node = std::get<TyExprStmt>(for_expr->body->stmts[0]).expr;
        auto* break_expr = std::get_if<TyBreak>(&break_node->node);
        assert(break_expr != nullptr);
        break_expr->value = make_ty_expr(
            break_node->span, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("42"), false},
            ty::num());
        mutated = true;
    }
    assert(mutated);

    const auto folded = cstc::tyir_interp::fold_program(*tyir);
    assert(!folded.has_value());
    assert(
        folded.error().message.find("'break' with a value is only allowed inside 'loop'")
        != std::string::npos);
}

static void test_materialization_mismatch_reports_error() {
    SymbolSession session;
    const auto ast = cstc::parser::parse_source(R"(
fn main() -> num {
    1
}
)");
    assert(ast.has_value());
    auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());

    bool mutated = false;
    for (TyItem& item : tyir->items) {
        auto* fn = std::get_if<TyFnDecl>(&item);
        if (fn == nullptr || fn->name != Symbol::intern("main"))
            continue;
        assert(fn->body != nullptr);
        assert(fn->body->tail.has_value());
        (*fn->body->tail)->ty = ty::ref(ty::num());
        mutated = true;
    }
    assert(mutated);

    const auto folded = cstc::tyir_interp::fold_program(*tyir);
    assert(!folded.has_value());
    assert(
        folded.error().message.find("mismatched compile-time reference shape")
        != std::string::npos);
}

static void test_null_materialization_reports_error() {
    SymbolSession session;
    const cstc::tyir_interp::detail::ProgramView program;
    const auto materialized =
        cstc::tyir_interp::detail::value_to_expr(program, nullptr, ty::num(), {});
    assert(!materialized.has_value());
    assert(
        materialized.error().message.find("missing compile-time value while materializing type")
        != std::string::npos);
}

static void test_comparison_requires_numeric_operands() {
    SymbolSession session;
    const auto ast = cstc::parser::parse_source(R"(
fn cmp(x: num, y: num) -> bool {
    x < y
}
)");
    assert(ast.has_value());
    auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());

    bool mutated = false;
    for (TyItem& item : tyir->items) {
        auto* fn = std::get_if<TyFnDecl>(&item);
        if (fn == nullptr || fn->name != Symbol::intern("cmp"))
            continue;
        assert(fn->body != nullptr);
        assert(fn->body->tail.has_value());
        auto* binary = std::get_if<TyBinary>(&(*fn->body->tail)->node);
        assert(binary != nullptr);
        binary->lhs = make_ty_expr(
            binary->lhs->span, TyLiteral{TyLiteral::Kind::Bool, cstc::symbol::kInvalidSymbol, true},
            ty::bool_());
        binary->rhs = make_ty_expr(
            binary->rhs->span,
            TyLiteral{TyLiteral::Kind::Bool, cstc::symbol::kInvalidSymbol, false}, ty::bool_());
        mutated = true;
    }
    assert(mutated);

    const auto folded = cstc::tyir_interp::fold_program(*tyir);
    assert(!folded.has_value());
    assert(folded.error().message.find("numeric comparison operands") != std::string::npos);
}

int main() {
    test_const_function_call_folds_to_literal();
    test_runtime_call_remains_in_tyir();
    test_short_circuit_boolean_ops_do_not_eval_dead_rhs();
    test_move_only_local_and_borrow_can_fold();
    test_move_only_return_materializes_owned_string();
    test_string_intrinsics_fold_to_owned_string();
    test_assert_intrinsics_fold_to_unit();
    test_runtime_intrinsic_call_is_preserved();
    test_dead_if_body_is_not_folded();
    test_dead_while_body_is_not_folded();
    test_dead_for_body_and_step_are_not_folded();
    test_dead_for_still_folds_reachable_init();
    test_reached_assertion_failure_reports_error();
    test_error_stack_records_called_function();
    test_recursive_const_eval_reports_call_depth_error();
    test_infinite_loop_reports_step_budget_error();
    test_infinite_while_reports_step_budget_error();
    test_infinite_for_reports_step_budget_error();
    test_bare_break_in_for_still_folds_to_unit();
    test_break_value_in_for_reports_const_eval_error();
    test_materialization_mismatch_reports_error();
    test_null_materialization_reports_error();
    test_comparison_requires_numeric_operands();
    return 0;
}
