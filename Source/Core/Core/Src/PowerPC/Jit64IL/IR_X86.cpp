// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

/*
For a more general explanation of the IR, see IR.cpp.

X86 codegen is a backward pass followed by a forward pass.

The first pass to actually doing codegen is a liveness analysis pass.
Liveness is important for two reasons: one, it lets us do dead code
elimination, which results both from earlier folding, PPC
instructions with unused parts like srawx, and just random strangeness.
The other bit is that is allows us to identify the last instruction to
use a value: this is absolutely essential for register allocation
because it the allocator needs to be able to free unused registers.
In addition, this allows eliminating redundant mov instructions in a lot
of cases.

The register allocation is linear scan allocation.
*/

#ifdef _MSC_VER
#pragma warning(disable:4146)   // unary minus operator applied to unsigned type, result still unsigned
#endif

#include "IR.h"
#include "../PPCTables.h"
#include "../../CoreTiming.h"
#include "Thunk.h"
#include "../../HW/Memmap.h"
#include "JitILAsm.h"
#include "JitIL.h"
#include "../../HW/GPFifo.h"
#include "../../ConfigManager.h"
#include "x64Emitter.h"
#include "../../../../Common/Src/CPUDetect.h"
#include "MathUtil.h"
#include "../../Core.h"
#include "HW/ProcessorInterface.h"

static ThunkManager thunks;

using namespace IREmitter;
using namespace Gen;

static const unsigned int MAX_NUMBER_OF_REGS = 16;

struct RegInfo {
	JitIL *Jit;
	IRBuilder* Build;
	InstLoc FirstI;
	std::vector<unsigned> IInfo;
	std::vector<InstLoc> lastUsed;
	InstLoc regs[MAX_NUMBER_OF_REGS];
	InstLoc fregs[MAX_NUMBER_OF_REGS];
	unsigned numSpills;
	unsigned numFSpills;
	bool MakeProfile;
	bool UseProfile;
	unsigned numProfiledLoads;
	unsigned exitNumber;

	RegInfo(JitIL* j, InstLoc f, unsigned insts) : Jit(j), FirstI(f), IInfo(insts), lastUsed(insts) {
		for (unsigned i = 0; i < MAX_NUMBER_OF_REGS; i++) {
			regs[i] = 0;
			fregs[i] = 0;
		}
		numSpills = 0;
		numFSpills = 0;
		numProfiledLoads = 0;
		exitNumber = 0;
		MakeProfile = UseProfile = false;
	}

	private:
		RegInfo(RegInfo&); // DO NOT IMPLEMENT
};

static void regMarkUse(RegInfo& R, InstLoc I, InstLoc Op, unsigned OpNum) {
	unsigned& info = R.IInfo[Op - R.FirstI];
	if (info == 0) R.IInfo[I - R.FirstI] |= 1 << (OpNum + 1);
	if (info < 2) info++;
	R.lastUsed[Op - R.FirstI] = max(R.lastUsed[Op - R.FirstI], I);
}

static unsigned regReadUse(RegInfo& R, InstLoc I) {
	return R.IInfo[I - R.FirstI] & 3;
}

static unsigned SlotSet[1000];
static unsigned ProfiledLoads[1000];
static u8 GC_ALIGNED16(FSlotSet[16*1000]);

static OpArg regLocForSlot(RegInfo& RI, unsigned slot) {
	return M(&SlotSet[slot - 1]);
}

static unsigned regCreateSpill(RegInfo& RI, InstLoc I) {
	unsigned newSpill = ++RI.numSpills;
	RI.IInfo[I - RI.FirstI] |= newSpill << 16;
	return newSpill;
}

static unsigned regGetSpill(RegInfo& RI, InstLoc I) {
	return RI.IInfo[I - RI.FirstI] >> 16;
}

static void regSpill(RegInfo& RI, X64Reg reg) {
	if (!RI.regs[reg]) return;
	unsigned slot = regGetSpill(RI, RI.regs[reg]);
	if (!slot) {
		slot = regCreateSpill(RI, RI.regs[reg]);
		RI.Jit->MOV(32, regLocForSlot(RI, slot), R(reg));
	}
	RI.regs[reg] = 0;
}

static OpArg fregLocForSlot(RegInfo& RI, unsigned slot) {
	return M(&FSlotSet[slot*16]);
}

static unsigned fregCreateSpill(RegInfo& RI, InstLoc I) {
	unsigned newSpill = ++RI.numFSpills;
	RI.IInfo[I - RI.FirstI] |= newSpill << 16;
	return newSpill;
}

static unsigned fregGetSpill(RegInfo& RI, InstLoc I) {
	return RI.IInfo[I - RI.FirstI] >> 16;
}

static void fregSpill(RegInfo& RI, X64Reg reg) {
	if (!RI.fregs[reg]) return;
	unsigned slot = fregGetSpill(RI, RI.fregs[reg]);
	if (!slot) {
		slot = fregCreateSpill(RI, RI.fregs[reg]);
		RI.Jit->MOVAPD(fregLocForSlot(RI, slot), reg);
	}
	RI.fregs[reg] = 0;
}

// ECX is scratch, so we don't allocate it
#ifdef _M_X64

// 64-bit - calling conventions differ between linux & windows, so...
#ifdef _WIN32
static const X64Reg RegAllocOrder[] = {RSI, RDI, R12, R13, R14, R8, R9, R10, R11};
#else
static const X64Reg RegAllocOrder[] = {RBP, R12, R13, R14, R8, R9, R10, R11};
#endif
static const int RegAllocSize = sizeof(RegAllocOrder) / sizeof(X64Reg);
static const X64Reg FRegAllocOrder[] = {XMM6, XMM7, XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15, XMM2, XMM3, XMM4, XMM5};
static const int FRegAllocSize = sizeof(FRegAllocOrder) / sizeof(X64Reg);

#else

// 32-bit
static const X64Reg RegAllocOrder[] = {EDI, ESI, EBP, EBX, EDX, EAX};
static const int RegAllocSize = sizeof(RegAllocOrder) / sizeof(X64Reg);
static const X64Reg FRegAllocOrder[] = {XMM2, XMM3, XMM4, XMM5, XMM6, XMM7};
static const int FRegAllocSize = sizeof(FRegAllocOrder) / sizeof(X64Reg);

#endif

static X64Reg regFindFreeReg(RegInfo& RI) {
	for (int i = 0; i < RegAllocSize; i++)
		if (RI.regs[RegAllocOrder[i]] == 0)
			return RegAllocOrder[i];

	int bestIndex = -1;
	InstLoc bestEnd = 0;
	for (int i = 0; i < RegAllocSize; ++i) {
		const InstLoc start = RI.regs[RegAllocOrder[i]];
		const InstLoc end = RI.lastUsed[start - RI.FirstI];
		if (bestEnd < end) {
			bestEnd = end;
			bestIndex = i;
		}
	}

	X64Reg reg = RegAllocOrder[bestIndex];
	regSpill(RI, reg);
	return reg;
}

static X64Reg fregFindFreeReg(RegInfo& RI) {
	for (int i = 0; i < FRegAllocSize; i++)
		if (RI.fregs[FRegAllocOrder[i]] == 0)
			return FRegAllocOrder[i];

	int bestIndex = -1;
	InstLoc bestEnd = 0;
	for (int i = 0; i < FRegAllocSize; ++i) {
		const InstLoc start = RI.fregs[FRegAllocOrder[i]];
		const InstLoc end = RI.lastUsed[start - RI.FirstI];
		if (bestEnd < end) {
			bestEnd = end;
			bestIndex = i;
		}
	}

	X64Reg reg = FRegAllocOrder[bestIndex];
	fregSpill(RI, reg);
	return reg;
}

static OpArg regLocForInst(RegInfo& RI, InstLoc I) {
	for (int i = 0; i < RegAllocSize; i++)
		if (RI.regs[RegAllocOrder[i]] == I)
			return R(RegAllocOrder[i]);

	if (regGetSpill(RI, I) == 0)
		PanicAlert("Retrieving unknown spill slot?!");
	return regLocForSlot(RI, regGetSpill(RI, I));
}

static OpArg fregLocForInst(RegInfo& RI, InstLoc I) {
	for (int i = 0; i < FRegAllocSize; i++)
		if (RI.fregs[FRegAllocOrder[i]] == I)
			return R(FRegAllocOrder[i]);

	if (fregGetSpill(RI, I) == 0)
		PanicAlert("Retrieving unknown spill slot?!");
	return fregLocForSlot(RI, fregGetSpill(RI, I));
}

static void regClearInst(RegInfo& RI, InstLoc I) {
	for (int i = 0; i < RegAllocSize; i++)
		if (RI.regs[RegAllocOrder[i]] == I)
			RI.regs[RegAllocOrder[i]] = 0;
}

static void fregClearInst(RegInfo& RI, InstLoc I) {
	for (int i = 0; i < FRegAllocSize; i++)
		if (RI.fregs[FRegAllocOrder[i]] == I)
			RI.fregs[FRegAllocOrder[i]] = 0;
}

static X64Reg regEnsureInReg(RegInfo& RI, InstLoc I) {
	OpArg loc = regLocForInst(RI, I);
	if (!loc.IsSimpleReg()) {
		X64Reg newReg = regFindFreeReg(RI);
		RI.Jit->MOV(32, R(newReg), loc);
		loc = R(newReg);
	}
	return loc.GetSimpleReg();
}

static X64Reg fregEnsureInReg(RegInfo& RI, InstLoc I) {
	OpArg loc = fregLocForInst(RI, I);
	if (!loc.IsSimpleReg()) {
		X64Reg newReg = fregFindFreeReg(RI);
		RI.Jit->MOVAPD(newReg, loc);
		loc = R(newReg);
	}
	return loc.GetSimpleReg();
}

