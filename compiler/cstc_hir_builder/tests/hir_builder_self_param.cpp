#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn read(point: Point) -> i32 {
    point.x
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("fn read(point: Point) -> i32") != std::string::npos);
    return 0;
}
