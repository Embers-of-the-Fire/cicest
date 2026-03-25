#ifndef CICEST_COMPILER_CSTC_REPL_REPL_HPP
#define CICEST_COMPILER_CSTC_REPL_REPL_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace cstc::repl {

/// Configuration for one interactive REPL session.
struct SessionOptions {
    /// Directory used as the root module location for relative imports.
    ///
    /// The REPL materializes a temporary root module inside this directory so
    /// that `import { ... } from "foo.cst";` resolves the same way as it would
    /// from a normal source file in that directory.
    std::filesystem::path session_root_dir;

    /// Optional linker override used when turning snippets into executables.
    std::optional<std::string> linker;
};

/// Outcome category for one submitted REPL fragment.
enum class SubmissionStatus {
    /// The fragment was handled successfully.
    Success,
    /// The fragment failed before program execution (parse/type/link/etc.).
    Error,
    /// The generated executable ran and returned a non-zero status.
    RuntimeError,
    /// The user requested that the REPL exit.
    ExitRequested,
};

/// Result of one `Session::submit(...)` call.
struct SubmissionResult {
    /// High-level outcome category.
    SubmissionStatus status = SubmissionStatus::Success;
    /// Whether the persisted REPL state changed.
    bool state_changed = false;
    /// Whether a generated executable was run.
    bool executed = false;
    /// Captured stdout from the generated executable.
    std::string stdout_output;
    /// Captured stderr from the generated executable.
    std::string stderr_output;
    /// Human-readable informational message for successful submissions.
    std::string info_message;
    /// Human-readable error message for failed submissions.
    std::string error_message;

    [[nodiscard]] bool succeeded() const { return status == SubmissionStatus::Success; }
};

/// Stateful Cicest REPL session.
///
/// The REPL persists:
/// - top-level items (`import`, `struct`, `enum`, `fn`, `extern`)
/// - non-discard top-level `let` bindings entered at the prompt
///
/// Persisted `let` bindings are replayed when later snippets are executed so
/// that subsequent turns can reference earlier bindings without requiring a
/// dedicated interpreter backend.
class Session {
public:
    explicit Session(SessionOptions options = {});
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    /// Returns whether `input` should keep collecting more lines.
    ///
    /// This is intended for interactive prompt frontends. It uses lexer/parser
    /// heuristics to keep multi-line items and expressions open until they are
    /// structurally complete.
    [[nodiscard]] bool needs_continuation(std::string_view input) const;

    /// Submits one complete fragment to the REPL.
    ///
    /// Supported inputs:
    /// - REPL commands such as `:help`, `:show`, `:reset`, `:quit`
    /// - top-level items
    /// - block-body fragments (statements and/or a trailing expression)
    [[nodiscard]] SubmissionResult submit(std::string_view input);

    /// Clears all persisted items and bindings.
    void reset();

    /// Returns the current persisted program source synthesized by the REPL.
    ///
    /// This includes the internal helper functions and the generated `main`
    /// body that replays persisted bindings.
    [[nodiscard]] std::string persisted_source() const;

private:
    class Impl;
    Impl* impl_;
};

/// Returns the text printed by the `:help` command.
[[nodiscard]] std::string help_text();

/// Returns the startup banner printed by the interactive REPL.
[[nodiscard]] std::string startup_text();

} // namespace cstc::repl

#endif // CICEST_COMPILER_CSTC_REPL_REPL_HPP
