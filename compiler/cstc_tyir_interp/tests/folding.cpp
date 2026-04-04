#include <cassert>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>
#include <cstc_tyir_builder/builder.hpp>
#include <cstc_tyir_interp/detail.hpp>
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

static cstc::tyir_builder::LowerError must_fail_to_lower(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(!tyir.has_value());
    return tyir.error();
}

static TyProgram must_fold_with_constraint_prelude(const char* source) {
    const std::string full_source =
        std::string(
            "[[lang = \"cstc_constraint\"]] enum Constraint { Valid, Invalid }"
            "[[lang = \"cstc_std_constraint\"]] extern \"lang\" fn "
            "constraint(value: bool) -> Constraint;")
        + source;
    return must_fold(full_source.c_str());
}

static cstc::tyir_interp::EvalError must_fail_to_fold_with_constraint_prelude(const char* source) {
    const std::string full_source =
        std::string(
            "[[lang = \"cstc_constraint\"]] enum Constraint { Valid, Invalid }"
            "[[lang = \"cstc_std_constraint\"]] extern \"lang\" fn "
            "constraint(value: bool) -> Constraint;")
        + source;
    return must_fail_to_fold(full_source.c_str());
}

static cstc::tyir_builder::LowerError
    must_fail_to_lower_with_constraint_prelude(const char* source) {
    const std::string full_source =
        std::string(
            "[[lang = \"cstc_constraint\"]] enum Constraint { Valid, Invalid }"
            "[[lang = \"cstc_std_constraint\"]] extern \"lang\" fn "
            "constraint(value: bool) -> Constraint;")
        + source;
    return must_fail_to_lower(full_source.c_str());
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

static const TyCall& require_call(const TyExprPtr& expr) {
    assert(std::holds_alternative<TyCall>(expr->node));
    return std::get<TyCall>(expr->node);
}

static const TyStructInit& require_struct_init(const TyExprPtr& expr) {
    assert(std::holds_alternative<TyStructInit>(expr->node));
    return std::get<TyStructInit>(expr->node);
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

static void test_generic_struct_materialization_substitutes_field_types() {
    SymbolSession session;
    const auto program = must_fold(R"(
struct Box<T> {
    value: T
}

fn wrap() -> Box<num> {
    Box<num> { value: 1 }
}
)");

    const TyStructInit& init = require_struct_init(require_tail(find_fn(program, "wrap")));
    assert(init.generic_args.size() == 1);
    assert(init.generic_args[0] == ty::num());
    assert(init.fields.size() == 1);
    assert(init.fields[0].name == Symbol::intern("value"));
    assert(init.fields[0].value->ty == ty::num());

    const TyLiteral& value = require_literal(init.fields[0].value);
    assert(value.kind == TyLiteral::Kind::Num);
    assert(value.symbol.as_str() == std::string_view{"1"});
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

static void test_borrowed_str_free_intrinsic_is_noop() {
    SymbolSession session;
    cstc::tyir_interp::detail::ProgramView program;
    cstc::tyir_interp::detail::EvalContext ctx{
        program,
        {},
        cstc::tyir_interp::detail::kDefaultEvalStepBudget,
        cstc::tyir_interp::detail::kDefaultEvalCallDepth,
        {},
    };
    TyExternFnDecl decl;
    decl.name = Symbol::intern("str_free");
    decl.link_name = Symbol::intern("cstc_std_str_free");
    decl.params.resize(1);

    auto result = cstc::tyir_interp::detail::eval_lang_intrinsic(
        decl,
        {cstc::tyir_interp::detail::make_string(
            "hello", cstc::tyir_interp::detail::Value::StringOwnership::BorrowedLiteral)},
        ctx, {});
    assert(result.has_value());
    assert((*result)->kind == cstc::tyir_interp::detail::Value::Kind::Unit);
}

static void test_constraint_intrinsic_returns_constraint_enum() {
    SymbolSession session;
    cstc::tyir_interp::detail::ProgramView program;
    cstc::tyir_interp::detail::EvalContext ctx{
        program,
        {},
        cstc::tyir_interp::detail::kDefaultEvalStepBudget,
        cstc::tyir_interp::detail::kDefaultEvalCallDepth,
        {},
    };
    TyExternFnDecl decl;
    decl.name = Symbol::intern("constraint");
    decl.link_name = Symbol::intern("cstc_std_constraint");
    decl.params.resize(1);
    decl.return_ty = ty::named(Symbol::intern("Constraint"));

    auto result = cstc::tyir_interp::detail::eval_lang_intrinsic(
        decl, {cstc::tyir_interp::detail::make_bool(false)}, ctx, {});
    assert(result.has_value());
    assert((*result)->kind == cstc::tyir_interp::detail::Value::Kind::Enum);
    assert((*result)->type_name == Symbol::intern("Constraint"));
    assert((*result)->variant_name == Symbol::intern("Invalid"));
}

static void test_borrow_of_folded_owned_string_keeps_folded_rhs() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn to_str(value: num) -> str;
runtime extern "lang" fn println(value: &str);

fn main() {
    let rendered: str = to_str(42);
    println(&rendered);
}
)");

    const TyFnDecl& main_fn = find_fn(program, "main");
    assert(main_fn.body != nullptr);
    assert(main_fn.body->stmts.size() == 2);

    const auto& rendered_stmt = std::get<TyLetStmt>(main_fn.body->stmts[0]);
    const TyLiteral& rendered_literal = require_literal(rendered_stmt.init);
    assert(rendered_literal.kind == TyLiteral::Kind::OwnedStr);
    assert(rendered_literal.symbol.as_str() == std::string_view{"\"42\""});

    const auto& print_stmt = std::get<TyExprStmt>(main_fn.body->stmts[1]);
    assert(std::holds_alternative<TyCall>(print_stmt.expr->node));
    const auto& print_call = std::get<TyCall>(print_stmt.expr->node);
    assert(print_call.args.size() == 1);
    assert(std::holds_alternative<TyBorrow>(print_call.args[0]->node));
    const auto& borrow = std::get<TyBorrow>(print_call.args[0]->node);
    const TyLiteral& borrow_literal = require_literal(borrow.rhs);
    assert(borrow_literal.kind == TyLiteral::Kind::OwnedStr);
    assert(borrow_literal.symbol.as_str() == std::string_view{"\"42\""});
}

static void test_borrow_preserves_folded_rhs_when_ref_cannot_materialize() {
    SymbolSession session;
    const auto program = must_fold(R"(
runtime extern "lang" fn sink(value: &num);

fn main() {
    sink(&(1 + 2));
}
)");

    const TyFnDecl& main_fn = find_fn(program, "main");
    assert(main_fn.body != nullptr);
    assert(main_fn.body->stmts.size() == 1);

    const auto& sink_stmt = std::get<TyExprStmt>(main_fn.body->stmts[0]);
    assert(std::holds_alternative<TyCall>(sink_stmt.expr->node));
    const auto& sink_call = std::get<TyCall>(sink_stmt.expr->node);
    assert(sink_call.args.size() == 1);
    assert(std::holds_alternative<TyBorrow>(sink_call.args[0]->node));

    const auto& borrow = std::get<TyBorrow>(sink_call.args[0]->node);
    const TyLiteral& literal = require_literal(borrow.rhs);
    assert(literal.kind == TyLiteral::Kind::Num);
    assert(literal.symbol.as_str() == std::string_view{"3"});
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
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
extern "lang" fn assert(condition: bool);

fn main() {
    for (let _ = assert(false); false; assert(true)) { assert(true); }
}
)");

    assert(error.message.find("compile-time assertion failed") != std::string::npos);
}

