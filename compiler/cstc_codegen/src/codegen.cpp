/// @file codegen.cpp
/// @brief LLVM IR code generation from LIR.
///
/// All LLVM header usage is confined to this translation unit.
///
/// High-level lowering pipeline:
/// 1. Declare aggregate types (structs/enums).
/// 2. Declare function signatures.
/// 3. Lower function bodies block-by-block.
/// 4. Run local promotion passes.
/// 5. Print the final module.
///
/// This file is intentionally implementation-only. Callers interact through
/// `emit_llvm_ir` in `codegen.hpp`.

#include <cstc_codegen/codegen.hpp>

#include <cassert>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>

namespace cstc::codegen {

using namespace cstc::lir;
using cstc::symbol::Symbol;

// ─── CodegenContext ─────────────────────────────────────────────────────────

/// Owns all transient LLVM objects required while lowering one `LirProgram`.
///
/// The context is single-use: construct, call `emit()`, discard. This keeps
/// ownership and temporary lowering state local to a single API invocation.

class CodegenContext {
public:
    /// Creates a fresh lowering context and initializes the target module.
    CodegenContext(const LirProgram& program, std::string_view module_name)
        : program_(program)
        , module_(std::string(module_name), context_)
        , builder_(context_) {}

    /// Executes the full lowering pipeline and returns textual LLVM IR.
    std::string emit() {
        declare_structs();
        declare_enums();
        declare_functions();
        define_functions();
        run_passes();
        return print_module();
    }

private:
    // ─── Type mapping ───────────────────────────────────────────────────────

    /// Converts a TyIR type into its LLVM IR representation.
    ///
    /// Named types are resolved through previously declared struct/enum maps.
    /// Unknown names conservatively fall back to an empty struct type.
    llvm::Type* map_type(const Ty& ty) {
        switch (ty.kind) {
        case tyir::TyKind::Num: return llvm::Type::getDoubleTy(context_);
        case tyir::TyKind::Bool: return llvm::Type::getInt1Ty(context_);
        case tyir::TyKind::Str: return llvm::PointerType::getUnqual(context_);
        case tyir::TyKind::Unit: return llvm::StructType::get(context_);
        case tyir::TyKind::Never: return llvm::Type::getVoidTy(context_);
        case tyir::TyKind::Named: {
            const std::string type_name(ty.name.as_str());
            auto it = struct_types_.find(type_name);
            if (it != struct_types_.end())
                return it->second;
            auto eit = enum_types_.find(type_name);
            if (eit != enum_types_.end())
                return eit->second;
            // ZST structs still get an empty struct type
            return llvm::StructType::get(context_);
        }
        }
        return llvm::Type::getVoidTy(context_);
    }

    /// Converts function return types.
    ///
    /// Unit and never map to `void` in function signatures.
    llvm::Type* map_return_type(const Ty& ty) {
        if (ty.kind == tyir::TyKind::Unit || ty.kind == tyir::TyKind::Never)
            return llvm::Type::getVoidTy(context_);
        return map_type(ty);
    }

    /// Returns true if the function return path is represented as `ret void`.
    bool is_void_return(const Ty& ty) {
        return ty.kind == tyir::TyKind::Unit || ty.kind == tyir::TyKind::Never;
    }

    /// Returns true if `ty` is the unit type.
    bool is_unit_type(const Ty& ty) { return ty.kind == tyir::TyKind::Unit; }

    // ─── Struct declarations ────────────────────────────────────────────────

    /// Declares all struct types before any function body lowering occurs.
    ///
    /// This guarantees named type lookup succeeds even with forward references.
    void declare_structs() {
        for (const LirStructDecl& decl : program_.structs) {
            std::string name(decl.name.as_str());
            if (decl.is_zst || decl.fields.empty()) {
                auto* st = llvm::StructType::create(context_, name);
                st->setBody({});
                struct_types_[name] = st;
                struct_decls_[name] = &decl;
            } else {
                std::vector<llvm::Type*> field_types;
                field_types.reserve(decl.fields.size());
                for (const LirStructField& field : decl.fields)
                    field_types.push_back(map_type(field.ty));

                auto* st = llvm::StructType::create(context_, field_types, name);
                struct_types_[name] = st;
                struct_decls_[name] = &decl;
            }
        }
    }

    // ─── Enum declarations ──────────────────────────────────────────────────

