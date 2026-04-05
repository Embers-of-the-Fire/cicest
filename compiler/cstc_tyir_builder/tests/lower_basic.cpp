#include <cassert>
#include <string>

#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>
#include <cstc_tyir_builder/builder.hpp>

using namespace cstc::tyir_builder;
using namespace cstc::tyir;
using namespace cstc::symbol;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static TyProgram must_lower(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(tyir.has_value());
    return *tyir;
}

static void must_fail(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(!tyir.has_value());
}

static void must_fail_with_message(const char* source, const char* expected_message_part) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(!tyir.has_value());
    assert(tyir.error().message.find(expected_message_part) != std::string::npos);
}

static TyProgram must_lower_with_constraint_prelude(const char* source) {
    const std::string full_source =
        std::string(
            "[[lang = \"cstc_constraint\"]] enum Constraint { Valid, Invalid }"
            "[[lang = \"cstc_std_constraint\"]] extern \"lang\" fn "
            "constraint(value: bool) -> Constraint;")
        + source;
    const auto ast = cstc::parser::parse_source(full_source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(tyir.has_value());
    return *tyir;
}

static void
    must_fail_with_constraint_prelude(const char* source, const char* expected_message_part) {
    const std::string full_source =
        std::string(
            "[[lang = \"cstc_constraint\"]] enum Constraint { Valid, Invalid }"
            "[[lang = \"cstc_std_constraint\"]] extern \"lang\" fn "
            "constraint(value: bool) -> Constraint;")
        + source;
    const auto ast = cstc::parser::parse_source(full_source);
    assert(ast.has_value());
    const auto tyir = lower_program(*ast);
    assert(!tyir.has_value());
    assert(tyir.error().message.find(expected_message_part) != std::string::npos);
}

static const TyCall& require_constraint_call(const TyExprPtr& expr) {
    const auto& call = std::get<TyCall>(expr->node);
    assert(call.fn_name == Symbol::intern("constraint"));
    assert(call.args.size() == 1);
    return call;
}

static const TyDeclProbe& require_decl_probe(const TyExprPtr& expr) {
    return std::get<TyDeclProbe>(expr->node);
}

// ─── Empty program ────────────────────────────────────────────────────────────

static void test_empty_program() {
    const auto prog = must_lower("");
    assert(prog.items.empty());
}

// ─── Struct declaration ───────────────────────────────────────────────────────

static void test_struct_decl() {
    const auto prog = must_lower("struct Point { x: num, y: num }");
    assert(prog.items.size() == 1);

    const auto& decl = std::get<TyStructDecl>(prog.items[0]);
    assert(decl.name == Symbol::intern("Point"));
    assert(!decl.is_zst);
    assert(decl.fields.size() == 2);
    assert(decl.fields[0].name == Symbol::intern("x"));
    assert(decl.fields[0].ty == ty::num());
    assert(decl.fields[1].name == Symbol::intern("y"));
    assert(decl.fields[1].ty == ty::num());
}

static void test_zst_struct() {
    const auto prog = must_lower("struct Marker;");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyStructDecl>(prog.items[0]);
    assert(decl.is_zst);
    assert(decl.fields.empty());
}

static void test_generic_struct_decl_preserves_metadata_and_field_type() {
    const auto prog = must_lower_with_constraint_prelude("struct Box<T> where true { value: T }");
    assert(prog.items.size() == 3);
    const auto& decl = std::get<TyStructDecl>(prog.items[2]);
    assert(decl.generic_params.size() == 1);
    assert(decl.generic_params[0].name == Symbol::intern("T"));
    assert(decl.where_clause.size() == 1);
    assert(decl.lowered_where_clause.size() == 1);
    assert(decl.fields.size() == 1);
    assert(decl.fields[0].ty.name == Symbol::intern("T"));
    assert(decl.fields[0].ty.generic_args.empty());
}

static void test_struct_decl_preserves_lang_item_name() {
    const auto prog = must_lower("[[lang = \"cstc_marker\"]] struct Marker;");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyStructDecl>(prog.items[0]);
    assert(decl.lang_name == Symbol::intern("cstc_marker"));
}

static void test_generic_struct_where_clause_lowers_generic_type_args() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn helper<T>() -> bool { true }"
        "struct Box<T> where helper::<T>() { value: T }");
    assert(prog.items.size() == 4);
    const auto& decl = std::get<TyStructDecl>(prog.items[3]);
    assert(decl.lowered_where_clause.size() == 1);
    const auto& wrapped = require_constraint_call(decl.lowered_where_clause[0].expr);
    const auto& call = std::get<TyCall>(wrapped.args[0]->node);
    assert(call.generic_args.size() == 1);
    assert(call.generic_args[0].name == Symbol::intern("T"));
    assert(call.generic_args[0].generic_args.empty());
}

