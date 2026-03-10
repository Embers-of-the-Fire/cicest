#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
runtime fn run() -> i32 { 2 }
const fn frozen() -> runtime i32 { 4 }
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("fn run() -> runtime i32") != std::string::npos);
    assert(hir.find("fn frozen() -> !runtime runtime i32") != std::string::npos);
    return 0;
}
