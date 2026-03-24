#include <cassert>
#include <memory>
#include <string>

#include <cstc_ast/ast.hpp>
#include <cstc_ast/printer.hpp>
#include <cstc_symbol/symbol.hpp>

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

cstc::ast::ExprPtr num(std::string_view text) {
    return cstc::ast::make_expr(
        {}, cstc::ast::LiteralExpr{
                .kind = cstc::ast::LiteralExpr::Kind::Num,
                .symbol = cstc::symbol::Symbol::intern(text),
            });
}

cstc::ast::ExprPtr boolean(bool value) {
    return cstc::ast::make_expr(
        {}, cstc::ast::LiteralExpr{
                .kind = cstc::ast::LiteralExpr::Kind::Bool,
                .symbol = value ? cstc::symbol::kw::True_ : cstc::symbol::kw::False_,
                .bool_value = value,
            });
}

cstc::ast::ExprPtr unit_lit() {
    return cstc::ast::make_expr(
        {}, cstc::ast::LiteralExpr{
                .kind = cstc::ast::LiteralExpr::Kind::Unit, .symbol = cstc::symbol::kw::UnitLit});
}

cstc::ast::ExprPtr str_lit(std::string_view text) {
    return cstc::ast::make_expr(
        {}, cstc::ast::LiteralExpr{
                .kind = cstc::ast::LiteralExpr::Kind::Str,
                .symbol = cstc::symbol::Symbol::intern(text),
            });
}

cstc::ast::ExprPtr path(std::string_view name) {
    return cstc::ast::make_expr(
        {}, cstc::ast::PathExpr{.head = cstc::symbol::Symbol::intern(name), .tail = std::nullopt});
}

cstc::ast::ExprPtr path2(std::string_view head, std::string_view tail) {
    return cstc::ast::make_expr(
        {}, cstc::ast::PathExpr{
                .head = cstc::symbol::Symbol::intern(head),
                .tail = cstc::symbol::Symbol::intern(tail),
            });
}

cstc::ast::ExprPtr unary(cstc::ast::UnaryOp op, cstc::ast::ExprPtr rhs) {
    return cstc::ast::make_expr({}, cstc::ast::UnaryExpr{.op = op, .rhs = std::move(rhs)});
}

cstc::ast::ExprPtr binary(cstc::ast::BinaryOp op, cstc::ast::ExprPtr lhs, cstc::ast::ExprPtr rhs) {
    return cstc::ast::make_expr(
        {}, cstc::ast::BinaryExpr{.op = op, .lhs = std::move(lhs), .rhs = std::move(rhs)});
}

cstc::ast::ExprPtr field_access(cstc::ast::ExprPtr base, std::string_view field) {
    return cstc::ast::make_expr(
        {}, cstc::ast::FieldAccessExpr{
                .base = std::move(base),
                .field = cstc::symbol::Symbol::intern(field),
            });
}

cstc::ast::BlockPtr empty_block() { return std::make_shared<cstc::ast::BlockExpr>(); }

cstc::ast::BlockPtr block_with_tail(cstc::ast::ExprPtr tail) {
    auto blk = std::make_shared<cstc::ast::BlockExpr>();
    blk->tail = std::move(tail);
    return blk;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_program_header() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    const std::string out = cstc::ast::format_program(prog);
    assert(out == "Program\n");
}

void test_zst_struct() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::StructDecl s;
    s.name = cstc::symbol::Symbol::intern("Empty");
    s.is_zst = true;
    prog.items.push_back(std::move(s));
    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("StructDecl Empty ;") != std::string::npos);
}

