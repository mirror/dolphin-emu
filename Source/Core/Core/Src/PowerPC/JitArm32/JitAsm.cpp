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
#include "ArmInterface.h"
#include "../JitCommon/JitCache.h"

#include "../../HW/GPFifo.h"
#include "../../Core.h"
#include "JitAsm.h"
#include "ArmABI.h"
#include "ArmEmitter.h"

using namespace ArmGen;

//static int temp32; // unused?

//TODO - make an option
//#if _DEBUG
static bool enableDebug = false; 
//#else
//		bool enableDebug = false; 
//#endif

//static bool enableStatistics = false; //unused?

//GLOBAL STATIC ALLOCATIONS x86
//EAX - ubiquitous scratch register - EVERYBODY scratches this

//GLOBAL STATIC ALLOCATIONS x64
//EAX - ubiquitous scratch register - EVERYBODY scratches this
//RBX - Base pointer of memory
//R15 - Pointer to array of block pointers 

JitArmAsmRoutineManager asm_routines;

// PLAN: no more block numbers - crazy opcodes just contain offset within
// dynarec buffer
// At this offset - 4, there is an int specifying the block number.
void Test(unsigned int Block)
{
	printf("Got block: %08x\n", Block);
}
void Test2(unsigned int Block)
{
	printf("Second: %08x\n", Block);
}
void JitArmAsmRoutineManager::Generate()
{
	enterCode = GetCodePtr();

	ARMABI_CallFunction((void*)&CoreTiming::Advance);
	ARMABI_MOVIMM32(R9, (u32)&PowerPC::ppcState.pc);
	LDR(R9, R9);// Load the current PC into R9
	MOV(ARM_PARAM1, R9);
	ARMABI_CallFunction((void*)Test2);

	ARMABI_MOVIMM32(R10, JIT_ICACHE_MASK);
	AND(R9, R9, R10); // R9 contains PC & JIT_ICACHE_MASK here.
	// Confirmed good to this point 08-03-12

	ARMABI_MOVIMM32(R10, (u32)jitarm->GetBlockCache()->GetICache());
	// Confirmed That this loads the base iCache Location correctly 08-04-12

	LDR(R9, R10, R9, true, true); // R9 contains iCache[PC & JIT_ICACHE_MASK] here
	// R9 Confirmed this is the correct iCache Location loaded.
	TST(R9, 0xFC); // Test  to see if it is a JIT block.
	
	SetCC(CC_EQ); // Only run next part if R9 is zero
	// Success, it is our Jitblock.
	ARMABI_MOVIMM32(R10, (u32)jitarm->GetBlockCache()->GetCodePointers());
	// LDR R10 right here to get CodePointers()[0] pointer.
	REV(R9, R9); // Reversing this gives us our JITblock.
	MOV(ARM_PARAM1, R9);
	ARMABI_CallFunction((void*)Test2);
	ADD(R10, R10, R9);
	LDR(R10, R10); // Shouldn't need to ADD before this LDR, just use Indexed address
	// _LR contains exit to Jit::Run place before this
	PUSH(1, _LR);
	BLX(R10);
	POP(1, _LR);
	// _LR now contains exit to here.
	
	// MOV(_PC, _LR);
	SetCC(); // Return to always executing codes
	UpdateAPSR(true, 0, true, 0); // Clear our host register flags out.

	ARMABI_MOVIMM32(ARM_PARAM1, (u32)&PowerPC::ppcState.pc);
	LDR(ARM_PARAM1, ARM_PARAM1); 
	ARMABI_CallFunction((void*)&ArmJit);
	

	MOV(_PC, _LR);
	Flush();
}

void JitArmAsmRoutineManager::GenerateCommon()
{
/*	fifoDirectWrite8 = AlignCode4();
	GenFifoWrite(8);
	fifoDirectWrite16 = AlignCode4();
	GenFifoWrite(16);
	fifoDirectWrite32 = AlignCode4();
	GenFifoWrite(32);
	fifoDirectWriteFloat = AlignCode4();
	GenFifoFloatWrite();
	fifoDirectWriteXmm64 = AlignCode4(); 
	GenFifoXmm64Write();

	GenQuantizedLoads();
	GenQuantizedStores();
	GenQuantizedSingleStores();
*/
	//CMPSD(R(XMM0), M(&zero), 
	// TODO

	// Fast write routines - special case the most common hardware write
	// TODO: use this.
	// Even in x86, the param values will be in the right registers.
	/*
	const u8 *fastMemWrite8 = AlignCode16();
	CMP(32, R(ABI_PARAM2), Imm32(0xCC008000));
	FixupBranch skip_fast_write = J_CC(CC_NE, false);
	MOV(32, EAX, M(&m_gatherPipeCount));
	MOV(8, MDisp(EAX, (u32)&m_gatherPipe), ABI_PARAM1);
	ADD(32, 1, M(&m_gatherPipeCount));
	RET();
	SetJumpTarget(skip_fast_write);
	CALL((void *)&Memory::Write_U8);*/
}