static void test_struct_with_named_field() {
    const auto prog = must_lower(
        "struct Color;"
        "struct Brush { c: Color }");
    assert(prog.items.size() == 2);
    const auto& brush = std::get<TyStructDecl>(prog.items[1]);
    assert(
        brush.fields[0].ty
        == ty::named(Symbol::intern("Color"), kInvalidSymbol, ValueSemantics::Copy));
}

static void test_struct_undefined_type_error() { must_fail("struct Foo { x: Unknown }"); }

static void test_struct_ref_field_error() {
    must_fail_with_message("struct Foo { value: &num }", "reference fields are not supported");
}

static void test_struct_direct_recursive_field_error() {
    must_fail_with_message(
        "struct Node { next: Node }", "non-productive recursive type declaration detected");
}

static void test_struct_mutual_recursive_field_error() {
    must_fail_with_message(
        "struct Left { right: Right }"
        "struct Right { left: Left }",
        "non-productive recursive type declaration detected");
}

static void test_struct_cycle_validation_follows_declaration_order() {
    const auto ast = cstc::parser::parse_source(
        "struct Second { next: Second }"
        "struct First { next: First }");
    assert(ast.has_value());

    const auto tyir = lower_program(*ast);
    assert(!tyir.has_value());
    assert(tyir.error().message.find("expanding 'Second<>'") != std::string::npos);
}

static void test_generic_struct_expanding_recursive_field_error() {
    must_fail_with_message(
        "struct Nest<T> { next: Nest<Nest<T>> }",
        "generic instantiation depth limit reached during type checking");
}

static void test_named_struct_chain_does_not_consume_generic_instantiation_budget() {
    std::string source;
    constexpr int depth = 40;
    for (int index = 0; index < depth; ++index) {
        source += "struct S" + std::to_string(index);
        if (index + 1 < depth)
            source += " { next: S" + std::to_string(index + 1) + " }";
        else
            source += ";";
    }

    const auto prog = must_lower(source.c_str());
    assert(prog.items.size() == static_cast<std::size_t>(depth));
}

static void test_duplicate_struct_name_error() {
    must_fail_with_message(
        "struct Point { x: num }"
        "struct Point { y: num }",
        "duplicate struct name 'Point'");
}

// ─── Enum declaration ─────────────────────────────────────────────────────────

static void test_enum_decl() {
    const auto prog = must_lower("enum Dir { North, South, East, West }");
    assert(prog.items.size() == 1);

    const auto& decl = std::get<TyEnumDecl>(prog.items[0]);
    assert(decl.name == Symbol::intern("Dir"));
    assert(decl.variants.size() == 4);
    assert(decl.variants[0].name == Symbol::intern("North"));
    assert(decl.variants[3].name == Symbol::intern("West"));
}

static void test_duplicate_enum_name_error() {
    must_fail_with_message(
        "enum Dir { North }"
        "enum Dir { South }",
        "duplicate enum name 'Dir'");
}

