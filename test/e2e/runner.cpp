#include <cstc_end_to_end/end_to_end.hpp>

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        const cstc::end_to_end::Config config =
            cstc::end_to_end::parse_args(argc, argv, CSTC_BINARY_PATH);
        const cstc::end_to_end::SuiteResult result = cstc::end_to_end::run_suite(config);

        if (!config.quiet)
            cstc::end_to_end::print_suite_summary(result, std::cout, std::cerr);

        if (config.report_path.has_value())
            cstc::end_to_end::write_json_report(result, *config.report_path);

        return result.all_passed() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
