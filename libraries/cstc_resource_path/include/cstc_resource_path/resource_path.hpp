#pragma once

#include <filesystem>
#include <string_view>

namespace cstc::resource_path {

extern const char* const rt_library_filename;

[[nodiscard]] std::filesystem::path normalize_existing_path(const std::filesystem::path& path);
[[nodiscard]] bool
    path_exists(const std::filesystem::path& path, std::string_view resource_description);
[[nodiscard]] std::filesystem::path
    canonicalize_or_throw(const std::filesystem::path& path, std::string_view resource_description);
[[nodiscard]] std::filesystem::path self_exe_dir();
[[nodiscard]] std::filesystem::path resolve_std_dir(const std::filesystem::path& fallback_std_dir);
[[nodiscard]] std::filesystem::path resolve_rt_path(const std::filesystem::path& fallback_rt_path);

} // namespace cstc::resource_path
