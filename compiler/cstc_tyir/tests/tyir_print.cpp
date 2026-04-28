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

static void test_print_generic_struct() {
    TyStructDecl decl;
    decl.name = Symbol::intern("Box");
    decl.generic_params = {
        GenericParam{Symbol::intern("T"), {}}
    };
    decl.where_clause = {
        GenericConstraint{
                          cstc::ast::make_expr(
                {},
                          cstc::ast::PathExpr{
                    .head = Symbol::intern("ready"),
                    .tail = std::nullopt,
                    .display_head = kInvalidSymbol,
                    .generic_args = {},
                }),
                          {},
                          },
    };
    decl.fields = {
        TyFieldDecl{Symbol::intern("value"), ty::named(Symbol::intern("T")), {}},
    };

    TyProgram prog;
    prog.items.push_back(std::move(decl));

    const std::string out = format_program(prog);
    assert(contains(out, "TyStructDecl Box<T>"));
    assert(contains(out, "Where"));
    assert(contains(out, "value: T"));
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

static void test_print_generic_zst_struct_where_clause() {
    TyStructDecl decl;
    decl.name = Symbol::intern("Marker");
    decl.is_zst = true;
    decl.generic_params = {
        GenericParam{Symbol::intern("T"), {}}
    };
    decl.where_clause = {
        GenericConstraint{
                          cstc::ast::make_expr(
                {},
                          cstc::ast::PathExpr{
                    .head = Symbol::intern("ready"),
                    .tail = std::nullopt,
                    .display_head = kInvalidSymbol,
                    .generic_args = {},
                }),
                          {},
                          },
    };

    TyProgram prog;
    prog.items.push_back(std::move(decl));

    const std::string out = format_program(prog);
    assert(contains(out, "TyStructDecl Marker<T>"));
    assert(contains(out, "Where"));
    assert(contains(out, "ready"));
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

static void test_print_generic_enum() {
    TyEnumDecl decl;
    decl.name = Symbol::intern("Result");
    decl.generic_params = {
        GenericParam{Symbol::intern("T"), {}},
        GenericParam{Symbol::intern("E"), {}},
    };
    decl.variants = {
        TyEnumVariant{ Symbol::intern("Ok"), std::nullopt, {}},
        TyEnumVariant{Symbol::intern("Err"), std::nullopt, {}},
    };

    TyProgram prog;
    prog.items.push_back(std::move(decl));
    const std::string out = format_program(prog);
    assert(contains(out, "TyEnumDecl Result<T, E>"));
}

static void test_print_lang_item_enum() {
    TyEnumDecl decl;
    decl.name = Symbol::intern("Constraint");
    decl.lang_name = Symbol::intern("cstc_constraint");
    decl.variants = {
        TyEnumVariant{  Symbol::intern("Valid"), std::nullopt, {}},
        TyEnumVariant{Symbol::intern("Invalid"), std::nullopt, {}},
    };

    TyProgram prog;
    prog.items.push_back(std::move(decl));

    const std::string out = format_program(prog);
    assert(contains(out, "TyEnumDecl Constraint [[lang = \"cstc_constraint\"]]"));
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

static void test_print_runtime_items() {
    TyFnDecl fn;
    fn.name = Symbol::intern("dispatch");
    fn.params = {
        TyParam{
                Symbol::intern("job"),
                ty::named(Symbol::intern("Job"), kInvalidSymbol, ValueSemantics::Move, true),
                {},
                },
    };
    fn.return_ty = ty::named(Symbol::intern("Job"), kInvalidSymbol, ValueSemantics::Move, true);
    fn.body = std::make_shared<TyBlock>();
    fn.body->ty = fn.return_ty;

    TyExternFnDecl ext;
    ext.abi = Symbol::intern("lang");
    ext.name = Symbol::intern("poll");
    ext.params = {
        TyParam{
                Symbol::intern("value"),
                ty::ref(ty::str(true)),
                {},
                },
    };
    ext.return_ty = ty::unit();

    TyProgram prog;
    prog.items.push_back(std::move(fn));
    prog.items.push_back(std::move(ext));

    const std::string out = format_program(prog);
    assert(contains(out, "TyFnDecl dispatch(job: runtime Job) -> runtime Job"));
    assert(contains(out, "TyExternFnDecl \"lang\" poll(value: &runtime str) -> Unit"));
}

static void test_print_ct_required_parameter() {
    TyFnDecl fn;
    fn.name = Symbol::intern("reserve");
    fn.params = {
        TyParam{
                Symbol::intern("count"),
                ty::num(),
                {},
                ParamRequirement::CtRequired,
                },
    };
    fn.return_ty = ty::num();
    fn.body = std::make_shared<TyBlock>();
    fn.body->ty = ty::num();

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyFnDecl reserve(count: !runtime num) -> num"));
}

static void test_print_generic_fn_metadata() {
    TyFnDecl fn;
    fn.name = Symbol::intern("id");
    fn.generic_params = {
        GenericParam{Symbol::intern("T"), {}}
    };
    fn.params = {
        TyParam{Symbol::intern("value"), ty::num(), {}}
    };
    fn.return_ty = ty::num();
    fn.body = std::make_shared<TyBlock>();
    fn.body->ty = ty::num();
    fn.where_clause = {
        GenericConstraint{
                          cstc::ast::make_expr(
                {},
                          cstc::ast::PathExpr{
                    .head = Symbol::intern("T"),
                    .tail = std::nullopt,
                    .display_head = kInvalidSymbol,
                    .generic_args = {},
                }),
                          {},
                          }
    };

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyFnDecl id<T>(value: num) -> num"));
    assert(contains(out, "Where"));
}

static void test_print_call_and_struct_init_generic_args() {
    TyFnDecl fn;
    fn.name = Symbol::intern("main");
    fn.return_ty = ty::unit();
    fn.body = std::make_shared<TyBlock>();
    fn.body->ty = ty::unit();

    auto call = make_ty_expr(
        {},
        TyCall{
            Symbol::intern("id"),
            {ty::num()},
            {make_ty_expr(
                {}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("1"), false}, ty::num())}},
        ty::num());
    auto init = make_ty_expr(
        {},
        TyStructInit{
            Symbol::intern("Box"),
            {ty::num()},
            {{Symbol::intern("value"),
              make_ty_expr(
                  {}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("1"), false}, ty::num()),
              {}}}},
        ty::named(Symbol::intern("Box"), kInvalidSymbol, ValueSemantics::Move, false, {ty::num()}));
    fn.body->stmts = {
        TyExprStmt{call, {}},
        TyExprStmt{init, {}}
    };

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyCall(id): num"));
    assert(contains(out, "GenericArgs"));
    assert(contains(out, "Box<num>"));
}

static void test_print_deferred_generic_call() {
    TyFnDecl fn;
    fn.name = Symbol::intern("main");
    fn.return_ty = ty::unit();
    fn.body = std::make_shared<TyBlock>();
    fn.body->ty = ty::unit();

    auto deferred = make_ty_expr(
        {},
        TyDeferredGenericCall{
            Symbol::intern("on"),
            {std::nullopt},
            {},
        },
        ty::named(
            Symbol::intern("Flag"), kInvalidSymbol, ValueSemantics::Copy, false,
            {ty::named(Symbol::intern("T"))}));
    fn.body->stmts = {
        TyExprStmt{deferred, {}}
    };

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyDeferredGenericCall(on): Flag<T>"));
    assert(contains(out, "_"));
}

// ─── Literal expressions ─────────────────────────────────────────────────────

static void test_print_literals() {
    auto num_lit =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("42"), false}, ty::num());
    auto str_lit = make_ty_expr(
        {}, TyLiteral{TyLiteral::Kind::Str, Symbol::intern("\"hi\""), false}, ty::ref(ty::str()));
    auto owned_str_lit = make_ty_expr(
        {}, TyLiteral{TyLiteral::Kind::OwnedStr, Symbol::intern("\"owned\""), false}, ty::str());
    auto bool_true =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Bool, kInvalidSymbol, true}, ty::bool_());
    auto bool_false =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Bool, kInvalidSymbol, false}, ty::bool_());
    auto unit_lit =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Unit, kInvalidSymbol, false}, ty::unit());

    auto block = std::make_shared<TyBlock>();
    block->stmts = {
        TyExprStmt{      num_lit, {}},
        TyExprStmt{      str_lit, {}},
        TyExprStmt{owned_str_lit, {}},
        TyExprStmt{    bool_true, {}},
        TyExprStmt{   bool_false, {}},
        TyExprStmt{     unit_lit, {}},
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
    assert(contains(out, "TyLiteral(\"hi\"): &str"));
    assert(!contains(out, "TyLiteral(\"\"hi\"\"): &str"));
    assert(contains(out, "TyLiteral(owned \"owned\"): str"));
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
    auto call = make_ty_expr({}, TyCall{foo, {}, {arg}}, ty::bool_());

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

static void test_print_runtime_block() {
    auto inner = std::make_shared<TyBlock>();
    inner->tail =
        make_ty_expr({}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("3"), false}, ty::num());
    inner->ty = ty::num();

    auto outer = std::make_shared<TyBlock>();
    outer->tail = make_ty_expr({}, TyRuntimeBlock{inner}, ty::num(true));
    outer->ty = ty::num(true);

    TyFnDecl fn;
    fn.name = Symbol::intern("f");
    fn.return_ty = ty::num(true);
    fn.body = outer;

    TyProgram prog;
    prog.items.push_back(std::move(fn));

    const std::string out = format_program(prog);
    assert(contains(out, "TyRuntimeBlock: runtime num"));
    assert(contains(out, "TyLiteral(3): num"));
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
    test_print_generic_struct();
    test_print_zst_struct();
    test_print_generic_zst_struct_where_clause();
    test_print_enum();
    test_print_generic_enum();
    test_print_lang_item_enum();
    test_print_enum_with_discriminant();
    test_print_runtime_items();
    test_print_ct_required_parameter();
    test_print_generic_fn_metadata();
    test_print_call_and_struct_init_generic_args();
    test_print_deferred_generic_call();
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
    test_print_runtime_block();
    test_print_let_stmt();
    test_print_discard_let();

    return 0;
}