static void test_generic_enum_decl_preserves_metadata() {
    const auto prog =
        must_lower_with_constraint_prelude("enum Result<T, E> where true { Ok, Err }");
    assert(prog.items.size() == 3);
    const auto& decl = std::get<TyEnumDecl>(prog.items[2]);
    assert(decl.generic_params.size() == 2);
    assert(decl.generic_params[0].name == Symbol::intern("T"));
    assert(decl.generic_params[1].name == Symbol::intern("E"));
    assert(decl.where_clause.size() == 1);
    assert(decl.lowered_where_clause.size() == 1);
}

static void test_enum_decl_preserves_lang_item_name() {
    const auto prog =
        must_lower("[[lang = \"cstc_constraint\"]] enum Constraint { Valid, Invalid }");
    assert(prog.items.size() == 1);
    const auto& decl = std::get<TyEnumDecl>(prog.items[0]);
    assert(decl.lang_name == Symbol::intern("cstc_constraint"));
}

static void test_where_clause_accepts_explicit_constraint_value() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn id<T>(value: T) -> T where Constraint::Valid { value }");
    const auto& fn = std::get<TyFnDecl>(prog.items[2]);
    assert(fn.lowered_where_clause.size() == 1);
    assert(std::holds_alternative<EnumVariantRef>(fn.lowered_where_clause[0].expr->node));
}

static void test_where_clause_rejects_non_constraint_types() {
    must_fail_with_constraint_prelude(
        "fn id<T>(value: T) -> T where 1 { value }", "where clauses must evaluate to 'Constraint'");
}

static void test_where_clause_requires_explicit_constraint_intrinsic_annotation() {
    must_fail_with_message(
        R"(
[[lang = "cstc_constraint"]] enum Constraint { Valid, Invalid }
extern "lang" fn cstc_std_constraint(value: bool) -> Constraint;
fn id<T>(value: T) -> T where true { value }
)",
        "missing lang intrinsic 'cstc_std_constraint'");
}

static void test_where_clause_rejects_malformed_constraint_intrinsic_signature() {
    must_fail_with_message(
        R"(
[[lang = "cstc_constraint"]] enum Constraint { Valid, Invalid }
[[lang = "cstc_std_constraint"]] extern "lang" fn constraint() -> Constraint;
fn id<T>(value: T) -> T where true { value }
)",
        "lang intrinsic 'cstc_std_constraint' must have signature 'fn(bool) -> Constraint'");
}

static void test_enum_struct_name_collision_error() {
    must_fail_with_message(
        "enum Thing { A }"
        "struct Thing;",
        "duplicate struct name 'Thing'");
}

static void test_struct_enum_name_collision_error() {
    must_fail_with_message(
        "struct Thing;"
        "enum Thing { A }",
        "duplicate enum name 'Thing'");
}

// ─── Function declaration ─────────────────────────────────────────────────────

static void test_fn_no_return() {
    const auto prog = must_lower("fn noop() { }");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.name == Symbol::intern("noop"));
    assert(fn.return_ty == ty::unit());
    assert(fn.params.empty());
}

static void test_fn_with_params_and_return() {
    const auto prog = must_lower("fn add(x: num, y: num) -> num { x + y }");
    assert(prog.items.size() == 1);
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.name == Symbol::intern("add"));
    assert(fn.return_ty == ty::num());
    assert(fn.params.size() == 2);
    assert(fn.params[0].name == Symbol::intern("x"));
    assert(fn.params[0].ty == ty::num());
    assert(fn.params[1].name == Symbol::intern("y"));
    assert(fn.params[1].ty == ty::num());

    // Body should have a tail of type num
    assert(fn.body->ty == ty::num());
    assert(fn.body->tail.has_value());
    assert((*fn.body->tail)->ty == ty::num());
}