static void regSpillCallerSaved(RegInfo& RI) {
#ifdef _M_IX86
	// 32-bit
	regSpill(RI, EDX);
	regSpill(RI, ECX);
	regSpill(RI, EAX);
#else
	// 64-bit
	regSpill(RI, RCX);
	regSpill(RI, RDX);
	regSpill(RI, RSI); 
	regSpill(RI, RDI);
	regSpill(RI, R8);
	regSpill(RI, R9);
	regSpill(RI, R10);
	regSpill(RI, R11);
#endif
}

static X64Reg regUReg(RegInfo& RI, InstLoc I) {
	const OpArg loc = regLocForInst(RI, getOp1(I));
	if ((RI.IInfo[I - RI.FirstI] & 4) && loc.IsSimpleReg()) {
		return loc.GetSimpleReg();
	}
	return regFindFreeReg(RI);
}

// Recycle the register if the lifetime of op1 register ends at I.
static X64Reg fregURegWithoutMov(RegInfo& RI, InstLoc I) {
	const OpArg loc = fregLocForInst(RI, getOp1(I));
	if ((RI.IInfo[I - RI.FirstI] & 4) && loc.IsSimpleReg()) {
		return loc.GetSimpleReg();
	}
	return fregFindFreeReg(RI);
}

static X64Reg fregURegWithMov(RegInfo& RI, InstLoc I) {
	const OpArg loc = fregLocForInst(RI, getOp1(I));
	if ((RI.IInfo[I - RI.FirstI] & 4) && loc.IsSimpleReg()) {
		return loc.GetSimpleReg();
	}
	X64Reg reg = fregFindFreeReg(RI);
	RI.Jit->MOVAPD(reg, loc);
	return reg;
}

// Recycle the register if the lifetime of op1 register ends at I.
static X64Reg fregBinLHSRegWithMov(RegInfo& RI, InstLoc I) {
	const OpArg loc = fregLocForInst(RI, getOp1(I));
	if ((RI.IInfo[I - RI.FirstI] & 4) && loc.IsSimpleReg()) {
		return loc.GetSimpleReg();
	}
	X64Reg reg = fregFindFreeReg(RI);
	RI.Jit->MOVAPD(reg, loc);
	return reg;
}

// Recycle the register if the lifetime of op2 register ends at I.
static X64Reg fregBinRHSRegWithMov(RegInfo& RI, InstLoc I) {
	const OpArg loc = fregLocForInst(RI, getOp2(I));
	if ((RI.IInfo[I - RI.FirstI] & 8) && loc.IsSimpleReg()) {
		return loc.GetSimpleReg();
	}
	X64Reg reg = fregFindFreeReg(RI);
	RI.Jit->MOVAPD(reg, loc);
	return reg;
}

// If the lifetime of the register used by an operand ends at I,
// return the register. Otherwise return a free register.
static X64Reg regBinReg(RegInfo& RI, InstLoc I) {
	// FIXME: When regLocForInst() is extracted as a local variable,
	//        "Retrieving unknown spill slot?!" is shown.
	if ((RI.IInfo[I - RI.FirstI] & 4) &&
		regLocForInst(RI, getOp1(I)).IsSimpleReg()) {
		return regLocForInst(RI, getOp1(I)).GetSimpleReg();
	} else if ((RI.IInfo[I - RI.FirstI] & 8) &&
		regLocForInst(RI, getOp2(I)).IsSimpleReg()) {
		return regLocForInst(RI, getOp2(I)).GetSimpleReg();
	}
	return regFindFreeReg(RI);
}

static X64Reg regBinLHSReg(RegInfo& RI, InstLoc I) {
	if (RI.IInfo[I - RI.FirstI] & 4) {
		return regEnsureInReg(RI, getOp1(I));
	}
	X64Reg reg = regFindFreeReg(RI);
	RI.Jit->MOV(32, R(reg), regLocForInst(RI, getOp1(I)));
	return reg;
}

static void regNormalRegClear(RegInfo& RI, InstLoc I) {
	if (RI.IInfo[I - RI.FirstI] & 4)
		regClearInst(RI, getOp1(I));
	if (RI.IInfo[I - RI.FirstI] & 8)
		regClearInst(RI, getOp2(I));
}

static void fregNormalRegClear(RegInfo& RI, InstLoc I) {
	if (RI.IInfo[I - RI.FirstI] & 4)
		fregClearInst(RI, getOp1(I));
	if (RI.IInfo[I - RI.FirstI] & 8)
		fregClearInst(RI, getOp2(I));
}

static void regEmitBinInst(RegInfo& RI, InstLoc I,
			   void (JitIL::*op)(int, const OpArg&,
			                     const OpArg&),
			   bool commutable = false) {
	X64Reg reg;
	bool commuted = false;
	if (RI.IInfo[I - RI.FirstI] & 4) {
		reg = regEnsureInReg(RI, getOp1(I));
	} else if (commutable && (RI.IInfo[I - RI.FirstI] & 8)) {
		reg = regEnsureInReg(RI, getOp2(I));
		commuted = true;
	} else {
		reg = regFindFreeReg(RI);
		RI.Jit->MOV(32, R(reg), regLocForInst(RI, getOp1(I)));
	}
	if (isImm(*getOp2(I))) {
		unsigned RHS = RI.Build->GetImmValue(getOp2(I));
		if (RHS + 128 < 256) {
			(RI.Jit->*op)(32, R(reg), Imm8(RHS));
		} else {
			(RI.Jit->*op)(32, R(reg), Imm32(RHS));
		}
	} else if (commuted) {
		(RI.Jit->*op)(32, R(reg), regLocForInst(RI, getOp1(I)));
	} else {
		(RI.Jit->*op)(32, R(reg), regLocForInst(RI, getOp2(I)));
	}
	RI.regs[reg] = I;
	regNormalRegClear(RI, I);
}

static void fregEmitBinInst(RegInfo& RI, InstLoc I,
			    void (JitIL::*op)(X64Reg, OpArg)) {
	X64Reg reg;
	if (RI.IInfo[I - RI.FirstI] & 4) {
		reg = fregEnsureInReg(RI, getOp1(I));
	} else {
		reg = fregFindFreeReg(RI);
		RI.Jit->MOVAPD(reg, fregLocForInst(RI, getOp1(I)));
	}
	(RI.Jit->*op)(reg, fregLocForInst(RI, getOp2(I)));
	RI.fregs[reg] = I;
	fregNormalRegClear(RI, I);
}

// Mark and calculation routines for profiled load/store addresses
// Could be extended to unprofiled addresses.
static void regMarkMemAddress(RegInfo& RI, InstLoc I, InstLoc AI, unsigned OpNum) {
	if (isImm(*AI)) {
		unsigned addr = RI.Build->GetImmValue(AI);	
		if (Memory::IsRAMAddress(addr))
			return;
	}
	if (getOpcode(*AI) == Add && isImm(*getOp2(AI))) {
		regMarkUse(RI, I, getOp1(AI), OpNum);
		return;
	}
	regMarkUse(RI, I, AI, OpNum);
}

static void regClearDeadMemAddress(RegInfo& RI, InstLoc I, InstLoc AI, unsigned OpNum) {
	if (!(RI.IInfo[I - RI.FirstI] & (2 << OpNum)))
		return;
	if (isImm(*AI)) {
		unsigned addr = RI.Build->GetImmValue(AI);	
		if (Memory::IsRAMAddress(addr)) {
			return;
		}
	}
	InstLoc AddrBase;
	if (getOpcode(*AI) == Add && isImm(*getOp2(AI))) {
		AddrBase = getOp1(AI);
	} else {
		AddrBase = AI;
	}
	regClearInst(RI, AddrBase);
}

// in 64-bit build, this returns a completely bizarre address sometimes!
static OpArg regBuildMemAddress(RegInfo& RI, InstLoc I, InstLoc AI,
				unsigned OpNum,	unsigned Size, X64Reg* dest,
				bool Profiled,
				unsigned ProfileOffset = 0) {
	if (isImm(*AI)) {
		unsigned addr = RI.Build->GetImmValue(AI);	
		if (Memory::IsRAMAddress(addr)) {
			if (dest)
				*dest = regFindFreeReg(RI);
#ifdef _M_IX86
			// 32-bit
			if (Profiled) 
				return M((void*)((u8*)Memory::base + (addr & Memory::MEMVIEW32_MASK)));
			return M((void*)addr);
#else
			// 64-bit
			if (Profiled) {
				RI.Jit->LEA(32, EAX, M((void*)(u64)addr));
				return MComplex(RBX, EAX, SCALE_1, 0);
			}
			return M((void*)(u64)addr);
#endif
		}
	}
	unsigned offset;
	InstLoc AddrBase;
	if (getOpcode(*AI) == Add && isImm(*getOp2(AI))) {
		offset = RI.Build->GetImmValue(getOp2(AI));
		AddrBase = getOp1(AI);
	} else {
		offset = 0;
		AddrBase = AI;
	}
	X64Reg baseReg;
	// Ok, this stuff needs a comment or three :P -ector
	if (RI.IInfo[I - RI.FirstI] & (2 << OpNum)) {
		baseReg = regEnsureInReg(RI, AddrBase);
		regClearInst(RI, AddrBase);
		if (dest)
			*dest = baseReg;
	} else if (dest) {
		X64Reg reg = regFindFreeReg(RI);
		const OpArg loc = regLocForInst(RI, AddrBase);
		if (!loc.IsSimpleReg()) {
			RI.Jit->MOV(32, R(reg), loc);
			baseReg = reg;
		} else {
			baseReg = loc.GetSimpleReg();
		}
		*dest = reg;
	} else {
		baseReg = regEnsureInReg(RI, AddrBase);
	}

	if (Profiled) {
		// (Profiled mode isn't the default, at least for the moment)
#ifdef _M_IX86
		return MDisp(baseReg, (u32)Memory::base + offset + ProfileOffset);
#else
		RI.Jit->LEA(32, EAX, MDisp(baseReg, offset));
		return MComplex(RBX, EAX, SCALE_1, 0);
#endif
	}
	return MDisp(baseReg, offset);
}

