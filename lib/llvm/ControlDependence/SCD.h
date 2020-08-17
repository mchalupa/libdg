#ifndef DG_LLVM_SCD_H_
#define DG_LLVM_SCD_H_

#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Module.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "dg/util/debug.h"

#include <set>
#include <map>
#include <unordered_map>


namespace llvm {
class Function;
}

namespace dg {

class LLVMPointerAnalysis;

namespace llvmdg {

// Standard control dependencies based on the computation
// of post-dominance frontiers.
// This class uses purely LLVM, no internal representation
// like the other classes (we use the post-dominance computation from LLVM).
class SCD : public LLVMControlDependenceAnalysisImpl {

    void computePostDominators(llvm::Function& F);

    std::unordered_map<const llvm::BasicBlock *, std::set<llvm::BasicBlock *>> dependentBlocks;
    std::unordered_map<const llvm::BasicBlock *, std::set<llvm::BasicBlock *>> dependencies;
    std::set<const llvm::Function *> _computed;

    void computeOnDemand(const llvm::Function *F) {
        if (_computed.insert(F).second) {
            computePostDominators(*const_cast<llvm::Function*>(F));
        }
    }

public:
    using ValVec = LLVMControlDependenceAnalysis::ValVec;

    SCD(const llvm::Module *module,
        const LLVMControlDependenceAnalysisOptions& opts = {})
        : LLVMControlDependenceAnalysisImpl(module, opts) {}

    /// Getters of dependencies for a value
    ValVec getDependencies(const llvm::Instruction *) override { return {}; }
    ValVec getDependent(const llvm::Instruction *) override { return {}; }

    /// Getters of dependencies for a basic block
    ValVec getDependencies(const llvm::BasicBlock *b) override {
        computeOnDemand(b->getParent());

        auto &S = dependencies[b];
        return ValVec{S.begin(), S.end()};
    }

    ValVec getDependent(const llvm::BasicBlock *b) override {
        computeOnDemand(b->getParent());

        auto &S = dependentBlocks[b];
        return ValVec{S.begin(), S.end()};
    }

    void compute(const llvm::Function *F = nullptr) override {
        DBG(cda, "Triggering computation of all dependencies");
        if (F && !F->isDeclaration()) {
            computeOnDemand(F);
        } else {
            for (auto& f : *getModule()) {
                if (f.isDeclaration()) {
                    continue;
                }
                computeOnDemand(&f);
            }
        }
    }
};

} // namespace llvmdg
} // namespace dg

#endif
