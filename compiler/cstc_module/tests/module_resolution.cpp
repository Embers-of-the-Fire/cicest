#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

void must_lower(
    const std::filesystem::path& root_path, const std::filesystem::path& std_root_path) {
    cstc::span::SourceMap source_map;
    const auto program = cstc::module::load_program(source_map, root_path, std_root_path);
    if (!program.has_value())
        std::cerr << program.error().message << '\n';
    assert(program.has_value());

    const auto lowered = cstc::tyir_builder::lower_program(*program);
    if (!lowered.has_value())
        std::cerr << lowered.error().message << '\n';
    assert(lowered.has_value());
}

void must_fail_with_message(
    const std::filesystem::path& root_path, const std::filesystem::path& std_root_path,
    std::string_view expected) {
    cstc::span::SourceMap source_map;
    const auto program = cstc::module::load_program(source_map, root_path, std_root_path);
    assert(!program.has_value());
    assert(program.error().message.find(expected) != std::string::npos);
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

void test_prelude_is_implicit_and_duplicates_are_rejected() {
    cstc::symbol::SymbolSession session;
    const TempDir temp = make_temp_dir();

    write_file(temp.path / "std" / "prelude.cst", "pub fn print() { }\n");
    write_file(
        temp.path / "root.cst", "fn print() { }\n"
                                "fn main() { }\n");

    must_fail_with_message(
        temp.path / "root.cst", temp.path / "std", "duplicate function name 'print'");
}

} // namespace

int main() {
    test_private_helpers_do_not_collide_across_modules();
    test_private_import_is_rejected();
    test_pub_import_reexports_bindings();
    test_std_path_import_resolves_relative_to_std_root();
    test_prelude_is_implicit_and_duplicates_are_rejected();
    return 0;
}