static void regEmitMemLoad(RegInfo& RI, InstLoc I, unsigned Size) {
	if (RI.UseProfile) {
		unsigned curLoad = ProfiledLoads[RI.numProfiledLoads++];
		if (!(curLoad & 0x0C000000)) {
			X64Reg reg;
			OpArg addr = regBuildMemAddress(RI, I, getOp1(I), 1,
							Size, &reg, true,
							-(curLoad & 0xC0000000));
			RI.Jit->MOVZX(32, Size, reg, addr);
			RI.Jit->BSWAP(Size, reg);
			if (regReadUse(RI, I))
				RI.regs[reg] = I;
			return;
		} 
	}
	X64Reg reg;
	OpArg addr = regBuildMemAddress(RI, I, getOp1(I), 1, Size, &reg, false);
	RI.Jit->LEA(32, ECX, addr);
	if (RI.MakeProfile) {
		RI.Jit->MOV(32, M(&ProfiledLoads[RI.numProfiledLoads++]), R(ECX));
	}
	u32 mem_mask = 0;

	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bMMU || SConfig::GetInstance().m_LocalCoreStartupParameter.iTLBHack)
		mem_mask = 0x20000000;

	RI.Jit->TEST(32, R(ECX), Imm32(0x0C000000 | mem_mask));
	FixupBranch argh = RI.Jit->J_CC(CC_Z);

	// Slow safe read using Memory::Read_Ux routines
#ifdef _M_IX86  // we don't allocate EAX on x64 so no reason to save it.
	if (reg != EAX) {
		RI.Jit->PUSH(32, R(EAX));
	}
#endif
	switch (Size)
	{
	case 32: RI.Jit->ABI_CallFunctionR(thunks.ProtectFunction((void *)&Memory::Read_U32, 1), ECX); break;
	case 16: RI.Jit->ABI_CallFunctionR(thunks.ProtectFunction((void *)&Memory::Read_U16_ZX, 1), ECX); break;
	case 8:  RI.Jit->ABI_CallFunctionR(thunks.ProtectFunction((void *)&Memory::Read_U8_ZX, 1), ECX);  break;
	}
	if (reg != EAX) {
		RI.Jit->MOV(32, R(reg), R(EAX));
#ifdef _M_IX86
		RI.Jit->POP(32, R(EAX));
#endif
	}
	FixupBranch arg2 = RI.Jit->J();
	RI.Jit->SetJumpTarget(argh);
	RI.Jit->UnsafeLoadRegToReg(ECX, reg, Size, 0, false);
	RI.Jit->SetJumpTarget(arg2);
	if (regReadUse(RI, I))
		RI.regs[reg] = I;
}

static OpArg regSwappedImmForConst(RegInfo& RI, InstLoc I, unsigned Size) {
	unsigned imm = RI.Build->GetImmValue(I);
	if (Size == 32) {
		imm = Common::swap32(imm);
		return Imm32(imm);
	} else if (Size == 16) {
		imm = Common::swap16(imm);
		return Imm16(imm);
	} else {
		return Imm8(imm);
	}
}

static OpArg regImmForConst(RegInfo& RI, InstLoc I, unsigned Size) {
	unsigned imm = RI.Build->GetImmValue(I);
	if (Size == 32) {
		return Imm32(imm);
	} else if (Size == 16) {
		return Imm16(imm);
	} else {
		return Imm8(imm);
	}
}

static void regEmitMemStore(RegInfo& RI, InstLoc I, unsigned Size) {
	if (RI.UseProfile) {
		unsigned curStore = ProfiledLoads[RI.numProfiledLoads++];
		if (!(curStore & 0x0C000000)) {
			OpArg addr = regBuildMemAddress(RI, I, getOp2(I), 2,
							Size, 0, true,
							-(curStore & 0xC0000000));
			if (isImm(*getOp1(I))) {
				RI.Jit->MOV(Size, addr, regSwappedImmForConst(RI, getOp1(I), Size));
			} else {
				RI.Jit->MOV(32, R(ECX), regLocForInst(RI, getOp1(I)));
				RI.Jit->BSWAP(Size, ECX);
				RI.Jit->MOV(Size, addr, R(ECX));
			}
			if (RI.IInfo[I - RI.FirstI] & 4)
				regClearInst(RI, getOp1(I));
			return;
		} else if ((curStore & 0xFFFFF000) == 0xCC008000) {
			regSpill(RI, EAX);
			if (isImm(*getOp1(I))) {
				RI.Jit->MOV(Size, R(ECX), regSwappedImmForConst(RI, getOp1(I), Size));
			} else { 
				RI.Jit->MOV(32, R(ECX), regLocForInst(RI, getOp1(I)));
				RI.Jit->BSWAP(Size, ECX);
			}
			RI.Jit->MOV(32, R(EAX), M(&GPFifo::m_gatherPipeCount));
			RI.Jit->MOV(Size, MDisp(EAX, (u32)(u64)GPFifo::m_gatherPipe), R(ECX));
			RI.Jit->ADD(32, R(EAX), Imm8(Size >> 3));
			RI.Jit->MOV(32, M(&GPFifo::m_gatherPipeCount), R(EAX));
			RI.Jit->js.fifoBytesThisBlock += Size >> 3;
			if (RI.IInfo[I - RI.FirstI] & 4)
				regClearInst(RI, getOp1(I));
			regClearDeadMemAddress(RI, I, getOp2(I), 2);
			return;
		}
	}
	OpArg addr = regBuildMemAddress(RI, I, getOp2(I), 2, Size, 0, false);
	RI.Jit->LEA(32, ECX, addr);
	regSpill(RI, EAX);
	if (isImm(*getOp1(I))) {
		RI.Jit->MOV(Size, R(EAX), regImmForConst(RI, getOp1(I), Size));
	} else {
		RI.Jit->MOV(32, R(EAX), regLocForInst(RI, getOp1(I)));
	}
	if (RI.MakeProfile) {
		RI.Jit->MOV(32, M(&ProfiledLoads[RI.numProfiledLoads++]), R(ECX));
	}
	RI.Jit->SafeWriteRegToReg(EAX, ECX, Size, 0);
	if (RI.IInfo[I - RI.FirstI] & 4)
		regClearInst(RI, getOp1(I));
}

static void regEmitShiftInst(RegInfo& RI, InstLoc I, void (JitIL::*op)(int, OpArg, OpArg))
{
	X64Reg reg = regBinLHSReg(RI, I);
	if (isImm(*getOp2(I))) {
		unsigned RHS = RI.Build->GetImmValue(getOp2(I));
		(RI.Jit->*op)(32, R(reg), Imm8(RHS));
		RI.regs[reg] = I;
		return;
	}
	RI.Jit->MOV(32, R(ECX), regLocForInst(RI, getOp2(I)));
	(RI.Jit->*op)(32, R(reg), R(ECX));
	RI.regs[reg] = I;
	regNormalRegClear(RI, I);
}

static void regStoreInstToConstLoc(RegInfo& RI, unsigned width, InstLoc I, void* loc) {
	if (width != 32) {
		PanicAlert("Not implemented!");
		return;
	}
	if (isImm(*I)) {
		RI.Jit->MOV(32, M(loc), Imm32(RI.Build->GetImmValue(I)));
		return;
	}
	X64Reg reg = regEnsureInReg(RI, I);
	RI.Jit->MOV(32, M(loc), R(reg));
}

static void regEmitCmp(RegInfo& RI, InstLoc I) {
	if (isImm(*getOp2(I))) {
		unsigned RHS = RI.Build->GetImmValue(getOp2(I));
		RI.Jit->CMP(32, regLocForInst(RI, getOp1(I)), Imm32(RHS));
	} else {
		X64Reg reg = regEnsureInReg(RI, getOp1(I));
		RI.Jit->CMP(32, R(reg), regLocForInst(RI, getOp2(I)));
	}
}

static void regEmitICmpInst(RegInfo& RI, InstLoc I, CCFlags flag) {
	regEmitCmp(RI, I);
	RI.Jit->SETcc(flag, R(ECX)); // Caution: SETCC uses 8-bit regs!
	X64Reg reg = regBinReg(RI, I);
	RI.Jit->MOVZX(32, 8, reg, R(ECX));
	RI.regs[reg] = I;
	regNormalRegClear(RI, I);
}

static void regWriteExit(RegInfo& RI, InstLoc dest) {
	if (RI.MakeProfile) {
		if (isImm(*dest)) {
			RI.Jit->MOV(32, M(&PC), Imm32(RI.Build->GetImmValue(dest)));
		} else {
			RI.Jit->MOV(32, R(EAX), regLocForInst(RI, dest));
			RI.Jit->MOV(32, M(&PC), R(EAX));
		}
		RI.Jit->Cleanup();
		RI.Jit->SUB(32, M(&CoreTiming::downcount), Imm32(RI.Jit->js.downcountAmount));
		RI.Jit->JMP(((JitIL *)jit)->asm_routines.doReJit, true);
		return;
	}
	if (isImm(*dest)) {
		RI.Jit->WriteExit(RI.Build->GetImmValue(dest), RI.exitNumber++);
	} else {
		RI.Jit->WriteExitDestInOpArg(regLocForInst(RI, dest));
	}
}

// Helper function to check floating point exceptions
static double GC_ALIGNED16(isSNANTemp[2][2]);
static bool checkIsSNAN() {
	return MathUtil::IsSNAN(isSNANTemp[0][0]) || MathUtil::IsSNAN(isSNANTemp[1][0]);
}

