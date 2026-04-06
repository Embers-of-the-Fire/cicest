#include <cassert>

#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/type_compat.hpp>

using namespace cstc::tyir;
using namespace cstc::symbol;

static void test_compatible_allows_runtime_promotion() {
    const Symbol handle = Symbol::intern("Handle");
    assert(compatible(
        ty::named(handle), ty::named(handle, kInvalidSymbol, ValueSemantics::Move, true)));
    assert(!compatible(
        ty::named(handle, kInvalidSymbol, ValueSemantics::Move, true), ty::named(handle)));
}

static void test_compatible_handles_refs_recursively() {
    const Symbol handle = Symbol::intern("Handle");
    assert(compatible(
        ty::ref(ty::named(handle)),
        ty::ref(ty::named(handle, kInvalidSymbol, ValueSemantics::Move, true))));
    assert(!compatible(
        ty::ref(ty::named(handle, kInvalidSymbol, ValueSemantics::Move, true)),
        ty::ref(ty::named(handle))));
}

static void test_matches_type_shape_ignores_runtime_tags() {
    const Symbol handle = Symbol::intern("Handle");
    assert(matches_type_shape(
        ty::named(handle, kInvalidSymbol, ValueSemantics::Move, true), ty::named(handle)));
    assert(matches_type_shape(ty::never(), ty::bool_()));
}

static void test_common_type_promotes_runtime_and_display_name() {
    const Symbol handle = Symbol::intern("Handle");
    const Symbol display = Symbol::intern("DisplayHandle");
    const auto joined =
        common_type(ty::named(handle), ty::named(handle, display, ValueSemantics::Move, true));
    assert(joined.has_value());
    assert(joined->is_runtime);
    assert(joined->same_shape_as(ty::named(handle)));
    assert(joined->display_name == display);
}

static void test_common_type_handles_never_and_mismatch() {
    const auto joined = common_type(ty::never(), ty::bool_());
    assert(joined.has_value());
    assert(*joined == ty::bool_());
    assert(!common_type(ty::bool_(), ty::num()).has_value());
}

int main() {
    SymbolSession session;

    test_compatible_allows_runtime_promotion();
    test_compatible_handles_refs_recursively();
    test_matches_type_shape_ignores_runtime_tags();
    test_common_type_promotes_runtime_and_display_name();
    test_common_type_handles_never_and_mismatch();

    return 0;
}
