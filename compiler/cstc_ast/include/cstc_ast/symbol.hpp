#ifndef CICEST_COMPILER_CSTC_AST_SYMBOL_HPP
#define CICEST_COMPILER_CSTC_AST_SYMBOL_HPP

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cstc::ast {

/// A lightweight interned identifier handle.
/// Equality is O(1) symbol_id comparison — no string comparison needed.
struct Symbol {
    std::uint32_t symbol_id;

    [[nodiscard]] bool operator==(const Symbol& rhs) const noexcept {
        return symbol_id == rhs.symbol_id;
    }

    [[nodiscard]] bool operator!=(const Symbol& rhs) const noexcept {
        return symbol_id != rhs.symbol_id;
    }
};

namespace detail {

/// FNV-1a 32-bit hash — fast, non-cryptographic.
[[nodiscard]] constexpr std::uint32_t fnv1a32(std::string_view str) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char c : str) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

} // namespace detail

/// Global symbol intern table.
///
/// Every identifier string is interned here exactly once. Subsequent interns
/// of the same string return the same Symbol. Comparison of two Symbols is
/// therefore a single integer comparison.
///
/// The symbol_id assigned to each entry is its FNV-1a 32-bit hash.
/// On the (extremely rare) collision two distinct strings produce the same
/// hash, the second string receives a linearly-probed fallback id so that
/// every entry retains a unique symbol_id.
class SymbolTable {
public:
    SymbolTable() = default;
    SymbolTable(const SymbolTable&) = delete;
    SymbolTable(SymbolTable&&) = delete;
    SymbolTable& operator=(const SymbolTable&) = delete;
    SymbolTable& operator=(SymbolTable&&) = delete;

    /// Intern `name` and return its Symbol.
    /// Calling intern() twice with equal strings yields equal Symbols.
    [[nodiscard]] Symbol intern(std::string_view name) {
        // Fast path: already interned.
        auto it = lookup_.find(name);
        if (it != lookup_.end())
            return it->second;

        // Compute a candidate id from the hash.
        std::uint32_t id = detail::fnv1a32(name);

        // Resolve collisions: if `id` is already claimed by a *different*
        // string, increment until we find a free slot.
        while (id_to_str_.count(id) && id_to_str_.at(id) != name)
            ++id;

        strings_.emplace_back(name);
        const std::string& stored = strings_.back();

        lookup_.emplace(stored, Symbol{id});
        id_to_str_.emplace(id, stored);

        return Symbol{id};
    }

    /// Look up the source string for a Symbol. Returns an empty string_view
    /// if the id is unknown (should not happen in well-formed usage).
    [[nodiscard]] std::string_view str(Symbol sym) const noexcept {
        auto it = id_to_str_.find(sym.symbol_id);
        if (it == id_to_str_.end())
            return {};
        return it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return strings_.size(); }

private:
    /// Owns the interned strings so string_view keys in lookup_ stay valid.
    std::vector<std::string> strings_;
    /// name → Symbol (keyed by string_view into strings_).
    std::unordered_map<std::string_view, Symbol> lookup_;
    /// symbol_id → interned string (for reverse lookup / collision tracking).
    std::unordered_map<std::uint32_t, std::string> id_to_str_;
};

/// Site where a symbol is first defined (e.g., a declaration node).
class SymbolDefinitionSite {
    Symbol symbol_;

public:
    explicit SymbolDefinitionSite(Symbol sym) : symbol_(sym) {}

    [[nodiscard]] Symbol symbol() const noexcept { return symbol_; }
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_SYMBOL_HPP
