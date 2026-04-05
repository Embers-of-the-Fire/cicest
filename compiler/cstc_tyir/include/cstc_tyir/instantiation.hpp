#ifndef CICEST_COMPILER_CSTC_TYIR_INSTANTIATION_HPP
#define CICEST_COMPILER_CSTC_TYIR_INSTANTIATION_HPP

#include <cstddef>
#include <optional>
#include <vector>

#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>

namespace cstc::tyir {

inline constexpr std::size_t kMaxGenericInstantiationDepth = 32;

enum class InstantiationPhase {
    TypeChecking,
    ConstEval,
    Monomorphization,
};

struct InstantiationFrame {
    cstc::symbol::Symbol item_name = cstc::symbol::kInvalidSymbol;
    cstc::symbol::Symbol display_name = cstc::symbol::kInvalidSymbol;
    cstc::span::SourceSpan span;
    std::vector<Ty> generic_args;
};

struct InstantiationLimitDiagnostic {
    InstantiationPhase phase = InstantiationPhase::TypeChecking;
    std::size_t active_limit = kMaxGenericInstantiationDepth;
    std::vector<InstantiationFrame> stack;
};

} // namespace cstc::tyir

#endif // CICEST_COMPILER_CSTC_TYIR_INSTANTIATION_HPP
