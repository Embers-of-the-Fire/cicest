#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn max<T>(a: T, b: T) -> T
    where sizeof(T) == 4, decl(Comparable<T>)
{
    a
}
)";

    const std::string hir = lower_source_to_hir(source);
    const std::string expected = "fn max<T>(a: T, b: T) -> T\n"
                                 "max::body {\n"
                                 "  a\n"
                                 "}\n"
                                 "max::constraint {\n"
                                 "  sizeof(T) == 4\n"
                                 "  decl_valid(Comparable<T>)\n"
                                 "}\n";

    assert(hir == expected);
    return 0;
}
