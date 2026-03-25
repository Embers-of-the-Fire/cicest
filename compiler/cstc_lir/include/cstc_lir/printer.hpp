#ifndef CICEST_COMPILER_CSTC_LIR_PRINTER_HPP
#define CICEST_COMPILER_CSTC_LIR_PRINTER_HPP

/// @file printer.hpp
/// @brief Human-readable CFG formatter for LIR programs.
///
/// Usage:
/// @code
///   cstc::symbol::SymbolSession session;
///   const auto lir = cstc::lir_builder::lower_program(tyir_program);
///   std::cout << cstc::lir::format_program(lir);
/// @endcode
///
/// The output shows each function as a sequence of numbered basic blocks,
/// with each block containing its assignments and its terminator.

#include <string>

#include <cstc_lir/lir.hpp>

namespace cstc::lir {

/// Renders a human-readable indented view of the LIR program.
///
/// Symbol ids are resolved through the current session's global symbol table.
[[nodiscard]] inline std::string format_program(const LirProgram& program);

} // namespace cstc::lir

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace cstc::lir {

namespace detail {

inline void indent(std::ostringstream& out, std::size_t level) {
    out << std::string(level * 2, ' ');
}

[[nodiscard]] inline std::string_view unary_op_name(cstc::ast::UnaryOp op) {
    switch (op) {
    case cstc::ast::UnaryOp::Borrow: return "&";
    case cstc::ast::UnaryOp::Negate: return "-";
    case cstc::ast::UnaryOp::Not: return "!";
    }
    return "?";
}

[[nodiscard]] inline std::string_view binary_op_name(cstc::ast::BinaryOp op) {
    switch (op) {
    case cstc::ast::BinaryOp::Add: return "+";
    case cstc::ast::BinaryOp::Sub: return "-";
    case cstc::ast::BinaryOp::Mul: return "*";
    case cstc::ast::BinaryOp::Div: return "/";
    case cstc::ast::BinaryOp::Mod: return "%";
    case cstc::ast::BinaryOp::Eq: return "==";
    case cstc::ast::BinaryOp::Ne: return "!=";
    case cstc::ast::BinaryOp::Lt: return "<";
    case cstc::ast::BinaryOp::Le: return "<=";
    case cstc::ast::BinaryOp::Gt: return ">";
    case cstc::ast::BinaryOp::Ge: return ">=";
    case cstc::ast::BinaryOp::And: return "&&";
    case cstc::ast::BinaryOp::Or: return "||";
    }
    return "?";
}

/// Formats a place as "_%id" or "_%id.field[.field...]".
[[nodiscard]] inline std::string format_place(const LirPlace& place) {
    std::string s = "_%";
    s += std::to_string(place.local_id);
    if (place.kind == LirPlace::Kind::Field) {
        for (const cstc::symbol::Symbol field_name : place.field_path) {
            s += ".";
            s += field_name.is_valid() ? std::string(field_name.as_str()) : "<field>";
        }
    }
    return s;
}

/// Formats an operand as "copy(place)", "move(place)", or the constant display string.
[[nodiscard]] inline std::string format_operand(const LirOperand& op) {
    if (op.kind == LirOperand::Kind::Copy)
        return "copy(" + format_place(op.place) + ")";
    if (op.kind == LirOperand::Kind::Move)
        return "move(" + format_place(op.place) + ")";
    return op.constant.display();
}

inline void print_rvalue(std::ostringstream& out, const LirRvalue& rv) {
    std::visit(
        [&](const auto& node) {
            using N = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<N, LirUse>) {
                out << format_operand(node.operand);
            } else if constexpr (std::is_same_v<N, LirBorrow>) {
                out << "Borrow(" << format_place(node.place) << ")";
            } else if constexpr (std::is_same_v<N, LirBinaryOp>) {
                out << "BinOp(" << binary_op_name(node.op) << ", " << format_operand(node.lhs)
                    << ", " << format_operand(node.rhs) << ")";
            } else if constexpr (std::is_same_v<N, LirUnaryOp>) {
                out << "UnaryOp(" << unary_op_name(node.op) << ", " << format_operand(node.operand)
                    << ")";
            } else if constexpr (std::is_same_v<N, LirCall>) {
                out << "Call(";
                out << (node.fn_name.is_valid() ? std::string(node.fn_name.as_str()) : "<fn>");
                for (const LirOperand& arg : node.args)
                    out << ", " << format_operand(arg);
                out << ")";
            } else if constexpr (std::is_same_v<N, LirStructInit>) {
                out << "StructInit(";
                out
                    << (node.type_name.is_valid() ? std::string(node.type_name.as_str())
                                                  : "<type>");
                for (const LirStructInitField& f : node.fields) {
                    out << ", ";
                    out << (f.name.is_valid() ? std::string(f.name.as_str()) : "<field>");
                    out << ": " << format_operand(f.value);
                }
                out << ")";
            } else if constexpr (std::is_same_v<N, LirEnumVariantRef>) {
                out << "EnumVariant(";
                out
                    << (node.enum_name.is_valid() ? std::string(node.enum_name.as_str())
                                                  : "<enum>");
                out << "::";
                out
                    << (node.variant_name.is_valid() ? std::string(node.variant_name.as_str())
                                                     : "<variant>");
                out << ")";
            }
        },
        rv.node);
}

inline void
    print_terminator(std::ostringstream& out, const LirTerminator& term, std::size_t level) {
    std::visit(
        [&](const auto& node) {
            using N = std::decay_t<decltype(node)>;
            indent(out, level);
            if constexpr (std::is_same_v<N, LirReturn>) {
                out << "return";
                if (node.value.has_value())
                    out << " " << format_operand(*node.value);
                out << "\n";
            } else if constexpr (std::is_same_v<N, LirJump>) {
                out << "jump bb" << node.target << "\n";
            } else if constexpr (std::is_same_v<N, LirSwitchBool>) {
                out << "switchBool(" << format_operand(node.condition) << ") -> [true: bb"
                    << node.true_target << ", false: bb" << node.false_target << "]\n";
            } else if constexpr (std::is_same_v<N, LirUnreachable>) {
                out << "unreachable\n";
            }
        },
        term.node);
}

inline void print_stmt(std::ostringstream& out, const LirStmt& stmt, std::size_t level) {
    std::visit(
        [&](const auto& node) {
            using N = std::decay_t<decltype(node)>;
            indent(out, level);
            if constexpr (std::is_same_v<N, LirAssign>) {
                out << format_place(node.dest) << " = ";
                print_rvalue(out, node.rhs);
                out << "\n";
            } else if constexpr (std::is_same_v<N, LirDrop>) {
                out << "drop _%" << node.local << "\n";
            }
        },
        stmt.node);
}

inline void
    print_basic_block(std::ostringstream& out, const LirBasicBlock& block, std::size_t level) {
    indent(out, level);
    out << "bb" << block.id << ":\n";
    for (const LirStmt& stmt : block.stmts)
        print_stmt(out, stmt, level + 1);
    print_terminator(out, block.terminator, level + 1);
}

inline void print_fn_def(std::ostringstream& out, const LirFnDef& fn, std::size_t level) {
    indent(out, level);
    if (fn.is_runtime)
        out << "runtime ";
    out << "fn ";
    out << (fn.name.is_valid() ? std::string(fn.name.as_str()) : "<fn>");
    out << "(";
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0)
            out << ", ";
        const LirParam& p = fn.params[i];
        out << (p.name.is_valid() ? std::string(p.name.as_str()) : "<param>");
        out << ": " << p.ty.display();
    }
    out << ") -> " << fn.return_ty.display() << "\n";

    // Locals table
    indent(out, level + 1);
    out << "locals: [";
    for (std::size_t i = 0; i < fn.locals.size(); ++i) {
        if (i > 0)
            out << ", ";
        const LirLocalDecl& loc = fn.locals[i];
        out << "_%" << loc.id << ": " << loc.ty.display();
        if (loc.debug_name.has_value() && loc.debug_name->is_valid())
            out << " /* " << loc.debug_name->as_str() << " */";
    }
    out << "]\n";

    for (const LirBasicBlock& block : fn.blocks)
        print_basic_block(out, block, level + 1);
}