void test_struct_with_various_field_types() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::StructDecl s;
    s.name = cstc::symbol::Symbol::intern("Foo");
    s.fields.push_back({
        .name = cstc::symbol::Symbol::intern("a"),
        .type = {cstc::ast::TypeKind::Num, cstc::symbol::Symbol::intern("num"), {}, nullptr},
        .span = {}
    });
    s.fields.push_back({
        .name = cstc::symbol::Symbol::intern("b"),
        .type = {cstc::ast::TypeKind::Str, cstc::symbol::Symbol::intern("str"), {}, nullptr},
        .span = {}
    });
    s.fields.push_back({
        .name = cstc::symbol::Symbol::intern("c"),
        .type = {cstc::ast::TypeKind::Bool, cstc::symbol::Symbol::intern("bool"), {}, nullptr},
        .span = {}
    });
    s.fields.push_back({
        .name = cstc::symbol::Symbol::intern("d"),
        .type = {cstc::ast::TypeKind::Unit, cstc::symbol::Symbol::intern("Unit"), {}, nullptr},
        .span = {}
    });
    s.fields.push_back({
        .name = cstc::symbol::Symbol::intern("e"),
        .type = {cstc::ast::TypeKind::Named, cstc::symbol::Symbol::intern("Bar"), {}, nullptr},
        .span = {}
    });
    prog.items.push_back(std::move(s));
    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("a: num") != std::string::npos);
    assert(out.find("b: str") != std::string::npos);
    assert(out.find("c: bool") != std::string::npos);
    assert(out.find("d: Unit") != std::string::npos);
    assert(out.find("e: Bar") != std::string::npos);
}

void test_struct_with_ref_field_type() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::StructDecl s;
    s.name = cstc::symbol::Symbol::intern("View");
    s.fields.push_back({
        .name = cstc::symbol::Symbol::intern("name"),
        .type =
            cstc::ast::TypeRef{
                               .kind = cstc::ast::TypeKind::Ref,
                               .symbol = cstc::symbol::kInvalidSymbol,
                               .display_name = cstc::symbol::kInvalidSymbol,
                               .pointee = std::make_shared<cstc::ast::TypeRef>(cstc::ast::TypeRef{
                    .kind = cstc::ast::TypeKind::Str,
                    .symbol = cstc::symbol::Symbol::intern("str"),
                    .display_name = cstc::symbol::kInvalidSymbol,
                    .pointee = nullptr,
                }),
                               },
        .span = {},
    });
    prog.items.push_back(std::move(s));
    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("name: &str") != std::string::npos);
}

void test_struct_attributes_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::StructDecl s;
    s.name = cstc::symbol::Symbol::intern("Tagged");
    s.is_zst = true;
    s.attributes.push_back({
        .name = cstc::symbol::Symbol::intern("foo"),
        .value = std::nullopt,
        .span = {},
    });
    s.attributes.push_back({
        .name = cstc::symbol::Symbol::intern("bar"),
        .value = cstc::symbol::Symbol::intern("baz"),
        .span = {},
    });
    prog.items.push_back(std::move(s));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Attribute [[foo]]") != std::string::npos);
    assert(out.find("Attribute [[bar = \"baz\"]]") != std::string::npos);
    assert(out.find("StructDecl Tagged ;") != std::string::npos);
}

void test_struct_attribute_values_are_escaped() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::StructDecl s;
    s.name = cstc::symbol::Symbol::intern("Escaped");
    s.is_zst = true;
    s.attributes.push_back({
        .name = cstc::symbol::Symbol::intern("value"),
        .value = cstc::symbol::Symbol::intern("quote\" slash\\ line\nnext"),
        .span = {},
    });
    prog.items.push_back(std::move(s));

    const std::string out = cstc::ast::format_program(prog);
    assert(
        out.find("Attribute [[value = \"quote\\\" slash\\\\ line\\nnext\"]]") != std::string::npos);
}

void test_enum_plain_variants() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::EnumDecl e;
    e.name = cstc::symbol::Symbol::intern("Color");
    e.variants.push_back(
        {.name = cstc::symbol::Symbol::intern("Red"), .discriminant = std::nullopt, .span = {}});
    e.variants.push_back(
        {.name = cstc::symbol::Symbol::intern("Green"), .discriminant = std::nullopt, .span = {}});
    e.variants.push_back(
        {.name = cstc::symbol::Symbol::intern("Blue"), .discriminant = std::nullopt, .span = {}});
    prog.items.push_back(std::move(e));
    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("EnumDecl Color") != std::string::npos);
    assert(out.find("Red") != std::string::npos);
    assert(out.find("Green") != std::string::npos);
    assert(out.find("Blue") != std::string::npos);
    // No discriminant lines
    assert(out.find(" = ") == std::string::npos);
}

void test_enum_with_discriminants() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::EnumDecl e;
    e.name = cstc::symbol::Symbol::intern("Status");
    e.variants.push_back({
        .name = cstc::symbol::Symbol::intern("Ok"),
        .discriminant = cstc::symbol::Symbol::intern("0"),
        .span = {},
    });
    e.variants.push_back({
        .name = cstc::symbol::Symbol::intern("Err"),
        .discriminant = cstc::symbol::Symbol::intern("1"),
        .span = {},
    });
    prog.items.push_back(std::move(e));
    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Ok = 0") != std::string::npos);
    assert(out.find("Err = 1") != std::string::npos);
}

