#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <cstc_module/module.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir_builder/builder.hpp>

namespace {

struct TempDir {
    std::filesystem::path path;

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

TempDir make_temp_dir() {
    const auto seed = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / ("cstc-module-test-" + seed);
    std::filesystem::create_directories(path);
    return TempDir{path};
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    assert(file.good());
    file << contents;
    assert(file.good());
}

cstc::ast::Program must_load_program(
    const std::filesystem::path& root_path, const std::filesystem::path& std_root_path) {
    cstc::span::SourceMap source_map;
    const auto program = cstc::module::load_program(source_map, root_path, std_root_path);
    if (!program.has_value())
        std::cerr << program.error().message << '\n';
    assert(program.has_value());
    return *program;
}

cstc::tyir::TyProgram
    must_lower(const std::filesystem::path& root_path, const std::filesystem::path& std_root_path) {
    const auto program = must_load_program(root_path, std_root_path);
    const auto lowered = cstc::tyir_builder::lower_program(program);
    if (!lowered.has_value())
        std::cerr << lowered.error().message << '\n';
    assert(lowered.has_value());
    return *lowered;
}

void must_fail_with_message(
    const std::filesystem::path& root_path, const std::filesystem::path& std_root_path,
    std::string_view expected) {
    cstc::span::SourceMap source_map;
    const auto program = cstc::module::load_program(source_map, root_path, std_root_path);
    assert(!program.has_value());
    assert(program.error().message.find(expected) != std::string::npos);
}

std::string must_fail_lower_with_message(
    const std::filesystem::path& root_path, const std::filesystem::path& std_root_path,
    std::string_view expected) {
    const auto program = must_load_program(root_path, std_root_path);
    const auto lowered = cstc::tyir_builder::lower_program(program);
    if (lowered.has_value())
        std::cerr << "expected lowering to fail\n";
    assert(!lowered.has_value());
    assert(lowered.error().message.find(expected) != std::string::npos);
    return lowered.error().message;
}

const cstc::ast::FnDecl*
    find_fn_decl(const cstc::ast::Program& program, std::string_view display_name) {
    for (const cstc::ast::Item& item : program.items) {
        const auto* fn = std::get_if<cstc::ast::FnDecl>(&item);
        if (fn == nullptr)
            continue;
        if (fn->display_name.as_str() == display_name)
            return fn;
    }

    return nullptr;
}

const cstc::tyir::TyExternFnDecl*
    find_extern_fn_decl(const cstc::tyir::TyProgram& program, std::string_view link_name) {
    for (const cstc::tyir::TyItem& item : program.items) {
        const auto* decl = std::get_if<cstc::tyir::TyExternFnDecl>(&item);
        if (decl == nullptr)
            continue;
        if (decl->link_name.as_str() == link_name)
            return decl;
    }

    return nullptr;
}

const cstc::tyir::TyFnDecl*
    find_ty_fn_decl(const cstc::tyir::TyProgram& program, std::string_view name) {
    for (const cstc::tyir::TyItem& item : program.items) {
        const auto* decl = std::get_if<cstc::tyir::TyFnDecl>(&item);
        if (decl == nullptr)
            continue;
        if (decl->name.as_str() == name)
            return decl;
    }

    return nullptr;
}

std::vector<const cstc::tyir::TyFnDecl*> collect_ty_fn_decls(const cstc::tyir::TyProgram& program) {
    std::vector<const cstc::tyir::TyFnDecl*> decls;
    for (const cstc::tyir::TyItem& item : program.items) {
        const auto* decl = std::get_if<cstc::tyir::TyFnDecl>(&item);
        if (decl != nullptr)
            decls.push_back(decl);
    }

    return decls;
}

void test_private_helpers_do_not_collide_across_modules() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(
        temp.path / "left.cst", "pub fn left() { helper(); }\n"
                                "fn helper() { }\n");
    write_file(
        temp.path / "right.cst", "pub fn right() { helper(); }\n"
                                 "fn helper() { }\n");
    write_file(
        temp.path / "root.cst", "import { left } from \"left.cst\";\n"
                                "import { right } from \"right.cst\";\n"
                                "fn main() { left(); right(); }\n");

    must_lower(temp.path / "root.cst", temp.path / "std");
}

void test_private_import_is_rejected() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(temp.path / "lib.cst", "fn secret() { }\n");
    write_file(
        temp.path / "root.cst", "import { secret } from \"lib.cst\";\n"
                                "fn main() { secret(); }\n");

