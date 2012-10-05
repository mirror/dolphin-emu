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
// Jit64 ComputerRC is signed
// JIT64 GenerateRC is unsigned
void JitArm::GenerateRC(int cr) {
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.cr_fast[cr]);
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
void JitArm::ComputeRC(int cr) {
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.cr_fast[cr]);
	FixupBranch pGreater  = B_CC(CC_GT);
	FixupBranch pLessThan = B_CC(CC_LT);

	MOV(rB, 0x2); // Result == 0
	FixupBranch continue1 = B();

	SetJumpTarget(pLessThan);
	MOV(rB, 0x8); // Result < 0
	FixupBranch continue2 = B();
	
	SetJumpTarget(pGreater);
	MOV(rB, 0x4); // Result > 0

	SetJumpTarget(continue1);
	SetJumpTarget(continue2);
	STRB(rA, rB, 0);
	gpr.Unlock(rA, rB);
}
// Wrong, causes SMS to jump to zero
void JitArm::addi(UGeckoInstruction inst)
{
	//if (inst.RA) 
	{
		Default(inst); return;
	}
	ARMReg RD = gpr.R(inst.RD);
	if (inst.RA)
	{
		ARMReg rA = gpr.GetReg(false);
		ARMReg RA = gpr.R(inst.RA);
		ARMABI_MOVI2R(rA, inst.SIMM_16);
		ADD(RD, RA, rA);
	}
	else
		ARMABI_MOVI2R(RD, inst.SIMM_16);
}
// Wrong
void JitArm::addis(UGeckoInstruction inst)
{
	//if (inst.RA) 
	{
		Default(inst); return;
	}
	ARMReg RD = gpr.R(inst.RD);
	if (inst.RA)
	{
		ARMReg rA = gpr.GetReg(false);
		ARMReg RA = gpr.R(inst.RA);
		ARMABI_MOVI2R(rA, inst.SIMM_16 << 16);
		ADD(RD, RA, rA);
	}
	else
		ARMABI_MOVI2R(RD, inst.SIMM_16 << 16);
}
// Wrong
void JitArm::addx(UGeckoInstruction inst)
{
	Default(inst); return;
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RB = gpr.R(inst.RB);
	ARMReg RD = gpr.R(inst.RD);
	ADDS(RD, RA, RB);
	if (inst.Rc) GenerateRC();
}
// Testing in SMS
void JitArm::ori(UGeckoInstruction inst)
{
	Default(inst); return;
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RS = gpr.R(inst.RS);
	ARMReg rA = gpr.GetReg();
	ARMABI_MOVI2R(rA, inst.UIMM);
	ORR(RA, RS, rA);
	gpr.Unlock(rA);
}
void JitArm::extshx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Integer)
	ARMReg RA, RS;
	RA = gpr.R(inst.RA);
	RS = gpr.R(inst.RS);
	SXTH(RA, RS);
	if (inst.RC){
		CMP(RA, 0);
		ComputeRC();
	}
}
void JitArm::extsbx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Integer)
	ARMReg RA, RS;
	RA = gpr.R(inst.RA);
	RS = gpr.R(inst.RS);
	SXTB(RA, RS);
	if (inst.RC){
		CMP(RA, 0);
		ComputeRC();
	}
}
// Seems fine in SMS
// Crashes BAM3K though
void JitArm::cmp (UGeckoInstruction inst)
{
	Default(inst); return;	
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RB = gpr.R(inst.RB);
	int crf = inst.CRFD;
	CMP(RA, RB);
	ComputeRC(crf);
}
// SMS crashes with this one
void JitArm::cmpi(UGeckoInstruction inst)
{
	Default(inst); return;
	ARMReg RA = gpr.R(inst.RA);
	ARMReg rA = gpr.GetReg();
	int crf = inst.CRFD;
	ARMABI_MOVI2R(rA, inst.SIMM_16);
	SXTH(rA, rA);
	CMP(RA, rA);
	gpr.Unlock(rA);
	ComputeRC(crf);
}
// Wrong
void JitArm::cmpli(UGeckoInstruction inst)
{
	// Bit special, look in to this one
	Default(inst); return;
	ARMReg RA = gpr.R(inst.RA);
	ARMReg rA = gpr.GetReg(false);
	int crf = inst.CRFD;
	u32 b = inst.UIMM;
	ARMABI_MOVI2R(rA, b);
	CMP(RA, rA);
	GenerateRC(crf);		 
}
// Wrong
void JitArm::negx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Integer)
	Default(inst);return;
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RD = gpr.R(inst.RD);

	RSBS(RD, RA, 0);
	if (inst.Rc)
	{
		GenerateRC();
	}
	if (inst.OE)
	{
		BKPT(0x333);
		//GenerateOverflow();
	}
}
// Wrong
void JitArm::orx(UGeckoInstruction inst)
{
	Default(inst); return;
	ARMReg rA = gpr.R(inst.RA);
	ARMReg rS = gpr.R(inst.RS);
	ARMReg rB = gpr.R(inst.RB);
	ORR(rA, rS, rB);
	if (inst.Rc)
	{
		CMP(rA, 0);
		ComputeRC();
	}
}
// Wrong
void JitArm::rlwinmx(UGeckoInstruction inst)
{
	Default(inst); return;
	if (inst.Rc) {
		Default(inst);
		return;
	}
	u32 mask = Helper_Mask(inst.MB,inst.ME);
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RS = gpr.R(inst.RS);
	ARMReg rA = gpr.GetReg();
	ARMABI_MOVI2R(rA, mask);

	Operand2 Shift(32 - inst.SH, ROR, RS); // This rotates left, while ARM has only rotate right, so swap it.
	if (inst.Rc)
	{
		ANDS(RA, rA, Shift);
		GenerateRC();	
	}
	else
		AND (RA, rA, Shift);
	gpr.Unlock(rA);

	//m_GPR[inst.RA] = _rotl(m_GPR[inst.RS],inst.SH) & mask;
	if (inst.Rc) GenerateRC(); 
}

