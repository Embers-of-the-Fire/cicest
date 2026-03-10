#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn validate<T>(value: T) -> T
    where decl(Vec<T>)
{
    value
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("fn validate<T>(value: T) -> T") != std::string::npos);
    assert(hir.find("decl_valid(Vec<T>)") != std::string::npos);
    return 0;
}
