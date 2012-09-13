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
#include "Common.h"
#include "Thunk.h"

#include "../../Core.h"
#include "../PowerPC.h"
#include "../../CoreTiming.h"
#include "../PPCTables.h"
#include "ArmEmitter.h"

#include "Jit.h"
#include "JitRegCache.h"
#include "JitAsm.h"

// The branches are known good, or at least reasonably good.
// No need for a disable-mechanism.

// If defined, clears CR0 at blr and bl-s. If the assumption that
// flags never carry over between functions holds, then the task for 
// an optimizer becomes much easier.

// #define ACID_TEST

// Zelda and many more games seem to pass the Acid Test. 


using namespace ArmGen;
void JitArm::sc(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)

//	gpr.Flush(FLUSH_ALL);
//	fpr.Flush(FLUSH_ALL);
	ARMABI_MOVIMM32((u32)&PC, js.compilerPC + 4);
	
	ARMABI_MOVIMM32(R0, (u32)&PowerPC::ppcState.Exceptions);
	ARMABI_MOVIMM32(R2, EXCEPTION_SYSCALL);
	LDREX(R1, R0);
	ORR(R1, R1, R2);
	STREX(R2, R0, R1);
	DMB();

	WriteExceptionExit();
}

void JitArm::rfi(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)

//	gpr.Flush(FLUSH_ALL);
//	fpr.Flush(FLUSH_ALL);
	// See Interpreter rfi for details
	const u32 mask = 0x87C0FFFF;
	const u32 clearMSR13 = 0xFFFBFFFF; // Mask used to clear the bit MSR[13]
	// MSR = ((MSR & ~mask) | (SRR1 & mask)) & clearMSR13;
	// R0 = MSR location
	// R1 = MSR contents
	// R2 = Mask
	// R3 = Mask
	ARMABI_MOVIMM32(R0, (u32)&MSR);
	ARMABI_MOVIMM32(R2, (~mask) & clearMSR13);
	ARMABI_MOVIMM32(R3, mask & clearMSR13);

	LDR(R1, R0);

	AND(R1, R1, R2); // R1 = Masked MSR

	ARMABI_MOVIMM32(R2, (u32)&SRR1);
	LDR(R2, R2); // R2 contains SRR1 here

	AND(R2, R2, R3); // R2 contains masked SRR1 here
	ORR(R2, R1, R2); // R2 = Masked MSR OR masked SRR1

	
	ARMABI_MOVIMM32(R0, (u32)&MSR);
	STR(R0, R2); // STR R2 in to R0

	ARMABI_MOVIMM32(R0, (u32)&SRR0);
	LDR(R0, R0);

	WriteRfiExitDestInR0();
	//AND(32, M(&MSR), Imm32((~mask) & clearMSR13));
	//MOV(32, R(EAX), M(&SRR1));
	//AND(32, R(EAX), Imm32(mask & clearMSR13));
	//OR(32, M(&MSR), R(EAX));
	// NPC = SRR0;
	//MOV(32, R(EAX), M(&SRR0));
	//WriteRfiExitDestInEAX();
}

void JitArm::bx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)
	// We must always process the following sentence
	// even if the blocks are merged by PPCAnalyst::Flatten().
	if (inst.LK)
		ARMABI_MOVIMM32((u32)&LR, js.compilerPC + 4);

	// If this is not the last instruction of a block,
	// we will skip the rest process.
	// Because PPCAnalyst::Flatten() merged the blocks.
	if (!js.isLastInstruction) {
		return;
	}

	//gpr.Flush(FLUSH_ALL);
	//fpr.Flush(FLUSH_ALL);

	u32 destination;
	if (inst.AA)
		destination = SignExt26(inst.LI << 2);
	else
		destination = js.compilerPC + SignExt26(inst.LI << 2);
#ifdef ACID_TEST
	// TODO: Not implemented yet.
