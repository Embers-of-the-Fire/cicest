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
        .kind = PathType{
            .segments = {std::move(name)},
            .args = {},
        },
    };
}

Type contract_type(TypeContractKind kind, Type inner) {
    return Type{
        .kind = ContractType{
            .kind = kind,
            .inner = make_type(std::move(inner.kind)),
        },
    };
}

} // namespace

int main() {
    Type nested_runtime = contract_type(
        TypeContractKind::Runtime,
        contract_type(TypeContractKind::Runtime, path_type("i32")));

    std::vector<FnParam> params;
    params.push_back(FnParam{
        .name = "value",
        .type = contract_type(TypeContractKind::Async, path_type("i32")),
    });

    std::vector<Declaration> declarations;
    declarations.push_back(Declaration{
        .header = FunctionDecl{
            .name = "normalize_contracts",
            .generic_params = {},
            .params = std::move(params),
            .return_type = std::move(nested_runtime),
        },
        .body = {},
        .constraints = {},
    });

    const Module module{
        .declarations = std::move(declarations),
    };

    const std::string printed = format_hir(module);
    const std::string expected =
        "fn normalize_contracts(value: async i32) -> runtime i32\n"
        "normalize_contracts::body {\n"
        "}\n"
        "normalize_contracts::constraint {\n"
        "}\n";

    assert(printed == expected);
    return 0;
}