    /// Declares all enum types and records variant discriminant mappings.
    ///
    /// Current representation is discriminant-only: `{ i32 }`.
    void declare_enums() {
        for (const LirEnumDecl& decl : program_.enums) {
            std::string name(decl.name.as_str());
            // Enums are represented as { i32 } (discriminant only)
            auto* i32_ty = llvm::Type::getInt32Ty(context_);
            auto* st = llvm::StructType::create(context_, {i32_ty}, name);
            enum_types_[name] = st;

            // Build discriminant map: variant_name -> index
            auto& disc_map = enum_discriminants_[name];
            for (std::size_t i = 0; i < decl.variants.size(); ++i) {
                std::string vname(decl.variants[i].name.as_str());
                if (decl.variants[i].discriminant.has_value()) {
                    std::string disc_text(decl.variants[i].discriminant->as_str());
                    disc_map[vname] = static_cast<uint32_t>(std::stoul(disc_text));
                } else {
                    disc_map[vname] = static_cast<uint32_t>(i);
                }
            }
        }
    }

    // ─── Function forward declarations ──────────────────────────────────────

    /// Creates all function declarations first to support direct calls between
    /// functions regardless of source order.
    void declare_functions() {
        for (const LirFnDef& fn : program_.fns) {
            std::vector<llvm::Type*> param_types;
            param_types.reserve(fn.params.size());
            for (const LirParam& p : fn.params)
                param_types.push_back(map_type(p.ty));

            llvm::Type* ret_ty = map_return_type(fn.return_ty);
            auto* fn_ty = llvm::FunctionType::get(ret_ty, param_types, false);
            auto* llvm_fn = llvm::Function::Create(
                fn_ty, llvm::Function::ExternalLinkage, std::string(fn.name.as_str()), &module_);

            functions_[std::string(fn.name.as_str())] = llvm_fn;
        }
    }

    // ─── Function body lowering ─────────────────────────────────────────────

    /// Lowers every function body in the program.
    void define_functions() {
        for (const LirFnDef& fn : program_.fns) {
            lower_function(fn);
        }
    }

    /// Lowers one LIR function into the predeclared LLVM function.
    ///
    /// Strategy:
    /// - Materialize all LLVM basic blocks up front.
    /// - Allocate local storage in entry block.
    /// - Seed parameter locals.
    /// - Lower each LIR basic block in order.
    void lower_function(const LirFnDef& fn) {
        llvm::Function* llvm_fn = functions_[std::string(fn.name.as_str())];
        current_fn_ = llvm_fn;
        local_allocas_.clear();
        basic_blocks_.clear();

        // Create all basic blocks upfront
        for (const LirBasicBlock& bb : fn.blocks) {
            std::string bb_name = "bb" + std::to_string(bb.id);
            auto* llvm_bb = llvm::BasicBlock::Create(context_, bb_name, llvm_fn);
            basic_blocks_.push_back(llvm_bb);
        }

        // Create allocas in the entry block for each local
        builder_.SetInsertPoint(basic_blocks_[0]);
        local_allocas_.resize(fn.locals.size(), nullptr);
        for (const LirLocalDecl& local : fn.locals) {
            llvm::Type* local_ty = map_type(local.ty);
            std::string alloca_name;
            if (local.debug_name.has_value() && local.debug_name->is_valid())
                alloca_name = std::string(local.debug_name->as_str());
            else
                alloca_name = "_" + std::to_string(local.id);

            auto* alloca = builder_.CreateAlloca(local_ty, nullptr, alloca_name);
            local_allocas_[local.id] = alloca;
        }

        // Store function args into param allocas
        unsigned arg_idx = 0;
        for (auto& arg : llvm_fn->args()) {
            if (arg_idx < fn.params.size()) {
                builder_.CreateStore(&arg, local_allocas_[fn.params[arg_idx].local]);
            }
            ++arg_idx;
        }

        // Lower each block
        for (const LirBasicBlock& bb : fn.blocks) {
            lower_block(fn, bb);
        }
    }

    /// Lowers one LIR basic block's statements and final terminator.
    void lower_block(const LirFnDef& fn, const LirBasicBlock& bb) {
        builder_.SetInsertPoint(basic_blocks_[bb.id]);

        for (const LirStmt& stmt : bb.stmts)
            lower_stmt(fn, stmt);

        lower_terminator(fn, bb.terminator);
    }

