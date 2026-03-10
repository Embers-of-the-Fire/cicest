#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn type_forms(a: &i32, b: i32, c: fn(i32) -> bool, d: _) -> _ {
    a
}

fn runtime_flatten() -> runtime runtime i32 {
    0
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(
        hir.find("fn type_forms(a: &i32, b: i32, c: fn(i32) -> bool, d: _) -> _")
        != std::string::npos);
    assert(hir.find("fn runtime_flatten() -> runtime i32") != std::string::npos);
    return 0;
}
