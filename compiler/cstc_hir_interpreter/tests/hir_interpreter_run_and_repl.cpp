#include <cassert>
#include <string>

#include <cstc_hir_interpreter/interpreter.hpp>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn add(a: i32, b: i32) -> i32 {
    a + b
}

fn main() -> i32 {
    let x: i32 = add(20, 22);
    x
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

    const auto repl_assign = interpreter.eval_repl_line("let y = add(1, 2)");
    assert(repl_assign.ok);

    const auto repl_eval = interpreter.eval_repl_line("y + 10");
    assert(repl_eval.ok);
    assert(repl_eval.value.has_value());
    assert(cstc::hir::interpreter::format_value(*repl_eval.value) == "13");

    return 0;
}
