#include <cassert>
#include <filesystem>
#include <string>

#include <cstc_resource_path/resource_path.hpp>

namespace {

void test_rt_library_filename_is_configured() {
    const std::string filename = cstc::resource_path::rt_library_filename;
    assert(!filename.empty());
}

void test_self_exe_dir_resolves_current_binary_directory() {
    const auto exe_dir = cstc::resource_path::self_exe_dir();
    assert(!exe_dir.empty());
    assert(std::filesystem::exists(exe_dir));
}

void test_std_dir_falls_back_when_installed_layout_is_absent() {
    const auto fallback = std::filesystem::temp_directory_path() / "cicest-resource-path-std";
    assert(cstc::resource_path::resolve_std_dir(fallback) == fallback);
}

void test_rt_path_falls_back_when_installed_layout_is_absent() {
    const auto fallback = std::filesystem::temp_directory_path() / "cicest-resource-path-rt.a";
    assert(cstc::resource_path::resolve_rt_path(fallback) == fallback);
}

} // namespace

int main() {
    test_rt_library_filename_is_configured();
    test_self_exe_dir_resolves_current_binary_directory();
    test_std_dir_falls_back_when_installed_layout_is_absent();
    test_rt_path_falls_back_when_installed_layout_is_absent();
    return 0;
}
