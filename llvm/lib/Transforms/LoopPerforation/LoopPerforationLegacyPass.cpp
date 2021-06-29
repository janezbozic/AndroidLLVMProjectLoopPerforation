
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <llvm/Analysis/IVUsers.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <iostream>
#include <fstream>
#include <algorithm>

using namespace llvm;
using namespace std;

namespace {

class LoopPerforationLegacyPass : public LoopPass {
public:
  static char ID;
  LoopPerforationLegacyPass() : LoopPass(ID) {
    initializeLoopPerforationLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnLoop(Loop *L, LPPassManager &) override {

    if (L->getHeader()->getParent()->getName().find("PERF") == std::string::npos)
      return false;

    if (!L->isLoopSimplifyForm())
      return false;


    // Find the canonical induction variable for this loop
    PHINode *PHI = L->getCanonicalInductionVariable();

    if (PHI== nullptr)
      return false;

    // Find where the induction variable is modified by finding a user that
    // is also an incoming value to the phi
    Value *ValueToChange = nullptr;

    ScalarEvolution *se = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    Optional< Loop::LoopBounds > bounds = L->getBounds(*se);

    Value &IVFinalVal = bounds->getFinalIVValue();
    //Value &IVInitialVal = bounds->getInitialIVValue();

    for (auto User : PHI->users()) {
      for (auto &Incoming : PHI->incoming_values()) {
        if (Incoming == User) {
          ValueToChange = Incoming;
          break; // TODO: what if there are multiple?
        }
      }
    }

    if (ValueToChange == nullptr || !isa<BinaryOperator>(ValueToChange))
      return false;

    BinaryOperator *Increment = dyn_cast<BinaryOperator>(ValueToChange);
    for (auto &Op : Increment->operands()) {
      if (Op == PHI)
        continue;

      int LoopRate = getRateForPerf() ;//LoopPerfRate;
      Type *ConstType = Op->getType();
      Constant *NewInc = ConstantInt::get(ConstType, LoopRate /*value*/, true /*issigned*/);

      ICmpInst *LatchCmpInst = getLatchCmpInst(*L);

      if (LatchCmpInst == nullptr)
        continue;

      Op = NewInc;

      Instruction *InsertBefore = L->getLoopPreheader()->getTerminator();

      auto *Rem = BinaryOperator::Create(Instruction::SRem, &IVFinalVal,
                                         NewInc, "", InsertBefore);

      auto *NewUpper = BinaryOperator::Create(Instruction::Sub, &IVFinalVal,
                                              Rem, "", InsertBefore);

      LatchCmpInst->setOperand(1, NewUpper);

      return true;
    }

    // should never reach here
    return false;
  };

  void getAnalysisUsage(AnalysisUsage &AU) const override {

    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<IVUsersWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequiredID(LoopSimplifyID);

  }

  ICmpInst *getLatchCmpInst(const Loop &L) const {

    if (BasicBlock *Latch = L.getLoopLatch())
      if (BranchInst *BI = dyn_cast_or_null<BranchInst>(Latch->getTerminator()))
        if (BI->isConditional())
          return dyn_cast<ICmpInst>(BI->getCondition());

    return nullptr;

  }

  int getRateForPerf() {

    string home = getenv("HOME");
    std::ifstream cFile ( home + "/perforation.config");
    if (cFile.is_open())
    {
      std::string line;
      while(getline(cFile, line)){
        if (line[0] == '\n' || line[0] == '\0') continue;
        if(line[0] == '#' || line.empty())
          continue;
        auto delimiterPos = line.find("=");
        auto name = line.substr(0, delimiterPos);
        auto value = line.substr(delimiterPos + 1);
        return std::stoi(value);
      }

    }
    else {
      std::cerr << "Couldn't open config file for reading.\n Perforation Rate is 1.";
    }
    return 1;
  }
};
}

/*Pass *llvm::createLoopPerforationLegacyPass() {
  return new LoopPerforationLegacyPass();
}*/

char LoopPerforationLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(LoopPerforationLegacyPass, "loop-perforation",
                      "Perforate loops", false, false)
  INITIALIZE_PASS_DEPENDENCY(LoopPass)
  INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(IVUsersWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_END(LoopPerforationLegacyPass, "loop-perforation",
                    "Perforate loops", false, false)

static void addLoopPerforationPass(const PassManagerBuilder &Builder,
                                   legacy::PassManagerBase &PM) {
  PM.add(new LoopPerforationLegacyPass());
}
static RegisterStandardPasses
    RegisterLoopPerforation(PassManagerBuilder::EP_LateLoopOptimizations,
                   addLoopPerforationPass);
