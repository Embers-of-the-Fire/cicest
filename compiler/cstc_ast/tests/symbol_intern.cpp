#include <cassert>
#include <cstc_ast/symbol.hpp>

int main() {
    cstc::ast::SymbolTable table;

    // Same string → same symbol.
    const auto foo1 = table.intern("foo");
    const auto foo2 = table.intern("foo");
    assert(foo1 == foo2);

    // Different strings → different symbols.
    const auto bar = table.intern("bar");
    assert(foo1 != bar);

    // Reverse lookup.
    assert(table.str(foo1) == "foo");
    assert(table.str(bar) == "bar");

    // Table size is deduplicated.
    assert(table.size() == 2);

    // SymbolDefinitionSite round-trip.
    cstc::ast::SymbolDefinitionSite site(foo1);
    assert(site.symbol() == foo1);

    return 0;
}
