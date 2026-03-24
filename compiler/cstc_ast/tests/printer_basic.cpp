#include <cassert>
#include <string>

#include <cstc_ast/ast.hpp>
#include <cstc_ast/printer.hpp>
#include <cstc_symbol/symbol.hpp>

int main() {
    cstc::symbol::SymbolSession session;

    cstc::ast::Program program;

    cstc::ast::StructDecl user;
    user.is_public = true;
    user.name = cstc::symbol::Symbol::intern("User");
    user.fields.push_back(
        cstc::ast::FieldDecl{
            .name = cstc::symbol::Symbol::intern("id"),
            .type =
                cstc::ast::TypeRef{
                                   .kind = cstc::ast::TypeKind::Num,
                                   .symbol = cstc::symbol::Symbol::intern("num"),
                                   .display_name = cstc::symbol::kInvalidSymbol,
                                   .pointee = nullptr,
                                   },
            .span = {.start = 0, .end = 0},
    });
    program.items.push_back(user);

    cstc::ast::ImportDecl import;
    import.path = cstc::symbol::Symbol::intern("@std/prelude.cst");
    import.items.push_back(
        cstc::ast::ImportItem{
            .name = cstc::symbol::Symbol::intern("println"),
            .alias = cstc::symbol::Symbol::intern("log"),
            .span = {.start = 0, .end = 0},
    });
    program.items.push_back(import);

    const std::string rendered = cstc::ast::format_program(program);
    assert(rendered.find("Program") != std::string::npos);
    assert(rendered.find("Pub StructDecl User") != std::string::npos);
    assert(rendered.find("id: num") != std::string::npos);
    assert(rendered.find("ImportDecl from \"@std/prelude.cst\"") != std::string::npos);
    assert(rendered.find("println as log") != std::string::npos);

    return 0;
}
