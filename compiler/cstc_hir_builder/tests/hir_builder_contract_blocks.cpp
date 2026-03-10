#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn runtime_block() -> i32 {
    runtime { 3 }
}

fn const_block() -> i32 {
    const { 4 }
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("fn runtime_block() -> i32") != std::string::npos);
    assert(hir.find("runtime {\n    3\n  }") != std::string::npos);

    assert(hir.find("fn const_block() -> i32") != std::string::npos);
    assert(hir.find("const {\n    4\n  }") != std::string::npos);
    return 0;
}
