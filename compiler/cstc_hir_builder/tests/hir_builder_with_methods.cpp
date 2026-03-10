#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn length(point: Point) -> i32 { point.x }
fn shift(point: Point, dx: i32) -> i32 { dx }
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("fn length(point: Point) -> i32") != std::string::npos);
    assert(hir.find("fn shift(point: Point, dx: i32) -> i32") != std::string::npos);
    assert(hir.find("length::body") != std::string::npos);
    assert(hir.find("shift::body") != std::string::npos);
    return 0;
}