    must_fail_with_message(temp.path / "root.cst", temp.path / "std", "is private in module");
}

void test_pub_import_reexports_bindings() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(temp.path / "leaf.cst", "pub fn open_file() { }\n");
    write_file(temp.path / "bridge.cst", "pub import { open_file as open } from \"leaf.cst\";\n");
    write_file(
        temp.path / "root.cst", "import { open } from \"bridge.cst\";\n"
                                "fn main() { open(); }\n");

    must_lower(temp.path / "root.cst", temp.path / "std");
}

void test_import_alias_can_share_name_across_type_and_value_namespaces() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(
        temp.path / "lib.cst", "pub struct TypeThing;\n"
                               "pub fn make_foo() -> TypeThing { TypeThing { } }\n");
    write_file(
        temp.path / "root.cst", "import { TypeThing as Foo, make_foo as Foo } from \"lib.cst\";\n"
                                "fn main() { let value: Foo = Foo(); }\n");

    must_lower(temp.path / "root.cst", temp.path / "std");
}

void test_import_alias_duplicate_value_binding_is_rejected_by_resolver() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(
        temp.path / "lib.cst", "pub fn left() { }\n"
                               "pub fn right() { }\n");
    write_file(
        temp.path / "root.cst", "import { left as foo, right as foo } from \"lib.cst\";\n"
                                "fn main() { foo(); }\n");

    must_fail_with_message(
        temp.path / "root.cst", temp.path / "std", "duplicate function name 'foo'");
}

void test_pub_items_can_share_export_name_across_type_and_value_namespaces() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(
        temp.path / "lib.cst", "pub struct Foo;\n"
                               "pub fn Foo() -> Foo { Foo { } }\n");
    write_file(
        temp.path / "root.cst", "import { Foo } from \"lib.cst\";\n"
                                "fn main() { let value: Foo = Foo(); }\n");

    must_lower(temp.path / "root.cst", temp.path / "std");
}

void test_pub_type_does_not_export_private_value_binding_with_same_name() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(
        temp.path / "lib.cst", "pub struct Foo;\n"
                               "fn Foo() -> Foo { Foo { } }\n");
    write_file(
        temp.path / "root.cst", "import { Foo } from \"lib.cst\";\n"
                                "fn main() { let value: Foo = Foo(); }\n");

    must_fail_lower_with_message(
        temp.path / "root.cst", temp.path / "std", "undefined function 'Foo'");
}

void test_imported_type_keeps_prelude_fallback_value_namespace() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "pub fn Foo() -> num { 1 }\n");
    write_file(temp.path / "lib.cst", "pub struct Foo;\n");
    write_file(
        temp.path / "root.cst", "import { Foo } from \"lib.cst\";\n"
                                "fn main() -> num { Foo() }\n");

    must_lower(temp.path / "root.cst", temp.path / "std");
}

void test_local_value_keeps_prelude_fallback_type_namespace() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "pub struct Foo;\n");
    write_file(
        temp.path / "root.cst", "fn Foo() -> Foo { Foo { } }\n"
                                "fn main() { let value: Foo = Foo(); }\n");

    must_lower(temp.path / "root.cst", temp.path / "std");
}

void test_std_path_import_resolves_relative_to_std_root() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(
        temp.path / "std" / "io.cst", "pub struct File;\n"
                                      "pub fn open() -> File { File { } }\n");
    write_file(
        temp.path / "root.cst", "import { File, open } from \"@std/io.cst\";\n"
                                "fn use_file() -> File { open() }\n"
                                "fn main() { let _file = use_file(); }\n");

    must_lower(temp.path / "root.cst", temp.path / "std");
}

void test_local_item_shadows_prelude() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "pub fn print() { }\n");
    write_file(
        temp.path / "root.cst", "fn print() { }\n"
                                "fn main() { print(); }\n");

    const auto lowered = must_lower(temp.path / "root.cst", temp.path / "std");
    const cstc::tyir::TyFnDecl* main_fn = find_ty_fn_decl(lowered, "main");
    assert(main_fn != nullptr);
    assert(main_fn->body->stmts.size() == 1);
    const auto& call_stmt = std::get<cstc::tyir::TyExprStmt>(main_fn->body->stmts[0]);
    const auto& call = std::get<cstc::tyir::TyCall>(call_stmt.expr->node);
    assert(call.fn_name == cstc::symbol::Symbol::intern("print"));
}

