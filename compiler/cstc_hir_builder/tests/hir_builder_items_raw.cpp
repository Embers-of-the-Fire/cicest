#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
struct Marker;
struct Pair { left: i32, right: i32 }
enum Color {
    Red,
    Green,
    Blue,
}
extern fn puts(v: i32) -> i32;
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("struct Marker;") != std::string::npos);
    assert(hir.find("struct Pair { left: i32, right: i32 }") != std::string::npos);
    assert(hir.find("enum Color { Red, Green, Blue }") != std::string::npos);
    assert(hir.find("extern fn puts(v: i32) -> i32") != std::string::npos);
    return 0;
}
