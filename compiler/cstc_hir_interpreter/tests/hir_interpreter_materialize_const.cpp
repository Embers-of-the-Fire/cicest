#include <cassert>
#include <string>
#include <variant>

#include <cstc_hir/hir.hpp>
#include <cstc_hir_interpreter/interpreter.hpp>

#include "support.hpp"

int main() {
    const std::string source = R"(
const fn answer() -> i32 {
    40 + 2
}
)";

    auto module = lower_source_to_hir_module(source);

    auto* function = std::get_if<cstc::hir::FunctionDecl>(&module.declarations[0].header);
    assert(function != nullptr);
    assert(std::holds_alternative<cstc::hir::ContractType>(function->return_type.kind));

    cstc::hir::interpreter::HirInterpreter interpreter{module};
    const auto materialized = interpreter.materialize_const_types();

    assert(materialized.ok);
    assert(materialized.lifted_constants.contains("answer"));
    assert(cstc::hir::interpreter::format_value(materialized.lifted_constants.at("answer")) == "42");

    function = std::get_if<cstc::hir::FunctionDecl>(&module.declarations[0].header);
    assert(function != nullptr);
    assert(std::holds_alternative<cstc::hir::PathType>(function->return_type.kind));

    return 0;
}
