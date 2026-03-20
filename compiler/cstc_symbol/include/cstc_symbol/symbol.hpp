#ifndef CICEST_COMPILER_CSTC_SYMBOL_SYMBOL_HPP
#define CICEST_COMPILER_CSTC_SYMBOL_SYMBOL_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cstc::symbol {

namespace detail {

/// Internal string interner backing the thread-local global symbol table.
class SymbolTable {
public:
    SymbolTable() {
        entries_.push_back("");
        indices_.emplace("", 0U);
    }

    std::uint32_t intern(std::string_view text) {
        const std::string key(text);
        const auto found = indices_.find(key);
        if (found != indices_.end())
            return found->second;

        const std::uint32_t index = static_cast<std::uint32_t>(entries_.size());
        entries_.push_back(key);
        indices_.emplace(entries_.back(), index);
        return index;
    }

    [[nodiscard]] std::string_view resolve(std::uint32_t index) const {
        if (index >= entries_.size())
            return "<invalid-symbol>";
        return entries_[index];
    }

    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    std::vector<std::string> entries_;
    std::unordered_map<std::string, std::uint32_t> indices_;
};

/// Thread-local pointer to the current session's interner.
inline thread_local SymbolTable* g_interner = nullptr;

} // namespace detail

/// Interned symbol — an opaque index into the global symbol table.
///
/// All operations require an active `SymbolSession` on the current thread.
struct Symbol {
    /// Raw index into the interner (0 == invalid).
    std::uint32_t index = 0;

    /// Returns true when this symbol references a table entry.
    [[nodiscard]] constexpr bool is_valid() const { return index != 0; }

    /// Interns `text` in the current session's global interner and returns its symbol.
    [[nodiscard]] static Symbol intern(std::string_view text) {
        assert(detail::g_interner != nullptr && "Symbol::intern called outside a SymbolSession");
        return Symbol{detail::g_interner->intern(text)};
    }

    /// Resolves this symbol to its original interned string.
    [[nodiscard]] std::string_view as_str() const {
        assert(detail::g_interner != nullptr && "Symbol::as_str called outside a SymbolSession");
        return detail::g_interner->resolve(index);
    }

    friend constexpr bool operator==(Symbol lhs, Symbol rhs) = default;
};

/// Sentinel value for absent or unset symbols.
inline constexpr Symbol kInvalidSymbol{};

/// Hash functor for `Symbol` keys.
struct SymbolHash {
    [[nodiscard]] std::size_t operator()(Symbol s) const {
        return static_cast<std::size_t>(s.index);
    }
};

/// Well-known pre-interned keyword symbols with fixed indices.
///
/// These constants are valid in any `SymbolSession` because `SymbolSession`
/// pre-populates keywords in a deterministic order before any user text is
/// interned, guaranteeing the indices below are stable.
namespace kw {
inline constexpr Symbol Struct{1};
inline constexpr Symbol Enum{2};
inline constexpr Symbol Fn{3};
inline constexpr Symbol Let{4};
inline constexpr Symbol If{5};
inline constexpr Symbol Else{6};
inline constexpr Symbol For{7};
inline constexpr Symbol While{8};
inline constexpr Symbol Loop{9};
inline constexpr Symbol Break{10};
inline constexpr Symbol Continue{11};
inline constexpr Symbol Return_{12};
inline constexpr Symbol True_{13};
inline constexpr Symbol False_{14};
inline constexpr Symbol Unit{15};
inline constexpr Symbol Num{16};
inline constexpr Symbol Str{17};
inline constexpr Symbol Bool{18};
inline constexpr Symbol UnitLit{19}; // "()"
} // namespace kw

/// RAII guard that initializes a fresh global interner for the current thread.
///
/// Construct one at the top of `main` (or at the start of each test) before
/// calling any `Symbol::intern` / `Symbol::as_str` / lexer / parser functions.
/// All `Symbol` operations are undefined outside the lifetime of a session.
class SymbolSession {
public:
    SymbolSession()
        : interner_(std::make_unique<detail::SymbolTable>()) {
        // Pre-intern keywords in a fixed order so that the kw:: constants,
        // which are plain compile-time index values, remain stable across every
        // session.  The order here MUST match the index assignments in kw::.
        interner_->intern("struct");   // 1  == kw::Struct
        interner_->intern("enum");     // 2  == kw::Enum
        interner_->intern("fn");       // 3  == kw::Fn
        interner_->intern("let");      // 4  == kw::Let
        interner_->intern("if");       // 5  == kw::If
        interner_->intern("else");     // 6  == kw::Else
        interner_->intern("for");      // 7  == kw::For
        interner_->intern("while");    // 8  == kw::While
        interner_->intern("loop");     // 9  == kw::Loop
        interner_->intern("break");    // 10 == kw::Break
        interner_->intern("continue"); // 11 == kw::Continue
        interner_->intern("return");   // 12 == kw::Return_
        interner_->intern("true");     // 13 == kw::True_
        interner_->intern("false");    // 14 == kw::False_
        interner_->intern("Unit");     // 15 == kw::Unit
        interner_->intern("num");      // 16 == kw::Num
        interner_->intern("str");      // 17 == kw::Str
        interner_->intern("bool");     // 18 == kw::Bool
        interner_->intern("()");       // 19 == kw::UnitLit

        detail::g_interner = interner_.get();
    }

    ~SymbolSession() { detail::g_interner = nullptr; }

    SymbolSession(const SymbolSession&) = delete;
    SymbolSession& operator=(const SymbolSession&) = delete;

private:
    std::unique_ptr<detail::SymbolTable> interner_;
};

} // namespace cstc::symbol

#endif // CICEST_COMPILER_CSTC_SYMBOL_SYMBOL_HPP