static void test_fn_bool_return() {
    const auto prog = must_lower("fn yes() -> bool { true }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::bool_());
    assert(fn.body->ty == ty::bool_());
}

static void test_fn_str_return() {
    const auto prog = must_lower(
        "extern \"lang\" fn to_str(value: num) -> str;"
        "fn greeting() -> str { to_str(0) }");
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(fn.return_ty == ty::str());
}

static void test_fn_ref_return_rejected() {
    must_fail_with_message("fn greeting() -> &str { \"hello\" }", "reference return types");
}

static void test_runtime_fn_preserves_runtime_markers() {
    const auto prog =
        must_lower("struct Job; runtime fn dispatch(job: runtime Job) -> runtime Job { job }");
    assert(prog.items.size() == 2);
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(fn.params.size() == 1);
    assert(fn.params[0].ty.is_runtime);
    assert(fn.return_ty.is_runtime);
    assert(fn.body->ty.is_runtime);
}

static void test_runtime_fn_return_uses_runtime_sugar() {
    const auto prog = must_lower("struct Job; runtime fn dispatch(job: Job) -> Job { job }");
    assert(prog.items.size() == 2);
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(fn.params.size() == 1);
    assert(!fn.params[0].ty.is_runtime);
    assert(fn.return_ty.is_runtime);
    assert(fn.body->ty.is_runtime);
}

static void test_fn_preserves_generic_metadata() {
    const auto prog =
        must_lower_with_constraint_prelude("fn id<T>(value: T) -> T where true, 1 == 1 { value }");
    const auto& fn = std::get<TyFnDecl>(prog.items[2]);
    assert(fn.generic_params.size() == 1);
    assert(fn.generic_params[0].name == Symbol::intern("T"));
    assert(fn.params[0].ty.name == Symbol::intern("T"));
    assert(fn.return_ty.name == Symbol::intern("T"));
    assert(fn.where_clause.size() == 2);
    assert(fn.lowered_where_clause.size() == 2);
    assert(fn.lowered_where_clause[0].expr->ty.name == Symbol::intern("Constraint"));
    assert(fn.lowered_where_clause[1].expr->ty.name == Symbol::intern("Constraint"));
}

static void test_fn_where_clause_rejects_parameter_references() {
    must_fail_with_message(
        "fn id<T>(value: T) -> T where value == value { value }",
        "function where clauses cannot reference parameter 'value'");
}

static void test_fn_decl_where_clause_allows_parameter_references() {
    const auto prog =
        must_lower_with_constraint_prelude("fn add<T>(a: T) -> T where decl(a + a) { a + a }");
    const auto& fn = std::get<TyFnDecl>(prog.items[2]);
    assert(fn.lowered_where_clause.size() == 1);
    const auto& probe = require_decl_probe(fn.lowered_where_clause[0].expr);
    assert(!probe.is_invalid);
    assert(probe.expr.has_value());
    const auto& binary = std::get<TyBinary>((*probe.expr)->node);
    const auto& lhs = std::get<LocalRef>(binary.lhs->node);
    const auto& rhs = std::get<LocalRef>(binary.rhs->node);
    assert(lhs.name == Symbol::intern("a"));
    assert(rhs.name == Symbol::intern("a"));
    assert(fn.body->tail.has_value());
    assert(std::holds_alternative<TyBinary>((*fn.body->tail)->node));
}

static void test_struct_where_clause_rejects_return() {
    must_fail_with_message(
        "struct Box<T> where if true { return true; } else { true } { value: T }",
        "where clauses cannot contain 'return'");
}

static void test_fn_where_clause_rejects_return() {
    must_fail_with_message(
        "fn id<T>(value: T) -> T where if true { return true; } else { true } { value }",
        "where clauses cannot contain 'return'");
}

static void test_fn_where_clause_lowers_generic_type_args() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn helper<T>() -> bool { true }"
        "fn id<T>(value: T) -> T where helper::<T>() { value }");
    assert(prog.items.size() == 4);
    const auto& fn = std::get<TyFnDecl>(prog.items[3]);
    assert(fn.lowered_where_clause.size() == 1);
    const auto& wrapped = require_constraint_call(fn.lowered_where_clause[0].expr);
    const auto& call = std::get<TyCall>(wrapped.args[0]->node);
    assert(call.generic_args.size() == 1);
    assert(call.generic_args[0].name == Symbol::intern("T"));
    assert(call.generic_args[0].generic_args.empty());
}