void test_explicit_import_shadows_prelude_fallback() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "pub fn print() { }\n");
    write_file(temp.path / "lib.cst", "pub fn print() { }\n");
    write_file(
        temp.path / "root.cst", "import { print } from \"lib.cst\";\n"
                                "fn main() { print(); }\n");

    const auto lowered = must_lower(temp.path / "root.cst", temp.path / "std");
    const cstc::tyir::TyFnDecl* main_fn = find_ty_fn_decl(lowered, "main");
    assert(main_fn != nullptr);
    assert(main_fn->body->stmts.size() == 1);
    const auto& call_stmt = std::get<cstc::tyir::TyExprStmt>(main_fn->body->stmts[0]);
    const auto& call = std::get<cstc::tyir::TyCall>(call_stmt.expr->node);
    const auto fn_decls = collect_ty_fn_decls(lowered);
    assert(fn_decls.size() == 3);
    assert(fn_decls[0]->name.as_str().starts_with("__cst_mod_"));
    assert(fn_decls[1]->name.as_str().starts_with("__cst_mod_"));
    assert(fn_decls[2]->name == cstc::symbol::Symbol::intern("main"));
    assert(call.fn_name == fn_decls[1]->name);
    assert(call.fn_name != fn_decls[0]->name);
}

void test_pub_import_can_reexport_prelude_item() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "pub fn print() { }\n");
    write_file(temp.path / "bridge.cst", "pub import { print } from \"@std/prelude.cst\";\n");
    write_file(
        temp.path / "root.cst", "import { print } from \"bridge.cst\";\n"
                                "fn main() { print(); }\n");

    must_lower(temp.path / "root.cst", temp.path / "std");
}

void test_imported_fn_rewrite_preserves_display_name() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(temp.path / "lib.cst", "pub fn imported() { }\n");
    write_file(
        temp.path / "root.cst", "import { imported } from \"lib.cst\";\n"
                                "fn main() { imported(); }\n");

    const auto program = must_load_program(temp.path / "root.cst", temp.path / "std");
    const cstc::ast::FnDecl* imported = find_fn_decl(program, "imported");
    assert(imported != nullptr);
    assert(imported->display_name.as_str() == "imported");
    assert(imported->name.as_str() != "imported");
    assert(imported->name.as_str().starts_with("__cst_mod_"));
}

void test_imported_fn_diagnostics_use_source_name() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(temp.path / "lib.cst", "pub fn imported() -> num { }\n");
    write_file(
        temp.path / "root.cst", "import { imported } from \"lib.cst\";\n"
                                "fn main() { imported(); }\n");

    const std::string message = must_fail_lower_with_message(
        temp.path / "root.cst", temp.path / "std",
        "function 'imported' may fall through without returning a value of type 'num'");
    assert(message.find("__cst_mod_") == std::string::npos);
}

void test_imported_extern_link_name_uses_source_name() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "");
    write_file(temp.path / "lib.cst", "pub extern \"c\" fn puts(value: &str);\n");
    write_file(
        temp.path / "root.cst", "import { puts } from \"lib.cst\";\n"
                                "fn main() { puts(\"hello\"); }\n");

    const auto lowered = must_lower(temp.path / "root.cst", temp.path / "std");
    const cstc::tyir::TyExternFnDecl* imported = find_extern_fn_decl(lowered, "puts");
    assert(imported != nullptr);
    assert(imported->link_name.as_str() == "puts");
    assert(imported->name.as_str() != "puts");
    assert(imported->name.as_str().starts_with("__cst_mod_"));
}

} // namespace

int main() {
    test_private_helpers_do_not_collide_across_modules();
    test_private_import_is_rejected();
    test_pub_import_reexports_bindings();
    test_import_alias_can_share_name_across_type_and_value_namespaces();
    test_import_alias_duplicate_value_binding_is_rejected_by_resolver();
    test_pub_items_can_share_export_name_across_type_and_value_namespaces();
    test_pub_type_does_not_export_private_value_binding_with_same_name();
    test_imported_type_keeps_prelude_fallback_value_namespace();
    test_local_value_keeps_prelude_fallback_type_namespace();
    test_std_path_import_resolves_relative_to_std_root();
    test_local_item_shadows_prelude();
    test_explicit_import_shadows_prelude_fallback();
    test_pub_import_can_reexport_prelude_item();
    test_imported_fn_rewrite_preserves_display_name();
    test_imported_fn_diagnostics_use_source_name();
    test_imported_extern_link_name_uses_source_name();
    return 0;
}