static void test_dead_stmt_after_return_is_not_folded() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn assert(condition: bool);

fn main() -> num {
    return 1;
    assert(false);
}
)");

    const TyFnDecl& main_fn = find_fn(program, "main");
    assert(main_fn.body != nullptr);
    assert(main_fn.body->stmts.size() == 2);
    assert(!main_fn.body->tail.has_value());

    const auto& return_stmt = std::get<TyExprStmt>(main_fn.body->stmts[0]);
    assert(std::holds_alternative<TyReturn>(return_stmt.expr->node));
    const auto& return_expr = std::get<TyReturn>(return_stmt.expr->node);
    assert(return_expr.value.has_value());
    const TyLiteral& return_value = require_literal(*return_expr.value);
    assert(return_value.kind == TyLiteral::Kind::Num);
    assert(return_value.symbol.as_str() == std::string_view{"1"});

    const auto& dead_stmt = std::get<TyExprStmt>(main_fn.body->stmts[1]);
    const TyCall& dead_call = require_call(dead_stmt.expr);
    assert(dead_call.fn_name == Symbol::intern("assert"));
}

static void test_dead_tail_after_break_is_not_folded() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn assert(condition: bool);
runtime extern "lang" fn keep_running() -> bool;

fn main() {
    while keep_running() {
        break;
        assert(false)
    }
}
)");

    const auto& while_expr = std::get<TyWhile>(require_tail(find_fn(program, "main"))->node);
    assert(while_expr.body != nullptr);
    assert(while_expr.body->stmts.size() == 1);
    assert(while_expr.body->tail.has_value());

    const auto& break_stmt = std::get<TyExprStmt>(while_expr.body->stmts[0]);
    assert(std::holds_alternative<TyBreak>(break_stmt.expr->node));

    const TyCall& dead_call = require_call(*while_expr.body->tail);
    assert(dead_call.fn_name == Symbol::intern("assert"));
}

