#ifndef CICEST_COMPILER_CSTC_AST_PRINTER_HPP
#define CICEST_COMPILER_CSTC_AST_PRINTER_HPP

#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

#include "cstc_ast/ast.hpp"

namespace cstc::ast {

namespace detail {

class AstPrinterImpl {
public:
    AstPrinterImpl(std::ostream& out, const SymbolTable* symbols)
        : out_(out)
        , symbols_(symbols) {}

    void print_crate(const Crate& crate) {
        line("crate");
        const IndentScope scope{*this};

        if (crate.items.empty()) {
            line("(empty)");
            return;
        }

        for (const auto& item : crate.items)
            print_item(item);
    }

private:
    class IndentScope {
    public:
        explicit IndentScope(AstPrinterImpl& printer)
            : printer_(printer) {
            ++printer_.indent_;
        }

        ~IndentScope() { --printer_.indent_; }

    private:
        AstPrinterImpl& printer_;
    };

    [[nodiscard]] std::string symbol_text(Symbol symbol) const {
        if (symbols_ != nullptr) {
            const auto name = symbols_->str(symbol);
            if (!name.empty())
                return std::string{name};
        }
        return "$" + std::to_string(symbol.symbol_id);
    }

    [[nodiscard]] std::string path_text(const Path& path) const {
        if (path.segments.empty())
            return "<empty-path>";

        std::string text;
        for (std::size_t index = 0; index < path.segments.size(); ++index) {
            if (index != 0)
                text += "::";
            text += symbol_text(path.segments[index].name);
        }
        return text;
    }

    [[nodiscard]] static std::string lit_kind_text(LitKind kind) {
        switch (kind) {
        case LitKind::Int: return "int";
        case LitKind::Float: return "float";
        case LitKind::Bool: return "bool";
        case LitKind::String: return "string";
        case LitKind::Char: return "char";
        }

        return "unknown";
    }

    [[nodiscard]] static std::string keyword_kind_text(KeywordKind kind) {
        switch (kind) {
        case KeywordKind::Async: return "async";
        case KeywordKind::Runtime: return "runtime";
        case KeywordKind::NotAsync: return "!async";
        case KeywordKind::NotRuntime: return "!runtime";
        }

        return "unknown";
    }

    [[nodiscard]] static std::string unary_op_text(UnaryOp op) {
        switch (op) {
        case UnaryOp::Neg: return "neg";
        case UnaryOp::Not: return "not";
        case UnaryOp::Borrow: return "borrow";
        case UnaryOp::Deref: return "deref";
        }

        return "unknown";
    }

    [[nodiscard]] static std::string binary_op_text(BinaryOp op) {
        switch (op) {
        case BinaryOp::Add: return "add";
        case BinaryOp::Sub: return "sub";
        case BinaryOp::Mul: return "mul";
        case BinaryOp::Div: return "div";
        case BinaryOp::Mod: return "mod";
        case BinaryOp::BitAnd: return "bitand";
        case BinaryOp::BitOr: return "bitor";
        case BinaryOp::BitXor: return "bitxor";
        case BinaryOp::Shl: return "shl";
        case BinaryOp::Shr: return "shr";
        case BinaryOp::Eq: return "eq";
        case BinaryOp::Ne: return "ne";
        case BinaryOp::Lt: return "lt";
        case BinaryOp::Gt: return "gt";
        case BinaryOp::Le: return "le";
        case BinaryOp::Ge: return "ge";
        case BinaryOp::And: return "and";
        case BinaryOp::Or: return "or";
        case BinaryOp::Assign: return "assign";
        }

        return "unknown";
    }

    void line(std::string_view text) {
        for (std::size_t level = 0; level < indent_; ++level)
            out_ << "  ";
        out_ << text << '\n';
    }

    void print_keyword_modifiers(const std::vector<KeywordModifier>& keywords) {
        if (keywords.empty()) {
            line("keywords: (none)");
            return;
        }

        line("keywords");
        const IndentScope scope{*this};
        for (const auto& keyword : keywords) {
            std::string text = keyword_kind_text(keyword.kind);
            if (keyword.type_var.has_value())
                text += "<" + symbol_text(*keyword.type_var) + ">";
            line(text);
        }
    }

