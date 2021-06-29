#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <algorithm>
#include <llvm/Analysis/IVUsers.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

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

    unsigned LoopRate = 0;
    if (MDNode *LoopID = L->getLoopID())
      LoopRate = GetPerforationMetadata(LoopID, "llvm.loop.perforate") ;//LoopPerfRate;

    if (LoopRate == 0)
      return false;

    if (!L->isLoopSimplifyForm()) {
      return false;
    }

    // Find the canonical induction variable for this loop
    PHINode *PHI = L->getCanonicalInductionVariable();

    if (PHI== nullptr) {
      return false;
    }

    // Find where the induction variable is modified by finding a user that
    // is also an incoming value to the phi
    Value *ValueToChange = nullptr;

    ScalarEvolution *Se = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    Optional< Loop::LoopBounds > Bounds = L->getBounds(*Se);

    Value &IVFinalVal = Bounds->getFinalIVValue();
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
    int i = 0;
    for (auto &Op : Increment->operands()) {
      if (Op == PHI) {
        i++;
        continue;
      }

      Type *ConstType = Op->getType();

      if (ConstType->getTypeID() != Type::IntegerTyID) {
        return false;
      }

      ConstantInt* Co = dyn_cast<llvm::ConstantInt>(Op);

      if (Co->getBitWidth() > 32)
        return false;

      int AddValue = Co->getSExtValue()*LoopRate;

      if (AddValue == 0)
        return false;

      ICmpInst *LatchCmpInst = getLatchCmpInst(*L);

      if (LatchCmpInst == nullptr)
        continue;

      GlobalValue *b = L->getHeader()->getModule()->getNamedGlobal("PERF_TOGGLE");

      LoadInst *loadInst;
      if (b)
        loadInst = new LoadInst(b->getValueType(), b, "", L->getLoopPreheader()->getTerminator());
      else
        return false;

      PHINode::Create(L->getHeader()->getType(), 2, "", loadInst->getNextNode());

      auto *NewIncPerf = BinaryOperator::Create(Instruction::Mul, Op,
                                                loadInst, "", Increment);

      //Op = NewIncPerf;
      PHINode::Create(L->getHeader()->getType(), 2, "", NewIncPerf->getNextNode());

      Increment->setOperand(i, NewIncPerf);


      Instruction *InsertBefore = L->getLoopPreheader()->getTerminator();

      auto *Rem = BinaryOperator::Create(Instruction::SRem, &IVFinalVal,
                                         Op, "", InsertBefore);


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
    //getLoopAnalysisUsage(AU);

  }

  ICmpInst *getLatchCmpInst(const Loop &L) const {

    if (BasicBlock *Latch = L.getLoopLatch())
      if (BranchInst *BI = dyn_cast_or_null<BranchInst>(Latch->getTerminator()))
        if (BI->isConditional())
          return dyn_cast<ICmpInst>(BI->getCondition());

    return nullptr;

  }
};
} // namespace

Pass *llvm::createLoopPerforationLegacyPass() {
  return new LoopPerforationLegacyPass();
}

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

unsigned llvm::GetPerforationMetadata(MDNode *LoopID, StringRef Name) {
  // First operand should refer to the loop id itself.
  assert(LoopID->getNumOperands() > 0 && "requires at least one operand");
  assert(LoopID->getOperand(0) == LoopID && "invalid loop id");

  for (unsigned i = 1, e = LoopID->getNumOperands(); i < e; ++i) {
    MDNode *MD = dyn_cast<MDNode>(LoopID->getOperand(i));
    if (!MD)
      continue;

    MDString *S = dyn_cast<MDString>(MD->getOperand(0));
    if (!S)
      continue;

    if (Name.equals(S->getString())) {
      return mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue();
    }
  }
  return 0;
}