static void test_dead_tail_after_continue_is_not_folded() {
    SymbolSession session;
    const auto program = must_fold(R"(
extern "lang" fn assert(condition: bool);
runtime extern "lang" fn keep_running() -> bool;

fn main() {
    while keep_running() {
        continue;
        assert(false)
    }
}
)");

    const auto& while_expr = std::get<TyWhile>(require_tail(find_fn(program, "main"))->node);
    assert(while_expr.body != nullptr);
    assert(while_expr.body->stmts.size() == 1);
    assert(while_expr.body->tail.has_value());

    const auto& continue_stmt = std::get<TyExprStmt>(while_expr.body->stmts[0]);
    assert(std::holds_alternative<TyContinue>(continue_stmt.expr->node));

    const TyCall& dead_call = require_call(*while_expr.body->tail);
    assert(dead_call.fn_name == Symbol::intern("assert"));
}

static void test_reached_assertion_failure_reports_error() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
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
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
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

static void test_assert_eq_requires_exact_runtime_equality() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
extern "lang" fn assert_eq(a: num, b: num);

fn main() {
    assert_eq(1, 1.0000000005);
}
)");

    assert(error.message.find("compile-time assert_eq failed") != std::string::npos);
}

static void test_malformed_const_function_call_reports_arity_error() {
    SymbolSession session;
    const auto ast = cstc::parser::parse_source(R"(
fn add(a: num, b: num) -> num {
    a + b
}

fn main() -> num {
    add(1, 2)
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
        auto* call = std::get_if<TyCall>(&(*fn->body->tail)->node);
        assert(call != nullptr);
        assert(call->args.size() == 2);
        call->args.pop_back();
        mutated = true;
    }
    assert(mutated);

    const auto folded = cstc::tyir_interp::fold_program(*tyir);
    assert(!folded.has_value());
    assert(folded.error().message.find("mismatched compile-time call arity") != std::string::npos);
    assert(folded.error().message.find("add") != std::string::npos);
    assert(folded.error().message.find("expected 2 argument(s), got 1") != std::string::npos);
    assert(folded.error().stack.empty());
}

static void test_malformed_lang_call_reports_arity_error() {
    SymbolSession session;
    const auto ast = cstc::parser::parse_source(R"(
extern "lang" fn assert_eq(a: num, b: num);

fn main() {
    assert_eq(1, 1);
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
        assert(fn->body->stmts.size() == 1);
        assert(std::holds_alternative<TyExprStmt>(fn->body->stmts[0]));
        auto& expr = std::get<TyExprStmt>(fn->body->stmts[0]).expr;
        auto* call = std::get_if<TyCall>(&expr->node);
        assert(call != nullptr);
        assert(call->args.size() == 2);
        call->args.push_back(make_ty_expr(
            expr->span, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("3"), false}, ty::num()));
        mutated = true;
    }
    assert(mutated);

    const auto folded = cstc::tyir_interp::fold_program(*tyir);
    assert(!folded.has_value());
    assert(folded.error().message.find("mismatched compile-time call arity") != std::string::npos);
    assert(folded.error().message.find("assert_eq") != std::string::npos);
    assert(folded.error().message.find("expected 2 argument(s), got 3") != std::string::npos);
    assert(folded.error().stack.empty());
}

static void test_intrinsic_decl_arity_mismatch_reports_error() {
    SymbolSession session;
    cstc::tyir_interp::detail::ProgramView program;
    cstc::tyir_interp::detail::EvalContext ctx{
        program,
        {},
        cstc::tyir_interp::detail::kDefaultEvalStepBudget,
        cstc::tyir_interp::detail::kDefaultEvalCallDepth,
        {},
    };
    TyExternFnDecl decl;
    decl.name = Symbol::intern("assert_eq");
    decl.link_name = Symbol::intern("cstc_std_assert_eq");
    decl.params.resize(1);

    auto result = cstc::tyir_interp::detail::eval_lang_intrinsic(
        decl, {cstc::tyir_interp::detail::make_num(1)}, ctx, {});
    assert(!result.has_value());
    assert(result.error().message.find("mismatched compile-time call arity") != std::string::npos);
    assert(result.error().message.find("assert_eq") != std::string::npos);
    assert(result.error().message.find("expected 2 argument(s), got 1") != std::string::npos);
    assert(result.error().stack.empty());
}

static void test_recursive_const_eval_reports_call_depth_error() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
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
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
fn main() -> ! {
    loop {}
}
)");

    assert(error.message.find("const-eval step budget exhausted") != std::string::npos);
    assert(error.stack.empty());
}