static void DoWriteCode(IRBuilder* ibuild, JitIL* Jit, bool UseProfile, bool MakeProfile) {
	//printf("Writing block: %x\n", js.blockStart);
	RegInfo RI(Jit, ibuild->getFirstInst(), ibuild->getNumInsts());
	RI.Build = ibuild;
	RI.UseProfile = UseProfile;
	RI.MakeProfile = MakeProfile;
	// Pass to compute liveness
	ibuild->StartBackPass();
	for (unsigned int index = (unsigned int)RI.IInfo.size() - 1; index != -1U; --index) {
		InstLoc I = ibuild->ReadBackward();
		unsigned int op = getOpcode(*I);
		bool thisUsed = regReadUse(RI, I) ? true : false;
		switch (op) {
		default:
			PanicAlert("Unexpected inst!");
		case Nop:
		case CInt16:
		case CInt32:
		case LoadGReg:
		case LoadLink:
		case LoadCR:
		case LoadCarry:
		case LoadCTR:
		case LoadMSR:
		case LoadFReg:
		case LoadFRegDENToZero:
		case LoadGQR:
		case BlockEnd:
		case BlockStart:
		case InterpreterFallback:
		case SystemCall:
		case RFIExit:
		case InterpreterBranch:		
		case ShortIdleLoop:
		case FPExceptionCheck:
		case DSIExceptionCheck:
		case ISIException:
		case ExtExceptionCheck:
		case BreakPointCheck:
		case Int3:
		case Tramp:
			// No liveness effects
			break;
		case SExt8:
		case SExt16:
		case BSwap32:
		case BSwap16:
		case Cntlzw:
		case Not:
		case DupSingleToMReg:
		case DoubleToSingle:
		case ExpandPackedToMReg:
		case CompactMRegToPacked:
		case FPNeg:
		case FPDup0:
		case FPDup1:
		case FSNeg:
		case FDNeg:
			if (thisUsed)
				regMarkUse(RI, I, getOp1(I), 1);
			break;
		case Load8:
		case Load16:
		case Load32:
			regMarkMemAddress(RI, I, getOp1(I), 1);
			break;
		case LoadDouble:
		case LoadSingle:
		case LoadPaired:
			if (thisUsed)
				regMarkUse(RI, I, getOp1(I), 1);
			break;
		case StoreCR:
		case StoreCarry:
		case StoreFPRF:
			regMarkUse(RI, I, getOp1(I), 1);
			break;
		case StoreGReg:
		case StoreLink:
		case StoreCTR:
		case StoreMSR:
		case StoreGQR:
		case StoreSRR:
		case StoreFReg:
			if (!isImm(*getOp1(I)))
				regMarkUse(RI, I, getOp1(I), 1);
			break;
		case Add:
		case Sub:
		case And:
		case Or:
		case Xor:
		case Mul:
		case MulHighUnsigned:
		case Rol:
		case Shl:
		case Shrl:
		case Sarl:
		case ICmpCRUnsigned:
		case ICmpCRSigned:
		case ICmpEq:
		case ICmpNe:
		case ICmpUgt:
		case ICmpUlt:
		case ICmpUge:
		case ICmpUle:
		case ICmpSgt:
		case ICmpSlt:
		case ICmpSge:
		case ICmpSle:
		case FSMul:
		case FSAdd:
		case FSSub:
		case FSRSqrt:
		case FDMul:
		case FDAdd:
		case FDSub:
		case FPAdd:
		case FPMul:
		case FPSub:
		case FPMerge00:
		case FPMerge01:
		case FPMerge10:
		case FPMerge11:
		case FDCmpCR:
		case InsertDoubleInMReg:
			if (thisUsed) {
				regMarkUse(RI, I, getOp1(I), 1);
				if (!isImm(*getOp2(I)))
					regMarkUse(RI, I, getOp2(I), 2);
			}
			break;
		case Store8:
		case Store16:
		case Store32:
			if (!isImm(*getOp1(I)))
				regMarkUse(RI, I, getOp1(I), 1);
			regMarkMemAddress(RI, I, getOp2(I), 2);
			break;
		case StoreSingle:
		case StoreDouble:
		case StorePaired:
			regMarkUse(RI, I, getOp1(I), 1);
			regMarkUse(RI, I, getOp2(I), 2);
			break;
		case BranchUncond:
			if (!isImm(*getOp1(I)))
				regMarkUse(RI, I, getOp1(I), 1);
			break;
		case IdleBranch:
			regMarkUse(RI, I, getOp1(getOp1(I)), 1);
			break;						 
		case BranchCond: {
			if (isICmp(*getOp1(I)) &&
			    isImm(*getOp2(getOp1(I)))) {
				regMarkUse(RI, I, getOp1(getOp1(I)), 1);
			} else {
				regMarkUse(RI, I, getOp1(I), 1);
			}
			if (!isImm(*getOp2(I)))
				regMarkUse(RI, I, getOp2(I), 2);
			break;
		}
		}
	}

	ibuild->StartForwardPass();
	for (unsigned i = 0; i != RI.IInfo.size(); i++) {
		InstLoc I = ibuild->ReadForward();
		bool thisUsed = regReadUse(RI, I) ? true : false;
		if (thisUsed) {
			// Needed for IR Writer
			ibuild->SetMarkUsed(I);
		}

		switch (getOpcode(*I)) {
		case InterpreterFallback: {
			unsigned InstCode = ibuild->GetImmValue(getOp1(I));
			unsigned InstLoc = ibuild->GetImmValue(getOp2(I));
			// There really shouldn't be anything live across an
			// interpreter call at the moment, but optimizing interpreter
			// calls isn't completely out of the question...
			regSpillCallerSaved(RI);
			Jit->MOV(32, M(&PC), Imm32(InstLoc));
			Jit->MOV(32, M(&NPC), Imm32(InstLoc+4));
			Jit->ABI_CallFunctionC((void*)GetInterpreterOp(InstCode),
					       InstCode);
			break;
		}
		case LoadGReg: {
			if (!thisUsed) break;
			X64Reg reg = regFindFreeReg(RI);
			unsigned ppcreg = *I >> 8;
			Jit->MOV(32, R(reg), M(&PowerPC::ppcState.gpr[ppcreg]));
			RI.regs[reg] = I;
			break;
		}
		case LoadCR: {
			if (!thisUsed) break;
			X64Reg reg = regFindFreeReg(RI);
			unsigned ppcreg = *I >> 8;
			Jit->MOVZX(32, 8, reg, M(&PowerPC::ppcState.cr_fast[ppcreg]));
			RI.regs[reg] = I;
			break;
		}
		case LoadCTR: {
			if (!thisUsed) break;
			X64Reg reg = regFindFreeReg(RI);
			Jit->MOV(32, R(reg), M(&CTR));
			RI.regs[reg] = I;
			break;
		}
		case LoadLink: {
			if (!thisUsed) break;
			X64Reg reg = regFindFreeReg(RI);
			Jit->MOV(32, R(reg), M(&LR));
			RI.regs[reg] = I;
			break;
		}
		case LoadMSR: {
			if (!thisUsed) break;
			X64Reg reg = regFindFreeReg(RI);
			Jit->MOV(32, R(reg), M(&MSR));
			RI.regs[reg] = I;
			break;
		}
		case LoadGQR: {
			if (!thisUsed) break;
			X64Reg reg = regFindFreeReg(RI);
			unsigned gqr = *I >> 8;
			Jit->MOV(32, R(reg), M(&GQR(gqr)));
			RI.regs[reg] = I;
			break;
		}
		case LoadCarry: {
			if (!thisUsed) break;
			X64Reg reg = regFindFreeReg(RI);
			Jit->MOV(32, R(reg), M(&PowerPC::ppcState.spr[SPR_XER]));
			Jit->SHR(32, R(reg), Imm8(29));
			Jit->AND(32, R(reg), Imm8(1));
			RI.regs[reg] = I;
			break;
		}
		case StoreGReg: {
			unsigned ppcreg = *I >> 16;
			regStoreInstToConstLoc(RI, 32, getOp1(I),
					       &PowerPC::ppcState.gpr[ppcreg]);
			regNormalRegClear(RI, I);
			break;
		}
		case StoreCR: {
			Jit->MOV(32, R(ECX), regLocForInst(RI, getOp1(I)));
			unsigned ppcreg = *I >> 16;
			// CAUTION: uses 8-bit reg!
			Jit->MOV(8, M(&PowerPC::ppcState.cr_fast[ppcreg]), R(ECX));
			regNormalRegClear(RI, I);
			break;
		}
		case StoreLink: {
			regStoreInstToConstLoc(RI, 32, getOp1(I), &LR);
			regNormalRegClear(RI, I);
			break;
		}
		case StoreCTR: {
			regStoreInstToConstLoc(RI, 32, getOp1(I), &CTR);
			regNormalRegClear(RI, I);
			break;
		}
		case StoreMSR: {
			unsigned InstLoc = ibuild->GetImmValue(getOp2(I));
			regStoreInstToConstLoc(RI, 32, getOp1(I), &MSR);
			regNormalRegClear(RI, I);

			// If some exceptions are pending and EE are now enabled, force checking
			// external exceptions when going out of mtmsr in order to execute delayed
			// interrupts as soon as possible.
			Jit->MOV(32, R(EAX), M(&MSR));
			Jit->TEST(32, R(EAX), Imm32(0x8000));
			FixupBranch eeDisabled = Jit->J_CC(CC_Z);

			Jit->MOV(32, R(EAX), M((void*)&PowerPC::ppcState.Exceptions));
			Jit->TEST(32, R(EAX), R(EAX));
			FixupBranch noExceptionsPending = Jit->J_CC(CC_Z);

			Jit->MOV(32, M(&PC), Imm32(InstLoc + 4));
			Jit->WriteExceptionExit(); // TODO: Implement WriteExternalExceptionExit for JitIL

			Jit->SetJumpTarget(eeDisabled);
			Jit->SetJumpTarget(noExceptionsPending);
			break;
		}
		case StoreGQR: {
			unsigned gqr = *I >> 16;
			regStoreInstToConstLoc(RI, 32, getOp1(I), &GQR(gqr));
			regNormalRegClear(RI, I);
			break;
		}
		case StoreSRR: {
			unsigned srr = *I >> 16;
			regStoreInstToConstLoc(RI, 32, getOp1(I),
					&PowerPC::ppcState.spr[SPR_SRR0+srr]);
			regNormalRegClear(RI, I);
			break;
		}
		case StoreCarry: {
			Jit->CMP(32, regLocForInst(RI, getOp1(I)), Imm8(0));
			FixupBranch nocarry = Jit->J_CC(CC_Z);
			Jit->JitSetCA();
			FixupBranch cont = Jit->J();
			Jit->SetJumpTarget(nocarry);
			Jit->JitClearCA();
			Jit->SetJumpTarget(cont);
			regNormalRegClear(RI, I);
			break;
		}
		case StoreFPRF: {
			Jit->MOV(32, R(ECX), regLocForInst(RI, getOp1(I)));
			Jit->AND(32, R(ECX), Imm8(0x1F));
			Jit->SHL(32, R(ECX), Imm8(12));
			Jit->AND(32, M(&FPSCR), Imm32(~(0x1F << 12)));
			Jit->OR(32, M(&FPSCR), R(ECX));
			regNormalRegClear(RI, I);
			break;
		}
		case Load8: {
			regEmitMemLoad(RI, I, 8);
			break;
		}
		case Load16: {
			regEmitMemLoad(RI, I, 16);
			break;
		}
		case Load32: {
			regEmitMemLoad(RI, I, 32);
			break;
		}
		case Store8: {
			regEmitMemStore(RI, I, 8);
			break;
		}
		case Store16: {
			regEmitMemStore(RI, I, 16);
			break;
		}
		case Store32: {
			regEmitMemStore(RI, I, 32);
			break;
		}
		case SExt8: {
			if (!thisUsed) break;
			X64Reg reg = regUReg(RI, I);
			Jit->MOV(32, R(ECX), regLocForInst(RI, getOp1(I)));
			Jit->MOVSX(32, 8, reg, R(ECX));
			RI.regs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case SExt16: {
			if (!thisUsed) break;
			X64Reg reg = regUReg(RI, I);
			Jit->MOVSX(32, 16, reg, regLocForInst(RI, getOp1(I)));
			RI.regs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case Cntlzw: {
			if (!thisUsed) break;
			X64Reg reg = regUReg(RI, I);
			Jit->MOV(32, R(ECX), Imm32(63));
			Jit->BSR(32, reg, regLocForInst(RI, getOp1(I)));
			Jit->CMOVcc(32, reg, R(ECX), CC_Z);
			Jit->XOR(32, R(reg), Imm8(31));
			RI.regs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case Not: {
			if (!thisUsed) break;
			X64Reg reg = regBinLHSReg(RI, I);
			Jit->NOT(32, R(reg));
			RI.regs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case And: {
			if (!thisUsed) break;
			regEmitBinInst(RI, I, &JitIL::AND, true);
			break;
		}
		case Xor: {
			if (!thisUsed) break;
			regEmitBinInst(RI, I, &JitIL::XOR, true);
			break;
		}
		case Sub: {
			if (!thisUsed) break;
			regEmitBinInst(RI, I, &JitIL::SUB);
			break;
		}
		case Or: {
			if (!thisUsed) break;
			regEmitBinInst(RI, I, &JitIL::OR, true);
			break;
		}
		case Add: {
			if (!thisUsed) break;
			regEmitBinInst(RI, I, &JitIL::ADD, true);
			break;
		}
		case Mul: {
			if (!thisUsed) break;
			// FIXME: Use three-address capability of IMUL!
			X64Reg reg = regBinLHSReg(RI, I);
			if (isImm(*getOp2(I))) {
				unsigned RHS = RI.Build->GetImmValue(getOp2(I));
				if (RHS + 128 < 256) {
					Jit->IMUL(32, reg, Imm8(RHS));
				} else {
					Jit->IMUL(32, reg, Imm32(RHS));
				}
			} else {
				Jit->IMUL(32, reg, regLocForInst(RI, getOp2(I)));
			}
			RI.regs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case MulHighUnsigned: {
			if (!thisUsed) break;
			regSpill(RI, EAX);
			regSpill(RI, EDX);
			X64Reg reg = regBinReg(RI, I);
			if (isImm(*getOp2(I))) {
				unsigned RHS = RI.Build->GetImmValue(getOp2(I));
				Jit->MOV(32, R(EAX), Imm32(RHS));
			} else {
				Jit->MOV(32, R(EAX), regLocForInst(RI, getOp2(I)));
			}
			Jit->MUL(32, regLocForInst(RI, getOp1(I)));
			Jit->MOV(32, R(reg), R(EDX));
			RI.regs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case Rol: {
			if (!thisUsed) break;
			regEmitShiftInst(RI, I, &JitIL::ROL);
			break;
		}
		case Shl: {
			if (!thisUsed) break;
			regEmitShiftInst(RI, I, &JitIL::SHL);
			break;
		}
		case Shrl: {
			if (!thisUsed) break;
			regEmitShiftInst(RI, I, &JitIL::SHR);
			break;
		}
		case Sarl: {
			if (!thisUsed) break;
			regEmitShiftInst(RI, I, &JitIL::SAR);
			break;
		}
		case ICmpEq: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_E);
			break;
		}
		case ICmpNe: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_NE);
			break;
		}
		case ICmpUgt: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_A);
			break;
		}
		case ICmpUlt: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_B);
			break;
		}
		case ICmpUge: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_AE);
			break;
		}
		case ICmpUle: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_BE);
			break;
		}
		case ICmpSgt: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_G);
			break;
		}
		case ICmpSlt: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_L);
			break;
		}
		case ICmpSge: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_GE);
			break;
		}
		case ICmpSle: {
			if (!thisUsed) break;
			regEmitICmpInst(RI, I, CC_LE);
			break;
		}
		case ICmpCRUnsigned: {
			if (!thisUsed) break;
			regEmitCmp(RI, I);
			X64Reg reg = regBinReg(RI, I);
			FixupBranch pLesser  = Jit->J_CC(CC_B);
			FixupBranch pGreater = Jit->J_CC(CC_A);
			Jit->MOV(32, R(reg), Imm32(0x2)); // _x86Reg == 0
			FixupBranch continue1 = Jit->J();
			Jit->SetJumpTarget(pGreater);
			Jit->MOV(32, R(reg), Imm32(0x4)); // _x86Reg > 0
			FixupBranch continue2 = Jit->J();
			Jit->SetJumpTarget(pLesser);
			Jit->MOV(32, R(reg), Imm32(0x8)); // _x86Reg < 0
			Jit->SetJumpTarget(continue1);
			Jit->SetJumpTarget(continue2);
			RI.regs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case ICmpCRSigned: {
			if (!thisUsed) break;
			regEmitCmp(RI, I);
			X64Reg reg = regBinReg(RI, I);
			FixupBranch pLesser  = Jit->J_CC(CC_L);
			FixupBranch pGreater = Jit->J_CC(CC_G);
			Jit->MOV(32, R(reg), Imm32(0x2)); // _x86Reg == 0
			FixupBranch continue1 = Jit->J();
			Jit->SetJumpTarget(pGreater);
			Jit->MOV(32, R(reg), Imm32(0x4)); // _x86Reg > 0
			FixupBranch continue2 = Jit->J();
			Jit->SetJumpTarget(pLesser);
			Jit->MOV(32, R(reg), Imm32(0x8)); // _x86Reg < 0
			Jit->SetJumpTarget(continue1);
			Jit->SetJumpTarget(continue2);
			RI.regs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case LoadSingle: {
			if (!thisUsed) break;
			X64Reg reg = fregFindFreeReg(RI);
			Jit->MOV(32, R(ECX), regLocForInst(RI, getOp1(I)));
			RI.Jit->UnsafeLoadRegToReg(ECX, ECX, 32, 0, false);
			Jit->MOVD_xmm(reg, R(ECX));
			RI.fregs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case LoadDouble: {
			if (!thisUsed) break;
			X64Reg reg = fregFindFreeReg(RI);
			if (cpu_info.bSSSE3) {
				static const u32 GC_ALIGNED16(maskSwapa64_1[4]) = 
				{0x04050607L, 0x00010203L, 0xFFFFFFFFL, 0xFFFFFFFFL};
#ifdef _M_X64
				// TODO: Remove regEnsureInReg() and use ECX
				X64Reg address = regEnsureInReg(RI, getOp1(I));
				Jit->MOVQ_xmm(reg, MComplex(RBX, address, SCALE_1, 0));
#else
				X64Reg address = regBinLHSReg(RI, I);
				Jit->AND(32, R(address), Imm32(Memory::MEMVIEW32_MASK));
				Jit->MOVQ_xmm(reg, MDisp(address, (u32)Memory::base));
#endif
				Jit->PSHUFB(reg, M((void*)maskSwapa64_1));
			} else {
				const OpArg loc = regLocForInst(RI, getOp1(I));
				Jit->MOV(32, R(ECX), loc);
				Jit->ADD(32, R(ECX), Imm8(4));
				RI.Jit->UnsafeLoadRegToReg(ECX, ECX, 32, 0, false);
				Jit->MOVD_xmm(reg, R(ECX));
				Jit->MOV(32, R(ECX), loc);
				RI.Jit->UnsafeLoadRegToReg(ECX, ECX, 32, 0, false);
				Jit->MOVD_xmm(XMM0, R(ECX));
				Jit->PUNPCKLDQ(reg, R(XMM0));
			}
			RI.fregs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case LoadPaired: {
			if (!thisUsed) break;
			regSpill(RI, EAX);
			regSpill(RI, EDX);
			X64Reg reg = fregFindFreeReg(RI);
			// The lower 3 bits is for GQR index. The next 1 bit is for inst.W
			unsigned int quantreg = (*I >> 16) & 0x7;
			unsigned int w = *I >> 19;
			Jit->MOVZX(32, 16, EAX, M(((char *)&GQR(quantreg)) + 2));
			Jit->MOVZX(32, 8, EDX, R(AL));
			Jit->OR(32, R(EDX), Imm8(w << 3));
#ifdef _M_IX86
			int addr_scale = SCALE_4;
#else
			int addr_scale = SCALE_8;
#endif
			Jit->MOV(32, R(ECX), regLocForInst(RI, getOp1(I)));
			Jit->ABI_AlignStack(0);
			Jit->CALLptr(MScaled(EDX, addr_scale, (u32)(u64)(((JitIL *)jit)->asm_routines.pairedLoadQuantized)));
			Jit->ABI_RestoreStack(0);
			Jit->MOVAPD(reg, R(XMM0));
			RI.fregs[reg] = I;
			regNormalRegClear(RI, I);
			break;
		}
		case StoreSingle: {
			regSpill(RI, EAX);
			const OpArg loc1 = fregLocForInst(RI, getOp1(I));
			if (loc1.IsSimpleReg()) {
				Jit->MOVD_xmm(R(EAX), loc1.GetSimpleReg());
			} else {
				Jit->MOV(32, R(EAX), loc1);
			}
			Jit->MOV(32, R(ECX), regLocForInst(RI, getOp2(I)));
			RI.Jit->SafeWriteRegToReg(EAX, ECX, 32, 0);
			if (RI.IInfo[I - RI.FirstI] & 4)
				fregClearInst(RI, getOp1(I));
			if (RI.IInfo[I - RI.FirstI] & 8)
				regClearInst(RI, getOp2(I));
			break;
		}
		case StoreDouble: {
			regSpill(RI, EAX);
			// Please fix the following code
			// if SafeWriteRegToReg() is modified.
			u32 mem_mask = Memory::ADDR_MASK_HW_ACCESS;
			if (Core::g_CoreStartupParameter.bMMU ||
				Core::g_CoreStartupParameter.iTLBHack) {
				mem_mask |= Memory::ADDR_MASK_MEM1;
			}

			if (Jit->jo.debug)
			{
				mem_mask |= Memory::EXRAM_MASK;
			}

			Jit->TEST(32, regLocForInst(RI, getOp2(I)), Imm32(mem_mask));
			FixupBranch safe = Jit->J_CC(CC_NZ);
				// Fast routine
				if (cpu_info.bSSSE3) {
					static const u32 GC_ALIGNED16(maskSwapa64_1[4]) = 
					{0x04050607L, 0x00010203L, 0xFFFFFFFFL, 0xFFFFFFFFL};

					X64Reg value = fregBinLHSRegWithMov(RI, I);
					Jit->PSHUFB(value, M((void*)maskSwapa64_1));
					Jit->MOV(32, R(ECX), regLocForInst(RI, getOp2(I)));
#ifdef _M_X64
					Jit->MOVQ_xmm(MComplex(RBX, ECX, SCALE_1, 0), value);
#else
					Jit->AND(32, R(ECX), Imm32(Memory::MEMVIEW32_MASK));
					Jit->MOVQ_xmm(MDisp(ECX, (u32)Memory::base), value);
#endif
				} else {
					regSpill(RI, EAX);
					OpArg loc = fregLocForInst(RI, getOp1(I));
					if (!loc.IsSimpleReg() || !(RI.IInfo[I - RI.FirstI] & 4)) {
						Jit->MOVAPD(XMM0, loc);
						loc = R(XMM0);
					}
					Jit->MOVD_xmm(R(EAX), loc.GetSimpleReg());
					Jit->MOV(32, R(ECX), regLocForInst(RI, getOp2(I)));
					RI.Jit->UnsafeWriteRegToReg(EAX, ECX, 32, 4);

					Jit->PSRLQ(loc.GetSimpleReg(), 32);
					Jit->MOVD_xmm(R(EAX), loc.GetSimpleReg());
					Jit->MOV(32, R(ECX), regLocForInst(RI, getOp2(I)));
					RI.Jit->UnsafeWriteRegToReg(EAX, ECX, 32, 0);
				}
			FixupBranch exit = Jit->J(true);
			Jit->SetJumpTarget(safe);
				// Safe but slow routine
				OpArg value = fregLocForInst(RI, getOp1(I));
				OpArg address = regLocForInst(RI, getOp2(I));
				Jit->MOVAPD(XMM0, value);
				Jit->PSRLQ(XMM0, 32);
				Jit->MOVD_xmm(R(EAX), XMM0);
				Jit->MOV(32, R(ECX), address);
				RI.Jit->SafeWriteRegToReg(EAX, ECX, 32, 0);

				Jit->MOVAPD(XMM0, value);
				Jit->MOVD_xmm(R(EAX), XMM0);
				Jit->MOV(32, R(ECX), address);
				RI.Jit->SafeWriteRegToReg(EAX, ECX, 32, 4);
			Jit->SetJumpTarget(exit);

			if (RI.IInfo[I - RI.FirstI] & 4)
				fregClearInst(RI, getOp1(I));
			if (RI.IInfo[I - RI.FirstI] & 8)
				regClearInst(RI, getOp2(I));
			break;
		}
		case StorePaired: {
			regSpill(RI, EAX);
			regSpill(RI, EDX);
			u32 quantreg = *I >> 24;
			Jit->MOVZX(32, 16, EAX, M(&PowerPC::ppcState.spr[SPR_GQR0 + quantreg]));
			Jit->MOVZX(32, 8, EDX, R(AL));
#ifdef _M_IX86
			int addr_scale = SCALE_4;
#else
			int addr_scale = SCALE_8;
#endif
			Jit->MOV(32, R(ECX), regLocForInst(RI, getOp2(I)));
			Jit->MOVAPD(XMM0, fregLocForInst(RI, getOp1(I)));
			Jit->ABI_AlignStack(0);
			Jit->CALLptr(MScaled(EDX, addr_scale, (u32)(u64)(((JitIL *)jit)->asm_routines.pairedStoreQuantized)));
			Jit->ABI_RestoreStack(0);
			if (RI.IInfo[I - RI.FirstI] & 4)
				fregClearInst(RI, getOp1(I));
			if (RI.IInfo[I - RI.FirstI] & 8)
				regClearInst(RI, getOp2(I));
			break;
		}
		case DupSingleToMReg: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithoutMov(RI, I);
			Jit->CVTSS2SD(reg, fregLocForInst(RI, getOp1(I)));
			Jit->MOVDDUP(reg, R(reg));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case InsertDoubleInMReg: {
			if (!thisUsed) break;
			// r[0] = op1[0]; r[1] = op2[1];

			// TODO: Optimize the case that the register of op1 can be
			//       recycled. (SHUFPD may not be so fast.)
			X64Reg reg = fregBinRHSRegWithMov(RI, I);
			OpArg loc1 = fregLocForInst(RI, getOp1(I));
			if (loc1.IsSimpleReg()) {
				Jit->MOVSD(reg, loc1);
			} else {
				// If op1 is in FSlotSet, we have to mov loc1 to XMM0
				// before MOVSD/MOVSS.
				// Because register<->memory transfer with MOVSD/MOVSS
				// clears upper 64/96-bits of the destination register.
				Jit->MOVAPD(XMM0, loc1);
				Jit->MOVSD(reg, R(XMM0));
			}
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case ExpandPackedToMReg: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithoutMov(RI, I);
			Jit->CVTPS2PD(reg, fregLocForInst(RI, getOp1(I)));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case CompactMRegToPacked: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithoutMov(RI, I);
			Jit->CVTPD2PS(reg, fregLocForInst(RI, getOp1(I)));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FSNeg: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithMov(RI, I);
			static const u32 GC_ALIGNED16(ssSignBits[4]) = 
				{0x80000000};
			Jit->PXOR(reg, M((void*)&ssSignBits));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FDNeg: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithMov(RI, I);
			static const u64 GC_ALIGNED16(ssSignBits[2]) = 
				{0x8000000000000000ULL};
			Jit->PXOR(reg, M((void*)&ssSignBits));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FPNeg: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithMov(RI, I);
			static const u32 GC_ALIGNED16(psSignBits[4]) = 
				{0x80000000, 0x80000000};
			Jit->PXOR(reg, M((void*)&psSignBits));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FPDup0: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithMov(RI, I);
			Jit->PUNPCKLDQ(reg, R(reg));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FPDup1: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithMov(RI, I);
			Jit->SHUFPS(reg, R(reg), 0xE5);
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case LoadFReg: {
			if (!thisUsed) break;
			X64Reg reg = fregFindFreeReg(RI);
			unsigned ppcreg = *I >> 8;
			Jit->MOVAPD(reg, M(&PowerPC::ppcState.ps[ppcreg]));
			RI.fregs[reg] = I;
			break;
		}	    
		case LoadFRegDENToZero: {
			if (!thisUsed) break;
			X64Reg reg = fregFindFreeReg(RI);
			unsigned ppcreg = *I >> 8;
			char *p = (char*)&(PowerPC::ppcState.ps[ppcreg][0]);
			Jit->MOV(32, R(ECX), M(p+4));
			Jit->AND(32, R(ECX), Imm32(0x7ff00000));
			Jit->CMP(32, R(ECX), Imm32(0x38000000));
			FixupBranch ok = Jit->J_CC(CC_AE);
			Jit->AND(32, M(p+4), Imm32(0x80000000));
			Jit->MOV(32, M(p), Imm32(0));
			Jit->SetJumpTarget(ok);
			Jit->MOVAPD(reg, M(&PowerPC::ppcState.ps[ppcreg]));
			RI.fregs[reg] = I;
			break;
		}
		case StoreFReg: {
			unsigned ppcreg = *I >> 16;
			Jit->MOVAPD(M(&PowerPC::ppcState.ps[ppcreg]),
				      fregEnsureInReg(RI, getOp1(I)));
			fregNormalRegClear(RI, I);
			break;
		}
		case DoubleToSingle: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithoutMov(RI, I);
			Jit->CVTSD2SS(reg, fregLocForInst(RI, getOp1(I)));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FSMul: {
			if (!thisUsed) break;
			fregEmitBinInst(RI, I, &JitIL::MULSS);
			break;
		}
		case FSAdd: {
			if (!thisUsed) break;
			fregEmitBinInst(RI, I, &JitIL::ADDSS);
			break;
		}
		case FSSub: {
			if (!thisUsed) break;
			fregEmitBinInst(RI, I, &JitIL::SUBSS);
			break;
		}
		case FSRSqrt: {
			if (!thisUsed) break;
			X64Reg reg = fregURegWithoutMov(RI, I);
			Jit->RSQRTSS(reg, fregLocForInst(RI, getOp1(I)));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FDMul: {
			if (!thisUsed) break;
			fregEmitBinInst(RI, I, &JitIL::MULSD);
			break;
		}
		case FDAdd: {
			if (!thisUsed) break;
			fregEmitBinInst(RI, I, &JitIL::ADDSD);
			break;
		}
		case FDSub: {
			if (!thisUsed) break;
			fregEmitBinInst(RI, I, &JitIL::SUBSD);
			break;
		}
		case FDCmpCR: {
			const u32 ordered = *I >> 24;
			X64Reg destreg = regFindFreeReg(RI);
			// TODO: Remove an extra MOVSD if loc1.IsSimpleReg()
			OpArg loc1 = fregLocForInst(RI, getOp1(I));
			OpArg loc2 = fregLocForInst(RI, getOp2(I));
			Jit->MOVSD(XMM0, loc1);
			Jit->UCOMISD(XMM0, loc2);
			FixupBranch pNan     = Jit->J_CC(CC_P);
			FixupBranch pEqual   = Jit->J_CC(CC_Z);
			FixupBranch pLesser  = Jit->J_CC(CC_C);
			// Greater
			Jit->MOV(32, R(destreg), Imm32(0x4));
			FixupBranch continue1 = Jit->J();
			// NaN
			Jit->SetJumpTarget(pNan);
			Jit->MOV(32, R(destreg), Imm32(0x1));

			static const u32 FPSCR_VE = (u32)1 << (31 - 24);
			static const u32 FPSCR_VXVC = (u32)1 << (31 - 12);
			static const u32 FPSCR_VXSNAN = (u32)1 << (31 - 7);
			static const u32 FPSCR_FX = (u32)1 << (31 - 0);

			if (ordered) {
				// fcmpo
				// TODO: Optimize the following code if slow.
				//       SNAN check may not be needed
				//       because it does not happen so much.
				Jit->MOVSD(M(isSNANTemp[0]), XMM0);
				if (loc2.IsSimpleReg()) {
					Jit->MOVSD(M(isSNANTemp[1]), loc2.GetSimpleReg());
				} else {
					Jit->MOVSD(XMM0, loc2);
					Jit->MOVSD(M(isSNANTemp[1]), XMM0);
				}
				Jit->ABI_CallFunction((void*)checkIsSNAN);
				Jit->TEST(8, R(EAX), R(EAX));
				FixupBranch ok = Jit->J_CC(CC_Z);
					Jit->OR(32, M(&FPSCR), Imm32(FPSCR_FX)); // FPSCR.FX = 1;
					Jit->OR(32, M(&FPSCR), Imm32(FPSCR_VXSNAN)); // FPSCR.Hex |= mask;
					Jit->TEST(32, M(&FPSCR), Imm32(FPSCR_VE));
					FixupBranch finish0 = Jit->J_CC(CC_NZ);
						Jit->OR(32, M(&FPSCR), Imm32(FPSCR_VXVC)); // FPSCR.Hex |= mask;
						FixupBranch finish1 = Jit->J();
				Jit->SetJumpTarget(ok);
					Jit->OR(32, M(&FPSCR), Imm32(FPSCR_FX)); // FPSCR.FX = 1;
					Jit->OR(32, M(&FPSCR), Imm32(FPSCR_VXVC)); // FPSCR.Hex |= mask;
				Jit->SetJumpTarget(finish0);
				Jit->SetJumpTarget(finish1);
			} else {
				// fcmpu
				// TODO: Optimize the following code if slow
				Jit->MOVSD(M(isSNANTemp[0]), XMM0);
				if (loc2.IsSimpleReg()) {
					Jit->MOVSD(M(isSNANTemp[1]), loc2.GetSimpleReg());
				} else {
					Jit->MOVSD(XMM0, loc2);
					Jit->MOVSD(M(isSNANTemp[1]), XMM0);
				}
				Jit->ABI_CallFunction((void*)checkIsSNAN);
				Jit->TEST(8, R(EAX), R(EAX));
				FixupBranch finish = Jit->J_CC(CC_Z);
					Jit->OR(32, M(&FPSCR), Imm32(FPSCR_FX)); // FPSCR.FX = 1;
					Jit->OR(32, M(&FPSCR), Imm32(FPSCR_VXVC)); // FPSCR.Hex |= mask;
				Jit->SetJumpTarget(finish);
			}

			FixupBranch continue2 = Jit->J();
			// Equal
			Jit->SetJumpTarget(pEqual);
			Jit->MOV(32, R(destreg), Imm32(0x2));
			FixupBranch continue3 = Jit->J();
			// Less
			Jit->SetJumpTarget(pLesser);
			Jit->MOV(32, R(destreg), Imm32(0x8));
			Jit->SetJumpTarget(continue1);
			Jit->SetJumpTarget(continue2);
			Jit->SetJumpTarget(continue3);
			RI.regs[destreg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FPAdd: {
			if (!thisUsed) break;
			fregEmitBinInst(RI, I, &JitIL::ADDPS);
			break;
		}
		case FPMul: {
			if (!thisUsed) break;
			fregEmitBinInst(RI, I, &JitIL::MULPS);
			break;
		}
		case FPSub: {
			if (!thisUsed) break;
			fregEmitBinInst(RI, I, &JitIL::SUBPS);
			break;
		}
		case FPMerge00: {
			// r[0] = op1[0]; r[1] = op2[0];
			if (!thisUsed) break;
			// TODO: Optimize the case that the register of only op2 can be
			//       recycled.
			X64Reg reg = fregBinLHSRegWithMov(RI, I);
			Jit->PUNPCKLDQ(reg, fregLocForInst(RI, getOp2(I)));
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FPMerge01: {
			// r[0] = op1[0]; r[1] = op2[1];
			if (!thisUsed) break;
			// TODO: Optimize the case that the register of only op1 can be
			//       recycled.
			X64Reg reg = fregBinRHSRegWithMov(RI, I);
			OpArg loc1 = fregLocForInst(RI, getOp1(I));
			if (loc1.IsSimpleReg()) {
				Jit->MOVSS(reg, loc1);
			} else {
				Jit->MOVAPD(XMM0, loc1);
				Jit->MOVSS(reg, R(XMM0));
			}
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FPMerge10: {
			// r[0] = op1[1]; r[1] = op2[0];
			if (!thisUsed) break;
			// TODO: Optimize the case that the register of only op2 can be
			//       recycled.
			X64Reg reg = fregBinLHSRegWithMov(RI, I);
			OpArg loc2 = fregLocForInst(RI, getOp2(I));
			if (loc2.IsSimpleReg()) {
				Jit->MOVSS(reg, loc2);
			} else {
				Jit->MOVAPD(XMM0, loc2);
				Jit->MOVSS(reg, R(XMM0));
			}
			Jit->SHUFPS(reg, R(reg), 0xF1);
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case FPMerge11: {
			// r[0] = op1[1]; r[1] = op2[1];
			if (!thisUsed) break;
			// TODO: Optimize the case that the register of only op2 can be
			//       recycled.
			X64Reg reg = fregBinLHSRegWithMov(RI, I);
			// TODO: Check whether the following code works
			//       when the op1 is in the FSlotSet
			Jit->PUNPCKLDQ(reg, fregLocForInst(RI, getOp2(I)));
			Jit->SHUFPD(reg, R(reg), 0x1);
			RI.fregs[reg] = I;
			fregNormalRegClear(RI, I);
			break;
		}
		case CInt32:
		case CInt16: {
			if (!thisUsed) break;
			X64Reg reg = regFindFreeReg(RI);
			Jit->MOV(32, R(reg), Imm32(ibuild->GetImmValue(I)));
			RI.regs[reg] = I;
			break;
		}
		case BlockStart:
		case BlockEnd:
			break;

		case IdleBranch: {			
			Jit->CMP(32, regLocForInst(RI, getOp1(getOp1(I))),
					 Imm32(RI.Build->GetImmValue(getOp2(getOp1(I)))));			
			FixupBranch cont = Jit->J_CC(CC_NE);

			RI.Jit->Cleanup(); // is it needed?			
			Jit->ABI_CallFunction((void *)&PowerPC::OnIdleIL);
			
			Jit->MOV(32, M(&PC), Imm32(ibuild->GetImmValue( getOp2(I) )));
			Jit->JMP(((JitIL *)jit)->asm_routines.testExceptions, true);

			Jit->SetJumpTarget(cont);
			if (RI.IInfo[I - RI.FirstI] & 4)
					regClearInst(RI, getOp1(getOp1(I)));
			if (RI.IInfo[I - RI.FirstI] & 8)
				regClearInst(RI, getOp2(I));
			break;
		}

		case BranchCond: {
			if (isICmp(*getOp1(I)) &&
			    isImm(*getOp2(getOp1(I)))) {
				Jit->CMP(32, regLocForInst(RI, getOp1(getOp1(I))),
					 Imm32(RI.Build->GetImmValue(getOp2(getOp1(I)))));
				CCFlags flag;
				switch (getOpcode(*getOp1(I))) {
					case ICmpEq: flag = CC_NE; break;
					case ICmpNe: flag = CC_E; break;
					case ICmpUgt: flag = CC_BE; break;
					case ICmpUlt: flag = CC_AE; break;
					case ICmpUge: flag = CC_L; break;
					case ICmpUle: flag = CC_A; break;
					case ICmpSgt: flag = CC_LE; break;
					case ICmpSlt: flag = CC_GE; break;
					case ICmpSge: flag = CC_L; break;
					case ICmpSle: flag = CC_G; break;
					default: PanicAlert("cmpXX"); flag = CC_O; break;
				}
				FixupBranch cont = Jit->J_CC(flag);
				regWriteExit(RI, getOp2(I));
				Jit->SetJumpTarget(cont);
				if (RI.IInfo[I - RI.FirstI] & 4)
					regClearInst(RI, getOp1(getOp1(I)));
			} else {
				Jit->CMP(32, regLocForInst(RI, getOp1(I)), Imm8(0));
				FixupBranch cont = Jit->J_CC(CC_Z);
				regWriteExit(RI, getOp2(I));
				Jit->SetJumpTarget(cont);
				if (RI.IInfo[I - RI.FirstI] & 4)
					regClearInst(RI, getOp1(I));
			}
			if (RI.IInfo[I - RI.FirstI] & 8)
				regClearInst(RI, getOp2(I));
			break;
		}
		case BranchUncond: {
			regWriteExit(RI, getOp1(I));
			regNormalRegClear(RI, I);
			break;
		}		
		case ShortIdleLoop: {
			unsigned InstLoc = ibuild->GetImmValue(getOp1(I));
			Jit->ABI_CallFunction((void *)&CoreTiming::Idle);
			Jit->MOV(32, M(&PC), Imm32(InstLoc));
			Jit->JMP(((JitIL *)jit)->asm_routines.testExceptions, true);
			break;
		}
		case SystemCall: {
			unsigned InstLoc = ibuild->GetImmValue(getOp1(I));
			Jit->LOCK();
			Jit->OR(32, M((void *)&PowerPC::ppcState.Exceptions), Imm32(EXCEPTION_SYSCALL));
			Jit->MOV(32, M(&PC), Imm32(InstLoc + 4));
			Jit->WriteExceptionExit();
			break;
		}
		case InterpreterBranch: {
			Jit->MOV(32, R(EAX), M(&NPC));
			Jit->WriteExitDestInOpArg(R(EAX));
			break;
		}
		case RFIExit: {
			// See Interpreter rfi for details
			const u32 mask = 0x87C0FFFF;
			// MSR = (MSR & ~mask) | (SRR1 & mask);
			Jit->MOV(32, R(EAX), M(&MSR));
			Jit->MOV(32, R(ECX), M(&SRR1));
			Jit->AND(32, R(EAX), Imm32(~mask));
			Jit->AND(32, R(ECX), Imm32(mask));
			Jit->OR(32, R(EAX), R(ECX));       
			// MSR &= 0xFFFBFFFF; // Mask used to clear the bit MSR[13]
			Jit->AND(32, R(EAX), Imm32(0xFFFBFFFF));
			Jit->MOV(32, M(&MSR), R(EAX));
			// NPC = SRR0; 
			Jit->MOV(32, R(EAX), M(&SRR0));
			Jit->WriteRfiExitDestInOpArg(R(EAX));
			break;
		}
		case FPExceptionCheck: {
			unsigned InstLoc = ibuild->GetImmValue(getOp1(I));
			//This instruction uses FPU - needs to add FP exception bailout
			Jit->TEST(32, M(&PowerPC::ppcState.msr), Imm32(1 << 13)); // Test FP enabled bit
			FixupBranch b1 = Jit->J_CC(CC_NZ);

			// If a FPU exception occurs, the exception handler will read
			// from PC.  Update PC with the latest value in case that happens.
			Jit->MOV(32, M(&PC), Imm32(InstLoc));
			Jit->SUB(32, M(&CoreTiming::downcount), Jit->js.downcountAmount > 127 ? Imm32(Jit->js.downcountAmount) : Imm8(Jit->js.downcountAmount)); 
			Jit->JMP(Jit->asm_routines.fpException, true);
			Jit->SetJumpTarget(b1);
			break;
		}
		case DSIExceptionCheck: {
			unsigned InstLoc = ibuild->GetImmValue(getOp1(I));
			Jit->TEST(32, M((void *)&PowerPC::ppcState.Exceptions), Imm32(EXCEPTION_DSI));
			FixupBranch noMemException = Jit->J_CC(CC_Z);

			// If a memory exception occurs, the exception handler will read
			// from PC.  Update PC with the latest value in case that happens.
			Jit->MOV(32, M(&PC), Imm32(InstLoc));
			Jit->WriteExceptionExit();
			Jit->SetJumpTarget(noMemException);
			break;
		}
		case ISIException: {
			unsigned InstLoc = ibuild->GetImmValue(getOp1(I));

			// Address of instruction could not be translated
			Jit->MOV(32, M(&NPC), Imm32(InstLoc));
			Jit->OR(32, M((void *)&PowerPC::ppcState.Exceptions), Imm32(EXCEPTION_ISI));

			// Remove the invalid instruction from the icache, forcing a recompile
#ifdef _M_IX86
			if (InstLoc & JIT_ICACHE_VMEM_BIT)
				Jit->MOV(32, M((jit->GetBlockCache()->GetICacheVMEM() + (InstLoc & JIT_ICACHE_MASK))), Imm32(JIT_ICACHE_INVALID_WORD));
			else if (InstLoc & JIT_ICACHE_EXRAM_BIT)
				Jit->MOV(32, M((jit->GetBlockCache()->GetICacheEx() + (InstLoc & JIT_ICACHEEX_MASK))), Imm32(JIT_ICACHE_INVALID_WORD));
			else
				Jit->MOV(32, M((jit->GetBlockCache()->GetICache() + (InstLoc & JIT_ICACHE_MASK))), Imm32(JIT_ICACHE_INVALID_WORD));

#else
			if (InstLoc & JIT_ICACHE_VMEM_BIT)
				Jit->MOV(64, R(RAX), ImmPtr(jit->GetBlockCache()->GetICacheVMEM() + (InstLoc & JIT_ICACHE_MASK)));
			else if (InstLoc & JIT_ICACHE_EXRAM_BIT)
				Jit->MOV(64, R(RAX), ImmPtr(jit->GetBlockCache()->GetICacheEx() + (InstLoc & JIT_ICACHEEX_MASK)));
			else
				Jit->MOV(64, R(RAX), ImmPtr(jit->GetBlockCache()->GetICache() + (InstLoc & JIT_ICACHE_MASK)));
			Jit->MOV(32, MatR(RAX), Imm32(JIT_ICACHE_INVALID_WORD));
#endif
			Jit->WriteExceptionExit();
			break;
		}
		case ExtExceptionCheck: {
			unsigned InstLoc = ibuild->GetImmValue(getOp1(I));

			Jit->TEST(32, M((void *)&PowerPC::ppcState.Exceptions), Imm32(EXCEPTION_ISI | EXCEPTION_PROGRAM | EXCEPTION_SYSCALL | EXCEPTION_FPU_UNAVAILABLE | EXCEPTION_DSI | EXCEPTION_ALIGNMENT));
			FixupBranch clearInt = Jit->J_CC(CC_NZ);
			Jit->TEST(32, M((void *)&PowerPC::ppcState.Exceptions), Imm32(EXCEPTION_EXTERNAL_INT));
			FixupBranch noExtException = Jit->J_CC(CC_Z);
			Jit->TEST(32, M((void *)&PowerPC::ppcState.msr), Imm32(0x0008000));
			FixupBranch noExtIntEnable = Jit->J_CC(CC_Z);
			Jit->TEST(32, M((void *)&ProcessorInterface::m_InterruptCause), Imm32(ProcessorInterface::INT_CAUSE_CP | ProcessorInterface::INT_CAUSE_PE_TOKEN | ProcessorInterface::INT_CAUSE_PE_FINISH));
			FixupBranch noCPInt = Jit->J_CC(CC_Z);

			Jit->MOV(32, M(&PC), Imm32(InstLoc));
			Jit->WriteExceptionExit();

			Jit->SetJumpTarget(noCPInt);
			Jit->SetJumpTarget(noExtIntEnable);
			Jit->SetJumpTarget(noExtException);
			Jit->SetJumpTarget(clearInt);
			break;
		}
		case BreakPointCheck: {
			unsigned InstLoc = ibuild->GetImmValue(getOp1(I));

			Jit->MOV(32, M(&PC), Imm32(InstLoc));
			Jit->ABI_CallFunction(reinterpret_cast<void *>(&PowerPC::CheckBreakPoints));
			Jit->TEST(32, M((void*)PowerPC::GetStatePtr()), Imm32(0xFFFFFFFF));
			FixupBranch noBreakpoint = Jit->J_CC(CC_Z);
			Jit->WriteExit(InstLoc, 0);
			Jit->SetJumpTarget(noBreakpoint);
			break;
		}
		case Int3: {
			Jit->INT3();
			break;
		}
		case Tramp: break;
		case Nop: break;
		default:
			PanicAlert("Unknown JIT instruction; aborting!");
			exit(1);
		}
	}

	for (unsigned i = 0; i < MAX_NUMBER_OF_REGS; i++) {
		if (RI.regs[i]) {
			// Start a game in Burnout 2 to get this. Or animal crossing.
			PanicAlert("Incomplete cleanup! (regs)");
			exit(1);
		}
		if (RI.fregs[i]) {
			PanicAlert("Incomplete cleanup! (fregs)");
			exit(1);
		}
	}

	//if (!RI.MakeProfile && RI.numSpills)
	//	printf("Block: %x, numspills %d\n", Jit->js.blockStart, RI.numSpills);
	
	Jit->WriteExit(jit->js.curBlock->exitAddress[0], 0);
	Jit->UD2();
}

void JitIL::WriteCode() {
	DoWriteCode(&ibuild, this, false, SConfig::GetInstance().m_LocalCoreStartupParameter.bJITProfiledReJIT);
}

void ProfiledReJit() {
	JitIL *jitil = (JitIL *)jit;
	jitil->SetCodePtr(jitil->js.rewriteStart);
	DoWriteCode(&jitil->ibuild, jitil, true, false);
	jitil->js.curBlock->codeSize = (int)(jitil->GetCodePtr() - jitil->js.rewriteStart);
	jitil->GetBlockCache()->FinalizeBlock(jitil->js.curBlock->blockNum, jitil->jo.enableBlocklink,
	jitil->js.curBlock->normalEntry);
}
