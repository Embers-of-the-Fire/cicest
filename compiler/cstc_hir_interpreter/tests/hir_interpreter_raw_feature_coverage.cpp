#include <cassert>
#include <string>

#include <cstc_hir_interpreter/interpreter.hpp>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn identity<T>(value: T) -> T {
    value
}

fn main() -> i32 {
    for (; false; ) { return 0; };

    let point = Point { x: 40, y: 1 };
    let inc = lambda(value: i32) { value + 1 };

    if true {
        loop { return identity::<i32>(inc(point.x) + point.y); };
    } else {
        0
    }
}
)";

    auto module = lower_source_to_hir_module(source);
    cstc::hir::interpreter::HirInterpreter interpreter{module};

    const auto run_result = interpreter.run(cstc::hir::interpreter::RunOptions{
        .entry = "main",
        .mode = cstc::hir::interpreter::ExecutionMode::Runtime,
        .call_depth_limit = 256,
    });

    assert(run_result.ok);
    assert(run_result.value.has_value());
    assert(cstc::hir::interpreter::format_value(*run_result.value) == "42");
    return 0;
}