    // ─── Statement lowering ─────────────────────────────────────────────────

    /// Lowers an assignment statement (`dest = rvalue`).
    ///
    /// Calls returning `void` are represented as `nullptr` from rvalue lowering
    /// and intentionally skip a destination store.
    void lower_stmt(const LirFnDef& fn, const LirStmt& stmt) {
        llvm::Value* rhs = lower_rvalue(fn, stmt.rhs);
        if (rhs == nullptr)
            return; // void call result — nothing to store

        llvm::Value* dest = lower_place_addr(stmt.dest);
        builder_.CreateStore(rhs, dest);
    }

    // ─── Place lowering ─────────────────────────────────────────────────────

    /// Returns an address suitable for storing into `place`.
    llvm::Value* lower_place_addr(const LirPlace& place) {
        llvm::AllocaInst* base = local_allocas_[place.local_id];
        if (place.kind == LirPlace::Kind::Local)
            return base;

        // Field access: GEP into the struct
        llvm::Type* struct_ty = base->getAllocatedType();
        unsigned field_idx = find_struct_field_index(struct_ty, place.field_name);
        return builder_.CreateStructGEP(
            struct_ty, base, field_idx, std::string(place.field_name.as_str()));
    }

    /// Loads the runtime value currently held by `place`.
    llvm::Value* lower_place_load(const LirPlace& place) {
        if (place.kind == LirPlace::Kind::Local) {
            llvm::AllocaInst* alloca = local_allocas_[place.local_id];
            return builder_.CreateLoad(alloca->getAllocatedType(), alloca);
        }

        // Field: load the struct, then extractvalue
        llvm::AllocaInst* base = local_allocas_[place.local_id];
        llvm::Value* struct_val = builder_.CreateLoad(base->getAllocatedType(), base);
        unsigned field_idx = find_struct_field_index(base->getAllocatedType(), place.field_name);
        return builder_.CreateExtractValue(struct_val, field_idx);
    }

    /// Resolves a struct field symbol to its positional field index.
    unsigned find_struct_field_index(llvm::Type* ty, Symbol field_name) {
        auto* struct_ty = llvm::dyn_cast<llvm::StructType>(ty);
        if (!struct_ty || !struct_ty->hasName())
            return 0;

        std::string sname(struct_ty->getName());
        auto it = struct_decls_.find(sname);
        if (it == struct_decls_.end())
            return 0;

        const LirStructDecl* decl = it->second;
        for (std::size_t i = 0; i < decl->fields.size(); ++i) {
            if (decl->fields[i].name == field_name)
                return static_cast<unsigned>(i);
        }
        return 0;
    }

    // ─── Operand lowering ───────────────────────────────────────────────────

    /// Lowers an operand into an LLVM SSA value.
    llvm::Value* lower_operand(const LirOperand& op) {
        if (op.kind == LirOperand::Kind::Copy)
            return lower_place_load(op.place);

        return lower_const(op.constant);
    }

