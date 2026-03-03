#include <cassert>
#include <memory>
#include <utility>
#include <variant>

#include <cstc_ast/ast.hpp>

using namespace cstc::ast;
using cstc::span::SourceSpan;

/// Build a minimal AST for: `fn main() -> () { 42 }`
static void test_fn_main() {
    NodeIdAllocator ids;
    SymbolTable syms;

    const auto main_sym = syms.intern("main");

    // --- Return type: () ---
    auto ret_ty = std::make_unique<TypeNode>(TypeNode{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .kind = TupleType{.elements = {}},
    });

    // --- Body: { 42 } ---
    Lit lit_42{
        .span = SourceSpan{0, 0},
        .kind = LitKind::Int,
        .value = "42",
    };
    auto lit_expr = std::make_unique<Expr>(Expr{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .kind = LitExpr{.lit = std::move(lit_42)},
    });

    ExprStmt tail{.expr = std::move(lit_expr), .has_semi = false};
    Stmt tail_stmt{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .kind = std::move(tail),
    };

    std::vector<Stmt> stmts;
    stmts.push_back(std::move(tail_stmt));

    Block body{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .stmts = std::move(stmts),
    };

    // --- Function signature ---
    FnSig sig{
        .self_param = std::nullopt,
        .params = {},
        .ret_ty = std::move(ret_ty),
    };

    // --- FnItem ---
    FnItem fn_item{
        .keywords = {},
        .name = main_sym,
        .generics = Generics{},
        .sig = std::move(sig),
        .body = std::move(body),
    };

    Item item{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .kind = std::move(fn_item),
    };

    // --- Verify ---

    // NodeIds are sequential starting from 1 (0 is DUMMY_NODE_ID).
    assert(DUMMY_NODE_ID.id == 0);
    assert(item.id.id == 5);

    // Check variant access.
    assert(std::holds_alternative<FnItem>(item.kind));
    const auto& fn = std::get<FnItem>(item.kind);
    assert(fn.name == main_sym);
    assert(syms.str(fn.name) == "main");
    assert(fn.sig.params.empty());
    assert(fn.body.stmts.size() == 1);

    const auto& stmt = fn.body.stmts[0];
    assert(std::holds_alternative<ExprStmt>(stmt.kind));
    const auto& es = std::get<ExprStmt>(stmt.kind);
    assert(!es.has_semi);
    assert(std::holds_alternative<LitExpr>(es.expr->kind));
    const auto& le = std::get<LitExpr>(es.expr->kind);
    assert(le.lit.kind == LitKind::Int);
    assert(le.lit.value == "42");

    // Return type is a unit tuple.
    assert(std::holds_alternative<TupleType>(fn.sig.ret_ty->kind));
    const auto& tt = std::get<TupleType>(fn.sig.ret_ty->kind);
    assert(tt.elements.empty());
}

/// Test Crate construction.
static void test_crate() {
    Crate crate{.items = {}};
    assert(crate.items.empty());
}

/// Test keyword modifiers.
static void test_keyword_modifiers() {
    KeywordModifier km{
        .span = SourceSpan{0, 5},
        .kind = KeywordKind::Async,
        .type_var = std::nullopt,
    };
    assert(km.kind == KeywordKind::Async);
    assert(!km.type_var.has_value());
}

int main() {
    test_fn_main();
    test_crate();
    test_keyword_modifiers();
    return 0;
}