static void test_fn_where_clause_allows_call_callee_name_collision_with_parameter() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn helper() -> bool { true }"
        "fn id<T>(helper: T) -> T where helper() { helper }");
    assert(prog.items.size() == 4);
    const auto& fn = std::get<TyFnDecl>(prog.items[3]);
    assert(fn.lowered_where_clause.size() == 1);
    const auto& wrapped = require_constraint_call(fn.lowered_where_clause[0].expr);
    const auto& call = std::get<TyCall>(wrapped.args[0]->node);
    assert(call.fn_name == Symbol::intern("helper"));
    assert(call.args.empty());
}

static void test_fn_where_clause_allows_generic_call_callee_name_collision_with_parameter() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn helper<T>() -> bool { true }"
        "fn id<T>(helper: T) -> T where helper::<T>() { helper }");
    assert(prog.items.size() == 4);
    const auto& fn = std::get<TyFnDecl>(prog.items[3]);
    assert(fn.lowered_where_clause.size() == 1);
    const auto& wrapped = require_constraint_call(fn.lowered_where_clause[0].expr);
    const auto& call = std::get<TyCall>(wrapped.args[0]->node);
    assert(call.fn_name == Symbol::intern("helper"));
    assert(call.generic_args.size() == 1);
    assert(call.generic_args[0].name == Symbol::intern("T"));
    assert(call.generic_args[0].generic_args.empty());
}

static void test_decl_where_clause_lowers_to_constraint_probe() {
    const auto prog =
        must_lower_with_constraint_prelude("fn id<T>(value: T) -> T where decl(1 + 2) { value }");
    const auto& fn = std::get<TyFnDecl>(prog.items[2]);
    assert(fn.lowered_where_clause.size() == 1);
    assert(fn.lowered_where_clause[0].expr->ty.name == Symbol::intern("Constraint"));
    const auto& probe = require_decl_probe(fn.lowered_where_clause[0].expr);
    assert(!probe.is_invalid);
    assert(probe.expr.has_value());
    assert(std::holds_alternative<TyBinary>((*probe.expr)->node));
}

static void test_decl_probe_recovers_invalid_inner_expression() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn id<T>(value: T) -> T where decl(1 + true) { value }");
    const auto& fn = std::get<TyFnDecl>(prog.items[2]);
    const auto& probe = require_decl_probe(fn.lowered_where_clause[0].expr);
    assert(probe.is_invalid);
    assert(!probe.expr.has_value());
    assert(probe.invalid_reason.has_value());
}

static void test_decl_probe_preserves_generic_inner_call() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn helper<T>() -> bool { true }"
        "fn id<T>(value: T) -> T where decl(helper::<T>()) { value }");
    const auto& fn = std::get<TyFnDecl>(prog.items[3]);
    const auto& probe = require_decl_probe(fn.lowered_where_clause[0].expr);
    assert(!probe.is_invalid);
    assert(probe.expr.has_value());
    const auto& call = std::get<TyCall>((*probe.expr)->node);
    assert(call.fn_name == Symbol::intern("helper"));
    assert(call.generic_args.size() == 1);
    assert(call.generic_args[0].name == Symbol::intern("T"));
}

static void test_decl_probe_defers_generic_parameter_validation() {
    const auto prog =
        must_lower_with_constraint_prelude("fn probe<T>(a: T) -> T where decl(a + a) { a }");
    const auto& fn = std::get<TyFnDecl>(prog.items[2]);
    const auto& probe = require_decl_probe(fn.lowered_where_clause[0].expr);
    assert(!probe.is_invalid);
    assert(probe.expr.has_value());
    const auto& binary = std::get<TyBinary>((*probe.expr)->node);
    assert(std::get<LocalRef>(binary.lhs->node).name == Symbol::intern("a"));
    assert(std::get<LocalRef>(binary.rhs->node).name == Symbol::intern("a"));
}