    /// Lowers literal constants.
    ///
    /// String constants are emitted as global string objects and materialized
    /// as pointers.
    llvm::Value* lower_const(const LirConst& c) {
        switch (c.kind) {
        case LirConst::Kind::Num: {
            std::string text(c.symbol.as_str());
            double val = std::strtod(text.c_str(), nullptr);
            return llvm::ConstantFP::get(context_, llvm::APFloat(val));
        }
        case LirConst::Kind::Bool:
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context_), c.bool_value ? 1 : 0);
        case LirConst::Kind::Str: {
            // String literals: create a global constant and return a pointer
            std::string text(c.symbol.as_str());
            // Strip surrounding quotes if present
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
                text = text.substr(1, text.size() - 2);
            return builder_.CreateGlobalString(text);
        }
        case LirConst::Kind::Unit:
            // Unit is an empty struct {}
            return llvm::ConstantStruct::get(llvm::StructType::get(context_), {});
        }
        return llvm::UndefValue::get(llvm::Type::getVoidTy(context_));
    }

    // ─── Rvalue lowering ────────────────────────────────────────────────────

    /// Dispatches lowering for all supported rvalue node variants.
    llvm::Value* lower_rvalue(const LirFnDef& fn, const LirRvalue& rv) {
        return std::visit(
            [&](const auto& node) -> llvm::Value* { return lower_rvalue_node(fn, node); }, rv.node);
    }

    /// `Use` is just operand materialization.
    llvm::Value* lower_rvalue_node(const LirFnDef& /*fn*/, const LirUse& use) {
        return lower_operand(use.operand);
    }

    /// Lowers binary arithmetic/comparison/logical operators.
    llvm::Value* lower_rvalue_node(const LirFnDef& /*fn*/, const LirBinaryOp& binop) {
        llvm::Value* lhs = lower_operand(binop.lhs);
        llvm::Value* rhs = lower_operand(binop.rhs);

        using BO = cstc::ast::BinaryOp;
        switch (binop.op) {
        case BO::Add: return builder_.CreateFAdd(lhs, rhs, "add");
        case BO::Sub: return builder_.CreateFSub(lhs, rhs, "sub");
        case BO::Mul: return builder_.CreateFMul(lhs, rhs, "mul");
        case BO::Div: return builder_.CreateFDiv(lhs, rhs, "div");
        case BO::Mod: return builder_.CreateFRem(lhs, rhs, "mod");
        case BO::Eq: return builder_.CreateFCmpOEQ(lhs, rhs, "eq");
        case BO::Ne: return builder_.CreateFCmpONE(lhs, rhs, "ne");
        case BO::Lt: return builder_.CreateFCmpOLT(lhs, rhs, "lt");
        case BO::Le: return builder_.CreateFCmpOLE(lhs, rhs, "le");
        case BO::Gt: return builder_.CreateFCmpOGT(lhs, rhs, "gt");
        case BO::Ge: return builder_.CreateFCmpOGE(lhs, rhs, "ge");
        case BO::And: return builder_.CreateAnd(lhs, rhs, "and");
        case BO::Or: return builder_.CreateOr(lhs, rhs, "or");
        }
        return nullptr;
    }

    /// Lowers unary numeric and boolean operators.
    llvm::Value* lower_rvalue_node(const LirFnDef& /*fn*/, const LirUnaryOp& unop) {
        llvm::Value* operand = lower_operand(unop.operand);

        using UO = cstc::ast::UnaryOp;
        switch (unop.op) {
        case UO::Negate: return builder_.CreateFNeg(operand, "neg");
        case UO::Not:
            return builder_.CreateXor(operand, llvm::ConstantInt::getTrue(context_), "not");
        }
        return nullptr;
    }

    /// Lowers a direct function call.
    ///
    /// Returns `nullptr` when the callee returns `void`, allowing statement
    /// lowering to skip destination storage.
    llvm::Value* lower_rvalue_node(const LirFnDef& /*fn*/, const LirCall& call) {
        llvm::Function* callee = functions_[std::string(call.fn_name.as_str())];
        std::vector<llvm::Value*> args;
        args.reserve(call.args.size());
        for (const LirOperand& arg : call.args)
            args.push_back(lower_operand(arg));

        llvm::Value* result = builder_.CreateCall(callee, args);

        // If the callee returns void, return nullptr to signal "no value to store"
        if (callee->getReturnType()->isVoidTy())
            return nullptr;

        return result;
    }

    /// Lowers struct literal construction.
    ///
    /// Starts from `undef` and incrementally inserts each named field.
    llvm::Value* lower_rvalue_node(const LirFnDef& /*fn*/, const LirStructInit& init) {
        std::string type_name(init.type_name.as_str());
        auto it = struct_types_.find(type_name);

        if (it == struct_types_.end() || init.fields.empty()) {
            // ZST or unknown — return empty struct
            return llvm::ConstantStruct::get(llvm::StructType::get(context_), {});
        }

        llvm::StructType* st = it->second;
        const LirStructDecl* decl = struct_decls_[type_name];

        // Start with undef and insertvalue for each field
        llvm::Value* agg = llvm::UndefValue::get(st);
        for (const LirStructInitField& field : init.fields) {
            llvm::Value* val = lower_operand(field.value);
            unsigned idx = 0;
            for (std::size_t i = 0; i < decl->fields.size(); ++i) {
                if (decl->fields[i].name == field.name) {
                    idx = static_cast<unsigned>(i);
                    break;
                }
            }
            agg = builder_.CreateInsertValue(agg, val, idx);
        }
        return agg;
    }

    /// Lowers enum variant references to discriminant-only aggregate values.
    llvm::Value* lower_rvalue_node(const LirFnDef& /*fn*/, const LirEnumVariantRef& ref) {
        std::string enum_name(ref.enum_name.as_str());
        std::string variant_name(ref.variant_name.as_str());

        auto eit = enum_types_.find(enum_name);
        if (eit == enum_types_.end())
            return llvm::UndefValue::get(llvm::StructType::get(context_));

        llvm::StructType* st = eit->second;
        uint32_t disc = enum_discriminants_[enum_name][variant_name];

        llvm::Value* agg = llvm::UndefValue::get(st);
        llvm::Value* disc_val = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), disc);
        return builder_.CreateInsertValue(agg, disc_val, 0);
    }

    // ─── Terminator lowering ────────────────────────────────────────────────

    /// Lowers a block terminator into control-flow instructions.
    void lower_terminator(const LirFnDef& fn, const LirTerminator& term) {
        std::visit([&](const auto& node) { lower_terminator_node(fn, node); }, term.node);
    }

    /// Lowers return terminators.
    void lower_terminator_node(const LirFnDef& fn, const LirReturn& ret) {
        if (!ret.value.has_value() || is_void_return(fn.return_ty)) {
            builder_.CreateRetVoid();
        } else {
            llvm::Value* val = lower_operand(*ret.value);
            builder_.CreateRet(val);
        }
    }

    /// Lowers unconditional branches.
    void lower_terminator_node(const LirFnDef& /*fn*/, const LirJump& jump) {
        builder_.CreateBr(basic_blocks_[jump.target]);
    }

    /// Lowers boolean conditional branches.
    void lower_terminator_node(const LirFnDef& /*fn*/, const LirSwitchBool& sw) {
        llvm::Value* cond = lower_operand(sw.condition);
        builder_.CreateCondBr(cond, basic_blocks_[sw.true_target], basic_blocks_[sw.false_target]);
    }

    /// Lowers explicit unreachable terminators.
    void lower_terminator_node(const LirFnDef& /*fn*/, const LirUnreachable& /*u*/) {
        builder_.CreateUnreachable();
    }

    // ─── Optimization passes ────────────────────────────────────────────────

    /// Runs lightweight cleanup passes after naive alloca-based lowering.
    ///
    /// Currently applies promotion (mem2reg) to recover SSA-like IR.
    void run_passes() {
        llvm::LoopAnalysisManager lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager cgam;
        llvm::ModuleAnalysisManager mam;

        llvm::PassBuilder pb;
        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);

        // Run mem2reg on each function
        llvm::FunctionPassManager fpm;
        fpm.addPass(llvm::PromotePass());

        for (auto& func : module_) {
            if (!func.isDeclaration())
                fpm.run(func, fam);
        }
    }

    // ─── Module printing ────────────────────────────────────────────────────

    /// Serializes the module to textual LLVM IR.
    std::string print_module() {
        std::string ir;
        llvm::raw_string_ostream stream(ir);
        module_.print(stream, nullptr);
        stream.flush();
        return ir;
    }

    // ─── Data members ───────────────────────────────────────────────────────

    const LirProgram& program_;
    llvm::LLVMContext context_;
    llvm::Module module_;
    llvm::IRBuilder<> builder_;

    // Type maps
    std::unordered_map<std::string, llvm::StructType*> struct_types_;
    std::unordered_map<std::string, llvm::StructType*> enum_types_;
    std::unordered_map<std::string, const LirStructDecl*> struct_decls_;
    std::unordered_map<std::string, std::unordered_map<std::string, uint32_t>> enum_discriminants_;

    // Function map
    std::unordered_map<std::string, llvm::Function*> functions_;

    // Per-function state
    llvm::Function* current_fn_ = nullptr;
    std::vector<llvm::AllocaInst*> local_allocas_;
    std::vector<llvm::BasicBlock*> basic_blocks_;
};

// ─── Public API ─────────────────────────────────────────────────────────────

/// Emits LLVM IR using the default module name.
std::string emit_llvm_ir(const lir::LirProgram& program) {
    return emit_llvm_ir(program, "cicest_module");
}

/// Emits LLVM IR using a caller-provided module name.
std::string emit_llvm_ir(const lir::LirProgram& program, std::string_view module_name) {
    CodegenContext ctx(program, module_name);
    return ctx.emit();
}

} // namespace cstc::codegen