void test_fn_decl_no_params_no_return() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("noop");
    fn.body = empty_block();
    prog.items.push_back(std::move(fn));
    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("FnDecl noop()") != std::string::npos);
    // No " -> " since there is no return type.
    assert(out.find("FnDecl noop() ->") == std::string::npos);
}

void test_fn_decl_with_params_and_return() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("add");
    fn.params.push_back({
        .name = cstc::symbol::Symbol::intern("a"),
        .type = {cstc::ast::TypeKind::Num, cstc::symbol::Symbol::intern("num"), {}, nullptr},
        .span = {},
    });
    fn.params.push_back({
        .name = cstc::symbol::Symbol::intern("b"),
        .type = {cstc::ast::TypeKind::Num, cstc::symbol::Symbol::intern("num"), {}, nullptr},
        .span = {},
    });
    fn.return_type = cstc::ast::TypeRef{
        cstc::ast::TypeKind::Num, cstc::symbol::Symbol::intern("num"), {}, nullptr};
    fn.body = empty_block();
    prog.items.push_back(std::move(fn));
    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("FnDecl add(a: num, b: num) -> num") != std::string::npos);
}

void test_extern_fn_attributes_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::ExternFnDecl fn;
    fn.abi = cstc::symbol::Symbol::intern("c");
    fn.name = cstc::symbol::Symbol::intern("puts");
    fn.params.push_back({
        .name = cstc::symbol::Symbol::intern("s"),
        .type = {cstc::ast::TypeKind::Str, cstc::symbol::Symbol::intern("str"), {}, nullptr},
        .span = {},
    });
    fn.attributes.push_back({
        .name = cstc::symbol::Symbol::intern("link"),
        .value = cstc::symbol::Symbol::intern("puts"),
        .span = {},
    });
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Attribute [[link = \"puts\"]]") != std::string::npos);
    assert(out.find("ExternFnDecl \"c\" puts(s: str)") != std::string::npos);
}

void test_literals_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");

    // Place literals as statements, tail = unit
    fn.body = std::make_shared<cstc::ast::BlockExpr>();
    fn.body->statements.push_back(cstc::ast::ExprStmt{.expr = num("42"), .span = {}});
    fn.body->statements.push_back(cstc::ast::ExprStmt{.expr = str_lit("\"hi\""), .span = {}});
    fn.body->statements.push_back(cstc::ast::ExprStmt{.expr = boolean(true), .span = {}});
    fn.body->statements.push_back(cstc::ast::ExprStmt{.expr = boolean(false), .span = {}});
    fn.body->tail = unit_lit();
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("NumLit(42)") != std::string::npos);
    assert(out.find("StrLit(\"hi\")") != std::string::npos);
    assert(out.find("BoolLit(true)") != std::string::npos);
    assert(out.find("BoolLit(false)") != std::string::npos);
    assert(out.find("UnitLit") != std::string::npos);
}

void test_path_expr_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = std::make_shared<cstc::ast::BlockExpr>();
    fn.body->statements.push_back(cstc::ast::ExprStmt{.expr = path("myVar"), .span = {}});
    fn.body->tail = path2("State", "Running");
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Path(myVar)") != std::string::npos);
    assert(out.find("Path(State::Running)") != std::string::npos);
}

void test_unary_ops_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = std::make_shared<cstc::ast::BlockExpr>();
    fn.body->statements.push_back(
        cstc::ast::ExprStmt{.expr = unary(cstc::ast::UnaryOp::Negate, num("1")), .span = {}});
    fn.body->tail = unary(cstc::ast::UnaryOp::Not, boolean(true));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Unary(-)") != std::string::npos);
    assert(out.find("Unary(!)") != std::string::npos);
}

void test_borrow_expr_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("borrow");
    fn.body = std::make_shared<cstc::ast::BlockExpr>();
    fn.body->tail = unary(cstc::ast::UnaryOp::Borrow, path("value"));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Unary(&)") != std::string::npos);
}