static void test_decl_probe_defers_generic_let_annotation_validation() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn probe<T>(a: T) -> T where decl({ let x: num = a; x }) { a }");
    const auto& fn = std::get<TyFnDecl>(prog.items[2]);
    const auto& probe = require_decl_probe(fn.lowered_where_clause[0].expr);
    assert(!probe.is_invalid);
    assert(probe.expr.has_value());
    const auto& block = *std::get<TyBlockPtr>((*probe.expr)->node);
    assert(block.stmts.size() == 1);
    assert(std::holds_alternative<TyLetStmt>(block.stmts[0]));
}

static void test_decl_probe_defers_generic_if_branch_join_validation() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn probe<T>(a: T) -> T where decl(if true { 1 } else { a }) { a }");
    const auto& fn = std::get<TyFnDecl>(prog.items[2]);
    const auto& probe = require_decl_probe(fn.lowered_where_clause[0].expr);
    assert(!probe.is_invalid);
    assert(probe.expr.has_value());
    assert(std::holds_alternative<TyIf>((*probe.expr)->node));
}

static void test_decl_probe_contains_unresolved_generic_inference_in_let() {
    const auto prog = must_lower_with_constraint_prelude(
        "fn make<T>() -> T { loop {} }"
        "fn main() { let x = decl(make()); }");
    const auto& fn = std::get<TyFnDecl>(prog.items[3]);
    assert(fn.body->stmts.size() == 1);
    const auto& let_stmt = std::get<TyLetStmt>(fn.body->stmts[0]);
    const auto& probe = require_decl_probe(let_stmt.init);
    assert(!probe.is_invalid);
    assert(probe.expr.has_value());
    assert(std::holds_alternative<TyDeferredGenericCall>((*probe.expr)->node));
}

static void test_decl_probe_does_not_drive_if_branch_join_inference() {
    must_fail_with_message(
        "[[lang = \"cstc_constraint\"]] enum Constraint { Valid, Invalid }"
        "[[lang = \"cstc_std_constraint\"]] extern \"lang\" fn "
        "constraint(value: bool) -> Constraint;"
        "fn make<T>() -> T { loop {} }"
        "fn main(cond: bool) { if cond { decl(make()) } else { true }; }",
        "'if' then-branch has type 'Constraint' but else-branch has type 'bool'");
}

static void test_decl_runtime_use_is_rejected() {
    must_fail_with_message(
        R"(
[[lang = "cstc_constraint"]] enum Constraint { Valid, Invalid }
runtime fn probe() -> runtime Constraint { decl(1) }
fn main() {}
)",
        "decl(expr) is compile-time only and cannot be used where runtime behavior is required");
}

static void test_generic_type_arguments_lower_in_signatures() {
    const auto prog = must_lower(
        "struct Box<T> { value: T }"
        "fn wrap(value: Box<num>) -> Box<num> { value }");
    assert(prog.items.size() == 2);
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    const Ty expected =
        ty::named(Symbol::intern("Box"), kInvalidSymbol, ValueSemantics::Copy, false, {ty::num()});
    assert(fn.params[0].ty == expected);
    assert(fn.return_ty == expected);
}

