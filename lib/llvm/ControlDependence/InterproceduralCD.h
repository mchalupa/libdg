#ifndef DG_LLVM_INTERPROC_CD_H_
#define DG_LLVM_INTERPROC_CD_H_

#include <llvm/IR/Module.h>
#include <llvm/IR/Instruction.h>

#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisImpl.h"

#include <set>
#include <map>
#include <unordered_map>


namespace llvm {
class Function;
}

namespace dg {

class LLVMPointerAnalysis;

namespace llvmdg {

class CallGraph;


class LLVMInterprocCD : public LLVMControlDependenceAnalysisImpl {
    LLVMPointerAnalysis *PTA{nullptr};
    CallGraph *_cg{nullptr};

    struct FuncInfo {
        // points that may abort the program
        // (or cause infinite looping). That is,
        // points due to which the function may not return
        // to its caller
        std::set<const llvm::Value *> noret;
        bool hasCD = false;
    };

    // XXX: store it per function?
    // set of instructions from the same block on which is the given instruction
    // dependent
    std::unordered_map<const llvm::Instruction *, std::set<llvm::Value *>> _instrCD;
    // a set of block on which is the given instruction dependent
    std::unordered_map<const llvm::BasicBlock *, std::set<llvm::Value *>> _blockCD;
    // the set of instructions and blocks that depend on the given instruction
    std::unordered_map<const llvm::Instruction *, std::set<llvm::Value *>> _revInstrCD;

    std::unordered_map<const llvm::Function *, FuncInfo> _funcInfos;

    FuncInfo *getFuncInfo(const llvm::Function *F) {
        auto it = _funcInfos.find(F);
        return it == _funcInfos.end() ? nullptr : &it->second;
    }

    const FuncInfo *getFuncInfo(const llvm::Function *F) const {
        auto it = _funcInfos.find(F);
        return it == _funcInfos.end() ? nullptr : &it->second;
    }

    FuncInfo *getOrComputeFuncInfo(const llvm::Function *F) {
        auto it = _funcInfos.find(F);
        if (it != _funcInfos.end()) {
            return &it->second;
        }

        computeFuncInfo(F);
        auto *fi = getFuncInfo(F);
        assert(fi && "BUG: computeFuncInfo does not work");
        return fi;
    }

    FuncInfo *getOrComputeFullFuncInfo(const llvm::Function *fun) {
        auto *fi = getOrComputeFuncInfo(fun);
        assert(fi && "BUG in getOrComputeFuncInfo");

        if (!fi->hasCD) {
            computeCD(fun);
            assert(fi->hasCD && "BUG in computeCD");
        }

        return fi;
    }

    bool hasFuncInfo(const llvm::Function *fun) const {
       return _funcInfos.find(fun) != _funcInfos.end();
    }

    // recursively compute function info, 'stack' is there to detect recursive calls
    void computeFuncInfo(const llvm::Function *fun,
                         std::set<const llvm::Function *> stack = {});
    void computeCD(const llvm::Function *fun);

    std::vector<const llvm::Function *> getCalledFunctions(const llvm::Value *v);

public:
    using ValVec = LLVMControlDependenceAnalysisImpl::ValVec;

    LLVMInterprocCD(const llvm::Module *module,
                    const LLVMControlDependenceAnalysisOptions& opts = {},
                    LLVMPointerAnalysis *pta = nullptr,
                    CallGraph *cg = nullptr)
        : LLVMControlDependenceAnalysisImpl(module, opts), PTA(pta), _cg(cg) {}

    ValVec getNoReturns(const llvm::Function *F) const override {
        ValVec ret;
        if (auto *fi = getFuncInfo(F)) {
            for (auto *val : fi->noret)
                ret.push_back(const_cast<llvm::Value *>(val));
        }
        return ret;
    }

    /// Getters of dependencies for a value
    ValVec getDependencies(const llvm::Instruction *I) override {
        auto *fun = I->getParent()->getParent();
        auto *fi = getOrComputeFullFuncInfo(fun);
        assert(fi && "BUG in getOrComputeFuncInfo");
        assert(fi->hasCD && "BUG in computeCD");

        ValVec ret;
        // dependencies in the same block
        auto instrIt = _instrCD.find(I);
        if (instrIt != _instrCD.end()) {
            ret.insert(ret.end(), instrIt->second.begin(), instrIt->second.end());
        }

        // dependencies in the predecessor blocks
        auto blkIt = _blockCD.find(I->getParent());
        if (blkIt != _blockCD.end()) {
            ret.insert(ret.end(), blkIt->second.begin(), blkIt->second.end());
        }

        return ret;
    }

    ValVec getDependent(const llvm::Instruction *I) override {
        auto *fun = I->getParent()->getParent();
        auto *fi = getOrComputeFullFuncInfo(fun);
        assert(fi && "BUG in getOrComputeFuncInfo");
        assert(fi->hasCD && "BUG in computeCD");

        ValVec ret;
        auto instrIt = _revInstrCD.find(I);
        if (instrIt != _revInstrCD.end()) {
            for (auto *val : instrIt->second) {
                if (auto *B = llvm::dyn_cast<llvm::BasicBlock>(val)) {
                    for (auto& binst : *B) {
                        ret.push_back(&binst);
                    }
                } else {
                    ret.push_back(val);
                }

            }
            ret.insert(ret.end(), instrIt->second.begin(), instrIt->second.end());
        }

        return ret;
    }

    // there are no dependencies between basic blocks in this analysis
    ValVec getDependencies(const llvm::BasicBlock *) override { return {}; }
    ValVec getDependent(const llvm::BasicBlock *) override { return {}; }

    void compute(const llvm::Function *F = nullptr) override {
        if (F && !F->isDeclaration()) {
            if (!hasFuncInfo(F)) {
                computeFuncInfo(F);
            }
        } else {
            for (auto& f : *getModule()) {
                if (!f.isDeclaration() && !hasFuncInfo(&f)) {
                    computeFuncInfo(&f);
                }
            }
        }

    }
};

} // namespace llvmdg
} // namespace dg

#endif
