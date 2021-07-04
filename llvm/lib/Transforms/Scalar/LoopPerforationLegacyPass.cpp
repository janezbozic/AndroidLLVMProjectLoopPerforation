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
    //Calling initializing method for the loop
    initializeLoopPerforationLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  //runOnLoop method is triggered on every loop independently
  //If we return false, then the pass did not change anything, if we modify LLVM IR,
  //  then we return true.
  bool runOnLoop(Loop *L, LPPassManager &) override {

    bool LoopPerfEnabled = false;
    //Check if perforation is enabled for the loop
    if (MDNode *LoopID = L->getLoopID())
      LoopPerfEnabled = GetPerforationMetadata(LoopID, "llvm.loop.perforate.enable");

    if (!LoopPerfEnabled)
      return false;

    //We check if the loop is simple, if it is not, we can't perform perforation
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
    // We get bounds of the loop's bounds
    Optional< Loop::LoopBounds > Bounds = L->getBounds(*Se);

    //From bounds, we can get upper value of the loop, we have to change it, if the
    //  loop's current bound is not multiple of new perforated increment value
    Value &IVFinalVal = Bounds->getFinalIVValue();

    //In simple loops we get increment by matcing same node in users and
    //  incoming values of induction variable.
    for (auto User : PHI->users()) {
      for (auto &Incoming : PHI->incoming_values()) {
        if (Incoming == User) {
          ValueToChange = Incoming;
          break;
        }
      }
    }

    //We check that increment is not null and is a binary expression
    if (ValueToChange == nullptr || !isa<BinaryOperator>(ValueToChange))
      return false;

    //We cast to BinaryOperator and extract the incrementing value,
    //  in i = i+1, we extract 1 (example).
    BinaryOperator *Increment = dyn_cast<BinaryOperator>(ValueToChange);
    int i = 0;
    for (auto &Op : Increment->operands()) {
      if (Op == PHI) {
        i++;
        continue;
      }

      // Getting compare instruction (if) for end of the loop
      ICmpInst *LatchCmpInst = getLatchCmpInst(*L);

      if (LatchCmpInst == nullptr)
        continue;

      //Getting global variable's address for perforation factor
      GlobalValue *b = L->getHeader()->getModule()->getNamedGlobal("CLANG_PERFORATION_RATE");

      LoadInst *loadInst;
      if (b)
        //We have to load global variable from attained address
        loadInst = new LoadInst(b->getValueType(), b, "", L->getLoopPreheader()->getTerminator());
      else
        return false; //Return false if global variable does not exist in the program

      //Possibly remove
      PHINode::Create(L->getHeader()->getType(), 2, "", loadInst->getNextNode());

      //Creating a multiplicaiton statement for global value of perforation rate
      //  and current increment
      auto *NewIncPerf = BinaryOperator::Create(Instruction::Mul, Op,
                                                loadInst, "", Increment);

      //Possibly remove
      PHINode::Create(L->getHeader()->getType(), 2, "", NewIncPerf->getNextNode());

      //Setting operand in increment statement from old to the multiplied one
      Increment->setOperand(i, NewIncPerf);


      //Retrieving instruction for termination of the loop
      Instruction *InsertBefore = L->getLoopPreheader()->getTerminator();

      //Inserting remain instruction for new loops upper value
      auto *Rem = BinaryOperator::Create(Instruction::SRem, &IVFinalVal,
                                         Op, "", InsertBefore);


      //Subtracting remaining value from loop's upper value
      auto *NewUpper = BinaryOperator::Create(Instruction::Sub, &IVFinalVal,
                                              Rem, "", InsertBefore);

      //Setting loop's new upper value, which is multiple of current increment value
      LatchCmpInst->setOperand(1, NewUpper);

      //Returning true, because the LLVM IR has been changed
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
};
} // namespace

//Method called from ManagerBuilder class for creating and starting Pass
Pass *llvm::createLoopPerforationLegacyPass() {
  return new LoopPerforationLegacyPass();
}

//Setting pass' initialization values, this is executed when
//  the initializeLoopPerforationLegacyPassPass method is called
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

//Method for retrieving loop's metadata set with pragma instruction
//   #pragma clang loop perforate
bool llvm::GetPerforationMetadata(MDNode *LoopID, StringRef Name) {
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
      return true;
    }
  }
  return false;
}