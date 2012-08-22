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

void JitArm::mtmsr(UGeckoInstruction inst)
{
	INSTRUCTION_START
 	// Don't interpret this, if we do we get thrown out
/*	//JITDISABLE(SystemRegisters)
	if (!gpr.R(inst.RS).IsImm())
	{
		gpr.Lock(inst.RS);
		gpr.BindToRegister(inst.RS, true, false);
	}
	MOV(32, M(&MSR), gpr.R(inst.RS));
	gpr.UnlockAll();
	gpr.Flush(FLUSH_ALL);
	fpr.Flush(FLUSH_ALL);*/
//	MSR = m_GPR[_inst.RS];

	ARMABI_MOVIMM32(R10, (u32)&MSR);
	ARMABI_MOVIMM32(R11, (u32)&m_GPR[inst.RS]); 
	LDR(R11, R11);
	STR(R10, R11);
	WriteExit(js.compilerPC + 4, 0);
}
