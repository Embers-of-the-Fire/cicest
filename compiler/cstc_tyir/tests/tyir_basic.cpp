#include <cassert>
#include <string>

#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/printer.hpp>
#include <cstc_tyir/tyir.hpp>

using namespace cstc::tyir;
using namespace cstc::symbol;

// ─── Ty helpers ──────────────────────────────────────────────────────────────

static void test_ty_primitives() {
    assert(ty::unit().is_unit());
    assert(!ty::unit().is_never());
    assert(!ty::unit().is_named());

    assert(ty::never().is_never());
    assert(!ty::never().is_unit());

    assert(ty::num() == ty::num());
    assert(ty::num() != ty::str());
    assert(ty::bool_() != ty::unit());

    assert(ty::unit().display() == "Unit");
    assert(ty::num().display() == "num");
    assert(ty::str().display() == "str");
    assert(ty::bool_().display() == "bool");
    assert(ty::never().display() == "!");
}

static void test_ty_ref() {
    const Ty r = ty::ref(ty::str());
    assert(r.is_ref());
    assert(r.is_copy());
    assert(r.display() == "&str");
    assert(r == ty::ref(ty::str()));
}

static void test_ty_named() {
    const Symbol sym = Symbol::intern("Point");
    const Ty t = ty::named(sym);
    assert(t.is_named());
    assert(t.display() == "Point");
    assert(t == ty::named(sym));
    assert(t != ty::named(Symbol::intern("Color")));
}

static void test_runtime_ty() {
    const Symbol handle = Symbol::intern("Handle");
    const Ty runtime_handle = ty::named(handle, kInvalidSymbol, ValueSemantics::Move, true);
    assert(runtime_handle.display() == "runtime Handle");
    assert(runtime_handle.same_shape_as(ty::named(handle)));
    assert(runtime_handle != ty::named(handle));

    const Ty borrowed_runtime_handle = ty::ref(runtime_handle);
    assert(borrowed_runtime_handle.display() == "&runtime Handle");
    assert(borrowed_runtime_handle.same_shape_as(ty::ref(ty::named(handle))));
    assert(borrowed_runtime_handle != ty::ref(ty::named(handle)));
}

static void test_named_shape_distinguishes_nominal_types() {
    const Symbol point = Symbol::intern("Point");
    const Symbol color = Symbol::intern("Color");
    assert(!ty::named(point).same_shape_as(ty::named(color)));
    assert(!ty::ref(ty::named(point)).same_shape_as(ty::ref(ty::named(color))));
}

static void test_ty_equality_distinguishes_value_semantics() {
    const Symbol handle = Symbol::intern("Handle");
    assert(
        ty::named(handle, kInvalidSymbol, ValueSemantics::Move)
        != ty::named(handle, kInvalidSymbol, ValueSemantics::Copy));
}

// ─── TyExpr construction ─────────────────────────────────────────────────────

static void test_make_literal() {
    const TyExprPtr expr =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("42"), false}, ty::num());
    assert(expr != nullptr);
    assert(expr->ty == ty::num());
    assert(std::holds_alternative<TyLiteral>(expr->node));
    assert(std::get<TyLiteral>(expr->node).kind == TyLiteral::Kind::Num);
}

static void test_make_local_ref() {
    const Symbol x = Symbol::intern("x");
    const TyExprPtr expr = make_ty_expr({}, LocalRef{x}, ty::num());
    assert(expr->ty == ty::num());
    assert(std::holds_alternative<LocalRef>(expr->node));
    assert(std::get<LocalRef>(expr->node).name == x);
}

static void test_make_binary() {
    const Symbol x = Symbol::intern("x");
    const Symbol y = Symbol::intern("y");
    const TyExprPtr lhs = make_ty_expr({}, LocalRef{x}, ty::num());
    const TyExprPtr rhs = make_ty_expr({}, LocalRef{y}, ty::num());
    const TyExprPtr bin = make_ty_expr({}, TyBinary{cstc::ast::BinaryOp::Add, lhs, rhs}, ty::num());
    assert(bin->ty == ty::num());
    assert(std::holds_alternative<TyBinary>(bin->node));
}

// ─── TyProgram construction ──────────────────────────────────────────────────

static void test_empty_program() {
    const TyProgram prog;
    assert(prog.items.empty());
    const std::string out = format_program(prog);
    assert(out == "TyProgram\n");
}

static void test_fn_decl() {
    // fn add(x: num, y: num) -> num { x + y }
    const Symbol add = Symbol::intern("add");
    const Symbol x = Symbol::intern("x");
    const Symbol y = Symbol::intern("y");

    auto lhs = make_ty_expr({}, LocalRef{x}, ty::num());
    auto rhs = make_ty_expr({}, LocalRef{y}, ty::num());
    auto sum = make_ty_expr({}, TyBinary{cstc::ast::BinaryOp::Add, lhs, rhs}, ty::num());

    auto body = std::make_shared<TyBlock>();
    body->tail = sum;
    body->ty = ty::num();

    TyFnDecl fn;
    fn.name = add;
    fn.params = {
        TyParam{x, ty::num(), {}},
        TyParam{y, ty::num(), {}}
    };
    fn.return_ty = ty::num();
    fn.body = body;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(out.find("TyProgram") != std::string::npos);
    assert(out.find("TyFnDecl add") != std::string::npos);
    assert(out.find("-> num") != std::string::npos);
    assert(out.find("TyBinary(+): num") != std::string::npos);
    assert(out.find("TyLocal(x): num") != std::string::npos);
}

int main() {
    SymbolSession session;

    test_ty_primitives();
    test_ty_ref();
    test_ty_named();
    test_runtime_ty();
    test_named_shape_distinguishes_nominal_types();
    test_ty_equality_distinguishes_value_semantics();
    test_make_literal();
    test_make_local_ref();
    test_make_binary();
    test_empty_program();
    test_fn_decl();

    return 0;
}
