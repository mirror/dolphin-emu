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

// ========================
// See comments in Jit.cpp.
// ========================

// Mystery: Capcom vs SNK 800aa278

// CR flags approach:
//   * Store that "N+Z flag contains CR0" or "S+Z flag contains CR3".
//   * All flag altering instructions flush this
//   * A flush simply does a conditional write to the appropriate CRx.
//   * If flag available, branch code can become absolutely trivial.

// Settings
// ----------
#ifndef _JITARM_H
#define _JITARM_H
#include "../PPCAnalyst.h"
#include "../JitCommon/JitBase.h"
#include "JitAsm.h"


// Use these to control the instruction selection
// #define INSTRUCTION_START Default(inst); return;
// #define INSTRUCTION_START PPCTables::CountInstruction(inst);
#define INSTRUCTION_START


class JitArm : public JitBase
{
private:
	JitArmAsmRoutineManager asm_routines;

	// TODO: Make arm specific versions of these, shouldn't be too hard to
	// make it so we allocate some space at the start(?) of code generation
	// and keep the registers in a cache. Will burn this bridge when we get to
	// it.
	//GPRRegCache gpr;
	//FPURegCache fpr;

	PPCAnalyst::CodeBuffer code_buffer;

public:
	JitArm() : code_buffer(32000) {}
	~JitArm() {}

	void Init();
	void Shutdown();

	// Jit!

	void Jit(u32 em_address);
	const u8* DoJit(u32 em_address, PPCAnalyst::CodeBuffer *code_buf, JitBlock *b);
	JitBlockCache *GetBlockCache() { return &blocks; }


	void Trace();

	void ClearCache();

	const u8 *GetDispatcher() {
		return 0; 
	}
	const CommonAsmRoutines *GetAsmRoutines() {
		return 0; 
	}

	const char *GetName() {
		return "JITARM";
	}
	// Run!

	void Run();
	void SingleStep();

	// Utilities for use by opcodes

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInEAX();
	void WriteExceptionExit();
	void WriteRfiExitDestInEAX();
	void WriteCallInterpreter(UGeckoInstruction _inst);
	void Cleanup();

	// OPCODES
	void unknown_instruction(UGeckoInstruction _inst);
	void Default(UGeckoInstruction _inst);
	void DoNothing(UGeckoInstruction _inst);
	void HLEFunction(UGeckoInstruction _inst);

};


#endif // _JIT64_H
