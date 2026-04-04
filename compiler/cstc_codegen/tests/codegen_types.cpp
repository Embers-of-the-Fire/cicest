/// @file codegen_types.cpp
/// @brief LLVM IR codegen tests for types: structs, enums, field access,
///        struct init.

#include <cassert>
#include <string>

#include <cstc_codegen/codegen.hpp>
#include <cstc_lir/lir.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir_builder/builder.hpp>

using namespace cstc::symbol;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static std::string must_codegen(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    const auto lir = cstc::lir_builder::lower_program(*tyir);
    return cstc::codegen::emit_llvm_ir(lir);
}

static bool ir_contains(const std::string& ir, const std::string& needle) {
    return ir.find(needle) != std::string::npos;
}

static std::size_t count_occurrences(const std::string& ir, const std::string& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = ir.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// ─── Struct type declaration ──────────────────────────────────────────────────

static void test_struct_type() {
    const std::string ir = must_codegen(
        "struct Point { x: num, y: num }"
        "fn id(p: Point) -> Point { p }");
    assert(ir_contains(ir, "%Point = type { double, double }"));
}

// ─── ZST struct ──────────────────────────────────────────────────────────────

static void test_zst_struct() {
    const std::string ir = must_codegen(
        "struct Marker;"
        "fn id(m: Marker) -> Marker { m }");
    assert(ir_contains(ir, "%Marker = type {}"));
}

// ─── Enum type declaration ────────────────────────────────────────────────────

static void test_enum_type() {
    const std::string ir = must_codegen(
        "enum Color { Red, Green, Blue }"
        "fn id(c: Color) -> Color { c }");
    assert(ir_contains(ir, "%Color = type { i32 }"));
}

// ─── Struct initialization ────────────────────────────────────────────────────

static void test_struct_init() {
    const std::string ir = must_codegen(
        "struct Point { x: num, y: num }"
        "fn make() -> Point { Point { x: 1, y: 2 } }");
    assert(ir_contains(ir, "insertvalue") || ir_contains(ir, "ret %Point {"));
    assert(ir_contains(ir, "%Point"));
}

// ─── Field access ─────────────────────────────────────────────────────────────

static void test_field_access() {
    const std::string ir = must_codegen(
        "struct Point { x: num, y: num }"
        "fn get_x(p: Point) -> num { p.x }");
    assert(ir_contains(ir, "getelementptr inbounds nuw %Point"));
}

static void test_nested_field_access() {
    const std::string ir = must_codegen(
        "struct Inner { x: num, s: str }"
        "struct Outer { inner: Inner }"
        "fn get_x(o: Outer) -> num { o.inner.x }");
    assert(ir_contains(ir, "%Outer = type { %Inner }"));
    assert(ir_contains(ir, "%cstc.str = type { ptr, i64, i8 }"));
    assert(ir_contains(ir, "%Inner = type { double, %cstc.str }"));
    assert(ir_contains(ir, "getelementptr inbounds nuw %Outer"));
    assert(ir_contains(ir, "getelementptr inbounds nuw %Inner"));
}

// ─── Struct as parameter ──────────────────────────────────────────────────────

static void test_struct_param() {
    const std::string ir = must_codegen(
        "struct Point { x: num, y: num }"
        "fn sum(p: Point) -> num { p.x + p.y }");
    assert(ir_contains(ir, "%Point"));
    assert(ir_contains(ir, "fadd double"));
}

// ─── Enum variant reference ──────────────────────────────────────────────────

static void test_enum_variant_ref() {
    const std::string ir = must_codegen(
        "enum Dir { North, South, East, West }"
        "fn go() -> Dir { Dir::North }");
    assert(ir_contains(ir, "insertvalue") || ir_contains(ir, "ret %Dir"));
    assert(ir_contains(ir, "%Dir"));
}

// ─── Struct returned from function ───────────────────────────────────────────

static void test_struct_return() {
    const std::string ir = must_codegen(
        "struct Pair { a: num, b: bool }"
        "fn make() -> Pair { Pair { a: 3.14, b: true } }");
    assert(ir_contains(ir, "%Pair = type { double, i1 }"));
    assert(ir_contains(ir, "ret %Pair"));
}

static void test_generic_struct_codegen_uses_monomorphized_type() {
    const std::string ir = must_codegen(
        "struct Box<T> { value: T }"
        "fn make<T>(value: T) -> Box<T> { Box<T> { value: value } }"
        "fn main() -> num { let boxed: Box<num> = make(1); boxed.value }");

    assert(ir_contains(ir, "Box$inst$N"));
    assert(ir_contains(ir, "make$inst$N"));
}

static void test_generic_fn_codegen_reuses_single_instantiation() {
    const std::string ir = must_codegen(
        "fn id<T>(value: T) -> T { value }"
        "fn main() -> num { let first = id(1); id::<num>(first) }");

    assert(count_occurrences(ir, "id$inst$N") >= 2);
    assert(count_occurrences(ir, "define") >= 2);
}

int main() {
    SymbolSession session;

    test_struct_type();
    test_zst_struct();
    test_enum_type();
    test_struct_init();
    test_field_access();
    test_nested_field_access();
    test_struct_param();
    test_enum_variant_ref();
    test_struct_return();
    test_generic_struct_codegen_uses_monomorphized_type();
    test_generic_fn_codegen_reuses_single_instantiation();

    return 0;
}
