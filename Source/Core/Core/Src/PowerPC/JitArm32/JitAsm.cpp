// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/


#include "../../HW/Memmap.h"

#include "../PowerPC.h"
#include "../../CoreTiming.h"
#include "MemoryUtil.h"

#include "Jit.h"
#include "../JitCommon/JitCache.h"

#include "../../HW/GPFifo.h"
#include "../../Core.h"
#include "JitAsm.h"
#include "ArmEmitter.h"

using namespace ArmGen;

//TODO - make an option
//#if _DEBUG
//	bool enableDebug = false; 
//#else
//	bool enableDebug = false; 
//#endif

JitArmAsmRoutineManager asm_routines;

Operand2 arghmask(3, 3); // 0x0C000000 
Operand2 mask(3, 1); // ~(Memory::MEMVIEW32_MASK)

static const float GC_ALIGNED16(m_quantizeTableS[]) =
{
	(1 <<  0),	(1 <<  1),	(1 <<  2),	(1 <<  3),
	(1 <<  4),	(1 <<  5),	(1 <<  6),	(1 <<  7),
	(1 <<  8),	(1 <<  9),	(1 << 10),	(1 << 11),
	(1 << 12),	(1 << 13),	(1 << 14),	(1 << 15),
	(1 << 16),	(1 << 17),	(1 << 18),	(1 << 19),
	(1 << 20),	(1 << 21),	(1 << 22),	(1 << 23),
	(1 << 24),	(1 << 25),	(1 << 26),	(1 << 27),
	(1 << 28),	(1 << 29),	(1 << 30),	(1 << 31),
	1.0 / (1ULL << 32),	1.0 / (1 << 31),	1.0 / (1 << 30),	1.0 / (1 << 29),
	1.0 / (1 << 28),	1.0 / (1 << 27),	1.0 / (1 << 26),	1.0 / (1 << 25),
	1.0 / (1 << 24),	1.0 / (1 << 23),	1.0 / (1 << 22),	1.0 / (1 << 21),
	1.0 / (1 << 20),	1.0 / (1 << 19),	1.0 / (1 << 18),	1.0 / (1 << 17),
	1.0 / (1 << 16),	1.0 / (1 << 15),	1.0 / (1 << 14),	1.0 / (1 << 13),
	1.0 / (1 << 12),	1.0 / (1 << 11),	1.0 / (1 << 10),	1.0 / (1 <<  9),
	1.0 / (1 <<  8),	1.0 / (1 <<  7),	1.0 / (1 <<  6),	1.0 / (1 <<  5),
	1.0 / (1 <<  4),	1.0 / (1 <<  3),	1.0 / (1 <<  2),	1.0 / (1 <<  1),
}; 

static const float GC_ALIGNED16(m_dequantizeTableS[]) =
{
	1.0 / (1 <<  0),	1.0 / (1 <<  1),	1.0 / (1 <<  2),	1.0 / (1 <<  3),
	1.0 / (1 <<  4),	1.0 / (1 <<  5),	1.0 / (1 <<  6),	1.0 / (1 <<  7),
	1.0 / (1 <<  8),	1.0 / (1 <<  9),	1.0 / (1 << 10),	1.0 / (1 << 11),
	1.0 / (1 << 12),	1.0 / (1 << 13),	1.0 / (1 << 14),	1.0 / (1 << 15),
	1.0 / (1 << 16),	1.0 / (1 << 17),	1.0 / (1 << 18),	1.0 / (1 << 19),
	1.0 / (1 << 20),	1.0 / (1 << 21),	1.0 / (1 << 22),	1.0 / (1 << 23),
	1.0 / (1 << 24),	1.0 / (1 << 25),	1.0 / (1 << 26),	1.0 / (1 << 27),
	1.0 / (1 << 28),	1.0 / (1 << 29),	1.0 / (1 << 30),	1.0 / (1 << 31),
	(1ULL << 32),	(1 << 31),		(1 << 30),		(1 << 29),
	(1 << 28),		(1 << 27),		(1 << 26),		(1 << 25),
	(1 << 24),		(1 << 23),		(1 << 22),		(1 << 21),
	(1 << 20),		(1 << 19),		(1 << 18),		(1 << 17),
	(1 << 16),		(1 << 15),		(1 << 14),		(1 << 13),
	(1 << 12),		(1 << 11),		(1 << 10),		(1 <<  9),
	(1 <<  8),		(1 <<  7),		(1 <<  6),		(1 <<  5),
	(1 <<  4),		(1 <<  3),		(1 <<  2),		(1 <<  1),
}; 

