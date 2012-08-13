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

//	gpr.Flush(FLUSH_ALL);
//	fpr.Flush(FLUSH_ALL);

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

