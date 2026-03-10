#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include <cstc_hir/hir.hpp>
#include <cstc_hir/printer.hpp>

using namespace cstc::hir;

namespace {

Type path_type(std::string name) {
    return Type{
        .kind =
            PathType{
                     .segments = {std::move(name)},
                     .args = {},
                     },
    };
}

ExprPtr raw_expr(std::string text) { return make_expr(RawExpr{.text = std::move(text)}); }

ExprPtr path_expr(std::string name) { return make_expr(PathExpr{.segments = {std::move(name)}}); }

ExprPtr literal_expr(std::string text) { return make_expr(LiteralExpr{.text = std::move(text)}); }

} // namespace

int main() {
    std::vector<ExprPtr> body;
    body.push_back(make_expr(
        LiftedConstantExpr{
            .name = "cmp_size",
            .type = path_type("i32"),
            .value = "sizeof(T)",
        }));

    {
        std::vector<ExprPtr> args;
        args.push_back(path_expr("b"));

        body.push_back(make_expr(
            StaticMemberCallExpr{
                .receiver_type = path_type("T"),
                .member = "compare",
                .receiver = path_expr("a"),
                .args = std::move(args),
            }));
    }

    body.push_back(make_expr(
        MemberAccessExpr{
            .receiver = path_expr("a"),
            .member = "point",
        }));

    {
        std::vector<ExprPtr> args;
        args.push_back(literal_expr("1"));

        body.push_back(make_expr(
            MemberCallExpr{
                .receiver = path_expr("a"),
                .member = "advance",
                .args = std::move(args),
            }));
    }

    {
        std::vector<ExprPtr> fetch_args;
        fetch_args.push_back(path_expr("a"));

        body.push_back(make_expr(
            CallExpr{
                .callee = path_expr("fetch"),
                .args = std::move(fetch_args),
            }));
    }

    {
        std::vector<ExprPtr> block_body;
        {
            std::vector<ExprPtr> stage_args;
            stage_args.push_back(path_expr("a"));

            block_body.push_back(make_expr(
                CallExpr{
                    .callee = path_expr("stage"),
                    .args = std::move(stage_args),
                }));
        }

        body.push_back(make_expr(
            ContractBlockExpr{
                .kind = ContractBlockKind::Runtime,
                .body = std::move(block_body),
            }));
    }

    std::vector<ExprPtr> constraints;
    constraints.push_back(raw_expr("sizeof(T) == 4"));
    constraints.push_back(make_expr(
        DeclConstraintExpr{
            .checked_type = path_type("Comparable"),
        }));

    std::vector<FnParam> max_params;
    max_params.push_back(FnParam{.name = "a", .type = path_type("T")});
    max_params.push_back(FnParam{.name = "b", .type = path_type("T")});

    std::vector<Declaration> declarations;
    declarations.push_back(
        Declaration{
            .header =
                FunctionDecl{
                             .name = "max",
                             .generic_params = {"T"},
                             .params = std::move(max_params),
                             .return_type = path_type("T"),
                             },
            .body = std::move(body),
            .constraints = std::move(constraints),
    });

    std::vector<FnParam> apply_params;
    {
        Type fn_ptr_arg = path_type("i32");
        Type fn_ptr_ret = path_type("i32");

        FnPointerType fn_ptr_type;
        fn_ptr_type.params.push_back(make_type(std::move(fn_ptr_arg.kind)));
        fn_ptr_type.result = make_type(std::move(fn_ptr_ret.kind));

        Type callback_type{
            .kind = std::move(fn_ptr_type),
        };

        apply_params.push_back(FnParam{.name = "cb", .type = std::move(callback_type)});
    }

    declarations.push_back(
        Declaration{
            .header =
                FunctionDecl{
                             .name = "apply",
                             .generic_params = {},
                             .params = std::move(apply_params),
                             .return_type = path_type("i32"),
                             },
            .body = {},
            .constraints = {},
    });

    declarations.push_back(
        Declaration{
            .header =
                RawDecl{
                        .name = "noop",
                        .text = "fn noop() -> i32",
                        },
            .body = { },
 .constraints = {                             },
    });

    const Module module{
        .declarations = std::move(declarations),
    };

    const std::string printed = format_hir(module);
    const std::string expected = "fn max<T>(a: T, b: T) -> T\n"
                                 "max::body {\n"
                                 "  lifted cmp_size: i32 = sizeof(T)\n"
                                 "  T::compare(a, b)\n"
                                 "  member_access(a, point)\n"
                                 "  member_call(a, advance, 1)\n"
                                 "  fetch(a)\n"
                                 "  runtime {\n"
                                 "    stage(a)\n"
                                 "  }\n"
                                 "}\n"
                                 "max::constraint {\n"
                                 "  sizeof(T) == 4\n"
                                 "  decl_valid(Comparable)\n"
                                 "}\n"
                                 "\n"
                                 "fn apply(cb: fn(i32) -> i32) -> i32\n"
                                 "apply::body {\n"
                                 "}\n"
                                 "apply::constraint {\n"
                                 "}\n"
                                 "\n"
                                 "fn noop() -> i32\n"
                                 "noop::body {\n"
                                 "}\n"
                                 "noop::constraint {\n"
                                 "}\n";

    assert(printed == expected);
    return 0;
}
