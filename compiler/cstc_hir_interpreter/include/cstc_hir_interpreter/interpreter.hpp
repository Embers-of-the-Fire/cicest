#ifndef CICEST_COMPILER_CSTC_HIR_INTERPRETER_INTERPRETER_HPP
#define CICEST_COMPILER_CSTC_HIR_INTERPRETER_INTERPRETER_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <cstc_hir/hir.hpp>

namespace cstc::hir::interpreter {

enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string scope;
    std::string message;
};

struct ValidationResult {
    bool ok = true;
    std::vector<Diagnostic> diagnostics;
};

struct UnitValue {};

struct Value;

struct ObjectValue {
    std::string type_name;
    std::unordered_map<std::string, std::shared_ptr<Value>> fields;
};

struct LambdaValue {
    std::vector<std::string> params;
    std::string body;
};

struct FunctionRefValue {
    std::string name;
};

struct Value {
    std::variant<UnitValue, std::int64_t, bool, std::string, ObjectValue, LambdaValue,
        FunctionRefValue>
        kind;
};

[[nodiscard]] std::string format_value(const Value& value);

enum class ExecutionMode {
    Runtime,
    ConstEval,
};

struct RunOptions {
    std::string entry = "main";
    ExecutionMode mode = ExecutionMode::Runtime;
    std::size_t call_depth_limit = 256;
};

struct ExecutionResult {
    bool ok = true;
    std::optional<Value> value;
    std::vector<Diagnostic> diagnostics;
};

struct MaterializationResult {
    bool ok = true;
    std::vector<Diagnostic> diagnostics;
    std::unordered_map<std::string, Value> lifted_constants;
};

class HirInterpreter {
public:
    explicit HirInterpreter(cstc::hir::Module& module);
    ~HirInterpreter();

    HirInterpreter(const HirInterpreter&) = delete;
    HirInterpreter& operator=(const HirInterpreter&) = delete;

    HirInterpreter(HirInterpreter&&) noexcept;
    HirInterpreter& operator=(HirInterpreter&&) noexcept;

    [[nodiscard]] ValidationResult validate();
    [[nodiscard]] MaterializationResult materialize_const_types();
    [[nodiscard]] ExecutionResult run(const RunOptions& options = {});
    [[nodiscard]] ExecutionResult eval_repl_line(std::string_view line);

    [[nodiscard]] const std::unordered_map<std::string, Value>& lifted_constants() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cstc::hir::interpreter

#endif // CICEST_COMPILER_CSTC_HIR_INTERPRETER_INTERPRETER_HPP
