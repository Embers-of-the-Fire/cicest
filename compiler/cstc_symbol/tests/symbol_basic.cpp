#include <cassert>

#include <cstc_symbol/symbol.hpp>

int main() {
    cstc::symbol::SymbolSession session;

    const cstc::symbol::Symbol alpha_0 = cstc::symbol::Symbol::intern("alpha");
    const cstc::symbol::Symbol alpha_1 = cstc::symbol::Symbol::intern("alpha");
    const cstc::symbol::Symbol beta = cstc::symbol::Symbol::intern("beta");

    assert(alpha_0 == alpha_1);
    assert(alpha_0 != beta);
    assert(alpha_0.is_valid());
    assert(alpha_0.as_str() == "alpha");
    assert(beta.as_str() == "beta");
    assert(alpha_0.is_valid());
    static_assert(!cstc::symbol::kInvalidSymbol.is_valid());

    // kw:: constants are always valid within a session
    assert(cstc::symbol::kw::Struct.as_str() == "struct");
    assert(cstc::symbol::kw::Fn.as_str() == "fn");
    assert(cstc::symbol::kw::UnitLit.as_str() == "()");

    return 0;
}
