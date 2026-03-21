#include <cstc/linker_selection.hpp>

#include <array>
#include <cassert>
#include <string_view>

namespace {

using cstc::cli::LinkerFlavor;
using namespace std::literals;

void assert_linker_selection(
    LinkerFlavor flavor, const std::array<std::string_view, 3>& expected_candidates,
    std::string_view expected_fallback) {
    assert(cstc::cli::linker_candidates(flavor) == expected_candidates);
    assert(cstc::cli::fallback_linker_program(flavor) == expected_fallback);
}

} // namespace

int main() {
    assert_linker_selection(LinkerFlavor::Posix, {"c++"sv, "clang++"sv, "g++"sv}, "c++"sv);
    assert_linker_selection(LinkerFlavor::WindowsMinGW, {"c++"sv, "g++"sv, "clang++"sv}, "c++"sv);
    assert_linker_selection(
        LinkerFlavor::WindowsMSVC, {"clang++"sv, "c++"sv, "g++"sv}, "clang++"sv);
}
