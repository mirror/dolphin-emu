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
// ADDI and RLWINMX broken for now

// Assumes that Sign and Zero flags were set by the last operation. Preserves all flags and registers.
void JitArm::GenerateRC() {
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.cr_fast[0]);
	FixupBranch pZero  = B_CC(CC_EQ);
	FixupBranch pNegative = B_CC(CC_MI);
	MOV(rB, 0x4); // Result > 0
	FixupBranch continue1 = B();

	SetJumpTarget(pNegative);
	MOV(rB, 0x8); // Result < 0
	FixupBranch continue2 = B();

	
	SetJumpTarget(pZero);
	MOV(rB, 0x2); // Result == 0

	SetJumpTarget(continue1);
	SetJumpTarget(continue2);
	STRB(rA, rB, 0);
	gpr.Unlock(rA, rB);
}

void JitArm::addi(UGeckoInstruction _inst)
{
	Default(_inst); return;
	ARMReg RD = gpr.R(_inst.RD);
	ARMReg rA = gpr.GetReg();
	ARMABI_MOVI2R(rA, _inst.SIMM_16);
	if (_inst.RA)
	{
		ARMReg RA = gpr.R(_inst.RA);
		ADD(RD, RA, rA);
	}
	else
		MOV(RD, rA);
	gpr.Unlock(rA);
}
void JitArm::ori(UGeckoInstruction _inst)
{
	ARMReg RA = gpr.R(_inst.RA);
	ARMReg RS = gpr.R(_inst.RS);
	ARMReg rA = gpr.GetReg();
	ARMABI_MOVI2R(rA, _inst.UIMM);
	ORR(RA, RS, rA);
	gpr.Unlock(rA);
}
void JitArm::rlwinmx(UGeckoInstruction _inst)
{
	Default(_inst); return;

	u32 mask = Helper_Mask(_inst.MB,_inst.ME);
	ARMReg RA = gpr.R(_inst.RA);
	ARMReg RS = gpr.R(_inst.RS);
	ARMReg rA = gpr.GetReg();
	ARMABI_MOVI2R(rA, mask);

	Operand2 Shift(32 - _inst.SH, ROR, RS); // This rotates left, while ARM has only rotate right, so swap it.
	if( _inst.RC)
		ANDS(RA, rA, Shift);
	else
		AND (RA, rA, Shift);
	gpr.Unlock(rA);

	//m_GPR[_inst.RA] = _rotl(m_GPR[_inst.RS],_inst.SH) & mask;
	if (_inst.Rc) GenerateRC(); 
}

