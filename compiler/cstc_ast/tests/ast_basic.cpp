#include <cassert>
#include <memory>
#include <utility>
#include <variant>

#include <cstc_ast/ast.hpp>

using namespace cstc::ast;
using cstc::span::SourceSpan;

/// Build a minimal AST for: `fn main() -> i32 { 42 }`
static void test_fn_main() {
    NodeIdAllocator ids;
    SymbolTable syms;

    const auto main_sym = syms.intern("main");
    const auto i32_sym = syms.intern("i32");

    // --- Return type: i32 ---
    auto ret_ty = std::make_unique<TypeNode>(TypeNode{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .kind = PathType{
            .path = Path{
                .span = SourceSpan{0, 0},
                .segments = {PathSegment{.span = SourceSpan{0, 0}, .name = i32_sym}},
            },
            .args = std::nullopt,
        },
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

    // Return type is i32.
    assert(std::holds_alternative<PathType>(fn.sig.ret_ty->kind));
    const auto& ret = std::get<PathType>(fn.sig.ret_ty->kind);
    assert(ret.path.segments.size() == 1);
    assert(ret.path.segments[0].name == i32_sym);
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
        .kind = KeywordKind::Runtime,
        .type_var = std::nullopt,
    };
    assert(km.kind == KeywordKind::Runtime);
    assert(!km.type_var.has_value());
}

static void test_for_and_decl_expr_nodes() {
    NodeIdAllocator ids;
    SymbolTable syms;

    const auto vec_sym = syms.intern("Vec");
    auto decl_type = std::make_unique<TypeNode>(TypeNode{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .kind =
            PathType{
                           .path =
                    Path{
                        .span = SourceSpan{0, 0},
                        .segments = {PathSegment{.span = SourceSpan{0, 0}, .name = vec_sym}},
                    }, .args = std::nullopt,
                           },
    });

    auto decl_expr = std::make_unique<Expr>(Expr{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .kind =
            DeclExpr{
                           .type_expr = std::move(decl_type),
                           },
    });

    auto init_expr = std::make_unique<Expr>(Expr{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .kind =
            LitExpr{
                           .lit =
                    Lit{
                        .span = SourceSpan{0, 0},
                        .kind = LitKind::Int,
                        .value = "0",
                    },
                           },
    });

    auto cond_expr = std::make_unique<Expr>(Expr{
        .id = ids.next(),
        .span = SourceSpan{0, 0},
        .kind = LitExpr{.lit = Lit{.span = SourceSpan{0, 0}, .kind = LitKind::Bool, .value = "true"}},
    });

    ForExpr for_expr{
        .init = std::move(init_expr),
        .cond = std::move(cond_expr),
        .step = std::nullopt,
        .body =
            Block{
                     .id = ids.next(),
                     .span = SourceSpan{0, 0},
                     .stmts = {},
                     },
    };

    assert(std::holds_alternative<DeclExpr>(decl_expr->kind));
    const auto& decl = std::get<DeclExpr>(decl_expr->kind);
    assert(std::holds_alternative<PathType>(decl.type_expr->kind));

    assert(for_expr.init.has_value());
    assert(for_expr.cond.has_value());
    assert(!for_expr.step.has_value());
}

int main() {
    test_fn_main();
    test_crate();
    test_keyword_modifiers();
    test_for_and_decl_expr_nodes();
    return 0;
}
