/// @file lir_types.cpp
/// @brief Tests verifying that LIR correctly reuses the TyIR type system.

#include <cassert>

#include <cstc_lir/lir.hpp>
#include <cstc_symbol/symbol.hpp>

using namespace cstc::lir;
using namespace cstc::symbol;
using namespace cstc::tyir;

static void test_primitive_types() {
    SymbolSession session;
    assert(ty::unit() == ty::unit());
    assert(ty::num() == ty::num());
    assert(ty::str() == ty::str());
    assert(ty::bool_() == ty::bool_());
    assert(ty::never() == ty::never());
}

static void test_named_type() {
    SymbolSession session;
    const Symbol s = Symbol::intern("Point");
    const Ty t = ty::named(s);
    assert(t.kind == TyKind::Named);
    assert(t.name == s);
    assert(t.is_named());
}

static void test_type_equality() {
    SymbolSession session;
    assert(ty::num() == ty::num());
    assert(!(ty::num() == ty::bool_()));
    assert(!(ty::str() == ty::unit()));
    const Symbol s = Symbol::intern("Foo");
    assert(ty::named(s) == ty::named(s));
    assert(!(ty::named(s) == ty::num()));
    assert(ty::ref(ty::str()) == ty::ref(ty::str()));
}

static void test_type_predicates() {
    SymbolSession session;
    assert(ty::unit().is_unit());
    assert(!ty::num().is_unit());
    assert(ty::never().is_never());
    assert(!ty::bool_().is_never());
    assert(ty::named(Symbol::intern("X")).is_named());
    assert(!ty::num().is_named());
    assert(ty::ref(ty::num()).is_ref());
}

static void test_type_display() {
    SymbolSession session;
    assert(ty::unit().display() == "Unit");
    assert(ty::num().display() == "num");
    assert(ty::str().display() == "str");
    assert(ty::bool_().display() == "bool");
    assert(ty::never().display() == "!");
    assert(ty::named(Symbol::intern("MyType")).display() == "MyType");
    assert(ty::ref(ty::str()).display() == "&str");
}

static void test_runtime_type_display() {
    SymbolSession session;
    const Symbol handle = Symbol::intern("Handle");
    assert(
        ty::named(handle, kInvalidSymbol, ValueSemantics::Move, true).display()
        == "runtime Handle");
    assert(ty::ref(ty::str(true)).display() == "&runtime str");
}

static void test_const_ty_consistency() {
    SymbolSession session;
    const Symbol s42 = Symbol::intern("42");
    const Symbol shel = Symbol::intern("hello");
    assert(LirConst::num(s42).ty() == ty::num());
    assert(LirConst::str(shel).ty() == ty::ref(ty::str()));
    assert(LirConst::bool_(true).ty() == ty::bool_());
    assert(LirConst::unit().ty() == ty::unit());
}

static void test_local_decl_various_types() {
    SymbolSession session;

    LirLocalDecl loc_unit;
    loc_unit.id = 0;
    loc_unit.ty = ty::unit();
    assert(loc_unit.ty.is_unit());

    LirLocalDecl loc_num;
    loc_num.id = 1;
    loc_num.ty = ty::num();
    assert(loc_num.ty == ty::num());

    LirLocalDecl loc_named;
    loc_named.id = 2;
    loc_named.ty = ty::named(Symbol::intern("Foo"));
    assert(loc_named.ty.is_named());
    assert(loc_named.ty.name == Symbol::intern("Foo"));
}

int main() {
    test_primitive_types();
    test_named_type();
    test_type_equality();
    test_type_predicates();
    test_type_display();
    test_runtime_type_display();
    test_const_ty_consistency();
    test_local_decl_various_types();
    return 0;
}