void test_binary_ops_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = std::make_shared<cstc::ast::BlockExpr>();

    const std::array<cstc::ast::BinaryOp, 13> ops = {
        cstc::ast::BinaryOp::Add, cstc::ast::BinaryOp::Sub, cstc::ast::BinaryOp::Mul,
        cstc::ast::BinaryOp::Div, cstc::ast::BinaryOp::Mod, cstc::ast::BinaryOp::Eq,
        cstc::ast::BinaryOp::Ne,  cstc::ast::BinaryOp::Lt,  cstc::ast::BinaryOp::Le,
        cstc::ast::BinaryOp::Gt,  cstc::ast::BinaryOp::Ge,  cstc::ast::BinaryOp::And,
        cstc::ast::BinaryOp::Or,
    };
    for (const auto op : ops)
        fn.body->statements.push_back(
            cstc::ast::ExprStmt{.expr = binary(op, num("1"), num("2")), .span = {}});
    fn.body->tail = num("0");
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Binary(+)") != std::string::npos);
    assert(out.find("Binary(-)") != std::string::npos);
    assert(out.find("Binary(*)") != std::string::npos);
    assert(out.find("Binary(/)") != std::string::npos);
    assert(out.find("Binary(%)") != std::string::npos);
    assert(out.find("Binary(==)") != std::string::npos);
    assert(out.find("Binary(!=)") != std::string::npos);
    assert(out.find("Binary(<)") != std::string::npos);
    assert(out.find("Binary(<=)") != std::string::npos);
    assert(out.find("Binary(>)") != std::string::npos);
    assert(out.find("Binary(>=)") != std::string::npos);
    assert(out.find("Binary(&&)") != std::string::npos);
    assert(out.find("Binary(||)") != std::string::npos);
}

void test_field_access_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = block_with_tail(field_access(path("obj"), "field"));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("FieldAccess(field)") != std::string::npos);
    assert(out.find("Path(obj)") != std::string::npos);
}

void test_call_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");

    cstc::ast::CallExpr call;
    call.callee = path("foo");
    call.args.push_back(num("1"));
    call.args.push_back(num("2"));
    fn.body = block_with_tail(cstc::ast::make_expr({}, std::move(call)));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Call") != std::string::npos);
    assert(out.find("Callee") != std::string::npos);
    assert(out.find("Arg") != std::string::npos);
}

void test_struct_init_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");

    cstc::ast::StructInitExpr init;
    init.type_name = cstc::symbol::Symbol::intern("Point");
    init.fields.push_back({
        .name = cstc::symbol::Symbol::intern("x"),
        .value = num("1"),
        .span = {},
    });
    fn.body = block_with_tail(cstc::ast::make_expr({}, std::move(init)));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("StructInit(Point)") != std::string::npos);
    assert(out.find("x:") != std::string::npos);
}

void test_if_with_else_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");

    cstc::ast::IfExpr if_expr;
    if_expr.condition = path("cond");
    if_expr.then_block = block_with_tail(num("1"));
    if_expr.else_branch = cstc::ast::make_expr({}, block_with_tail(num("2")));
    fn.body = block_with_tail(cstc::ast::make_expr({}, std::move(if_expr)));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("If") != std::string::npos);
    assert(out.find("Condition") != std::string::npos);
    assert(out.find("Then") != std::string::npos);
    assert(out.find("Else") != std::string::npos);
}

void test_if_no_else_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");

    cstc::ast::IfExpr if_expr;
    if_expr.condition = path("x");
    if_expr.then_block = empty_block();
    fn.body = block_with_tail(cstc::ast::make_expr({}, std::move(if_expr)));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("If") != std::string::npos);
    assert(out.find("Else") == std::string::npos);
}

void test_loop_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");

    cstc::ast::LoopExpr loop_expr;
    loop_expr.body = block_with_tail(cstc::ast::make_expr({}, cstc::ast::BreakExpr{}));
    fn.body = block_with_tail(cstc::ast::make_expr({}, std::move(loop_expr)));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Loop") != std::string::npos);
    assert(out.find("Break") != std::string::npos);
}

void test_while_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");

    cstc::ast::WhileExpr while_expr;
    while_expr.condition = path("cond");
    while_expr.body = block_with_tail(cstc::ast::make_expr({}, cstc::ast::ContinueExpr{}));
    fn.body = block_with_tail(cstc::ast::make_expr({}, std::move(while_expr)));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("While") != std::string::npos);
    assert(out.find("Condition") != std::string::npos);
    assert(out.find("Continue") != std::string::npos);
}

