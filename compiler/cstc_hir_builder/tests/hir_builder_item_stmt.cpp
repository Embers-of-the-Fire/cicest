#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn outer() -> i32 {
    fn inner() -> i32 { 0 }
    1
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("item inner") != std::string::npos);
    assert(hir.find("outer::body {") != std::string::npos);
    return 0;
}