static void test_infinite_while_reports_step_budget_error() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
fn main() {
    while true {}
}
)");

    assert(error.message.find("const-eval step budget exhausted") != std::string::npos);
    assert(error.stack.empty());
}

static void test_infinite_for_reports_step_budget_error() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
fn main() {
    for (;;) {}
}
)");

    assert(error.message.find("const-eval step budget exhausted") != std::string::npos);
    assert(error.stack.empty());
}

static void test_bare_break_in_for_still_folds_to_unit() {
    SymbolSession session;
    const auto program = must_fold_with_constraint_prelude(R"(
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

static void test_numeric_equality_treats_nan_as_not_equal() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    assert(!cstc::tyir_interp::detail::values_equal(
        cstc::tyir_interp::detail::make_num(nan), cstc::tyir_interp::detail::make_num(nan)));
}

static void test_generic_where_true_allows_instantiation() {
    SymbolSession session;
    const auto program = must_fold_with_constraint_prelude(R"(
fn always_true() -> bool {
    true
}

fn id<T>(value: T) -> T where always_true() {
    value
}

fn main() -> num {
    id(1)
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Num);
    assert(literal.symbol.as_str() == std::string_view{"1"});
}

static void test_generic_where_false_reports_constraint_failure() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
fn always_false() -> bool {
    false
}

fn id<T>(value: T) -> T where always_false() {
    value
}

fn main() -> num {
    id(1)
}
)");

    assert(error.message.find("generic constraint failed") != std::string::npos);
    assert(error.message.find("function 'id'") != std::string::npos);
}

static void test_explicit_constraint_invalid_reports_constraint_failure() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
fn id<T>(value: T) -> T where Constraint::Invalid {
    value
}

fn main() -> num {
    id(1)
}
)");

    assert(error.message.find("generic constraint failed") != std::string::npos);
}

static void test_generic_where_parameter_references_are_rejected_while_lowering() {
    SymbolSession session;
    const auto error = must_fail_to_lower_with_constraint_prelude(R"(
fn id<T>(value: T) -> T where value == value {
    value
}

fn main() -> num {
    id(1)
}
)");

    assert(
        error.message.find("function where clauses cannot reference parameter 'value'")
        != std::string::npos);
}

static void test_generic_where_runtime_call_reports_runtime_only() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
runtime fn runtime_true() -> bool {
    true
}

fn id<T>(value: T) -> T where runtime_true() {
    value
}

fn main() -> num {
    id(1)
}
)");

    assert(error.message.find("runtime-only behavior") != std::string::npos);
    assert(error.message.find("function 'id'") != std::string::npos);
}

static void test_generic_where_runtime_loop_reports_runtime_only() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
runtime fn runtime_true() -> bool {
    true
}

fn loop_true() -> bool {
    loop {
        runtime_true();
    }
}

fn id<T>(value: T) -> T where loop_true() {
    value
}

fn main() -> num {
    id(1)
}
)");

    assert(error.message.find("runtime-only behavior") != std::string::npos);
    assert(error.message.find("function 'id'") != std::string::npos);
}

static void test_generic_where_runtime_while_reports_runtime_only() {
    SymbolSession session;
    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
runtime fn runtime_true() -> bool {
    true
}

fn while_true() -> bool {
    while true {
        runtime_true()
    }
    false
}

fn id<T>(value: T) -> T where while_true() {
    value
}

fn main() -> num {
    id(1)
}
)");

    assert(error.message.find("runtime-only behavior") != std::string::npos);
    assert(error.message.find("function 'id'") != std::string::npos);
}

