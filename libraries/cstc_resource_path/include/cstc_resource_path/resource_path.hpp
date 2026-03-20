#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#endif

#ifdef __APPLE__
# include <mach-o/dyld.h>
#endif

namespace cstc::resource_path {

/// Platform-correct filename for the runtime static library.
///
/// CMake produces `libcicest_rt.a` on GNU-like toolchains and `cicest_rt.lib`
/// on MSVC, so the installed-layout probe must match.
#ifdef _MSC_VER
inline constexpr const char* rt_library_filename = "cicest_rt.lib";
#else
inline constexpr const char* rt_library_filename = "libcicest_rt.a";
#endif

[[nodiscard]] inline std::filesystem::path
    normalize_existing_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(path, ec);
    if (!ec)
        return normalized;

    return path.lexically_normal();
}

[[nodiscard]] inline bool
    path_exists(const std::filesystem::path& path, std::string_view resource_description) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        throw std::runtime_error(
            "failed to inspect " + std::string(resource_description) + " '" + path.string()
            + "': " + ec.message());
    }

    return exists;
}

[[nodiscard]] inline std::filesystem::path canonicalize_or_throw(
    const std::filesystem::path& path, std::string_view resource_description) {
    std::error_code ec;
    const auto canonical = std::filesystem::canonical(path, ec);
    if (ec) {
        throw std::runtime_error(
            "failed to resolve " + std::string(resource_description) + " '" + path.string()
            + "': " + ec.message());
    }

    return canonical;
}

#if defined(__unix__) && !defined(__APPLE__)
[[nodiscard]] inline std::filesystem::path procfs_self_exe_dir() {
# ifdef __sun
    constexpr const char* proc_candidates[] = {
        "/proc/self/path/a.out",
        "/proc/self/exe",
        "/proc/curproc/file",
        "/proc/curproc/exe",
    };
# else
    constexpr const char* proc_candidates[] = {
        "/proc/self/exe",
        "/proc/curproc/file",
        "/proc/curproc/exe",
    };
# endif

    for (const char* candidate : proc_candidates) {
        std::error_code ec;
        const auto target = std::filesystem::read_symlink(candidate, ec);
        if (ec || target.empty())
            continue;

        const auto link_path = std::filesystem::path(candidate);
        const auto exe_path = target.is_absolute() ? target : link_path.parent_path() / target;
        return normalize_existing_path(exe_path).parent_path();
    }

    return {};
}
#endif

/// Returns the directory containing the currently running binary.
///
/// Uses platform APIs on Windows/macOS and procfs symlinks on supported Unix
/// variants so installed layouts remain relocatable across those targets.
[[nodiscard]] inline std::filesystem::path self_exe_dir() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};

        if (length < buffer.size())
            return normalize_existing_path(
                       std::filesystem::path(std::wstring(buffer.data(), length)))
                .parent_path();

        if (buffer.size() >= 32768)
            return {};

        std::size_t next_size = buffer.size() * 2;
        if (next_size > 32768)
            next_size = 32768;
        buffer.resize(next_size);
    }
#elifdef __APPLE__
    std::vector<char> buffer(4096);
    while (true) {
        uint32_t size = static_cast<uint32_t>(buffer.size());
        if (_NSGetExecutablePath(buffer.data(), &size) == 0)
            return normalize_existing_path(std::filesystem::path(buffer.data())).parent_path();

        if (size <= buffer.size())
            return {};

        buffer.resize(size);
    }
#elifdef __unix__
    return procfs_self_exe_dir();
#endif

    return {};
}

/// Returns the path to the std library directory.
///
/// Searches `<exe_dir>/../share/cicest/std` first (installed layout), then
/// falls back to the compile-time std directory path (development builds).
[[nodiscard]] inline std::filesystem::path
    resolve_std_dir(const std::filesystem::path& fallback_std_dir) {
    const auto bin_dir = self_exe_dir();
    if (!bin_dir.empty()) {
        const auto installed = bin_dir / ".." / "share" / "cicest" / "std";
        if (path_exists(installed / "prelude.cst", "installed std prelude"))
            return canonicalize_or_throw(installed, "installed std library directory");
    }

    return fallback_std_dir;
}

/// Returns the path to the runtime static library.
///
/// Searches `<exe_dir>/../lib/cicest/<rt_library_filename>` first (installed
/// layout), then falls back to the compile-time runtime archive path
/// (development builds). The filename is derived from `rt_library_filename`
/// so it matches the archive name CMake produces for the current toolchain.
[[nodiscard]] inline std::filesystem::path
    resolve_rt_path(const std::filesystem::path& fallback_rt_path) {
    const auto bin_dir = self_exe_dir();
    if (!bin_dir.empty()) {
        const auto installed = bin_dir / ".." / "lib" / "cicest" / rt_library_filename;
        if (path_exists(installed, "installed runtime static library"))
            return canonicalize_or_throw(installed, "installed runtime static library");
    }

    return fallback_rt_path;
}

} // namespace cstc::resource_path
