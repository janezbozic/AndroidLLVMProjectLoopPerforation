#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Function.h"
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

    int LoopPerfEnabled = 0;
    //Check if perforation is enabled for the loop
    if (MDNode *LoopID = L->getLoopID())
      LoopPerfEnabled = GetPerforationMetadata(LoopID, "llvm.loop.perforate.enable");

    if (LoopPerfEnabled == 0)
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
        return false;

      //Retrieving integer type dependent on context
      Type* int32Ty = Type::getInt32Ty(L->getHeader()->getContext());

      //Adding perforation function declaration to the LLVM IR
      FunctionCallee F = L->getHeader()->getModule()->getOrInsertFunction("CLANG_LOOP_PERFORATION_FUNCTION", Op->getType(), int32Ty, int32Ty);

      //Vector for function call arguments
      std::vector<Value *> CallArgs;

      //Creating constant integer value from loop's address of LoopID
      //  and hashing it, so it will be used as unique loop id for passing
      //  perforation factor
      std::hash<int> hasher;
      int hashed = hasher((intptr_t)&(L->getLoopID()->getOperand(0)));
      Constant *NewInc = ConstantInt::get(int32Ty, hashed /*value*/, true /*issigned*/);

      //Adding Integer value to arguments vector for the function call
      CallArgs.push_back(NewInc);

      // From bounds, we can get upper value of the loop, we have to change it, if the
      //   loop's current bound is not multiple of new perforated increment value
      if (Optional< Loop::LoopBounds > Bounds = L->getBounds(*Se)) {
        Value &IVFinalVal = Bounds->getFinalIVValue();
        if (isa<Constant>(&IVFinalVal)) {
          Constant *CI = dyn_cast<ConstantInt>(&IVFinalVal);
          CallArgs.push_back(CI);
        }
        else if (isa<LoadInst>(&IVFinalVal)) {
          LoadInst *loadedGV = new LoadInst(int32Ty, getLoadStorePointerOperand(&IVFinalVal), "LoadUpperBoundFromGV",
                                            L->getLoopPreheader()->getTerminator());
          CallArgs.push_back(loadedGV);
        }
        else{
          Constant *upperValue = ConstantInt::get(int32Ty, 2147483647  /*value*/, true /*issigned*/);
          CallArgs.push_back(upperValue);
        }
      }
      else{
        Constant *upperValue = ConstantInt::get(int32Ty, 2147483647  /*value*/, true /*issigned*/);
        CallArgs.push_back(upperValue);
      }

      CallInst *NewCall;
      if (F){
        //Inserting function call in the code
        NewCall = CallInst::Create(F, CallArgs, "CLANG_LOOP_PERFORATION_FUNCTION_CALL", L->getLoopPreheader()->getTerminator()  );
      }
      else
        return false;


      //Creating a multiplication statement for function's return value of perforation rate
      //  and current increment
      auto *NewIncPerf = BinaryOperator::Create(Instruction::Mul, Op,
                                                NewCall, "", Increment);

      //Setting operand in increment statement from old to the multiplied one
      Increment->setOperand(i, NewIncPerf);   //Mogoce phi namesto newincperf


      //Changing loop compare instruction to less than (in simple loops its equals by default)
      LatchCmpInst->setPredicate(ICmpInst::ICMP_SLT);

      //Returning true, because the LLVM IR has been changed
      return true;
    }

    // should never reach here
    return false;
  };

  //Declaring required passes (analyses) for execution of this pass.
  //  We need data from these passes, so they have to be executed before this pass
  void getAnalysisUsage(AnalysisUsage &AU) const override {

    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<IVUsersWrapperPass>();
    AU.addRequiredID(LoopSimplifyID);

  }

  //Retrieving compare instruction of the loop, so we can modify upper value
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
//Declaring required passes for execution of this pass (need to be executed before this pass).
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(IVUsersWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_END(LoopPerforationLegacyPass, "loop-perforation",
                    "Perforate loops", false, false)

//Method for retrieving loop's metadata set with pragma instruction
//   #pragma clang loop perforate (enable)
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
      //Return true for perforation of the loop.
      return true;
    }
  }
  return false;
}