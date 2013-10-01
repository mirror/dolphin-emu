// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.
#include "Common.h"
#include "Thunk.h"

#include "../../Core.h"
#include "../PowerPC.h"
#include "../../CoreTiming.h"
#include "../PPCTables.h"
#include "ArmEmitter.h"
#include "../../HW/Memmap.h"

#include "Jit.h"
#include "JitRegCache.h"
#include "JitAsm.h"

void JitArm::psq_l(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStorePairedOff)
	
	bool update = inst.OPCD == 57;
	s32 offset = inst.SIMM_12;

	// R12 contains scale
	// R11 contains type
	// R10 is the ADDR
	if (js.memcheck || !Core::g_CoreStartupParameter.bFastmem) { Default(inst); return; }
	
	js.block_flags &= BLOCK_USES_GQR0 << inst.I;

	const UGQR gqr(rSPR(SPR_GQR0 + inst.I));

	if (inst.W)
	{
		Default(inst);
		return;
	}

	MOVI2R(R10, (u32)offset);
	if (inst.RA || update) // Always uses the register on update
		ADD(R10, R10, gpr.R(inst.RA));
	if (update)
		MOV(gpr.R(inst.RA), R10);

	Operand2 mask(3, 1); // ~(Memory::MEMVIEW32_MASK)

	BIC(R10, R10, mask);
	MOVI2R(R12, (u32)Memory::base);
	ADD(R10, R10, R12);

	JitArmAsmRoutineManager::GenPairedLoadStore ourLoad = asm_routines.ARMPairedLoadQuantized[gqr.LD_TYPE + (inst.W ? 8 : 0)];
	(asm_routines.*ourLoad)(this, gqr.LD_SCALE);

	ARMReg vD0 = fpr.R0(inst.RS, false);
	ARMReg vD1 = fpr.R1(inst.RS, false);
	VCVT(vD0, S0, 0);
	if (!inst.W)
		VCVT(vD1, S1, 0);
	else
		MOVI2F(vD1, 1.0f, INVALID_REG); // No need for temp reg with 1.0f
}

void JitArm::psq_lx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStorePairedOff)

	bool update = inst.SUBOP10 == 38;
	// R12 contains scale
	// R11 contains type
	// R10 is the ADDR
	if (js.memcheck || !Core::g_CoreStartupParameter.bFastmem) { Default(inst); return; }
	
	js.block_flags &= BLOCK_USES_GQR0 << inst.Ix;

	if (inst.W)
	{
		Default(inst);
		return;
	}

	const UGQR gqr(rSPR(SPR_GQR0 + inst.Ix));

	if (inst.RA || update) // Always uses the register on update
		ADD(R10, gpr.R(inst.RB), gpr.R(inst.RA));
	else
		MOV(R10, gpr.R(inst.RB));

	if (update)
		MOV(gpr.R(inst.RA), R10);

	Operand2 mask(3, 1); // ~(Memory::MEMVIEW32_MASK)

	BIC(R10, R10, mask);
	MOVI2R(R12, (u32)Memory::base);
	ADD(R10, R10, R12);

	JitArmAsmRoutineManager::GenPairedLoadStore ourLoad = asm_routines.ARMPairedLoadQuantized[gqr.LD_TYPE + (inst.Wx ? 8 : 0)];
	(asm_routines.*ourLoad)(this, gqr.LD_SCALE);

	ARMReg vD0 = fpr.R0(inst.RS, false);
	ARMReg vD1 = fpr.R1(inst.RS, false);
	LDR(R14, R9, PPCSTATE_OFF(Exceptions));
	CMP(R14, EXCEPTION_DSI);
	SetCC(CC_NEQ);	

	VCVT(vD0, S0, 0);
	if (!inst.Wx)
		VCVT(vD1, S1, 0);
	else
		MOVI2F(vD1, 1.0f, INVALID_REG); // No need for temp reg with 1.0f
	SetCC();
}

void JitArm::psq_st(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStorePairedOff)

	bool update = inst.OPCD == 61;
	s32 offset = inst.SIMM_12;
	
	// R12 contains scale
	// R11 contains type
	// R10 is the ADDR
	if (js.memcheck || !Core::g_CoreStartupParameter.bFastmem) { Default(inst); return; }

	js.block_flags &= BLOCK_USES_GQR0 << inst.I;

	const UGQR gqr(rSPR(SPR_GQR0 + inst.I));

	if (inst.RA || update) // Always uses the register on update
	{
		MOVI2R(R14, offset);
		ADD(R10, gpr.R(inst.RA), R14);
	}
	else
		MOVI2R(R10, (u32)offset);

	if (update)
		MOV(gpr.R(inst.RA), R10);

	ARMReg vD0 = fpr.R0(inst.RS);
	VCVT(S0, vD0, 0);

	if (!inst.W)
	{
		ARMReg vD1 = fpr.R1(inst.RS);
		VCVT(S1, vD1, 0);
	}
	// floats passed through D0
	JitArmAsmRoutineManager::GenPairedLoadStore ourStore = asm_routines.ARMPairedStoreQuantized[gqr.ST_TYPE + (inst.W ? 8 : 0)];
	(asm_routines.*ourStore)(this, gqr.ST_SCALE);
}

void JitArm::psq_stx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStorePairedOff)

	bool update = inst.SUBOP10 == 39;
	
	// R12 contains scale
	// R11 contains type
	// R10 is the ADDR
	if (js.memcheck || !Core::g_CoreStartupParameter.bFastmem) { Default(inst); return; }

	js.block_flags &= BLOCK_USES_GQR0 << inst.Ix;

	const UGQR gqr(rSPR(SPR_GQR0 + inst.Ix));

	if (inst.RA || update) // Always uses the register on update
		ADD(R10, gpr.R(inst.RA), gpr.R(inst.RB));
	else
		MOV(R10, gpr.R(inst.RB));

	if (update)
		MOV(gpr.R(inst.RA), R10);

	ARMReg vD0 = fpr.R0(inst.RS);
	VCVT(S0, vD0, 0);

	if (!inst.Wx)
	{
		ARMReg vD1 = fpr.R1(inst.RS);
		VCVT(S1, vD1, 0);
	}
	// floats passed through D0
	JitArmAsmRoutineManager::GenPairedLoadStore ourStore = asm_routines.ARMPairedStoreQuantized[gqr.ST_TYPE + (inst.Wx ? 8 : 0)];
	(asm_routines.*ourStore)(this, gqr.ST_SCALE);
}
