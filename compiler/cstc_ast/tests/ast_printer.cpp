#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_ast/printer.hpp>

using namespace cstc::ast;
using cstc::span::SourceSpan;

int main() {
    NodeIdAllocator ids;
    SymbolTable symbols;

    const auto main_sym = symbols.intern("main");
    const auto i32_sym = symbols.intern("i32");

    auto return_type = std::make_unique<TypeNode>(TypeNode{
        .id = ids.next(),
        .span = SourceSpan{.start = 0, .end = 0},
        .kind =
            PathType{
                           .path =
                    Path{
                        .span = SourceSpan{.start = 0, .end = 0},
                        .segments = {PathSegment{
                            .span = SourceSpan{.start = 0, .end = 0},
                            .name = i32_sym,
                        }},
                    }, .args = std::nullopt,
                           },
    });

    auto literal_expr = std::make_unique<Expr>(Expr{
        .id = ids.next(),
        .span = SourceSpan{.start = 0, .end = 0},
        .kind =
            LitExpr{
                           .lit =
                    Lit{
                        .span = SourceSpan{.start = 0, .end = 0},
                        .kind = LitKind::Int,
                        .value = "42",
                    }},
    });

    auto expr_stmt = Stmt{
        .id = ids.next(),
        .span = SourceSpan{.start = 0, .end = 0},
        .kind =
            ExprStmt{
                           .expr = std::move(literal_expr),
                           .has_semi = false,
                           },
    };

    std::vector<Stmt> stmts;
    stmts.push_back(std::move(expr_stmt));

    Block body{
        .id = ids.next(),
        .span = SourceSpan{.start = 0, .end = 0},
        .stmts = std::move(stmts),
    };

    Item fn_item{
        .id = ids.next(),
        .span = SourceSpan{.start = 0, .end = 0},
        .kind =
            FnItem{
                           .keywords = {},
                           .name = main_sym,
                           .generics = Generics{},
                           .sig =
                    FnSig{
                        .self_param = std::nullopt,
                        .params = {},
                        .ret_ty = std::move(return_type),
                    }, .body = std::move(body),
                           },
    };

    std::vector<Item> items;
    items.push_back(std::move(fn_item));

    Crate crate{.items = std::move(items)};

    const std::string printed = format_ast(crate, &symbols);

    assert(!printed.empty());
    assert(printed.find("crate") != std::string::npos);
    assert(printed.find("item fn main") != std::string::npos);
    assert(printed.find("expr lit int 42") != std::string::npos);

    return 0;
}