void test_for_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");

    cstc::ast::ForExpr for_expr;
    for_expr.init = cstc::ast::ForInitLet{
        .discard = false,
        .name = cstc::symbol::Symbol::intern("i"),
        .type_annotation =
            cstc::ast::TypeRef{
                               cstc::ast::TypeKind::Num, cstc::symbol::Symbol::intern("num"), {}, nullptr},
        .initializer = num("0"),
        .span = {},
    };
    for_expr.condition = binary(cstc::ast::BinaryOp::Lt, path("i"), num("10"));
    for_expr.step = path("i");
    for_expr.body = empty_block();
    fn.body = block_with_tail(cstc::ast::make_expr({}, std::move(for_expr)));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("For") != std::string::npos);
    assert(out.find("Init") != std::string::npos);
    assert(out.find("Let i: num") != std::string::npos);
    assert(out.find("Condition") != std::string::npos);
    assert(out.find("Step") != std::string::npos);
    assert(out.find("Body") != std::string::npos);
}

void test_break_with_value_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = block_with_tail(cstc::ast::make_expr({}, cstc::ast::BreakExpr{.value = num("42")}));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Break") != std::string::npos);
    assert(out.find("NumLit(42)") != std::string::npos);
}

void test_return_with_value_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = block_with_tail(cstc::ast::make_expr({}, cstc::ast::ReturnExpr{.value = num("7")}));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Return") != std::string::npos);
    assert(out.find("NumLit(7)") != std::string::npos);
}

void test_let_stmt_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = std::make_shared<cstc::ast::BlockExpr>();
    fn.body->statements.push_back(
        cstc::ast::LetStmt{
            .discard = false,
            .name = cstc::symbol::Symbol::intern("x"),
            .type_annotation =
                cstc::ast::TypeRef{
                                   cstc::ast::TypeKind::Num, cstc::symbol::Symbol::intern("num"), {}, nullptr},
            .initializer = num("5"),
            .span = {},
    });
    fn.body->tail = path("x");
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Let x: num =") != std::string::npos);
}

void test_let_discard_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = std::make_shared<cstc::ast::BlockExpr>();
    fn.body->statements.push_back(
        cstc::ast::LetStmt{
            .discard = true,
            .name = cstc::symbol::kInvalidSymbol,
            .type_annotation = std::nullopt,
            .initializer = num("0"),
            .span = {},
        });
    fn.body->tail = unit_lit();
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Let _") != std::string::npos);
}

void test_expr_stmt_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = std::make_shared<cstc::ast::BlockExpr>();
    fn.body->statements.push_back(cstc::ast::ExprStmt{.expr = num("1"), .span = {}});
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("ExprStmt") != std::string::npos);
}

void test_block_tail_rendered() {
    cstc::symbol::SymbolSession session;
    cstc::ast::Program prog;
    cstc::ast::FnDecl fn;
    fn.name = cstc::symbol::Symbol::intern("f");
    fn.body = block_with_tail(num("99"));
    prog.items.push_back(std::move(fn));

    const std::string out = cstc::ast::format_program(prog);
    assert(out.find("Block") != std::string::npos);
    assert(out.find("Tail") != std::string::npos);
    assert(out.find("NumLit(99)") != std::string::npos);
}

} // namespace

int main() {
    test_program_header();
    test_zst_struct();
    test_struct_with_various_field_types();
    test_struct_with_ref_field_type();
    test_struct_attributes_rendered();
    test_struct_attribute_values_are_escaped();
    test_enum_plain_variants();
    test_enum_with_discriminants();
    test_fn_decl_no_params_no_return();
    test_fn_decl_with_params_and_return();
    test_extern_fn_attributes_rendered();
    test_literals_rendered();
    test_path_expr_rendered();
    test_unary_ops_rendered();
    test_borrow_expr_rendered();
    test_binary_ops_rendered();
    test_field_access_rendered();
    test_call_rendered();
    test_struct_init_rendered();
    test_if_with_else_rendered();
    test_if_no_else_rendered();
    test_loop_rendered();
    test_while_rendered();
    test_for_rendered();
    test_break_with_value_rendered();
    test_return_with_value_rendered();
    test_let_stmt_rendered();
    test_let_discard_rendered();
    test_expr_stmt_rendered();
    test_block_tail_rendered();
    return 0;
}