//	if (inst.LK)
//		AND(32, M(&PowerPC::ppcState.cr), Imm32(~(0xFF000000)));
#endif
	if (destination == js.compilerPC)
	{
		//PanicAlert("Idle loop detected at %08x", destination);
		//	CALL(ProtectFunction(&CoreTiming::Idle, 0));
		//	JMP(Asm::testExceptions, true);
		// make idle loops go faster
		js.downcountAmount += 8;
	}
	WriteExit(destination, 0);
}
// TODO: Finish these branch instructions
void JitArm::bcx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)

	// USES_CR
	_assert_msg_(DYNA_REC, js.isLastInstruction, "bcx not last instruction of block");

	//gpr.Flush(FLUSH_ALL);
	//fpr.Flush(FLUSH_ALL);

	FixupBranch pCTRDontBranch;
	if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)  // Decrement and test CTR
	{
		ARMABI_MOVIMM32(R0, (u32)&CTR);
		LDR(R2, R0);
		SUBS(R2, R2, 1);
		STR(R0, R2);
			
		//SUB(32, M(&CTR), Imm8(1));
		if (inst.BO & BO_BRANCH_IF_CTR_0)
			pCTRDontBranch = B_CC(CC_NEQ);
		else
			pCTRDontBranch = B_CC(CC_EQ);
	}

	FixupBranch pConditionDontBranch;
	if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)  // Test a CR bit
	{
		//printf("CR: %08x\n", PowerPC::ppcState.cr_fast[inst.BI >> 2]);
		ARMABI_MOVIMM32(R0, (u32)&PowerPC::ppcState.cr_fast[inst.BI >> 2]); 
		LDRB(R1, R0);
		ARMABI_MOVIMM32(R2, 8 >> (inst.BI & 3));
		ANDS(R1, R1, R2);
		//TEST(8, M(&PowerPC::ppcState.cr_fast[inst.BI >> 2]), Imm8(8 >> (inst.BI & 3)));
		if (inst.BO & BO_BRANCH_IF_TRUE)  // Conditional branch 
			pConditionDontBranch = B_CC(CC_EQ); // Zero
		else
			pConditionDontBranch = B_CC(CC_NEQ); // Not Zero
	}
	
	if (inst.LK)
		ARMABI_MOVIMM32((u32)&LR, js.compilerPC + 4);
	
	u32 destination;
	if(inst.AA)
		destination = SignExt16(inst.BD << 2);
	else
		destination = js.compilerPC + SignExt16(inst.BD << 2);
	WriteExit(destination, 0);

	if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)
		SetJumpTarget( pConditionDontBranch );
	if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)
		SetJumpTarget( pCTRDontBranch );

	WriteExit(js.compilerPC + 4, 1);
}

void JitArm::bclrx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)

	if (!js.isLastInstruction &&
		(inst.BO & (1 << 4)) && (inst.BO & (1 << 2))) {
		if (inst.LK)
		{
			ARMABI_MOVIMM32((u32)&LR, js.compilerPC + 4);
			//MOV(32, M(&LR), Imm32(js.compilerPC + 4));
		}
		return;
	}
	//gpr.Flush(FLUSH_ALL);
	//fpr.Flush(FLUSH_ALL);

	FixupBranch pCTRDontBranch;
	if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)  // Decrement and test CTR
	{
		ARMABI_MOVIMM32(R0, (u32)&CTR);
		LDR(R2, R0);
		SUBS(R2, R2, 1);
		STR(R0, R2);
			
		//SUB(32, M(&CTR), Imm8(1));
		if (inst.BO & BO_BRANCH_IF_CTR_0)
			pCTRDontBranch = B_CC(CC_NEQ);
		else
			pCTRDontBranch = B_CC(CC_EQ);
	}

	FixupBranch pConditionDontBranch;
	if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)  // Test a CR bit
	{
		ARMABI_MOVIMM32(R0, (u32)&PowerPC::ppcState.cr_fast[inst.BI >> 2]); 
		ARMABI_MOVIMM32(R2, 8 >> (inst.BI & 3));
		LDR(R1, R0);
		ANDS(R1, R1, R2);
		//TEST(8, M(&PowerPC::ppcState.cr_fast[inst.BI >> 2]), Imm8(8 >> (inst.BI & 3)));
		if (inst.BO & BO_BRANCH_IF_TRUE)  // Conditional branch 
			pConditionDontBranch = B_CC(CC_EQ);
		else
			pConditionDontBranch = B_CC(CC_NEQ);
	}

	// This below line can be used to prove that blr "eats flags" in practice.
	// This observation will let us do a lot of fun observations.
#ifdef ACID_TEST
	// TODO: Not yet implemented
	//	AND(32, M(&PowerPC::ppcState.cr), Imm32(~(0xFF000000)));
#endif

	//MOV(32, R(EAX), M(&LR));	
	//AND(32, R(EAX), Imm32(0xFFFFFFFC));
	ARMABI_MOVIMM32(R2, (u32)&LR);
	ARMABI_MOVIMM32(R1, 0xFFFFFFFC);
	LDR(R0, R2);
	AND(R0, R0, R1);
	STR(R2, R0);
	if (inst.LK)
		ARMABI_MOVIMM32((u32)&LR, js.compilerPC + 4);
	
	WriteExitDestInR0();

	if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)
		SetJumpTarget( pConditionDontBranch );
	if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)
		SetJumpTarget( pCTRDontBranch );
	WriteExit(js.compilerPC + 4, 1);

}
