#include <cassert>
#include <string>

#include "support.hpp"

int main() {
    const std::string source = R"(
fn first<T>(value: Vec<T>) -> Vec<T>
    where sizeof(T) == 4, decl(Comparable<T>)
{
    value
}
)";

    const std::string hir = lower_source_to_hir(source);

    assert(hir.find("fn first<T>(value: Vec<T>) -> Vec<T>") != std::string::npos);
    assert(hir.find("first::constraint {") != std::string::npos);
    assert(hir.find("sizeof(T) == 4") != std::string::npos);
    assert(hir.find("decl_valid(Comparable<T>)") != std::string::npos);
    return 0;
}