static void test_runtime_return_annotation_accepts_plain_value() {
    const auto prog = must_lower("fn promote() -> runtime num { 1 }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num(true));
    assert(fn.body->ty == ty::num());
}

static void test_runtime_return_type_mismatch_rejected() {
    must_fail_with_message(
        "struct Job; fn unwrap(job: runtime Job) -> Job { job }",
        "body has type 'runtime Job' but return type is 'Job'");
}

static void test_runtime_main_return_allowed() {
    const auto prog = must_lower("runtime fn main() -> num { 0 }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num(true));
    assert(fn.body->ty == ty::num(true));
}

static void test_duplicate_function_name_error() {
    must_fail_with_message("fn noop() { } fn noop() { }", "duplicate function name 'noop'");
}

// ─── Multiple items in order ──────────────────────────────────────────────────

static void test_item_order() {
    const auto prog = must_lower(
        "struct A;"
        "enum B { X }"
        "fn c() { }");
    assert(prog.items.size() == 3);
    assert(std::holds_alternative<TyStructDecl>(prog.items[0]));
    assert(std::holds_alternative<TyEnumDecl>(prog.items[1]));
    assert(std::holds_alternative<TyFnDecl>(prog.items[2]));
}

// ─── Type mismatch errors ─────────────────────────────────────────────────────

static void test_return_type_mismatch() { must_fail("fn f() -> num { true }"); }

static void test_non_unit_fn_fallthrough_error() { must_fail("fn f() -> num { }"); }

static void test_non_unit_fn_explicit_return_stmt() {
    const auto prog = must_lower("fn f() -> num { return 1; }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
    assert(!fn.body->tail.has_value());
}

static void test_non_unit_fn_if_else_returns_as_stmt() {
    const auto prog =
        must_lower("fn f(cond: bool) -> num { if cond { return 1; } else { return 2; }; }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
    assert(!fn.body->tail.has_value());
}

static void test_non_unit_fn_if_condition_return_no_fallthrough() {
    const auto prog = must_lower("fn f() -> num { if (return 1) { }; }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
    assert(!fn.body->tail.has_value());
}

static void test_non_unit_fn_while_condition_return_no_fallthrough() {
    const auto prog = must_lower("fn f() -> num { while (return 1) { }; }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
    assert(!fn.body->tail.has_value());
}

static void test_let_type_mismatch() { must_fail("fn f() { let x: bool = 42; }"); }

// ─── Never (!) return type ────────────────────────────────────────────────────

static void test_fn_never_return_type_valid() {
    const auto prog = must_lower("fn f() -> ! { loop {} }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::never());
}

static void test_fn_never_return_type_mismatch() {
    must_fail_with_message("fn f() -> ! { 42 }", "body has type 'num' but return type is '!'");
}

// ─── main return type constraints ─────────────────────────────────────────────

static void test_main_returns_unit() {
    const auto prog = must_lower("fn main() { }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::unit());
}

static void test_main_returns_num() {
    const auto prog = must_lower("fn main() -> num { 0 }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::num());
}

static void test_main_returns_never() {
    const auto prog = must_lower("fn main() -> ! { loop {} }");
    const auto& fn = std::get<TyFnDecl>(prog.items[0]);
    assert(fn.return_ty == ty::never());
}

static void test_main_returns_str_error() {
    must_fail_with_message(
        "extern \"lang\" fn to_str(value: num) -> str; fn main() -> str { to_str(1) }",
        "'main' function must return 'Unit', 'num', or '!' (never), found 'str'");
}

static void test_main_returns_bool_error() {
    must_fail_with_message(
        "fn main() -> bool { true }",
        "'main' function must return 'Unit', 'num', or '!' (never), found 'bool'");
}

static void test_main_returns_struct_error() {
    must_fail_with_message(
        "struct Point { x: num } fn main() -> Point { Point { x: 0 } }",
        "'main' function must return 'Unit', 'num', or '!' (never), found 'Point'");
}

static void test_main_returns_enum_error() {
    must_fail_with_message(
        "enum Dir { North } fn main() -> Dir { Dir::North }",
        "'main' function must return 'Unit', 'num', or '!' (never), found 'Dir'");
}

static void test_non_main_fn_accepts_any_return() {
    // Regression: non-main functions should still accept any return type
    const auto prog = must_lower(
        "struct Point { x: num }"
        "fn make_point() -> Point { Point { x: 1 } }");
    assert(prog.items.size() == 2);
    const auto& fn = std::get<TyFnDecl>(prog.items[1]);
    assert(
        fn.return_ty == ty::named(Symbol::intern("Point"), kInvalidSymbol, ValueSemantics::Copy));
}

int main() {
    SymbolSession session;

    test_empty_program();
    test_struct_decl();
    test_zst_struct();
    test_generic_struct_decl_preserves_metadata_and_field_type();
    test_struct_decl_preserves_lang_item_name();
    test_generic_struct_where_clause_lowers_generic_type_args();
    test_struct_with_named_field();
    test_struct_undefined_type_error();
    test_struct_ref_field_error();
    test_struct_direct_recursive_field_error();
    test_struct_mutual_recursive_field_error();
    test_struct_cycle_validation_follows_declaration_order();
    test_generic_struct_expanding_recursive_field_error();
    test_named_struct_chain_does_not_consume_generic_instantiation_budget();
    test_duplicate_struct_name_error();
    test_enum_decl();
    test_duplicate_enum_name_error();
    test_generic_enum_decl_preserves_metadata();
    test_enum_decl_preserves_lang_item_name();
    test_where_clause_accepts_explicit_constraint_value();
    test_where_clause_rejects_non_constraint_types();
    test_where_clause_requires_explicit_constraint_intrinsic_annotation();
    test_where_clause_rejects_malformed_constraint_intrinsic_signature();
    test_enum_struct_name_collision_error();
    test_struct_enum_name_collision_error();
    test_fn_no_return();
    test_fn_with_params_and_return();
    test_fn_bool_return();
    test_fn_str_return();
    test_fn_ref_return_rejected();
    test_runtime_fn_preserves_runtime_markers();
    test_runtime_fn_return_uses_runtime_sugar();
    test_fn_preserves_generic_metadata();
    test_fn_where_clause_rejects_parameter_references();
    test_fn_decl_where_clause_allows_parameter_references();
    test_struct_where_clause_rejects_return();
    test_fn_where_clause_rejects_return();
    test_fn_where_clause_lowers_generic_type_args();
    test_fn_where_clause_allows_call_callee_name_collision_with_parameter();
    test_fn_where_clause_allows_generic_call_callee_name_collision_with_parameter();
    test_decl_where_clause_lowers_to_constraint_probe();
    test_decl_probe_recovers_invalid_inner_expression();
    test_decl_probe_preserves_generic_inner_call();
    test_decl_probe_defers_generic_parameter_validation();
    test_decl_probe_defers_generic_let_annotation_validation();
    test_decl_probe_defers_generic_if_branch_join_validation();
    test_decl_probe_contains_unresolved_generic_inference_in_let();
    test_decl_probe_does_not_drive_if_branch_join_inference();
    test_decl_runtime_use_is_rejected();
    test_generic_type_arguments_lower_in_signatures();
    test_runtime_return_annotation_accepts_plain_value();
    test_runtime_return_type_mismatch_rejected();
    test_runtime_main_return_allowed();
    test_duplicate_function_name_error();
    test_item_order();
    test_return_type_mismatch();
    test_non_unit_fn_fallthrough_error();
    test_non_unit_fn_explicit_return_stmt();
    test_non_unit_fn_if_else_returns_as_stmt();
    test_non_unit_fn_if_condition_return_no_fallthrough();
    test_non_unit_fn_while_condition_return_no_fallthrough();
    test_let_type_mismatch();
    test_fn_never_return_type_valid();
    test_fn_never_return_type_mismatch();
    test_main_returns_unit();
    test_main_returns_num();
    test_main_returns_never();
    test_main_returns_str_error();
    test_main_returns_bool_error();
    test_main_returns_struct_error();
    test_main_returns_enum_error();
    test_non_main_fn_accepts_any_return();

    return 0;
}
