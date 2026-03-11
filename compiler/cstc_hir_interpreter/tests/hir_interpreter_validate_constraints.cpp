#include <cassert>
#include <string>

#include <cstc_hir_interpreter/interpreter.hpp>

#include "support.hpp"

int main() {
    const std::string valid_source = R"(
struct Vec<T>;

fn id<T>(value: T) -> T
    where decl(Vec<T>), sizeof(T) == 4
{
    value
}
)";

    auto valid_module = lower_source_to_hir_module(valid_source);
    cstc::hir::interpreter::HirInterpreter valid_interpreter{valid_module};
    const auto valid_result = valid_interpreter.validate();
    assert(valid_result.ok);

    const std::string invalid_source = R"(
fn bad<T>(value: T) -> T
    where decl(Missing<T>)
{
    value
}
)";

    auto invalid_module = lower_source_to_hir_module(invalid_source);
    cstc::hir::interpreter::HirInterpreter invalid_interpreter{invalid_module};
    const auto invalid_result = invalid_interpreter.validate();
    assert(!invalid_result.ok);

    bool has_missing_type_error = false;
    for (const auto& diagnostic : invalid_result.diagnostics) {
        if (diagnostic.message.find("Missing") != std::string::npos)
            has_missing_type_error = true;
    }

    assert(has_missing_type_error);
    return 0;
}
