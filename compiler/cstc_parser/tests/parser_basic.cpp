#include <cassert>
#include <string_view>
#include <variant>

#include <cstc_parser/parser.hpp>

using namespace cstc::ast;
using namespace cstc::parser;

static void test_parse_main_function() {
    constexpr std::string_view source = R"(
fn main() -> i32 {
    let x = 1;
    x
}
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(parsed.has_value());

    const auto& crate = parsed.value();
    assert(crate.items.size() == 1);

    const auto& item = crate.items.front();
    assert(std::holds_alternative<FnItem>(item.kind));

    const auto& fn = std::get<FnItem>(item.kind);
    assert(symbols.str(fn.name) == "main");
    assert(fn.sig.params.empty());
    assert(fn.body.stmts.size() == 2);

    const auto& let_stmt = fn.body.stmts[0];
    assert(std::holds_alternative<LetStmt>(let_stmt.kind));

    const auto& let_data = std::get<LetStmt>(let_stmt.kind);
    assert(let_data.init.has_value());
    assert(std::holds_alternative<BindingPat>(let_data.pat->kind));
    assert(std::holds_alternative<LitExpr>((*let_data.init)->kind));

    const auto& tail_stmt = fn.body.stmts[1];
    assert(std::holds_alternative<ExprStmt>(tail_stmt.kind));
    const auto& tail_expr_stmt = std::get<ExprStmt>(tail_stmt.kind);
    assert(!tail_expr_stmt.has_semi);
    assert(std::holds_alternative<PathExpr>(tail_expr_stmt.expr->kind));
}

static void test_parse_for_and_decl_intrinsic() {
    constexpr std::string_view source = R"(
fn run<T>() -> i32
    where decl(Vec<T>)
{
    for (; true; ) {
        1;
    };
    0
}
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(parsed.has_value());

    const auto& crate = parsed.value();
    assert(crate.items.size() == 1);

    const auto& item = crate.items.front();
    assert(std::holds_alternative<FnItem>(item.kind));

    const auto& fn = std::get<FnItem>(item.kind);
    assert(fn.generics.where_clause.has_value());
    assert(fn.generics.where_clause->predicates.size() == 1);
    assert(std::holds_alternative<DeclExpr>(fn.generics.where_clause->predicates[0].expr->kind));

    assert(fn.body.stmts.size() == 2);

    const auto& for_stmt = std::get<ExprStmt>(fn.body.stmts[0].kind);
    assert(for_stmt.has_semi);
    assert(std::holds_alternative<ForExpr>(for_stmt.expr->kind));

    const auto& for_expr = std::get<ForExpr>(for_stmt.expr->kind);
    assert(!for_expr.init.has_value());
    assert(for_expr.cond.has_value());
    assert(!for_expr.step.has_value());
    assert(for_expr.body.stmts.size() == 1);

    const auto& tail_stmt = std::get<ExprStmt>(fn.body.stmts[1].kind);
    assert(!tail_stmt.has_semi);
    assert(std::holds_alternative<LitExpr>(tail_stmt.expr->kind));
}

static void test_parse_constructor_fields_still_supported() {
    constexpr std::string_view source = R"(
fn build() -> i32 {
    let point = Point { x: 1, y: 2 };
    1
}
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(parsed.has_value());

    const auto& crate = parsed.value();
    assert(crate.items.size() == 1);

    const auto& item = crate.items.front();
    assert(std::holds_alternative<FnItem>(item.kind));

    const auto& fn = std::get<FnItem>(item.kind);
    assert(fn.body.stmts.size() == 2);

    const auto& let_stmt = std::get<LetStmt>(fn.body.stmts[0].kind);
    assert(let_stmt.init.has_value());
    assert(std::holds_alternative<ConstructorFieldsExpr>((*let_stmt.init)->kind));
}

static void test_parse_function_pointer_type() {
    constexpr std::string_view source = R"(
fn takes_callback(cb: fn(i32, bool)->i32) -> i32 {
    0
}
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(parsed.has_value());

    const auto& crate = parsed.value();
    assert(crate.items.size() == 1);

    const auto& item = crate.items.front();
    assert(std::holds_alternative<FnItem>(item.kind));

    const auto& fn = std::get<FnItem>(item.kind);
    assert(fn.sig.params.size() == 1);

    const auto& param_type = fn.sig.params.front().ty;
    assert(std::holds_alternative<FnPointerType>(param_type->kind));

    const auto& fn_ptr = std::get<FnPointerType>(param_type->kind);
    assert(fn_ptr.params.size() == 2);
    assert(std::holds_alternative<PathType>(fn_ptr.ret->kind));
}

static void test_parse_lambda_without_capture() {
    constexpr std::string_view source = R"(
fn make() -> i32 {
    let f = lambda(x: i32) {
        x + 1
    };
    0
}
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(parsed.has_value());

    const auto& crate = parsed.value();
    assert(crate.items.size() == 1);

    const auto& fn = std::get<FnItem>(crate.items.front().kind);
    assert(fn.body.stmts.size() == 2);

    const auto& let_stmt = std::get<LetStmt>(fn.body.stmts[0].kind);
    assert(let_stmt.init.has_value());
    assert(std::holds_alternative<LambdaExpr>((*let_stmt.init)->kind));

    const auto& lambda_expr = std::get<LambdaExpr>((*let_stmt.init)->kind);
    assert(lambda_expr.params.size() == 1);
}

static void test_lambda_capture_is_rejected() {
    constexpr std::string_view source = R"(
fn make() -> i32 {
    let y = 41;
    let f = lambda(x: i32) {
        x + y
    };
    0
}
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(!parsed.has_value());
}

static void test_lambda_may_call_global_function() {
    constexpr std::string_view source = R"(
fn id(x: i32) -> i32 {
    x
}

fn make() -> i32 {
    let f = lambda(x: i32) {
        id(x)
    };
    0
}
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(parsed.has_value());
}

static void test_removed_features_are_rejected() {
    constexpr std::string_view source = R"(
type Number = i32;
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(!parsed.has_value());

    const auto match_source = R"(
fn main() -> i32 {
    match 1 {
        1 => 1,
        _ => 0,
    }
}
)";
    assert(!parse_source(match_source, symbols).has_value());

    const auto with_source = R"(
with Point {
    fn read(self: Point) -> i32 { 0 }
}
)";
    assert(!parse_source(with_source, symbols).has_value());

    const auto tuple_struct_source = R"(
struct Pair(i32, i32);
)";
    assert(!parse_source(tuple_struct_source, symbols).has_value());

    const auto tuple_expr_source = R"(
fn main() -> i32 {
    let v = (1, 2);
    0
}
)";
    assert(!parse_source(tuple_expr_source, symbols).has_value());

    const auto method_source = R"(
fn main(p: Point) -> i32 {
    p.advance(1)
}
)";
    assert(!parse_source(method_source, symbols).has_value());
}

static void test_assignment_expression_is_rejected() {
    constexpr std::string_view source = R"(
fn main() -> i32 {
    let x: i32 = 1;
    x = 2;
}
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(!parsed.has_value());
}

int main() {
    test_parse_main_function();
    test_parse_for_and_decl_intrinsic();
    test_parse_constructor_fields_still_supported();
    test_parse_function_pointer_type();
    test_parse_lambda_without_capture();
    test_lambda_capture_is_rejected();
    test_lambda_may_call_global_function();
    test_removed_features_are_rejected();
    test_assignment_expression_is_rejected();
    return 0;
}
