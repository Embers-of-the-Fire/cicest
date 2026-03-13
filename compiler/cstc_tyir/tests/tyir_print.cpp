#include <cassert>
#include <memory>
#include <string>

#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/printer.hpp>
#include <cstc_tyir/tyir.hpp>

using namespace cstc::tyir;
using namespace cstc::symbol;
using namespace cstc::ast;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// ─── Struct / Enum declarations ──────────────────────────────────────────────

static void test_print_struct() {
    TyStructDecl decl;
    decl.name = Symbol::intern("Point");
    decl.is_zst = false;
    decl.fields = {
        TyFieldDecl{Symbol::intern("x"), ty::num(), {}},
        TyFieldDecl{Symbol::intern("y"), ty::num(), {}},
    };

    TyProgram prog;
    prog.items.push_back(std::move(decl));

    const std::string out = format_program(prog);
    assert(contains(out, "TyStructDecl Point"));
    assert(contains(out, "x: num"));
    assert(contains(out, "y: num"));
}

static void test_print_zst_struct() {
    TyStructDecl decl;
    decl.name = Symbol::intern("Marker");
    decl.is_zst = true;

    TyProgram prog;
    prog.items.push_back(std::move(decl));
    const std::string out = format_program(prog);
    assert(contains(out, "TyStructDecl Marker ;"));
}

static void test_print_enum() {
    TyEnumDecl decl;
    decl.name = Symbol::intern("Color");
    decl.variants = {
        TyEnumVariant{  Symbol::intern("Red"), std::nullopt, {}},
        TyEnumVariant{Symbol::intern("Green"), std::nullopt, {}},
        TyEnumVariant{ Symbol::intern("Blue"), std::nullopt, {}},
    };

    TyProgram prog;
    prog.items.push_back(std::move(decl));
    const std::string out = format_program(prog);
    assert(contains(out, "TyEnumDecl Color"));
    assert(contains(out, "Red"));
    assert(contains(out, "Green"));
    assert(contains(out, "Blue"));
}

static void test_print_enum_with_discriminant() {
    TyEnumDecl decl;
    decl.name = Symbol::intern("Status");
    decl.variants = {
        TyEnumVariant{   Symbol::intern("Ok"), Symbol::intern("0"), {}},
        TyEnumVariant{Symbol::intern("Error"), Symbol::intern("1"), {}},
    };

    TyProgram prog;
    prog.items.push_back(std::move(decl));
    const std::string out = format_program(prog);
    assert(contains(out, "Ok = 0"));
    assert(contains(out, "Error = 1"));
}

// ─── Literal expressions ─────────────────────────────────────────────────────

static void test_print_literals() {
    auto num_lit =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("42"), false}, ty::num());
    auto str_lit =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Str, Symbol::intern("hi"), false}, ty::str());
    auto bool_true =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Bool, kInvalidSymbol, true}, ty::bool_());
    auto bool_false =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Bool, kInvalidSymbol, false}, ty::bool_());
    auto unit_lit =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Unit, kInvalidSymbol, false}, ty::unit());

    auto block = std::make_shared<TyBlock>();
    block->stmts = {
        TyExprStmt{   num_lit, {}},
        TyExprStmt{   str_lit, {}},
        TyExprStmt{ bool_true, {}},
        TyExprStmt{bool_false, {}},
        TyExprStmt{  unit_lit, {}},
    };
    block->ty = ty::unit();

    TyFnDecl fn;
    fn.name = Symbol::intern("literals");
    fn.return_ty = ty::unit();
    fn.body = block;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyLiteral(42): num"));
    assert(contains(out, "TyLiteral(\"hi\"): str"));
    assert(contains(out, "TyLiteral(true): bool"));
    assert(contains(out, "TyLiteral(false): bool"));
    assert(contains(out, "TyLiteral(()): Unit"));
}

// ─── Path and reference expressions ─────────────────────────────────────────

static void test_print_local_ref() {
    const Symbol x = Symbol::intern("x");
    const TyExprPtr ex = make_ty_expr({}, LocalRef{x}, ty::num());

    auto block = std::make_shared<TyBlock>();
    block->tail = ex;
    block->ty = ty::num();

    TyFnDecl fn;
    fn.name = Symbol::intern("f");
    fn.params = {
        TyParam{x, ty::num(), {}}
    };
    fn.return_ty = ty::num();
    fn.body = block;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyLocal(x): num"));
}

