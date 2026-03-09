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

static void test_parse_keyword_and_match() {
    constexpr std::string_view source = R"(
async fn build() -> i32 {
    let point = Point { x: 1, y: 2 };
    match point {
        Point { x: x, y: _ } => x,
    }
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
    assert(fn.keywords.size() == 1);
    assert(fn.keywords.front().kind == KeywordKind::Async);
    assert(fn.body.stmts.size() == 2);

    const auto& let_stmt = std::get<LetStmt>(fn.body.stmts[0].kind);
    assert(let_stmt.init.has_value());
    assert(std::holds_alternative<ConstructorFieldsExpr>((*let_stmt.init)->kind));

    const auto& tail_stmt = std::get<ExprStmt>(fn.body.stmts[1].kind);
    assert(!tail_stmt.has_semi);
    assert(std::holds_alternative<MatchExpr>(tail_stmt.expr->kind));

    const auto& match_expr = std::get<MatchExpr>(tail_stmt.expr->kind);
    assert(match_expr.arms.size() == 1);
    assert(std::holds_alternative<ConstructorFieldsPat>(match_expr.arms[0].pat->kind));
}

static void test_parse_concept_with_and_intrinsic() {
    constexpr std::string_view source = R"(
concept Comparable<T> {
    fn compare(lhs: T, rhs: T) -> i32;
}

with Point {
    fn length(self: Point) -> i32 { 0 }
}

fn max<T>(a: T, b: T) -> T
    where concept(Comparable::<T>)
{
    a
}
)";

    SymbolTable symbols;
    const auto parsed = parse_source(source, symbols);
    assert(parsed.has_value());

    const auto& crate = parsed.value();
    assert(crate.items.size() == 3);

    const auto& concept_item = crate.items[0];
    assert(std::holds_alternative<ConceptItem>(concept_item.kind));
    const auto& concept_data = std::get<ConceptItem>(concept_item.kind);
    assert(symbols.str(concept_data.name) == "Comparable");
    assert(concept_data.methods.size() == 1);

    const auto& with_item = crate.items[1];
    assert(std::holds_alternative<WithItem>(with_item.kind));
    const auto& with = std::get<WithItem>(with_item.kind);
    assert(with.methods.size() == 1);
    assert(symbols.str(with.methods[0].name) == "length");

    const auto& fn_item = crate.items[2];
    assert(std::holds_alternative<FnItem>(fn_item.kind));
    const auto& fn = std::get<FnItem>(fn_item.kind);
    assert(fn.generics.where_clause.has_value());
    assert(fn.generics.where_clause->predicates.size() == 1);
    assert(std::holds_alternative<CallExpr>(fn.generics.where_clause->predicates[0].expr->kind));
}

static void test_assignment_expression_is_rejected() {
    constexpr std::string_view source = R"(
fn main() -> () {
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
    test_parse_keyword_and_match();
    test_parse_concept_with_and_intrinsic();
    test_assignment_expression_is_rejected();
    return 0;
}
