#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn field_access(p: Point) -> i32 {
    p.x
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("fn field_access(p: Point) -> i32") != std::string::npos);
    assert(hir.find("member_access(p, x)") != std::string::npos);
    return 0;
}
