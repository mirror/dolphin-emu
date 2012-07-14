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
#include "ArmEmitter.h"
#include "ArmABI.h"

using namespace ArmGen;
// Shared code between Win32 and Unix32
void ARMXEmitter::ARMABI_CallFunction(void *func) 
{
	ARMABI_MOVIMM32(R8, (u32)func);	
	PUSH(1, _LR);
	BLX(R8);
	POP(1, _LR);
}
void ARMXEmitter::ARMABI_PushAllCalleeSavedRegsAndAdjustStack() {
	// Note: 4 * 4 = 16 bytes, so alignment is preserved.
	PUSH(4, R0, R1, R2, R3);
}

void ARMXEmitter::ARMABI_PopAllCalleeSavedRegsAndAdjustStack() {
	POP(4, R3, R4, R5, R6);
}
void ARMXEmitter::ARMABI_MOVIMM32(ARMReg reg, u32 val)
{
	// TODO: We can do this in less instructions if we check for if it is
	// smaller than a 32bit variable. Like if it is a 8bit or 14bit(?)
	// variable it should be able to be moved to just a single MOV instruction
	// but for now, we are just taking the long route out and using the MOVW
	// and MOVT
	Operand2 LowVal(val);
	Operand2 HighVal((u16)(val >> 16));
	MOVW(reg, LowVal); MOVT(reg, HighVal);
}
