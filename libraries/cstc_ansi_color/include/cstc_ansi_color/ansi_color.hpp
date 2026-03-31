#ifndef CICEST_LIBRARY_CSTC_ANSI_COLOR_ANSI_COLOR_HPP
#define CICEST_LIBRARY_CSTC_ANSI_COLOR_ANSI_COLOR_HPP

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace cstc::ansi_color {

enum class Emission {
    Never,
    Always,
};

enum class Color {
    Default,
    Red,
    Yellow,
    Blue,
    Cyan,
    Green,
    White,
    BrightBlack,
};

struct Style {
    std::optional<Color> foreground;
    bool bold = false;

    [[nodiscard]] constexpr bool empty() const { return !foreground.has_value() && !bold; }
};

struct Environment {
    std::optional<std::string> no_color;
    std::optional<std::string> cicest_color;
    std::optional<std::string> force_color;
    std::optional<std::string> clicolor;
    std::optional<std::string> clicolor_force;
    std::optional<std::string> term;
};

[[nodiscard]] inline std::optional<std::string> env_copy(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr)
        return std::nullopt;
    return std::string(value);
}

[[nodiscard]] inline Environment current_environment() {
    return Environment{
        .no_color = env_copy("NO_COLOR"),
        .cicest_color = env_copy("CICEST_COLOR"),
        .force_color = env_copy("FORCE_COLOR"),
        .clicolor = env_copy("CLICOLOR"),
        .clicolor_force = env_copy("CLICOLOR_FORCE"),
        .term = env_copy("TERM"),
    };
}

[[nodiscard]] inline std::string ascii_lower_copy(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered;
}

[[nodiscard]] inline bool env_value_is_true(std::string_view value) {
    const std::string lowered = ascii_lower_copy(value);
    return lowered.empty() || lowered == "1" || lowered == "true" || lowered == "yes"
        || lowered == "on" || lowered == "always" || lowered == "force";
}

[[nodiscard]] inline bool env_value_is_false(std::string_view value) {
    const std::string lowered = ascii_lower_copy(value);
    return lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off"
        || lowered == "never";
}

[[nodiscard]] inline bool platform_supports_ansi(const Environment& environment) {
#ifdef _WIN32
    if (environment.term.has_value()) {
        const std::string lowered = ascii_lower_copy(*environment.term);
        if (!lowered.empty() && lowered != "dumb")
            return true;
    }
    return false;
#else
    if (!environment.term.has_value())
        return true;
    return ascii_lower_copy(*environment.term) != "dumb";
#endif
}

[[nodiscard]] inline Emission detect_emission(const Environment& environment, bool ansi_supported) {
    if (!ansi_supported)
        return Emission::Never;

    if (environment.no_color.has_value())
        return Emission::Never;

    if (environment.cicest_color.has_value()) {
        if (env_value_is_false(*environment.cicest_color))
            return Emission::Never;
        if (env_value_is_true(*environment.cicest_color))
            return Emission::Always;
    }

    if (environment.force_color.has_value() && env_value_is_true(*environment.force_color))
        return Emission::Always;

    if (environment.clicolor_force.has_value() && env_value_is_true(*environment.clicolor_force))
        return Emission::Always;

    if (environment.clicolor.has_value() && env_value_is_true(*environment.clicolor))
        return Emission::Always;

    return Emission::Never;
}

[[nodiscard]] inline Emission detect_emission(const Environment& environment) {
    return detect_emission(environment, platform_supports_ansi(environment));
}

[[nodiscard]] inline Emission detect_emission() {
    const Environment environment = current_environment();
    return detect_emission(environment, platform_supports_ansi(environment));
}

[[nodiscard]] inline const char* color_code(Color color) {
    switch (color) {
    case Color::Default: return "39";
    case Color::Red: return "31";
    case Color::Yellow: return "33";
    case Color::Blue: return "34";
    case Color::Cyan: return "36";
    case Color::Green: return "32";
    case Color::White: return "37";
    case Color::BrightBlack: return "90";
    }
    return "39";
}

[[nodiscard]] inline std::string prefix(const Style& style, Emission emission) {
    if (emission == Emission::Never || style.empty())
        return {};

    std::string sequence = "\x1b[";
    bool first = true;
    if (style.bold) {
        sequence += "1";
        first = false;
    }

    if (style.foreground.has_value()) {
        if (!first)
            sequence += ';';
        sequence += color_code(*style.foreground);
    }

    sequence += 'm';
    return sequence;
}

[[nodiscard]] inline std::string suffix(Emission emission) {
    if (emission == Emission::Never)
        return {};
    return "\x1b[0m";
}

[[nodiscard]] inline std::string
    paint(std::string_view text, const Style& style, Emission emission) {
    if (emission == Emission::Never || style.empty())
        return std::string(text);

    return prefix(style, emission) + std::string(text) + suffix(emission);
}

} // namespace cstc::ansi_color

#endif // CICEST_LIBRARY_CSTC_ANSI_COLOR_ANSI_COLOR_HPP
