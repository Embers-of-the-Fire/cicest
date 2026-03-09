#ifndef CICEST_COMPILER_CSTC_HIR_HIR_HPP
#define CICEST_COMPILER_CSTC_HIR_HIR_HPP

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cstc::hir {

struct Type;
struct Expr;

using TypePtr = std::unique_ptr<Type>;
using ExprPtr = std::unique_ptr<Expr>;

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

enum class TypeContractKind {
    Async,
    Runtime,
    NotAsync,
    NotRuntime,
};

struct PathType {
    std::vector<std::string> segments;
    std::vector<TypePtr> args;
};

struct ContractType {
    TypeContractKind kind;
    TypePtr inner;
};

struct TupleType {
    std::vector<TypePtr> elements;
};

struct FunctionType {
    std::vector<TypePtr> params;
    TypePtr result;
};

using TypeKind = std::variant<PathType, ContractType, TupleType, FunctionType>;

struct Type {
    TypeKind kind;
};

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

enum class ContractBlockKind {
    Async,
    Sync,
    Runtime,
    Const,
};

struct RawExpr {
    std::string text;
};

struct LiteralExpr {
    std::string text;
};

struct PathExpr {
    std::vector<std::string> segments;
};

struct BinaryExpr {
    std::string op;
    ExprPtr lhs;
    ExprPtr rhs;
};

struct CallExpr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
};

struct MemberAccessExpr {
    ExprPtr receiver;
    std::string member;
};

struct MemberCallExpr {
    ExprPtr receiver;
    std::string member;
    std::vector<ExprPtr> args;
};

struct StaticMemberAccessExpr {
    Type receiver_type;
    std::string member;
    ExprPtr receiver;
};

struct StaticMemberCallExpr {
    Type receiver_type;
    std::string member;
    ExprPtr receiver;
    std::vector<ExprPtr> args;
};

struct ContractBlockExpr {
    ContractBlockKind kind;
    std::vector<ExprPtr> body;
};

struct SyncExpr {
    ExprPtr expr;
};

struct LiftedConstantExpr {
    std::string name;
    Type type;
    std::string value;
};

using ExprKind = std::variant<RawExpr, LiteralExpr, PathExpr, BinaryExpr, CallExpr,
    MemberAccessExpr, MemberCallExpr, StaticMemberAccessExpr, StaticMemberCallExpr,
    ContractBlockExpr, SyncExpr, LiftedConstantExpr>;

struct Expr {
    ExprKind kind;
};

// ---------------------------------------------------------------------------
// Declarations and module
// ---------------------------------------------------------------------------

struct FnParam {
    std::string name;
    Type type;
};

struct FunctionDecl {
    std::string name;
    std::vector<std::string> generic_params;
    std::vector<FnParam> params;
    Type return_type;
};

struct RawDecl {
    std::string name;
    std::string text;
};

using DeclHeader = std::variant<FunctionDecl, RawDecl>;

struct Declaration {
    DeclHeader header;
    std::vector<ExprPtr> body;
    std::vector<ExprPtr> constraints;
};

struct Module {
    std::vector<Declaration> declarations;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] inline TypePtr make_type(TypeKind kind) {
    return std::make_unique<Type>(Type{.kind = std::move(kind)});
}

[[nodiscard]] inline ExprPtr make_expr(ExprKind kind) {
    return std::make_unique<Expr>(Expr{.kind = std::move(kind)});
}

} // namespace cstc::hir

#endif // CICEST_COMPILER_CSTC_HIR_HIR_HPP

