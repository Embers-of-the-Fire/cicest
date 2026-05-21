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

static void test_ty_named_with_generic_args() {
    const Symbol option = Symbol::intern("Option");
    const Ty t = ty::named(option, kInvalidSymbol, ValueSemantics::Move, false, {ty::num()});
    assert(t.is_named());
    assert(t.generic_args.size() == 1);
    assert(t.generic_args[0] == ty::num());
    assert(t.display() == "Option<num>");
    assert(t == ty::named(option, kInvalidSymbol, ValueSemantics::Move, false, {ty::num()}));
    assert(t != ty::named(option, kInvalidSymbol, ValueSemantics::Move, false, {ty::bool_()}));
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

static void test_availability_from_type_detects_nested_runtime_tags() {
    const Symbol box = Symbol::intern("Box");
    const Ty plain_box = ty::named(box, kInvalidSymbol, ValueSemantics::Move, false, {ty::num()});
    const Ty borrowed_runtime_box =
        ty::ref(ty::named(box, kInvalidSymbol, ValueSemantics::Move, false, {ty::num(true)}));

    assert(!ty_contains_runtime_tag(plain_box));
    assert(ty_contains_runtime_tag(borrowed_runtime_box));

    const Availability availability = availability_from_type(borrowed_runtime_box);
    assert(availability.kind == AvailabilityKind::Rt);
    assert(!availability.evidence.has_value());

    const Availability runtime_num = availability_from_type(ty::num(true));
    assert(runtime_num.kind == AvailabilityKind::Rt);
    assert(runtime_num.evidence == std::nullopt);
}

static void test_availability_join_preserves_first_evidence() {
    const Availability ct = availability_ct();
    const Availability runtime_from_type = availability_from_type(ty::num(true));
    assert(is_ct_available(ct));
    assert(!is_ct_available(runtime_from_type));
    assert(!runtime_from_type.evidence.has_value());

    const TyRuntimeEvidence first{
        {1, 2},
        "first"
    };
    const TyRuntimeEvidence second{
        {3, 4},
        "second"
    };
    const Availability joined = availability_join(availability_rt(first), availability_rt(second));
    assert(!is_ct_available(joined));
    assert(joined.evidence.has_value());
    assert(joined.evidence->span.start == 1);
    assert(joined.evidence->reason == "first");

    const Availability joined_fallback =
        availability_join(runtime_from_type, availability_rt(second));
    assert(joined_fallback.evidence.has_value());
    assert(joined_fallback.evidence->span.start == 3);
    assert(joined_fallback.evidence->reason == "second");
}

static void test_runtime_allowed_param_availability_is_symbolic_ct() {
    const Availability param = availability_runtime_allowed_param();
    assert(is_ct_available(param));
    assert(!is_ct_required_available(param));
    assert(param.depends_on_runtime_allowed_param);

    const Availability indexed_param = availability_runtime_allowed_param(1);
    assert(indexed_param.runtime_allowed_param_indices.size() == 1);
    assert(indexed_param.runtime_allowed_param_indices.contains(1));

    const Availability joined = availability_join(availability_ct(), indexed_param);
    assert(is_ct_available(joined));
    assert(!is_ct_required_available(joined));
    assert(joined.depends_on_runtime_allowed_param);
    assert(joined.runtime_allowed_param_indices.size() == 1);
    assert(joined.runtime_allowed_param_indices.contains(1));
}

static void test_symbolic_availability_exprs() {
    const AvailabilityExpr param0 = availability_expr_param(0);
    const AvailabilityExpr param1 = availability_expr_param(1);
    const AvailabilityExpr joined = availability_expr_join(param0, param1);
    assert(!availability_expr_always_ct(joined));
    assert(!availability_expr_forces_rt(joined));
    assert(availability_expr_display(joined) == "(param0 | param1)");

    const Availability substituted = availability_expr_substitute(
        joined, {availability_runtime_allowed_param(), availability_rt()});
    assert(substituted.kind == AvailabilityKind::Rt);
    assert(substituted.depends_on_runtime_allowed_param);

    const Availability missing_arg = availability_expr_substitute(param1, {availability_ct()});
    assert(missing_arg.kind == AvailabilityKind::Rt);

    const AvailabilityExpr forced = availability_expr_join(joined, availability_expr_rt());
    assert(availability_expr_forces_rt(forced));
    assert(availability_expr_display(forced) == "RT");
}

static void test_availability_expr_from_availability_preserves_param_provenance() {
    const Availability indexed_param = availability_runtime_allowed_param(1);
    const AvailabilityExpr param_expr = availability_expr_from_availability(indexed_param);
    assert(param_expr.kind == AvailabilityExprKind::Param);
    assert(param_expr.param_index == 1);
    assert(availability_expr_display(param_expr) == "param1");

    const Availability two_params = availability_join(
        availability_runtime_allowed_param(0), availability_runtime_allowed_param(2));
    const AvailabilityExpr joined_expr = availability_expr_from_availability(two_params);
    assert(availability_expr_display(joined_expr) == "(param0 | param2)");

    const AvailabilityExpr runtime_joined_expr =
        availability_expr_from_availability(availability_join(availability_rt(), indexed_param));
    assert(availability_expr_display(runtime_joined_expr) == "RT");
}

static void test_availability_projection() {
    assert(with_availability_projection(ty::num(), availability_ct()) == ty::num());
    assert(with_availability_projection(ty::num(), availability_rt()) == ty::num(true));
    assert(with_availability_projection(ty::never(), availability_rt()) == ty::never());

    const Ty runtime_pointee_ref =
        with_availability_projection(ty::ref(ty::num(true)), availability_ct());
    assert(!runtime_pointee_ref.is_runtime);
    assert(runtime_pointee_ref.pointee != nullptr);
    assert(runtime_pointee_ref.pointee->is_runtime);

    const Ty runtime_ref_handle =
        with_availability_projection(ty::ref(ty::num()), availability_rt());
    assert(runtime_ref_handle.is_runtime);
    assert(runtime_ref_handle.pointee != nullptr);
    assert(!runtime_ref_handle.pointee->is_runtime);
}

static void test_set_availability_projects_type() {
    const TyExprPtr expr = make_ty_expr(
        {}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("1"), false}, ty::num(),
        availability_rt());
    assert(expr->ty == ty::num(true));
    assert(expr->availability.kind == AvailabilityKind::Rt);
    set_availability(*expr, availability_ct());
    assert(expr->ty == ty::num());
    assert(expr->availability.kind == AvailabilityKind::Ct);

    TyBlock block;
    block.ty = ty::unit();
    set_availability(block, availability_rt());
    assert(block.ty == ty::unit(true));
    assert(block.availability.kind == AvailabilityKind::Rt);
    set_availability(block, availability_ct());
    assert(block.ty == ty::unit());
    assert(block.availability.kind == AvailabilityKind::Ct);
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
    assert(is_ct_available(*expr));
}

static void test_make_runtime_expr_derives_availability() {
    const TyExprPtr expr = make_ty_expr(
        {}, TyLiteral{TyLiteral::Kind::Num, Symbol::intern("1"), false}, ty::num(true));
    assert(expr != nullptr);
    assert(!is_ct_available(*expr));
    assert(expr->availability.kind == AvailabilityKind::Rt);
    assert(!expr->availability.evidence.has_value());
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

    const std::string otring out = format_program(prog);
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
    test_ty_named_with_generic_args();
    test_runtime_ty();
    test_availability_from_type_detects_nested_runtime_tags();
    test_availability_join_preserves_first_evidence();
    test_runtime_allowed_param_availability_is_symbolic_ct();
    test_symbolic_availability_exprs();
    test_availability_expr_from_availability_preserves_param_provenance();
    test_availability_projection();
    test_set_availability_projects_type();
    test_named_shape_distinguishes_nominal_types();
    test_ty_equality_distinguishes_value_semantics();
    test_make_literal();
    test_make_runtime_expr_derives_availability();
    test_make_local_ref();
    test_make_binary();
    test_empty_program();
    test_fn_decl();

    return 0;
}
