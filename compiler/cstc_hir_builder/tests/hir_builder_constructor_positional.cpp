#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn make_some() -> Option<i32> {
    Some(1)
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("Some(1)") != std::string::npos);
    return 0;
}