    void print_generics(const Generics& generics) {
        if (!generics.params.has_value() && !generics.where_clause.has_value()) {
            line("generics: (none)");
            return;
        }

        line("generics");
        const IndentScope scope{*this};

        if (generics.params.has_value()) {
            line("params");
            {
                const IndentScope params_scope{*this};
                if (generics.params->params.empty()) {
                    line("(empty)");
                } else {
                    for (const auto& param : generics.params->params)
                        line(symbol_text(param.name));
                }
            }
        }

        if (generics.where_clause.has_value()) {
            line("where");
            const IndentScope where_scope{*this};
            if (generics.where_clause->predicates.empty()) {
                line("(empty)");
            } else {
                for (const auto& predicate : generics.where_clause->predicates)
                    print_expr(*predicate.expr);
            }
        }
    }

    void print_fn_sig(const FnSig& sig) {
        line("signature");
        const IndentScope scope{*this};

        if (sig.self_param.has_value()) {
            line(sig.self_param->is_ref ? "self: &self" : "self: typed");
            {
                const IndentScope self_scope{*this};
                print_keyword_modifiers(sig.self_param->keywords);
                if (sig.self_param->explicit_ty.has_value())
                    print_type(*sig.self_param->explicit_ty->get());
            }
        } else {
            line("self: (none)");
        }

        line("params");
        {
            const IndentScope params_scope{*this};
            if (sig.params.empty()) {
                line("(empty)");
            } else {
                for (const auto& param : sig.params) {
                    line("param " + symbol_text(param.name));
                    const IndentScope param_scope{*this};
                    print_type(*param.ty);
                }
            }
        }

        line("return");
        {
            const IndentScope ret_scope{*this};
            print_type(*sig.ret_ty);
        }
    }

    void print_struct_fields(const std::vector<StructField>& fields) {
        if (fields.empty()) {
            line("fields: (empty)");
            return;
        }

        line("fields");
        const IndentScope scope{*this};
        for (const auto& field : fields) {
            line("field " + symbol_text(field.name));
            const IndentScope field_scope{*this};
            print_type(*field.ty);
        }
    }

