//===-- IntrinsicCleaner.cpp ----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#include "klee/Config/Version.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace klee {

char IntrinsicCleanerPass::ID;

bool IntrinsicCleanerPass::runOnModule(Module &M) {
  bool dirty = false;
  for (Module::iterator f = M.begin(), fe = M.end(); f != fe; ++f)
    for (Function::iterator b = f->begin(), be = f->end(); b != be; ++b)
      dirty |= runOnBasicBlock(*b, M);

  if (Function *Declare = M.getFunction("llvm.trap")) {
    Declare->eraseFromParent();
    dirty = true;
  }
  return dirty;
}

bool IntrinsicCleanerPass::runOnBasicBlock(BasicBlock &b, Module &M) {
  bool dirty = false;
  bool block_split=false;
  LLVMContext &ctx = M.getContext();
  
  unsigned WordSize = DataLayout.getPointerSizeInBits() / 8;
  for (BasicBlock::iterator i = b.begin(), ie = b.end();
       (i != ie) && (block_split == false);) {
    IntrinsicInst *ii = dyn_cast<IntrinsicInst>(&*i);
    // increment now since LowerIntrinsic deletion makes iterator invalid.
    ++i;  
    if(ii) {
      switch (ii->getIntrinsicID()) {
      case Intrinsic::vastart:
      case Intrinsic::vaend:
        break;
        
        // Lower vacopy so that object resolution etc is handled by
        // normal instructions.
        //
        // FIXME: This is much more target dependent than just the word size,
        // however this works for x86-32 and x86-64.
      case Intrinsic::vacopy: { // (dst, src) -> *((i8**) dst) = *((i8**) src)
        Value *dst = ii->getArgOperand(0);
        Value *src = ii->getArgOperand(1);

        if (WordSize == 4) {
          Type *i8pp = PointerType::getUnqual(PointerType::getUnqual(Type::getInt8Ty(ctx)));
          Value *castedDst = CastInst::CreatePointerCast(dst, i8pp, "vacopy.cast.dst", ii);
          Value *castedSrc = CastInst::CreatePointerCast(src, i8pp, "vacopy.cast.src", ii);
          Value *load = new LoadInst(castedSrc, "vacopy.read", ii);
          new StoreInst(load, castedDst, false, ii);
        } else {
          assert(WordSize == 8 && "Invalid word size!");
          Type *i64p = PointerType::getUnqual(Type::getInt64Ty(ctx));
          Value *pDst = CastInst::CreatePointerCast(dst, i64p, "vacopy.cast.dst", ii);
          Value *pSrc = CastInst::CreatePointerCast(src, i64p, "vacopy.cast.src", ii);
          Value *val = new LoadInst(pSrc, std::string(), ii); new StoreInst(val, pDst, ii);
          Value *off = ConstantInt::get(Type::getInt64Ty(ctx), 1);
          pDst = GetElementPtrInst::Create(pDst, off, std::string(), ii);
          pSrc = GetElementPtrInst::Create(pSrc, off, std::string(), ii);
          val = new LoadInst(pSrc, std::string(), ii); new StoreInst(val, pDst, ii);
          pDst = GetElementPtrInst::Create(pDst, off, std::string(), ii);
          pSrc = GetElementPtrInst::Create(pSrc, off, std::string(), ii);
          val = new LoadInst(pSrc, std::string(), ii); new StoreInst(val, pDst, ii);
        }
        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::sadd_with_overflow:
      case Intrinsic::ssub_with_overflow:
      case Intrinsic::smul_with_overflow:
      case Intrinsic::uadd_with_overflow:
      case Intrinsic::usub_with_overflow:
      case Intrinsic::umul_with_overflow: {
        IRBuilder<> builder(ii->getParent(), ii);

        Value *op1 = ii->getArgOperand(0);
        Value *op2 = ii->getArgOperand(1);
        
        Value *result = 0;
        Value *result_ext = 0;
        Value *overflow = 0;

        unsigned int bw = op1->getType()->getPrimitiveSizeInBits();
        unsigned int bw2 = op1->getType()->getPrimitiveSizeInBits()*2;

        if ((ii->getIntrinsicID() == Intrinsic::uadd_with_overflow) ||
            (ii->getIntrinsicID() == Intrinsic::usub_with_overflow) ||
            (ii->getIntrinsicID() == Intrinsic::umul_with_overflow)) {

          Value *op1ext =
            builder.CreateZExt(op1, IntegerType::get(M.getContext(), bw2));
          Value *op2ext =
            builder.CreateZExt(op2, IntegerType::get(M.getContext(), bw2));
          Value *int_max_s =
            ConstantInt::get(op1->getType(), APInt::getMaxValue(bw));
          Value *int_max =
            builder.CreateZExt(int_max_s, IntegerType::get(M.getContext(), bw2));

          if (ii->getIntrinsicID() == Intrinsic::uadd_with_overflow){
            result_ext = builder.CreateAdd(op1ext, op2ext);
          } else if (ii->getIntrinsicID() == Intrinsic::usub_with_overflow){
            result_ext = builder.CreateSub(op1ext, op2ext);
          } else if (ii->getIntrinsicID() == Intrinsic::umul_with_overflow){
            result_ext = builder.CreateMul(op1ext, op2ext);
          }
          overflow = builder.CreateICmpUGT(result_ext, int_max);

        } else if ((ii->getIntrinsicID() == Intrinsic::sadd_with_overflow) ||
                   (ii->getIntrinsicID() == Intrinsic::ssub_with_overflow) ||
                   (ii->getIntrinsicID() == Intrinsic::smul_with_overflow)) {

          Value *op1ext =
            builder.CreateSExt(op1, IntegerType::get(M.getContext(), bw2));
          Value *op2ext =
            builder.CreateSExt(op2, IntegerType::get(M.getContext(), bw2));
          Value *int_max_s =
            ConstantInt::get(op1->getType(), APInt::getSignedMaxValue(bw));
          Value *int_min_s =
            ConstantInt::get(op1->getType(), APInt::getSignedMinValue(bw));
          Value *int_max =
            builder.CreateSExt(int_max_s, IntegerType::get(M.getContext(), bw2));
          Value *int_min =
            builder.CreateSExt(int_min_s, IntegerType::get(M.getContext(), bw2));

          if (ii->getIntrinsicID() == Intrinsic::sadd_with_overflow){
            result_ext = builder.CreateAdd(op1ext, op2ext);
          } else if (ii->getIntrinsicID() == Intrinsic::ssub_with_overflow){
            result_ext = builder.CreateSub(op1ext, op2ext);
          } else if (ii->getIntrinsicID() == Intrinsic::smul_with_overflow){
            result_ext = builder.CreateMul(op1ext, op2ext);
          }
          overflow = builder.CreateOr(builder.CreateICmpSGT(result_ext, int_max),
                                      builder.CreateICmpSLT(result_ext, int_min));
        }

        // This trunc cound be replaced by a more general trunc replacement
        // that allows to detect also undefined behavior in assignments or
        // overflow in operation with integers whose dimension is smaller than
        // int's dimension, e.g.
        //     uint8_t = uint8_t + uint8_t;
        // if one desires the wrapping should write
        //     uint8_t = (uint8_t + uint8_t) & 0xFF;
        // before this, must check if it has side effects on other operations
        result = builder.CreateTrunc(result_ext, op1->getType());
        Value *resultStruct =
          builder.CreateInsertValue(UndefValue::get(ii->getType()), result, 0);
        resultStruct = builder.CreateInsertValue(resultStruct, overflow, 1);
        
        ii->replaceAllUsesWith(resultStruct);
        ii->removeFromParent();
        delete ii;
        dirty = true;
        break;
      }

      case Intrinsic::dbg_value:
      case Intrinsic::dbg_declare:
        // Remove these regardless of lower intrinsics flag. This can
        // be removed once IntrinsicLowering is fixed to not have bad
        // caches.
        ii->eraseFromParent();
        dirty = true;
        break;

      case Intrinsic::trap: {
        // Intrisic instruction "llvm.trap" found. Directly lower it to
        // a call of the abort() function.
        Function *F = cast<Function>(
          M.getOrInsertFunction(
            "abort", Type::getVoidTy(ctx), NULL));
        F->setDoesNotReturn();
        F->setDoesNotThrow();

        CallInst::Create(F, Twine(), ii);
        new UnreachableInst(ctx, ii);

        ii->eraseFromParent();

        dirty = true;
        break;
      }
      case Intrinsic::objectsize: {
        // We don't know the size of an object in general so we replace
        // with 0 or -1 depending on the second argument to the intrinsic.
        assert(ii->getNumArgOperands() == 2 && "wrong number of arguments");
        Value *minArg = ii->getArgOperand(1);
        assert(minArg && "Failed to get second argument");
        ConstantInt *minArgAsInt = dyn_cast<ConstantInt>(minArg);
        assert(minArgAsInt && "Second arg is not a ConstantInt");
        assert(minArgAsInt->getBitWidth() == 1 && "Second argument is not an i1");
        Value *replacement = NULL;
        IntegerType *intType = dyn_cast<IntegerType>(ii->getType());
        assert(intType && "intrinsic does not have integer return type");
        if (minArgAsInt->isZero()) {
          // min=false
          replacement = ConstantInt::get(intType, -1, /*isSigned=*/true);
        } else {
          // min=true
          replacement = ConstantInt::get(intType, 0, /*isSigned=*/false);
        }
        ii->replaceAllUsesWith(replacement);
        ii->eraseFromParent();
        dirty = true;
        break;
      }
      // inspired by https://gitlab.doc.ic.ac.uk/dsl11/klee-cl/blob/two_thread_hack/lib/Module/LowerSSE.cpp
      case Intrinsic::x86_sse2_storeu_dq: {
        assert(ii->getNumArgOperands() == 2 && "wrong number of arguments");
        Value *dst = ii->getArgOperand(0);
        assert(dst && "Failed to get first argument");
        Value *src = ii->getArgOperand(1);
        assert(src && "Failed to get second argument");
        VectorType* vecType = cast<VectorType>(src->getType());
        PointerType *vecPointerType = PointerType::get(vecType, 0);
        CastInst* cast = new BitCastInst(dst, vecPointerType, "", ii);
        new StoreInst(src, cast, false, ii);
        ii->eraseFromParent();
        dirty = true;
        break;
      }

      // The following instructions are all replaced by an "unreachable" since we don't actually use them...

      // ptestz described in http://dpdk.org/ml/archives/dev/2014-August/004567.html
      case Intrinsic::x86_sse41_ptestz: {
        assert(ii->getNumArgOperands() == 2 && "wrong number of arguments");
        Value *a = ii->getArgOperand(0);
        assert(a && "Failed to get first argument");
        Value *b = ii->getArgOperand(1);
        assert(b && "Failed to get second argument");

        Type* i32 = Type::getInt32Ty(ctx);

        /*I think the instruction should look like this...
        IRBuilder<> builder(ii->getParent(), ii);
	ii->replaceAllUsesWith(
          builder.CreateNot(
            builder.CreateAnd(
              builder.CreateAnd(
                builder.CreateExtractElement(a, ConstantInt::get(i32, 0)),
                builder.CreateExtractElement(b, ConstantInt::get(i32, 0))
              ),
              builder.CreateAnd(
                builder.CreateExtractElement(a, ConstantInt::get(i32, 1)),
                builder.CreateExtractElement(b, ConstantInt::get(i32, 1))
              )
            )
          )
        );*/
        new UnreachableInst(ctx, ii);
        ii->replaceAllUsesWith(ConstantInt::get(i32, 424242));
        ii->eraseFromParent();
        dirty = true;
        break;
      }
      case Intrinsic::x86_ssse3_pshuf_b_128: {
        assert(ii->getNumArgOperands() == 2 && "wrong number of arguments");
        Value *a = ii->getArgOperand(0);
        assert(a && "Failed to get first argument");
        new UnreachableInst(ctx, ii);
        ii->replaceAllUsesWith(a);
        ii->eraseFromParent();
        dirty = true;
        break;
      }
      case Intrinsic::x86_sse41_phminposuw: {
        assert(ii->getNumArgOperands() == 1 && "wrong number of arguments");
        Value *a = ii->getArgOperand(0);
        assert(a && "Failed to get first argument");
        new UnreachableInst(ctx, ii);
        ii->replaceAllUsesWith(a);
        ii->eraseFromParent();
        dirty = true;
        break;
      }

      // consistency fences, just ignore them
      case Intrinsic::x86_sse_sfence:
      case Intrinsic::x86_sse2_lfence:
      case Intrinsic::x86_sse2_mfence: {
        ii->eraseFromParent();
        dirty = true;
        break;
      }
      default:
        if (LowerIntrinsics)
          IL->LowerIntrinsicCall(ii);
        dirty = true;
        break;
      }
    }
  }

  return dirty;
}
}
