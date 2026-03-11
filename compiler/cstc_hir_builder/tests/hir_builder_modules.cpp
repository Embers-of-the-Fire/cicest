#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
import { foo as local_foo, bar } from "./math.cst";

export fn main() -> i32 {
    local_foo(bar())
}
)";

    const std::string hir = lower_source_to_hir(source);
    const std::string expected = "import { foo as local_foo, bar } from \"./math.cst\"\n"
                                 "\n"
                                 "export fn main() -> i32\n"
                                 "main::body {\n"
                                 "  local_foo(bar())\n"
                                 "}\n"
                                 "main::constraint {\n"
                                 "}\n";

    assert(hir == expected);
    return 0;
}
