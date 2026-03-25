#include <cstc_repl/repl.hpp>

#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
# include <io.h>
#else
# include <unistd.h>
#endif

namespace {

struct Options {
    std::optional<std::string> linker;
    bool show_help = false;
};

[[nodiscard]] std::string_view trim_view(std::string_view input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0)
        ++start;

    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0)
        --end;

    return input.substr(start, end - start);
}

[[nodiscard]] std::string usage() { return "Usage: cstc_repl [-h|--help] [--linker <linker>]"; }

[[nodiscard]] Options parse_options(int argc, char** argv) {
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];

        if (arg == "-h" || arg == "--help") {
            options.show_help = true;
            continue;
        }

        if (arg == "--linker") {
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for --linker\n" + usage());
            options.linker = std::string(argv[++index]);
            continue;
        }

        throw std::runtime_error("unknown option: " + std::string(arg) + "\n" + usage());
    }

    return options;
}

[[nodiscard]] bool stdin_is_terminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

[[nodiscard]] bool is_command_input(std::string_view input) {
    return !input.empty() && (input.front() == ':' || input.front() == '.');
}

void print_result(const cstc::repl::SubmissionResult& result) {
    if (!result.stdout_output.empty()) {
        std::cout << result.stdout_output;
        std::cout.flush();
    }

    if (!result.info_message.empty()) {
        std::cout << result.info_message << '\n';
        std::cout.flush();
    }

    if (!result.stderr_output.empty()) {
        std::cerr << result.stderr_output;
        std::cerr.flush();
    }

    if (!result.error_message.empty()) {
        std::cerr << result.error_message << '\n';
        std::cerr.flush();
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        if (options.show_help) {
            std::cout << usage() << '\n' << cstc::repl::help_text() << '\n';
            return 0;
        }

        cstc::repl::Session session(
            {.session_root_dir = std::filesystem::current_path(), .linker = options.linker});

        const bool interactive = stdin_is_terminal();
        std::string pending_input;
        std::string line;

        if (interactive)
            std::cout << cstc::repl::startup_text() << '\n';

        while (true) {
            if (interactive) {
                std::cout << (pending_input.empty() ? "cst> " : "...> ");
                std::cout.flush();
            }

            if (!std::getline(std::cin, line)) {
                if (pending_input.empty())
                    break;

                const cstc::repl::SubmissionResult result = session.submit(pending_input);
                print_result(result);
                if (result.status == cstc::repl::SubmissionStatus::ExitRequested)
                    break;
                pending_input.clear();
                break;
            }

            const std::string_view trimmed = trim_view(line);
            if (pending_input.empty() && trimmed.empty())
                continue;

            if (pending_input.empty() && is_command_input(trimmed)) {
                const cstc::repl::SubmissionResult result = session.submit(trimmed);
                print_result(result);
                if (result.status == cstc::repl::SubmissionStatus::ExitRequested)
                    break;
                continue;
            }

            pending_input += line;
            pending_input += '\n';

            if (session.needs_continuation(pending_input))
                continue;

            const cstc::repl::SubmissionResult result = session.submit(pending_input);
            print_result(result);
            pending_input.clear();

            if (result.status == cstc::repl::SubmissionStatus::ExitRequested)
                break;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
