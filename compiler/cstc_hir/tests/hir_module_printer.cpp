#include <cassert>
#include <string>
#include <vector>

#include <cstc_hir/hir.hpp>
#include <cstc_hir/printer.hpp>

using namespace cstc::hir;

namespace {

Type i32_type() {
    return Type{
        .kind =
            PathType{
                     .segments = {"i32"},
                     .args = {},
                     },
    };
}

} // namespace

int main() {
    std::vector<Declaration> declarations;
    declarations.push_back(
        Declaration{
            .header =
                ImportDecl{
                           .source = "\"./math.cst\"",
                           .specifiers = {
                               ImportSpecifier{
                                   .imported_name = "foo",
                                   .local_name = "foo",
                                   .has_alias = false,
                               },
                               ImportSpecifier{
                                   .imported_name = "bar",
                                   .local_name = "baz",
                                   .has_alias = true,
                               },
                           },
                           },
            .body = {},
            .constraints = {},
    });

    declarations.push_back(
        Declaration{
            .header =
                FunctionDecl{
                             .name = "main",
                             .generic_params = {},
                             .params = {},
                             .return_type = i32_type(),
                             .is_exported = true,
                             },
            .body = {},
            .constraints = {},
    });

    const Module module{
        .declarations = std::move(declarations),
    };

    const std::string printed = format_hir(module);
    const std::string expected = "import { foo, bar as baz } from \"./math.cst\"\n"
                                 "\n"
                                 "export fn main() -> i32\n"
                                 "main::body {\n"
                                 "}\n"
                                 "main::constraint {\n"
                                 "}\n";

    assert(printed == expected);
    return 0;
}