static void WriteDual32(u32 value1, u32 value2, u32 address)
{
	Memory::Write_U32(value1, address);
	Memory::Write_U32(value2, address + 4);
}

static void WriteDual16(u32 value1, u32 value2, u32 address)
{
	Memory::Write_U16(value1, address);
	Memory::Write_U16(value2, address + 2);
}

static void WriteDual8(u32 value1, u32 value2, u32 address)
{
	Memory::Write_U8(value1, address);
	Memory::Write_U8(value2, address + 1);
}

void JitArmAsmRoutineManager::Generate()
{
	enterCode = GetCodePtr();
	PUSH(9, R4, R5, R6, R7, R8, R9, R10, R11, _LR);
	// Take care to 8-byte align stack for function calls.
	// We are misaligned here because of an odd number of args for PUSH.
	// It's not like x86 where you need to account for an extra 4 bytes
	// consumed by CALL.
	SUB(_SP, _SP, 4);

	MOVI2R(R0, (u32)&CoreTiming::downcount);
	MOVI2R(R9, (u32)&PowerPC::ppcState.spr[0]);

	FixupBranch skipToRealDispatcher = B();
	dispatcher = GetCodePtr();	
		printf("Dispatcher is %p\n", dispatcher);

		// Downcount Check	
		// The result of slice decrementation should be in flags if somebody jumped here
		// IMPORTANT - We jump on negative, not carry!!!
		FixupBranch bail = B_CC(CC_MI);

		SetJumpTarget(skipToRealDispatcher); 
		dispatcherNoCheck = GetCodePtr();

		// This block of code gets the address of the compiled block of code
		// It runs though to the compiling portion if it isn't found
			LDR(R12, R9, PPCSTATE_OFF(pc));// Load the current PC into R12

			Operand2 iCacheMask = Operand2(0xE, 2); // JIT_ICACHE_MASK
			BIC(R12, R12, iCacheMask); // R12 contains PC & JIT_ICACHE_MASK here.

			MOVI2R(R14, (u32)jit->GetBlockCache()->GetICache());

			LDR(R12, R14, R12); // R12 contains iCache[PC & JIT_ICACHE_MASK] here
			// R12 Confirmed this is the correct iCache Location loaded.
			TST(R12, 0xFC); // Test  to see if it is a JIT block.

			SetCC(CC_EQ);
				// Success, it is our Jitblock.
				MOVI2R(R14, (u32)jit->GetBlockCache()->GetCodePointers());
				// LDR R14 right here to get CodePointers()[0] pointer.
				REV(R12, R12); // Reversing this gives us our JITblock.
				LSL(R12, R12, 2); // Multiply by four because address locations are u32 in size 
				LDR(R14, R14, R12); // Load the block address in to R14 

				B(R14);
				// No need to jump anywhere after here, the block will go back to dispatcher start
			SetCC();

		// If we get to this point, that means that we don't have the block cached to execute
		// So call ArmJit to compile the block and then execute it.
		MOVI2R(R14, (u32)&Jit);	
		BL(R14);
			
		B(dispatcherNoCheck);

		// fpException()
		// Floating Point Exception Check, Jumped to if false
		fpException = GetCodePtr();
			LDR(R0, R9, PPCSTATE_OFF(Exceptions));
			ORR(R0, R0, EXCEPTION_FPU_UNAVAILABLE);
			STR(R0, R9, PPCSTATE_OFF(Exceptions));
				QuickCallFunction(R14, (void*)&PowerPC::CheckExceptions);
			LDR(R0, R9, PPCSTATE_OFF(npc));
			STR(R0, R9, PPCSTATE_OFF(pc));
		B(dispatcher);

		SetJumpTarget(bail);
		doTiming = GetCodePtr();			
		// XXX: In JIT64, Advance() gets called /after/ the exception checking
		// once it jumps back to the start of outerLoop 
		QuickCallFunction(R14, (void*)&CoreTiming::Advance);

		// Does exception checking 
		testExceptions = GetCodePtr();
			LDR(R0, R9, PPCSTATE_OFF(pc));
			STR(R0, R9, PPCSTATE_OFF(npc));
				QuickCallFunction(R14, (void*)&PowerPC::CheckExceptions);
			LDR(R0, R9, PPCSTATE_OFF(npc));
			STR(R0, R9, PPCSTATE_OFF(pc));
		// Check the state pointer to see if we are exiting
		// Gets checked on every exception check
			MOVI2R(R0, (u32)PowerPC::GetStatePtr());
			MVN(R1, 0);
			LDR(R0, R0);
			TST(R0, R1);
			FixupBranch Exit = B_CC(CC_NEQ);

	B(dispatcher);
	
	SetJumpTarget(Exit);

	ADD(_SP, _SP, 4);

	POP(9, R4, R5, R6, R7, R8, R9, R10, R11, _PC);  // Returns
	
	GenerateCommon();

	FlushIcache();
}

