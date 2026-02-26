#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

int main() {
    llvm::LLVMContext context;
    auto module = std::make_unique<llvm::Module>("module", context);
    llvm::IRBuilder<> builder(context);

    auto* fnTy = llvm::FunctionType::get(builder.getInt32Ty(), false);
    auto* fn = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage, "main", module.get());

    auto* entry = llvm::BasicBlock::Create(context, "entry", fn);
    builder.SetInsertPoint(entry);
    builder.CreateRet(builder.getInt32(42));

    if (llvm::verifyFunction(*fn, &llvm::errs())) {
        llvm::errs() << "Function verification failed\n";
        return 1;
    }

    module->print(llvm::outs(), nullptr);
    return 0;
}
