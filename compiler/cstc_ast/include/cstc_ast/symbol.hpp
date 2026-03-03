#ifndef CICEST_COMPILER_CSTC_AST_SYMBOL_HPP
#define CICEST_COMPILER_CSTC_AST_SYMBOL_HPP

#include <cstdint>
namespace cstc::ast {

class SymbolDefinitionSite {
    std::uint32_t symbol_id;
};

struct Symbol {
    /// Unique identifier for the symbol.
    std::uint32_t symbol_id;
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_SYMBOL_HPP
