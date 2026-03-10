#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn fallback(x: i32) -> i32 {
    let c = Point { x: 1, y: 2 };
    if x > 0 { x } else { 0 };
    for (; x > 0; x - 1) { x; };
    loop { return x; };
    identity::<i32>(x)
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("let c = Point { x: 1, y: 2 }") != std::string::npos);
    assert(hir.find("if x > 0 { x } else { 0 };") != std::string::npos);
    assert(hir.find("for (; x > 0; x - 1) { x; };") != std::string::npos);
    assert(hir.find("loop { return x; };") != std::string::npos);
    assert(hir.find("identity::<i32>(x)") != std::string::npos);
    return 0;
}
