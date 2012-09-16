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
extern u32 Helper_Mask(u8 mb, u8 me);
// Assumes that Sign and Zero flags were set by the last operation. Preserves all flags and registers.
void JitArm::GenerateRC() {
	ARMABI_MOVIMM32(R0, (u32)&PowerPC::ppcState.cr_fast[0]);
	FixupBranch pZero  = B_CC(CC_EQ);
	FixupBranch pNegative = B_CC(CC_MI);
	MOV(R1, 0x4); // Result > 0
	FixupBranch continue1 = B();

	SetJumpTarget(pNegative);
	MOV(R1, 0x8); // Result < 0
	FixupBranch continue2 = B();

	
	SetJumpTarget(pZero);
	MOV(R1, 0x2); // Result == 0

	SetJumpTarget(continue1);
	SetJumpTarget(continue2);
	STRB(R0, R1, 0);
}

void JitArm::addi(UGeckoInstruction _inst)
{
	ARMABI_MOVIMM32(R0, (u32)&m_GPR[_inst.RD]);
	ARMABI_MOVIMM32(R1, _inst.SIMM_16);
	if (_inst.RA)
	{
		ARMABI_MOVIMM32(R2, (u32)&m_GPR[_inst.RA]);
		LDR(R2, R2);
		ADD(R1, R1, R2);
	}
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
void JitArm::rlwinmx(UGeckoInstruction _inst)
{
	u32 mask = Helper_Mask(_inst.MB,_inst.ME);
	ARMABI_MOVIMM32(R0, (u32)&m_GPR[_inst.RA]);
	ARMABI_MOVIMM32(R1, mask);
	ARMABI_MOVIMM32(R2, (u32)&m_GPR[_inst.RS]);
	LDR(R2, R2);

	Operand2 Shift(32 - _inst.SH, ROR, R2); // This rotates left, while ARM has only rotate right, so swap it.

	ANDS(R2, R1, Shift);
	STR(R0, R2);

	//m_GPR[_inst.RA] = _rotl(m_GPR[_inst.RS],_inst.SH) & mask;
	if (_inst.Rc) GenerateRC(); 
}

