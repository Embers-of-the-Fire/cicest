#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn body_stmts() -> i32 {
    let x: i32 = 1;
    let y = x + 1;
    y
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("let x: i32 = 1") != std::string::npos);
    assert(hir.find("let y = x + 1") != std::string::npos);
    assert(hir.find("body_stmts::body {\n  let x: i32 = 1\n  let y = x + 1\n  y\n}") != std::string::npos);
    return 0;
}

