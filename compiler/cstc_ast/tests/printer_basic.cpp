#include <cassert>
#include <string>

#include <cstc_ast/ast.hpp>
#include <cstc_ast/printer.hpp>

int main() {
    cstc::ast::Program program;

    cstc::ast::StructDecl user;
    user.name = "User";
    user.fields.push_back(
        cstc::ast::FieldDecl{
            .name = "id",
            .type = cstc::ast::TypeRef{.kind = cstc::ast::TypeKind::Num, .name = "num"},
            .span = {                      .start = 0,      .end = 0},
    });
    program.items.push_back(user);

    const std::string rendered = cstc::ast::format_program(program);
    assert(rendered.find("Program") != std::string::npos);
    assert(rendered.find("StructDecl User") != std::string::npos);
    assert(rendered.find("id: num") != std::string::npos);

    return 0;
}