void JitArmAsmRoutineManager::GenloadPairedFloatTwo(ARMXEmitter *emit, u32 level)
{
	NEONXEmitter nemit(emit);
	nemit.VLD1(I_32, D0, R10);
	nemit.VREV32(I_8, D0, D0);
}
void JitArmAsmRoutineManager::GenloadPairedFloatOne(ARMXEmitter *emit, u32 level)
{
	NEONXEmitter nemit(emit);
	nemit.VLD1(I_32, D0, R10);
	nemit.VREV32(I_8, D0, D0);
}
void JitArmAsmRoutineManager::GenloadPairedU8Two(ARMXEmitter *emit, u32 level)
{
	emit->LDRH(R12, R10);
	emit->SXTB(R12, R12);
	emit->VMOV(S0, R12);

	emit->LDRH(R12, R10, 2);
	emit->SXTB(R12, R12);
	emit->VMOV(S1, R12);
	
	emit->VCVT(S0, S0, TO_FLOAT);
	emit->VCVT(S1, S1, TO_FLOAT);

	emit->MOVI2F(S2, m_dequantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
	emit->VMUL(S1, S1, S2);
}
void JitArmAsmRoutineManager::GenloadPairedU8One(ARMXEmitter *emit, u32 level)
{
	emit->LDRB(R12, R10);
	emit->SXTB(R12, R12);
	emit->VMOV(S0, R12);

	emit->VCVT(S0, S0, TO_FLOAT);

	emit->MOVI2F(S2, m_dequantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
}
void JitArmAsmRoutineManager::GenloadPairedS8Two(ARMXEmitter *emit, u32 level)
{
	emit->LDRH(R12, R10);
	emit->SXTB(R12, R12);
	emit->VMOV(S0, R12);

	emit->LDRH(R12, R10, 2);
	emit->SXTB(R12, R12);
	emit->VMOV(S1, R12);

	emit->VCVT(S0, S0, TO_FLOAT | IS_SIGNED);
	emit->VCVT(S1, S1, TO_FLOAT | IS_SIGNED);

	emit->MOVI2F(S2, m_dequantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
	emit->VMUL(S1, S1, S2);
}
void JitArmAsmRoutineManager::GenloadPairedS8One(ARMXEmitter *emit, u32 level)
{
	emit->BIC(R10, R10, mask);
	emit->MOVI2R(R12, (u32)Memory::base);
	emit->ADD(R10, R10, R12);

	emit->LDRB(R12, R10);
	emit->SXTB(R12, R12);
	emit->VMOV(S0, R12);

	emit->VCVT(S0, S0, TO_FLOAT | IS_SIGNED);

	emit->MOVI2R(S2, m_dequantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
}
void JitArmAsmRoutineManager::GenloadPairedU16Two(ARMXEmitter *emit, u32 level)
{
	emit->LDRH(R12, R10);
	emit->REV16(R12, R12);
	emit->SXTH(R12, R12);
	emit->VMOV(S0, R12);

	emit->LDRH(R12, R10, 2);
	emit->REV16(R12, R12);
	emit->SXTH(R12, R12);
	emit->VMOV(S1, R12);

	emit->VCVT(S0, S0, TO_FLOAT);
	emit->VCVT(S1, S1, TO_FLOAT);

	emit->MOVI2F(S2, m_dequantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
	emit->VMUL(S1, S1, S2);
}
void JitArmAsmRoutineManager::GenloadPairedU16One(ARMXEmitter *emit, u32 level)
{
	emit->LDRH(R12, R10);
	emit->REV16(R12, R12);
	emit->VMOV(S0, R12);

	emit->VCVT(S0, S0, TO_FLOAT);

	emit->MOVI2F(S2, m_dequantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
}
void JitArmAsmRoutineManager::GenloadPairedS16Two(ARMXEmitter *emit, u32 level)
{
	emit->LDRH(R12, R10);
	emit->REV16(R12, R12);
	emit->SXTH(R12, R12);
	emit->VMOV(S0, R12);

	emit->LDRH(R12, R10, 2);
	emit->REV16(R12, R12);
	emit->SXTH(R12, R12);
	emit->VMOV(S1, R12);

	emit->VCVT(S0, S0, TO_FLOAT | IS_SIGNED);
	emit->VCVT(S1, S1, TO_FLOAT | IS_SIGNED);

	emit->MOVI2F(S2, m_dequantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
	emit->VMUL(S1, S1, S2);
}
void JitArmAsmRoutineManager::GenloadPairedS16One(ARMXEmitter *emit, u32 level)
{
	emit->LDRH(R12, R10);
	emit->REV16(R12, R12);
	emit->SXTH(R12, R12);
	emit->VMOV(S0, R12);

	emit->VCVT(S0, S0, TO_FLOAT | IS_SIGNED);

	emit->MOVI2F(S2, m_dequantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
}
void JitArmAsmRoutineManager::GenPairedIllegal(ARMXEmitter *emit, u32 level)
{
	emit->BKPT(0x10);
}

void JitArmAsmRoutineManager::GenstorePairedFloat(ARMXEmitter *emit, u32 level)
{
	NEONXEmitter nemit(emit);
	emit->TST(R10, arghmask);
	FixupBranch argh = emit->B_CC(CC_NEQ);
	emit->BIC(R10, R10, mask);
	emit->MOVI2R(R12, (u32)Memory::base);
	emit->ADD(R10, R10, R12);

	nemit.VREV32(I_8, D0, D0);
	nemit.VST1(I_32, D0, R10);
	FixupBranch done = emit->B();
	emit->SetJumpTarget(argh);

	emit->PUSH(4, R0, R1, R2, R3);
	emit->VMOV(R0, S0);
	emit->VMOV(R1, S1);
	emit->MOV(R2, R10);
	emit->MOVI2R(R12, (u32)&WriteDual32);
	emit->BL(R12);
	emit->POP(4, R0, R1, R2, R3);
	emit->SetJumpTarget(done);
}
void JitArmAsmRoutineManager::GenstorePairedS8(ARMXEmitter *emit, u32 level)
{
	emit->PUSH(4, R0, R1, R2, R3);
	
	emit->MOVI2F(S2, m_quantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
	emit->VMUL(S1, S1, S2);

	emit->VCVT(S0, S0, TO_INT | ROUND_TO_ZERO); 
	emit->VCVT(S1, S1, TO_INT | ROUND_TO_ZERO); 
	
	emit->VMOV(R0, S0);
	emit->VMOV(R1, S1);
	emit->MOV(R2, R10);
	emit->MOVI2R(R12, (u32)&WriteDual8);
	emit->BL(R12);
	
	emit->POP(4, R0, R1, R2, R3);
}
void JitArmAsmRoutineManager::GenstorePairedS16(ARMXEmitter *emit, u32 level)
{
	emit->PUSH(4, R0, R1, R2, R3);

	emit->MOVI2F(S2, m_quantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);
	emit->VMUL(S1, S1, S2);

	emit->VCVT(S0, S0, TO_INT | ROUND_TO_ZERO); 
	emit->VCVT(S1, S1, TO_INT | ROUND_TO_ZERO); 
	
	emit->VMOV(R0, S0);
	emit->VMOV(R1, S1);
	emit->MOV(R2, R10);
	emit->MOVI2R(R12, (u32)&WriteDual16);
	emit->BL(R12);
	
	emit->POP(4, R0, R1, R2, R3);
}
void JitArmAsmRoutineManager::GenstoreSingleFloat(ARMXEmitter *emit, u32 level)
{
	emit->TST(R10, arghmask);
	FixupBranch argh = emit->B_CC(CC_NEQ);
	emit->BIC(R10, R10, mask);
	emit->MOVI2R(R12, (u32)Memory::base);
	emit->ADD(R10, R10, R12);

	emit->VMOV(R12, S0);
	emit->REV(R12, R12);
	emit->STR(R12, R10); 
	FixupBranch done = emit->B();
	emit->SetJumpTarget(argh);

	emit->PUSH(4, R0, R1, R2, R3);
	emit->VMOV(R0, S0);
	emit->MOV(R1, R10);
	emit->MOVI2R(R10, (u32)&Memory::Write_U32);
	emit->BL(R10);

	emit->POP(4, R0, R1, R2, R3);
	emit->SetJumpTarget(done);
}
void JitArmAsmRoutineManager::GenstoreSingleS8(ARMXEmitter *emit, u32 level)
{
	emit->MOVI2F(S2, m_quantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);

	emit->TST(R10, arghmask);
	FixupBranch argh = emit->B_CC(CC_NEQ);
	emit->BIC(R10, R10, mask);
	emit->MOVI2R(R12, (u32)Memory::base);
	emit->ADD(R10, R10, R12);
	
	emit->VCVT(S0, S0, TO_INT | ROUND_TO_ZERO);
	emit->VMOV(R12, S0);
	emit->STRB(R12, R10); 

	FixupBranch done = emit->B();
	emit->SetJumpTarget(argh);

	emit->PUSH(4, R0, R1, R2, R3);
	emit->VMOV(R0, S0);
	emit->MOV(R1, R10);
	emit->MOVI2R(R10, (u32)&Memory::Write_U8);
	emit->BL(R10);
	emit->POP(4, R0, R1, R2, R3);
	emit->SetJumpTarget(done);
}
void JitArmAsmRoutineManager::GenstoreSingleS16(ARMXEmitter *emit, u32 level)
{
	emit->MOVI2F(S2, m_quantizeTableS[level], R12);
	emit->VMUL(S0, S0, S2);

	emit->TST(R10, arghmask);
	FixupBranch argh = emit->B_CC(CC_NEQ);
	emit->BIC(R10, R10, mask);
	emit->MOVI2R(R12, (u32)Memory::base);
	emit->ADD(R10, R10, R12);
	
	emit->VCVT(S0, S0, TO_INT | ROUND_TO_ZERO);
	emit->VMOV(R12, S0);
	emit->REV16(R12, R12);
	emit->STRH(R12, R10); 

	FixupBranch done = emit->B();
	emit->SetJumpTarget(argh);

	emit->PUSH(4, R0, R1, R2, R3);
	emit->VMOV(R0, S0);
	emit->MOV(R1, R10);
	emit->MOVI2R(R10, (u32)&Memory::Write_U16);
	emit->BL(R10);

	emit->POP(4, R0, R1, R2, R3);
	emit->SetJumpTarget(done);
}

void JitArmAsmRoutineManager::GenerateCommon()
{
	// R14 is LR
	// R12 is scratch
	// R11 is scale
	// R10 is the address
	
	ARMPairedLoadQuantized[0] = &JitArmAsmRoutineManager::GenloadPairedFloatTwo;
	ARMPairedLoadQuantized[1] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedLoadQuantized[2] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedLoadQuantized[3] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedLoadQuantized[4] = &JitArmAsmRoutineManager::GenloadPairedU8Two;
	ARMPairedLoadQuantized[5] = &JitArmAsmRoutineManager::GenloadPairedU16Two;
	ARMPairedLoadQuantized[6] = &JitArmAsmRoutineManager::GenloadPairedS8Two;
	ARMPairedLoadQuantized[7] = &JitArmAsmRoutineManager::GenloadPairedS16Two;

	ARMPairedLoadQuantized[8] = &JitArmAsmRoutineManager::GenloadPairedFloatOne;
	ARMPairedLoadQuantized[9] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedLoadQuantized[10] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedLoadQuantized[11] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedLoadQuantized[12] = &JitArmAsmRoutineManager::GenloadPairedU8One;
	ARMPairedLoadQuantized[13] = &JitArmAsmRoutineManager::GenloadPairedU16One;
	ARMPairedLoadQuantized[14] = &JitArmAsmRoutineManager::GenloadPairedS8One;
	ARMPairedLoadQuantized[15] = &JitArmAsmRoutineManager::GenloadPairedS16One;

	// Stores
	ARMPairedStoreQuantized[0] = &JitArmAsmRoutineManager::GenstorePairedFloat;
	ARMPairedStoreQuantized[1] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedStoreQuantized[2] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedStoreQuantized[3] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedStoreQuantized[4] = &JitArmAsmRoutineManager::GenstorePairedS8;
	ARMPairedStoreQuantized[5] = &JitArmAsmRoutineManager::GenstorePairedS16;
	ARMPairedStoreQuantized[6] = &JitArmAsmRoutineManager::GenstorePairedS8;
	ARMPairedStoreQuantized[7] = &JitArmAsmRoutineManager::GenstorePairedS16;

	ARMPairedStoreQuantized[8] = &JitArmAsmRoutineManager::GenstoreSingleFloat;
	ARMPairedStoreQuantized[9] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedStoreQuantized[10] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedStoreQuantized[11] = &JitArmAsmRoutineManager::GenPairedIllegal;
	ARMPairedStoreQuantized[12] = &JitArmAsmRoutineManager::GenstoreSingleS8;
	ARMPairedStoreQuantized[13] = &JitArmAsmRoutineManager::GenstoreSingleS16;
	ARMPairedStoreQuantized[14] = &JitArmAsmRoutineManager::GenstoreSingleS8;
	ARMPairedStoreQuantized[15] = &JitArmAsmRoutineManager::GenstoreSingleS16;
}
