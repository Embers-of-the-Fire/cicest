#include <cassert>
#include <string>
#include <variant>

#include <cstc_lir/lir.hpp>
#include <cstc_lir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir_builder/builder.hpp>

using namespace cstc::lir;
using namespace cstc::symbol;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static LirProgram must_lower(const char* source) {
    const auto ast = cstc::parser::parse_source(source);
    assert(ast.has_value());
    const auto tyir = cstc::tyir_builder::lower_program(*ast);
    assert(tyir.has_value());
    return cstc::lir_builder::lower_program(*tyir);
}

// ─── Extern fn forwarding ────────────────────────────────────────────────────

static void test_extern_fn_basic() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" fn print(value: str);)");
    assert(prog.extern_fns.size() == 1);
    const auto& ext = prog.extern_fns[0];
    assert(ext.abi == Symbol::intern("lang"));
    assert(ext.name == Symbol::intern("print"));
    assert(ext.params.size() == 1);
    assert(ext.params[0].name == Symbol::intern("value"));
    assert(ext.params[0].ty == cstc::tyir::ty::str());
    assert(ext.return_ty == cstc::tyir::ty::unit());
}

static void test_extern_fn_with_return() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" fn to_str(value: num) -> str;)");
    assert(prog.extern_fns.size() == 1);
    const auto& ext = prog.extern_fns[0];
    assert(ext.name == Symbol::intern("to_str"));
    assert(ext.params.size() == 1);
    assert(ext.params[0].ty == cstc::tyir::ty::num());
    assert(ext.return_ty == cstc::tyir::ty::str());
}

static void test_extern_fn_multiple_params() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" fn concat(a: str, b: str) -> str;)");
    assert(prog.extern_fns.size() == 1);
    const auto& ext = prog.extern_fns[0];
    assert(ext.params.size() == 2);
    assert(ext.params[0].name == Symbol::intern("a"));
    assert(ext.params[0].ty == cstc::tyir::ty::str());
    assert(ext.params[1].name == Symbol::intern("b"));
    assert(ext.params[1].ty == cstc::tyir::ty::str());
    assert(ext.return_ty == cstc::tyir::ty::str());
}

static void test_extern_fn_no_params() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" fn get_value() -> num;)");
    assert(prog.extern_fns.size() == 1);
    const auto& ext = prog.extern_fns[0];
    assert(ext.name == Symbol::intern("get_value"));
    assert(ext.params.empty());
    assert(ext.return_ty == cstc::tyir::ty::num());
}

// ─── Extern fn param local IDs ──────────────────────────────────────────────

static void test_extern_fn_param_local_ids() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" fn f(a: num, b: str, c: bool);)");
    assert(prog.extern_fns.size() == 1);
    const auto& ext = prog.extern_fns[0];
    assert(ext.params.size() == 3);
    // Params should get sequential local IDs starting at 0
    assert(ext.params[0].local == 0);
    assert(ext.params[1].local == 1);
    assert(ext.params[2].local == 2);
}

// ─── Multiple extern fns ────────────────────────────────────────────────────

static void test_multiple_extern_fns() {
    SymbolSession session;
    const auto prog = must_lower(R"(
extern "lang" fn print(value: str);
extern "lang" fn println(value: str);
extern "lang" fn to_str(value: num) -> str;
)");
    assert(prog.extern_fns.size() == 3);
    assert(prog.extern_fns[0].name == Symbol::intern("print"));
    assert(prog.extern_fns[1].name == Symbol::intern("println"));
    assert(prog.extern_fns[2].name == Symbol::intern("to_str"));
}

// ─── Extern fns alongside regular items ──────────────────────────────────────

static void test_extern_fn_with_regular_fn() {
    SymbolSession session;
    const auto prog = must_lower(R"(
extern "lang" fn print(value: str);
fn main() { print("hello"); }
)");
    assert(prog.extern_fns.size() == 1);
    assert(prog.fns.size() == 1);
    assert(prog.extern_fns[0].name == Symbol::intern("print"));
    assert(prog.fns[0].name == Symbol::intern("main"));
}

static void test_extern_struct_becomes_struct_decl() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "lang" struct Handle;)");
    // Extern structs become regular struct declarations with is_zst=true
    assert(prog.structs.size() == 1);
    assert(prog.structs[0].name == Symbol::intern("Handle"));
    assert(prog.structs[0].is_zst);
    assert(prog.structs[0].fields.empty());
    // No extern fns
    assert(prog.extern_fns.empty());
}

static void test_extern_fn_abi_preserved() {
    SymbolSession session;
    const auto prog = must_lower(R"(extern "c" fn puts(s: str);)");
    assert(prog.extern_fns.size() == 1);
    assert(prog.extern_fns[0].abi == Symbol::intern("c"));
}

// ─── Mixed extern and regular items ─────────────────────────────────────────

static void test_mixed_items() {
    SymbolSession session;
    const auto prog = must_lower(R"(
extern "lang" struct Handle;
extern "lang" fn create() -> Handle;
extern "lang" fn destroy(h: Handle);
struct Point { x: num, y: num }
fn main() {
    let h: Handle = create();
    destroy(h);
}
)");
    // Extern struct + regular struct
    assert(prog.structs.size() == 2);
    // Extern fns
    assert(prog.extern_fns.size() == 2);
    assert(prog.extern_fns[0].name == Symbol::intern("create"));
    assert(prog.extern_fns[1].name == Symbol::intern("destroy"));
    // Regular fn
    assert(prog.fns.size() == 1);
    assert(prog.fns[0].name == Symbol::intern("main"));
}

int main() {
    test_extern_fn_basic();
    test_extern_fn_with_return();
    test_extern_fn_multiple_params();
    test_extern_fn_no_params();
    test_extern_fn_param_local_ids();
    test_multiple_extern_fns();
    test_extern_fn_with_regular_fn();
    test_extern_struct_becomes_struct_decl();
    test_extern_fn_abi_preserved();
    test_mixed_items();
    return 0;
}