static void test_unused_generic_where_is_deferred_until_instantiation() {
    SymbolSession session;
    const auto program = must_fold_with_constraint_prelude(R"(
fn always_false<T>() -> bool {
    false
}

fn id<T>(value: T) -> T where always_false::<T>() {
    value
}

fn main() -> num {
    0
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Num);
    assert(literal.symbol.as_str() == std::string_view{"0"});
}

static void test_generic_call_constraint_is_deferred_inside_generic_body() {
    SymbolSession session;
    const auto program = must_fold_with_constraint_prelude(R"(
fn always_false<T>() -> bool {
    false
}

fn constrained<T>(value: T) -> T where always_false::<T>() {
    value
}

fn wrapper<T>(value: T) -> T {
    constrained::<T>(value)
}

fn main() -> num {
    0
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Num);
    assert(literal.symbol.as_str() == std::string_view{"0"});

    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
fn always_false<T>() -> bool {
    false
}

fn constrained<T>(value: T) -> T where always_false::<T>() {
    value
}

fn wrapper<T>(value: T) -> T {
    constrained::<T>(value)
}

fn main() -> num {
    wrapper(1)
}
)");

    assert(error.message.find("generic constraint failed") != std::string::npos);
    assert(error.message.find("function 'constrained'") != std::string::npos);
}

static void test_generic_struct_constraint_is_deferred_inside_generic_body() {
    SymbolSession session;
    const auto program = must_fold_with_constraint_prelude(R"(
fn always_false<T>() -> bool {
    false
}

struct Box<T> where always_false::<T>() {
    value: T
}

fn make_box<T>(value: T) -> Box<T> {
    Box<T> { value: value }
}

fn main() -> num {
    0
}
)");

    const TyLiteral& literal = require_literal(require_tail(find_fn(program, "main")));
    assert(literal.kind == TyLiteral::Kind::Num);
    assert(literal.symbol.as_str() == std::string_view{"0"});

    const auto error = must_fail_to_fold_with_constraint_prelude(R"(
fn always_false<T>() -> bool {
    false
}

struct Box<T> where always_false::<T>() {
    value: T
}

fn make_box<T>(value: T) -> Box<T> {
    Box<T> { value: value }
}

fn main() -> num {
    let b: Box<num> = make_box(1);
    b.value
}
)");

    assert(error.message.find("generic constraint failed") != std::string::npos);
    assert(error.message.find("type 'Box'") != std::string::npos);
}

int main() {
    test_const_function_call_folds_to_literal();
    test_runtime_call_remains_in_tyir();
    test_short_circuit_boolean_ops_do_not_eval_dead_rhs();
    test_move_only_local_and_borrow_can_fold();
    test_move_only_return_materializes_owned_string();
    test_generic_struct_materialization_substitutes_field_types();
    test_string_intrinsics_fold_to_owned_string();
    test_assert_intrinsics_fold_to_unit();
    test_borrowed_str_free_intrinsic_is_noop();
    test_constraint_intrinsic_returns_constraint_enum();
    test_borrow_of_folded_owned_string_keeps_folded_rhs();
    test_borrow_preserves_folded_rhs_when_ref_cannot_materialize();
    test_runtime_intrinsic_call_is_preserved();
    test_dead_if_body_is_not_folded();
    test_dead_while_body_is_not_folded();
    test_dead_for_body_and_step_are_not_folded();
    test_dead_for_still_folds_reachable_init();
    test_dead_stmt_after_return_is_not_folded();
    test_dead_tail_after_break_is_not_folded();
    test_dead_tail_after_continue_is_not_folded();
    test_reached_assertion_failure_reports_error();
    test_error_stack_records_called_function();
    test_assert_eq_requires_exact_runtime_equality();
    test_malformed_const_function_call_reports_arity_error();
    test_malformed_lang_call_reports_arity_error();
    test_intrinsic_decl_arity_mismatch_reports_error();
    test_recursive_const_eval_reports_call_depth_error();
    test_infinite_loop_reports_step_budget_error();
    test_infinite_while_reports_step_budget_error();
    test_infinite_for_reports_step_budget_error();
    test_bare_break_in_for_still_folds_to_unit();
    test_break_value_in_for_reports_const_eval_error();
    test_materialization_mismatch_reports_error();
    test_null_materialization_reports_error();
    test_comparison_requires_numeric_operands();
    test_numeric_equality_treats_nan_as_not_equal();
    test_generic_where_true_allows_instantiation();
    test_generic_where_false_reports_constraint_failure();
    test_explicit_constraint_invalid_reports_constraint_failure();
    test_generic_where_parameter_references_are_rejected_while_lowering();
    test_generic_where_runtime_call_reports_runtime_only();
    test_generic_where_runtime_loop_reports_runtime_only();
    test_generic_where_runtime_while_reports_runtime_only();
    test_unused_generic_where_is_deferred_until_instantiation();
    test_generic_call_constraint_is_deferred_inside_generic_body();
    test_generic_struct_constraint_is_deferred_inside_generic_body();
    return 0;
}
