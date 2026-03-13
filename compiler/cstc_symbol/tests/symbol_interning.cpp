#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>

#include <cstc_symbol/symbol.hpp>

namespace {

void test_all_keywords() {
    cstc::symbol::SymbolSession session;

    assert(cstc::symbol::kw::Struct.as_str()   == "struct");
    assert(cstc::symbol::kw::Enum.as_str()     == "enum");
    assert(cstc::symbol::kw::Fn.as_str()       == "fn");
    assert(cstc::symbol::kw::Let.as_str()      == "let");
    assert(cstc::symbol::kw::If.as_str()       == "if");
    assert(cstc::symbol::kw::Else.as_str()     == "else");
    assert(cstc::symbol::kw::For.as_str()      == "for");
    assert(cstc::symbol::kw::While.as_str()    == "while");
    assert(cstc::symbol::kw::Loop.as_str()     == "loop");
    assert(cstc::symbol::kw::Break.as_str()    == "break");
    assert(cstc::symbol::kw::Continue.as_str() == "continue");
    assert(cstc::symbol::kw::Return_.as_str()  == "return");
    assert(cstc::symbol::kw::True_.as_str()    == "true");
    assert(cstc::symbol::kw::False_.as_str()   == "false");
    assert(cstc::symbol::kw::Unit.as_str()     == "Unit");
    assert(cstc::symbol::kw::Num.as_str()      == "num");
    assert(cstc::symbol::kw::Str.as_str()      == "str");
    assert(cstc::symbol::kw::Bool.as_str()     == "bool");
    assert(cstc::symbol::kw::UnitLit.as_str()  == "()");
}

void test_keyword_indices_are_fixed() {
    cstc::symbol::SymbolSession session;

    assert(cstc::symbol::kw::Struct.index   == 1);
    assert(cstc::symbol::kw::Enum.index     == 2);
    assert(cstc::symbol::kw::Fn.index       == 3);
    assert(cstc::symbol::kw::Let.index      == 4);
    assert(cstc::symbol::kw::If.index       == 5);
    assert(cstc::symbol::kw::Else.index     == 6);
    assert(cstc::symbol::kw::For.index      == 7);
    assert(cstc::symbol::kw::While.index    == 8);
    assert(cstc::symbol::kw::Loop.index     == 9);
    assert(cstc::symbol::kw::Break.index    == 10);
    assert(cstc::symbol::kw::Continue.index == 11);
    assert(cstc::symbol::kw::Return_.index  == 12);
    assert(cstc::symbol::kw::True_.index    == 13);
    assert(cstc::symbol::kw::False_.index   == 14);
    assert(cstc::symbol::kw::Unit.index     == 15);
    assert(cstc::symbol::kw::Num.index      == 16);
    assert(cstc::symbol::kw::Str.index      == 17);
    assert(cstc::symbol::kw::Bool.index     == 18);
    assert(cstc::symbol::kw::UnitLit.index  == 19);
}

void test_intern_matches_keyword() {
    cstc::symbol::SymbolSession session;

    // Interning keyword text returns the same index as the kw:: constant.
    assert(cstc::symbol::Symbol::intern("struct") == cstc::symbol::kw::Struct);
    assert(cstc::symbol::Symbol::intern("fn")     == cstc::symbol::kw::Fn);
    assert(cstc::symbol::Symbol::intern("()")     == cstc::symbol::kw::UnitLit);
    assert(cstc::symbol::Symbol::intern("return") == cstc::symbol::kw::Return_);
    assert(cstc::symbol::Symbol::intern("true")   == cstc::symbol::kw::True_);
}

void test_symbol_as_map_key() {
    cstc::symbol::SymbolSession session;

    std::unordered_map<cstc::symbol::Symbol, int, cstc::symbol::SymbolHash> map;
    const auto a  = cstc::symbol::Symbol::intern("alpha");
    const auto b  = cstc::symbol::Symbol::intern("beta");
    const auto a2 = cstc::symbol::Symbol::intern("alpha");

    map[a] = 10;
    map[b] = 20;

    assert(map[a2] == 10);  // a2 is same symbol as a
    assert(map[b]  == 20);
    assert(map.size() == 2);
}

void test_large_interning() {
    cstc::symbol::SymbolSession session;

    std::vector<cstc::symbol::Symbol> symbols;
    symbols.reserve(500);
    for (int i = 0; i < 500; ++i)
        symbols.push_back(cstc::symbol::Symbol::intern("var_" + std::to_string(i)));

    for (int i = 0; i < 500; ++i) {
        assert(symbols[i].is_valid());
        assert(symbols[i].as_str() == "var_" + std::to_string(i));
        // Re-interning same text returns the same symbol.
        assert(cstc::symbol::Symbol::intern("var_" + std::to_string(i)) == symbols[i]);
    }

    // All symbols are pairwise distinct.
    for (int i = 0; i < 500; ++i)
        for (int j = i + 1; j < 500; ++j)
            assert(symbols[i] != symbols[j]);
}

void test_empty_string_is_invalid_symbol() {
    cstc::symbol::SymbolSession session;

    // The empty string is pre-registered at index 0, same as kInvalidSymbol.
    const auto empty = cstc::symbol::Symbol::intern("");
    assert(!empty.is_valid());
    assert(empty == cstc::symbol::kInvalidSymbol);
    assert(empty.as_str() == "");
}

void test_sessions_have_independent_interners() {
    // The first user-defined symbol should always land at index 20
    // (after the 19 pre-interned keywords) in a fresh session.
    {
        cstc::symbol::SymbolSession s1;
        const auto sym = cstc::symbol::Symbol::intern("unique_sentinel");
        assert(sym.index == 20);
        assert(sym.as_str() == "unique_sentinel");
    }
    {
        cstc::symbol::SymbolSession s2;
        const auto sym = cstc::symbol::Symbol::intern("unique_sentinel");
        assert(sym.index == 20);
        assert(sym.as_str() == "unique_sentinel");
    }
}

} // namespace

int main() {
    test_all_keywords();
    test_keyword_indices_are_fixed();
    test_intern_matches_keyword();
    test_symbol_as_map_key();
    test_large_interning();
    test_empty_string_is_invalid_symbol();
    test_sessions_have_independent_interners();
    return 0;
}
