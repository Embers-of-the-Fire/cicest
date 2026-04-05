#ifndef CICEST_COMPILER_CSTC_TYIR_INTERP_INTERP_HPP
#define CICEST_COMPILER_CSTC_TYIR_INTERP_INTERP_HPP

/// @file interp.hpp
/// @brief TyIR interpreter and const-folding pass.

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/instantiation.hpp>
#include <cstc_tyir/tyir.hpp>

namespace cstc::tyir_interp {

struct EvalStackFrame {
    cstc::symbol::Symbol fn_name = cstc::symbol::kInvalidSymbol;
    cstc::span::SourceSpan span;
};

struct EvalError {
    cstc::span::SourceSpan span;
    std::string message;
    std::vector<EvalStackFrame> stack;
    std::optional<cstc::tyir::InstantiationLimitDiagnostic> instantiation_limit;
};

enum class ConstraintEvalKind {
    Satisfied,
    Unsatisfied,
    NotConstEvaluable,
    RuntimeOnly,
    InvalidType,
};

struct ConstraintEvalResult {
    ConstraintEvalKind kind = ConstraintEvalKind::NotConstEvaluable;
    std::string detail;
};

[[nodiscard]] std::expected<tyir::TyProgram, EvalError>
    fold_program(const tyir::TyProgram& program);

} // namespace cstc::tyir_interp

#endif // CICEST_COMPILER_CSTC_TYIR_INTERP_INTERP_HPP