inline void print_struct_decl(std::ostringstream& out, const LirStructDecl& s, std::size_t level) {
    indent(out, level);
    if (s.is_zst) {
        out << "struct " << s.name.as_str() << ";\n";
    } else {
        out << "struct " << s.name.as_str() << " {\n";
        for (const LirStructField& f : s.fields) {
            indent(out, level + 1);
            out << f.name.as_str() << ": " << f.ty.display() << "\n";
        }
        indent(out, level);
        out << "}\n";
    }
}

inline void print_enum_decl(std::ostringstream& out, const LirEnumDecl& e, std::size_t level) {
    indent(out, level);
    out << "enum " << e.name.as_str() << " {\n";
    for (const LirEnumVariant& v : e.variants) {
        indent(out, level + 1);
        out << v.name.as_str();
        if (v.discriminant.has_value())
            out << " = " << v.discriminant->as_str();
        out << "\n";
    }
    indent(out, level);
    out << "}\n";
}

inline void
    print_extern_fn_decl(std::ostringstream& out, const LirExternFnDecl& fn, std::size_t level) {
    indent(out, level);
    if (fn.is_runtime)
        out << "runtime ";
    out << "extern \"" << fn.abi.as_str() << "\" fn ";
    out << (fn.name.is_valid() ? std::string(fn.name.as_str()) : "<fn>");
    out << "(";
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0)
            out << ", ";
        const LirParam& p = fn.params[i];
        out << (p.name.is_valid() ? std::string(p.name.as_str()) : "<param>");
        out << ": " << p.ty.display();
    }
    out << ") -> " << fn.return_ty.display() << "\n";
}

inline void print_extern_struct_decl(
    std::ostringstream& out, const LirExternStructDecl& s, std::size_t level) {
    indent(out, level);
    out << "extern \"" << s.abi.as_str() << "\" struct " << s.name.as_str() << ";\n";
}

} // namespace detail

inline std::string format_program(const LirProgram& program) {
    std::ostringstream out;
    out << "LirProgram\n";
    for (const LirStructDecl& s : program.structs)
        detail::print_struct_decl(out, s, 1);
    for (const LirEnumDecl& e : program.enums)
        detail::print_enum_decl(out, e, 1);
    for (const LirExternFnDecl& ext : program.extern_fns)
        detail::print_extern_fn_decl(out, ext, 1);
    for (const LirExternStructDecl& ext_s : program.extern_structs)
        detail::print_extern_struct_decl(out, ext_s, 1);
    for (const LirFnDef& fn : program.fns)
        detail::print_fn_def(out, fn, 1);
    return out.str();
}

} // namespace cstc::lir

#endif // CICEST_COMPILER_CSTC_LIR_PRINTER_HPP