static void test_print_enum_variant_ref() {
    const Symbol col = Symbol::intern("Color");
    const Symbol red = Symbol::intern("Red");
    const TyExprPtr ev = make_ty_expr({}, EnumVariantRef{col, red}, ty::named(col));

    auto block = std::make_shared<TyBlock>();
    block->tail = ev;
    block->ty = ty::named(col);

    TyFnDecl fn;
    fn.name = Symbol::intern("mk");
    fn.return_ty = ty::named(col);
    fn.body = block;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyVariant(Color::Red): Color"));
}

// ─── Struct init ─────────────────────────────────────────────────────────────

static void test_print_struct_init() {
    const Symbol pt = Symbol::intern("Point");
    const Symbol x = Symbol::intern("x");
    const Symbol y = Symbol::intern("y");

    TyStructInit init;
    init.type_name = pt;
    init.fields = {
        TyStructInitField{
                          x, make_ty_expr(
 {}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("1"), false}, ty::num()),
                          {}},
        TyStructInitField{
                          y, make_ty_expr(
 {}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("2"), false}, ty::num()),
                          {}},
    };

    auto block = std::make_shared<TyBlock>();
    block->tail = make_ty_expr({}, std::move(init), ty::named(pt));
    block->ty = ty::named(pt);

    TyFnDecl fn;
    fn.name = Symbol::intern("origin");
    fn.return_ty = ty::named(pt);
    fn.body = block;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyStructInit(Point): Point"));
    assert(contains(out, "x:"));
    assert(contains(out, "y:"));
}

// ─── Control-flow expressions ────────────────────────────────────────────────

static void test_print_if_else() {
    const Symbol flag = Symbol::intern("flag");
    auto cond = make_ty_expr({}, LocalRef{flag}, ty::bool_());

    auto then_block = std::make_shared<TyBlock>();
    then_block->tail =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("1"), false}, ty::num());
    then_block->ty = ty::num();

    auto else_block = std::make_shared<TyBlock>();
    else_block->tail =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("2"), false}, ty::num());
    else_block->ty = ty::num();

    TyIf iff;
    iff.condition = cond;
    iff.then_block = then_block;
    iff.else_branch = make_ty_expr({}, std::move(else_block), ty::num());

    auto outer = std::make_shared<TyBlock>();
    outer->tail = make_ty_expr({}, std::move(iff), ty::num());
    outer->ty = ty::num();

    TyFnDecl fn;
    fn.name = Symbol::intern("choose");
    fn.params = {
        TyParam{flag, ty::bool_(), {}}
    };
    fn.return_ty = ty::num();
    fn.body = outer;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyIf: num"));
    assert(contains(out, "Condition"));
    assert(contains(out, "Then"));
    assert(contains(out, "Else"));
}

static void test_print_loop_break_continue() {
    auto brk = make_ty_expr({}, TyBreak{std::nullopt}, ty::never());
    auto cont = make_ty_expr({}, TyContinue{}, ty::never());

    auto body = std::make_shared<TyBlock>();
    body->stmts = {
        TyExprStmt{ brk, {}},
        TyExprStmt{cont, {}}
    };
    body->ty = ty::unit();

    auto outer = std::make_shared<TyBlock>();
    outer->stmts = {
        TyExprStmt{make_ty_expr({}, TyLoop{body}, ty::unit()), {}}
    };
    outer->ty = ty::unit();

    TyFnDecl fn;
    fn.name = Symbol::intern("spin");
    fn.return_ty = ty::unit();
    fn.body = outer;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyLoop: Unit"));
    assert(contains(out, "TyBreak: !"));
    assert(contains(out, "TyContinue: !"));
}

static void test_print_return() {
    auto ret_val =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("0"), false}, ty::num());
    auto ret = make_ty_expr({}, TyReturn{ret_val}, ty::never());

    auto body = std::make_shared<TyBlock>();
    body->stmts = {
        TyExprStmt{ret, {}}
    };
    body->ty = ty::unit();

    TyFnDecl fn;
    fn.name = Symbol::intern("early");
    fn.return_ty = ty::num();
    fn.body = body;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyReturn: !"));
}

