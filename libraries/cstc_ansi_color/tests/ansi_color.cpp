#include <cassert>

#include <cstc_ansi_color/ansi_color.hpp>

namespace {

void test_detect_emission_defaults_to_never_without_opt_in() {
    cstc::ansi_color::Environment environment;
    environment.term = "xterm-256color";

    assert(cstc::ansi_color::detect_emission(environment) == cstc::ansi_color::Emission::Never);
}

void test_detect_emission_respects_enable_and_disable_variables() {
    cstc::ansi_color::Environment enabled;
    enabled.cicest_color = "always";
    enabled.term = "xterm-256color";
    assert(cstc::ansi_color::detect_emission(enabled) == cstc::ansi_color::Emission::Always);

    cstc::ansi_color::Environment disabled;
    disabled.no_color = "1";
    disabled.cicest_color = "always";
    disabled.term = "xterm-256color";
    assert(cstc::ansi_color::detect_emission(disabled) == cstc::ansi_color::Emission::Never);
}

void test_paint_is_plain_when_disabled() {
    const std::string rendered = cstc::ansi_color::paint(
        "error", cstc::ansi_color::Style{.foreground = cstc::ansi_color::Color::Red, .bold = true},
        cstc::ansi_color::Emission::Never);
    assert(rendered == "error");
}

void test_paint_wraps_escape_codes_when_enabled() {
    const std::string rendered = cstc::ansi_color::paint(
        "warning",
        cstc::ansi_color::Style{.foreground = cstc::ansi_color::Color::Yellow, .bold = true},
        cstc::ansi_color::Emission::Always);
    assert(rendered.starts_with("\x1b[1;33m"));
    assert(rendered.ends_with("\x1b[0m"));
}

void test_platform_supports_ansi_disables_dumb_term() {
    cstc::ansi_color::Environment environment;
    environment.term = "dumb";
    assert(!cstc::ansi_color::platform_supports_ansi(environment));
}

} // namespace

int main() {
    test_detect_emission_defaults_to_never_without_opt_in();
    test_detect_emission_respects_enable_and_disable_variables();
    test_paint_is_plain_when_disabled();
    test_paint_wraps_escape_codes_when_enabled();
    test_platform_supports_ansi_disables_dumb_term();
    return 0;
}
