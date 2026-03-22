#pragma once

#include <array>
#include <string_view>

namespace cstc::cli {

enum class LinkerFlavor {
    Posix,
    WindowsMinGW,
    WindowsMSVC,
};

inline constexpr std::array<std::string_view, 3> posix_linker_candidates{
    "c++",
    "clang++",
    "g++",
};

// MinGW shells often expose both clang++ and GCC drivers, but c++/g++ is the
// more reliable default for GNU-runtime linkage.
inline constexpr std::array<std::string_view, 3> windows_mingw_linker_candidates{
    "c++",
    "g++",
    "clang++",
};

inline constexpr std::array<std::string_view, 3> windows_msvc_linker_candidates{
    "clang++",
    "c++",
    "g++",
};

[[nodiscard]] constexpr LinkerFlavor host_linker_flavor() {
#ifdef _WIN32
# ifdef _MSC_VER
    return LinkerFlavor::WindowsMSVC;
# else
    return LinkerFlavor::WindowsMinGW;
# endif
#else
    return LinkerFlavor::Posix;
#endif
}

[[nodiscard]] constexpr const std::array<std::string_view, 3>&
    linker_candidates(LinkerFlavor flavor) {
    switch (flavor) {
    case LinkerFlavor::Posix: return posix_linker_candidates;
    case LinkerFlavor::WindowsMinGW: return windows_mingw_linker_candidates;
    case LinkerFlavor::WindowsMSVC: return windows_msvc_linker_candidates;
    }

    return posix_linker_candidates;
}

[[nodiscard]] constexpr std::string_view fallback_linker_program(LinkerFlavor flavor) {
    switch (flavor) {
    case LinkerFlavor::WindowsMSVC: return "clang++";
    case LinkerFlavor::Posix:
    case LinkerFlavor::WindowsMinGW: return "c++";
    }

    return "c++";
}

} // namespace cstc::cli