static void test_print_while() {
    const Symbol i = Symbol::intern("i");
    auto cond = make_ty_expr({}, LocalRef{i}, ty::bool_());

    auto body = std::make_shared<TyBlock>();
    body->ty = ty::unit();

    auto outer = std::make_shared<TyBlock>();
    outer->stmts = {
        TyExprStmt{make_ty_expr({}, TyWhile{cond, body}, ty::unit()), {}}
    };
    outer->ty = ty::unit();

    TyFnDecl fn;
    fn.name = Symbol::intern("spin2");
    fn.return_ty = ty::unit();
    fn.body = outer;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyWhile: Unit"));
    assert(contains(out, "Condition"));
    assert(contains(out, "Body"));
}

static void test_print_for() {
    const Symbol i = Symbol::intern("i");

    TyForInit init;
    init.discard = false;
    init.name = i;
    init.ty = ty::num();
    init.init =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("0"), false}, ty::num());

    auto cond = make_ty_expr(
        {},
        TyBinary{
            BinaryOp::Lt, make_ty_expr({}, LocalRef{i}, ty::num()),
            make_ty_expr(
                {}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("10"), false}, ty::num())},
        ty::bool_());

    auto body = std::make_shared<TyBlock>();
    body->ty = ty::unit();

    TyFor for_expr;
    for_expr.init = std::move(init);
    for_expr.condition = cond;
    for_expr.body = body;

    auto outer = std::make_shared<TyBlock>();
    outer->stmts = {
        TyExprStmt{make_ty_expr({}, std::move(for_expr), ty::unit()), {}}
    };
    outer->ty = ty::unit();

    TyFnDecl fn;
    fn.name = Symbol::intern("count");
    fn.return_ty = ty::unit();
    fn.body = outer;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyFor: Unit"));
    assert(contains(out, "Let i: num ="));
    assert(contains(out, "Condition"));
    assert(contains(out, "TyBinary(<): bool"));
}

// ─── Call expression ─────────────────────────────────────────────────────────

static void test_print_call() {
    const Symbol foo = Symbol::intern("foo");
    const Symbol x = Symbol::intern("x");

    auto arg = make_ty_expr({}, LocalRef{x}, ty::num());
    auto call = make_ty_expr({}, TyCall{foo, {arg}}, ty::bool_());

    auto block = std::make_shared<TyBlock>();
    block->tail = call;
    block->ty = ty::bool_();

    TyFnDecl fn;
    fn.name = Symbol::intern("bar");
    fn.return_ty = ty::bool_();
    fn.body = block;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyCall(foo): bool"));
    assert(contains(out, "Arg"));
    assert(contains(out, "TyLocal(x): num"));
}

// ─── Let statement ───────────────────────────────────────────────────────────

static void test_print_let_stmt() {
    const Symbol v = Symbol::intern("v");
    auto init =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("99"), false}, ty::num());

    auto block = std::make_shared<TyBlock>();
    block->stmts = {
        TyLetStmt{false, v, ty::num(), init, {}}
    };
    block->ty = ty::unit();

    TyFnDecl fn;
    fn.name = Symbol::intern("g");
    fn.return_ty = ty::unit();
    fn.body = block;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "Let v: num ="));
    assert(contains(out, "TyLiteral(99): num"));
}

static void test_print_discard_let() {
    auto init =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Unit, kInvalidSymbol, false}, ty::unit());

    auto block = std::make_shared<TyBlock>();
    block->stmts = {
        TyLetStmt{true, kInvalidSymbol, ty::unit(), init, {}}
    };
    block->ty = ty::unit();

    TyFnDecl fn;
    fn.name = Symbol::intern("h");
    fn.return_ty = ty::unit();
    fn.body = block;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "Let _: Unit ="));
}

int main() {
    SymbolSession session;

    test_print_struct();
    test_print_zst_struct();
    test_print_enum();
    test_print_enum_with_discriminant();
    test_print_literals();
    test_print_local_ref();
    test_print_enum_variant_ref();
    test_print_struct_init();
    test_print_if_else();
    test_print_loop_break_continue();
    test_print_return();
    test_print_while();
    test_print_for();
    test_print_call();
    test_print_let_stmt();
    test_print_discard_let();

    return 0;
}
