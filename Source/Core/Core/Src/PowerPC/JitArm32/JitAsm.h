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

#ifndef _JITARMASM_H
#define _JITARMASM_H
#include "ArmEmitter.h"
#include "../JitCommon/JitAsmCommon.h"
using namespace ArmGen;

class JitArmAsmRoutineManager : public CommonAsmRoutinesBase, public ARMXCodeBlock 
{
private:
	void Generate();
	void GenerateCommon();

	// Gens
	void GenPairedIllegal(ARMXEmitter *emit, u32 level);
	void GenloadPairedFloatTwo(ARMXEmitter *emit, u32 level);
	void GenloadPairedFloatOne(ARMXEmitter *emit, u32 level);
	void GenloadPairedU8Two(ARMXEmitter *emit, u32 level);
	void GenloadPairedU8One(ARMXEmitter *emit, u32 level);
	void GenloadPairedS8Two(ARMXEmitter *emit, u32 level);
	void GenloadPairedS8One(ARMXEmitter *emit, u32 level);
	void GenloadPairedU16Two(ARMXEmitter *emit, u32 level);
	void GenloadPairedU16One(ARMXEmitter *emit, u32 level);
	void GenloadPairedS16Two(ARMXEmitter *emit, u32 level);
	void GenloadPairedS16One(ARMXEmitter *emit, u32 level);

	void GenstorePairedFloat(ARMXEmitter *emit, u32 level);
	void GenstoreSingleFloat(ARMXEmitter *emit, u32 level);
	void GenstorePairedS8(ARMXEmitter *emit, u32 level);
	void GenstoreSingleS8(ARMXEmitter *emit, u32 level);
	void GenstorePairedS16(ARMXEmitter *emit, u32 level);
	void GenstoreSingleS16(ARMXEmitter *emit, u32 level);

public:
	
	typedef void (JitArmAsmRoutineManager::* GenPairedLoadStore)(ARMXEmitter*, u32);
	GenPairedLoadStore ARMPairedLoadQuantized[16];
	GenPairedLoadStore ARMPairedStoreQuantized[16];
	void Init() {
		AllocCodeSpace(8192);
		Generate();
		WriteProtect();
	}

	void Shutdown() {
		FreeCodeSpace();
	}
};

extern JitArmAsmRoutineManager asm_routines;

#endif  // _JIT64ASM_H