    void print_item(const Item& item) {
        std::visit(
            [this](const auto& kind) {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, FnItem>) {
                    line("item fn " + symbol_text(kind.name));
                    const IndentScope scope{*this};
                    print_keyword_modifiers(kind.keywords);
                    print_generics(kind.generics);
                    print_fn_sig(kind.sig);
                    print_block(kind.body);
                } else if constexpr (std::is_same_v<Kind, ExternFnItem>) {
                    line("item extern fn " + symbol_text(kind.name));
                    const IndentScope scope{*this};
                    print_keyword_modifiers(kind.keywords);
                    print_fn_sig(kind.sig);
                } else if constexpr (std::is_same_v<Kind, MarkerStructItem>) {
                    line("item struct " + symbol_text(kind.name) + " (marker)");
                    const IndentScope scope{*this};
                    print_generics(kind.generics);
                } else if constexpr (std::is_same_v<Kind, NamedStructItem>) {
                    line("item struct " + symbol_text(kind.name) + " (named)");
                    const IndentScope scope{*this};
                    print_generics(kind.generics);
                    print_struct_fields(kind.fields);
                } else if constexpr (std::is_same_v<Kind, TupleStructItem>) {
                    line("item struct " + symbol_text(kind.name) + " (tuple)");
                    const IndentScope scope{*this};
                    print_generics(kind.generics);
                    line("fields");
                    {
                        const IndentScope field_scope{*this};
                        if (kind.fields.empty()) {
                            line("(empty)");
                        } else {
                            for (const auto& field_type : kind.fields)
                                print_type(*field_type);
                        }
                    }
                } else if constexpr (std::is_same_v<Kind, EnumItem>) {
                    line("item enum " + symbol_text(kind.name));
                    const IndentScope scope{*this};
                    print_generics(kind.generics);
                    line("variants");
                    {
                        const IndentScope variant_scope{*this};
                        if (kind.variants.empty()) {
                            line("(empty)");
                        } else {
                            for (const auto& variant : kind.variants) {
                                line("variant " + symbol_text(variant.name));
                                const IndentScope value_scope{*this};
                                std::visit(
                                    [this](const auto& variant_kind) {
                                        using VariantKind = std::decay_t<decltype(variant_kind)>;
                                        if constexpr (std::is_same_v<VariantKind, UnitVariant>) {
                                            line("unit");
                                        } else if constexpr (std::is_same_v<
                                                                 VariantKind, FieldsVariant>) {
                                            print_struct_fields(variant_kind.fields);
                                        } else if constexpr (std::is_same_v<
                                                                 VariantKind, TupleVariant>) {
                                            line("tuple");
                                            const IndentScope tuple_scope{*this};
                                            if (variant_kind.types.empty()) {
                                                line("(empty)");
                                            } else {
                                                for (const auto& tuple_type : variant_kind.types)
                                                    print_type(*tuple_type);
                                            }
                                        }
                                    },
                                    variant.kind);
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<Kind, TypeAliasItem>) {
                    line("item type " + symbol_text(kind.name));
                    const IndentScope scope{*this};
                    print_generics(kind.generics);
                    print_type(*kind.ty);
                }
            },
            item.kind);
    }

    void print_block(const Block& block) {
        line("block");
        const IndentScope scope{*this};

        if (block.stmts.empty()) {
            line("(empty)");
            return;
        }

        for (const auto& stmt : block.stmts)
            print_stmt(stmt);
    }

    void print_stmt(const Stmt& stmt) {
        std::visit(
            [this](const auto& kind) {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, LetStmt>) {
                    line("stmt let");
                    const IndentScope scope{*this};
                    print_pat(*kind.pat);

                    if (kind.ty.has_value())
                        print_type(*kind.ty->get());
                    else
                        line("type: (none)");

                    if (kind.init.has_value())
                        print_expr(*kind.init->get());
                    else
                        line("init: (none)");
                } else if constexpr (std::is_same_v<Kind, ExprStmt>) {
                    line(kind.has_semi ? "stmt expr;" : "stmt expr");
                    const IndentScope scope{*this};
                    print_expr(*kind.expr);
                } else if constexpr (std::is_same_v<Kind, ItemStmt>) {
                    line("stmt item");
                    const IndentScope scope{*this};
                    print_item(*kind.item);
                }
            },
            stmt.kind);
    }

    void print_type(const TypeNode& type_node) {
        std::visit(
            [this](const auto& kind) {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, PathType>) {
                    line("type path " + path_text(kind.path));
                    if (kind.args.has_value()) {
                        const IndentScope scope{*this};
                        line("generic-args");
                        {
                            const IndentScope args_scope{*this};
                            if (kind.args->args.empty()) {
                                line("(empty)");
                            } else {
                                for (const auto& arg : kind.args->args)
                                    print_type(*arg);
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<Kind, KeywordType>) {
                    line("type keyword");
                    const IndentScope scope{*this};
                    print_keyword_modifiers(kind.keywords);
                    print_type(*kind.inner);
                } else if constexpr (std::is_same_v<Kind, RefType>) {
                    line("type ref");
                    const IndentScope scope{*this};
                    print_type(*kind.inner);
                } else if constexpr (std::is_same_v<Kind, TupleType>) {
                    line("type tuple");
                    const IndentScope scope{*this};
                    if (kind.elements.empty()) {
                        line("(empty)");
                    } else {
                        for (const auto& element : kind.elements)
                            print_type(*element);
                    }
                } else if constexpr (std::is_same_v<Kind, FnType>) {
                    line("type fn");
                    const IndentScope scope{*this};
                    line("params");
                    {
                        const IndentScope param_scope{*this};
                        if (kind.params.empty()) {
                            line("(empty)");
                        } else {
                            for (const auto& param : kind.params)
                                print_type(*param);
                        }
                    }
                    line("return");
                    {
                        const IndentScope ret_scope{*this};
                        print_type(*kind.ret);
                    }
                } else if constexpr (std::is_same_v<Kind, InferredType>) {
                    line("type _");
                }
            },
            type_node.kind);
    }

    void print_pat(const Pat& pat) {
        std::visit(
            [this](const auto& kind) {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, WildcardPat>) {
                    line("pat _");
                } else if constexpr (std::is_same_v<Kind, BindingPat>) {
                    line("pat binding " + symbol_text(kind.name));
                } else if constexpr (std::is_same_v<Kind, LitPat>) {
                    line("pat lit " + lit_kind_text(kind.lit.kind) + " " + kind.lit.value);
                } else if constexpr (std::is_same_v<Kind, ConstructorFieldsPat>) {
                    line("pat constructor-fields " + path_text(kind.constructor));
                    const IndentScope scope{*this};
                    if (kind.fields.empty()) {
                        line("(empty)");
                    } else {
                        for (const auto& field : kind.fields) {
                            line("field " + symbol_text(field.name));
                            const IndentScope field_scope{*this};
                            print_pat(*field.pat);
                        }
                    }
                } else if constexpr (std::is_same_v<Kind, ConstructorPositionalPat>) {
                    line("pat constructor-positional " + path_text(kind.constructor));
                    const IndentScope scope{*this};
                    if (kind.args.empty()) {
                        line("(empty)");
                    } else {
                        for (const auto& arg : kind.args)
                            print_pat(*arg);
                    }
                } else if constexpr (std::is_same_v<Kind, ConstructorUnitPat>) {
                    line("pat constructor-unit " + path_text(kind.constructor));
                } else if constexpr (std::is_same_v<Kind, TuplePat>) {
                    line("pat tuple");
                    const IndentScope scope{*this};
                    if (kind.elements.empty()) {
                        line("(empty)");
                    } else {
                        for (const auto& element : kind.elements)
                            print_pat(*element);
                    }
                } else if constexpr (std::is_same_v<Kind, OrPat>) {
                    line("pat or");
                    const IndentScope scope{*this};
                    if (kind.alternatives.empty()) {
                        line("(empty)");
                    } else {
                        for (const auto& alternative : kind.alternatives)
                            print_pat(*alternative);
                    }
                } else if constexpr (std::is_same_v<Kind, AsPat>) {
                    line("pat as " + symbol_text(kind.name));
                    const IndentScope scope{*this};
                    print_pat(*kind.inner);
                }
            },
            pat.kind);
    }

    void print_expr(const Expr& expr) {
        std::visit(
            [this](const auto& kind) {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, LitExpr>) {
                    line("expr lit " + lit_kind_text(kind.lit.kind) + " " + kind.lit.value);
                } else if constexpr (std::is_same_v<Kind, PathExpr>) {
                    line("expr path " + path_text(kind.path));
                } else if constexpr (std::is_same_v<Kind, BlockExpr>) {
                    line("expr block");
                    const IndentScope scope{*this};
                    print_block(kind.block);
                } else if constexpr (std::is_same_v<Kind, GroupedExpr>) {
                    line("expr grouped");
                    const IndentScope scope{*this};
                    print_expr(*kind.inner);
                } else if constexpr (std::is_same_v<Kind, TupleExpr>) {
                    line("expr tuple");
                    const IndentScope scope{*this};
                    if (kind.elements.empty()) {
                        line("(empty)");
                    } else {
                        for (const auto& element : kind.elements)
                            print_expr(*element);
                    }
                } else if constexpr (std::is_same_v<Kind, UnaryExpr>) {
                    line("expr unary " + unary_op_text(kind.op));
                    const IndentScope scope{*this};
                    print_expr(*kind.operand);
                } else if constexpr (std::is_same_v<Kind, BinaryExpr>) {
                    line("expr binary " + binary_op_text(kind.op));
                    const IndentScope scope{*this};
                    print_expr(*kind.lhs);
                    print_expr(*kind.rhs);
                } else if constexpr (std::is_same_v<Kind, CallExpr>) {
                    line("expr call");
                    const IndentScope scope{*this};
                    line("callee");
                    {
                        const IndentScope callee_scope{*this};
                        print_expr(*kind.callee);
                    }
                    line("args");
                    {
                        const IndentScope arg_scope{*this};
                        if (kind.args.empty()) {
                            line("(empty)");
                        } else {
                            for (const auto& arg : kind.args)
                                print_expr(*arg);
                        }
                    }
                } else if constexpr (std::is_same_v<Kind, MethodCallExpr>) {
                    line("expr method-call " + symbol_text(kind.method.name));
                    const IndentScope scope{*this};
                    line("receiver");
                    {
                        const IndentScope receiver_scope{*this};
                        print_expr(*kind.receiver);
                    }
                    if (kind.turbofish.has_value()) {
                        line("turbofish");
                        const IndentScope turbofish_scope{*this};
                        if (kind.turbofish->args.empty()) {
                            line("(empty)");
                        } else {
                            for (const auto& arg : kind.turbofish->args)
                                print_type(*arg);
                        }
                    }
                    line("args");
                    {
                        const IndentScope arg_scope{*this};
                        if (kind.args.empty()) {
                            line("(empty)");
                        } else {
                            for (const auto& arg : kind.args)
                                print_expr(*arg);
                        }
                    }
                } else if constexpr (std::is_same_v<Kind, FieldExpr>) {
                    line("expr field " + symbol_text(kind.field));
                    const IndentScope scope{*this};
                    print_expr(*kind.object);
                } else if constexpr (std::is_same_v<Kind, ConstructorFieldsExpr>) {
                    line("expr constructor-fields " + path_text(kind.constructor));
                    const IndentScope scope{*this};
                    if (kind.fields.empty()) {
                        line("(empty)");
                    } else {
                        for (const auto& field : kind.fields) {
                            line("field " + symbol_text(field.name));
                            const IndentScope field_scope{*this};
                            print_expr(*field.value);
                        }
                    }
                } else if constexpr (std::is_same_v<Kind, ConstructorPositionalExpr>) {
                    line("expr constructor-positional " + path_text(kind.constructor));
                    const IndentScope scope{*this};
                    if (kind.args.empty()) {
                        line("(empty)");
                    } else {
                        for (const auto& arg : kind.args)
                            print_expr(*arg);
                    }
                } else if constexpr (std::is_same_v<Kind, LambdaExpr>) {
                    line("expr lambda");
                    const IndentScope scope{*this};
                    line("params");
                    {
                        const IndentScope param_scope{*this};
                        if (kind.params.empty()) {
                            line("(empty)");
                        } else {
                            for (const auto& param : kind.params) {
                                line("param " + symbol_text(param.name));
                                if (param.ty.has_value()) {
                                    const IndentScope ty_scope{*this};
                                    print_type(*param.ty->get());
                                }
                            }
                        }
                    }
                    print_block(kind.body);
                } else if constexpr (std::is_same_v<Kind, MatchExpr>) {
                    line("expr match");
                    const IndentScope scope{*this};
                    line("scrutinee");
                    {
                        const IndentScope scrutinee_scope{*this};
                        print_expr(*kind.scrutinee);
                    }
                    line("arms");
                    {
                        const IndentScope arm_scope{*this};
                        if (kind.arms.empty()) {
                            line("(empty)");
                        } else {
                            for (const auto& arm : kind.arms) {
                                line("arm");
                                const IndentScope one_arm_scope{*this};
                                print_pat(*arm.pat);
                                print_expr(*arm.body);
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<Kind, IfExpr>) {
                    line("expr if");
                    const IndentScope scope{*this};
                    line("condition");
                    {
                        const IndentScope cond_scope{*this};
                        print_expr(*kind.cond);
                    }
                    line("then");
                    {
                        const IndentScope then_scope{*this};
                        print_block(kind.then_block);
                    }
                    if (kind.else_expr.has_value()) {
                        line("else");
                        const IndentScope else_scope{*this};
                        print_expr(*kind.else_expr->get());
                    }
                } else if constexpr (std::is_same_v<Kind, LoopExpr>) {
                    line("expr loop");
                    const IndentScope scope{*this};
                    print_block(kind.body);
                } else if constexpr (std::is_same_v<Kind, ReturnExpr>) {
                    line("expr return");
                    if (kind.value.has_value()) {
                        const IndentScope scope{*this};
                        print_expr(*kind.value->get());
                    }
                } else if constexpr (std::is_same_v<Kind, KeywordBlockExpr>) {
                    line("expr keyword-block");
                    const IndentScope scope{*this};
                    print_keyword_modifiers(kind.keywords);
                    print_block(kind.body);
                } else if constexpr (std::is_same_v<Kind, TurbofishExpr>) {
                    line("expr turbofish");
                    const IndentScope scope{*this};
                    line("base");
                    {
                        const IndentScope base_scope{*this};
                        print_expr(*kind.base);
                    }
                    line("args");
                    {
                        const IndentScope arg_scope{*this};
                        if (kind.args.args.empty()) {
                            line("(empty)");
                        } else {
                            for (const auto& arg : kind.args.args)
                                print_type(*arg);
                        }
                    }
                }
            },
            expr.kind);
    }

    std::ostream& out_;
    const SymbolTable* symbols_;
    std::size_t indent_ = 0;
};

} // namespace detail

class AstPrinter {
public:
    explicit AstPrinter(const SymbolTable* symbols = nullptr)
        : symbols_(symbols) {}

    [[nodiscard]] std::string format(const Crate& crate) const {
        std::ostringstream out;
        print(out, crate);
        return out.str();
    }

    void print(std::ostream& out, const Crate& crate) const {
        detail::AstPrinterImpl printer{out, symbols_};
        printer.print_crate(crate);
    }

private:
    const SymbolTable* symbols_;
};

[[nodiscard]] inline std::string
    format_ast(const Crate& crate, const SymbolTable* symbols = nullptr) {
    return AstPrinter{symbols}.format(crate);
}

inline void print_ast(std::ostream& out, const Crate& crate, const SymbolTable* symbols = nullptr) {
    AstPrinter{symbols}.print(out, crate);
}

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_PRINTER_HPP
