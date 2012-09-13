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

void JitArm::addi(UGeckoInstruction _inst)
{
	ARMABI_MOVIMM32(R0, (u32)&m_GPR[_inst.RD]);
	ARMABI_MOVIMM32(R1, _inst.SIMM_16);
	if (_inst.RA)
	{
		ARMABI_MOVIMM32(R2, (u32)&m_GPR[_inst.RA]);
		LDR(R2, R2);
		ADD(R1, R1, R2);
		STR(R0, R1);
	}
	else
		STR(R0, R1);
}
void JitArm::ori(UGeckoInstruction _inst)
{
	ARMABI_MOVIMM32(R0, (u32)&m_GPR[_inst.RA]);
	ARMABI_MOVIMM32(R1, (u32)&m_GPR[_inst.RS]);
	ARMABI_MOVIMM32(R2, _inst.UIMM);
	LDR(R1, R1);
	ORR(R1, R1, R2);
	STR(R0, R1);
}
