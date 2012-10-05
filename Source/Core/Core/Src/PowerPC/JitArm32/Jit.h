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
#include "../CPUCoreBase.h"
#include "../PPCAnalyst.h"
#include "JitArmCache.h"
#include "JitRegCache.h"
#include "JitAsm.h"


// Use these to control the instruction selection
// #define INSTRUCTION_START Default(inst); return;
// #define INSTRUCTION_START PPCTables::CountInstruction(inst);
#define INSTRUCTION_START
#define JITDISABLE(type) \
	if (Core::g_CoreStartupParameter.bJITOff || \
	Core::g_CoreStartupParameter.bJIT##type##Off) \
	{Default(inst); return;}


class JitArm : public CPUCoreBase, public ArmGen::ARMXCodeBlock 
{
private:
	struct JitState
	{
		u32 compilerPC;
		u32 next_compilerPC;
		u32 blockStart;
		bool cancel;
		bool skipnext;
		UGeckoInstruction next_inst;  // for easy peephole opt.
		int blockSize;
		int instructionNumber;
		int downcountAmount;
		int block_flags;

		bool isLastInstruction;
		bool memcheck;

		int fifoBytesThisBlock;

		PPCAnalyst::BlockStats st;
		PPCAnalyst::BlockRegStats gpa;
		PPCAnalyst::BlockRegStats fpa;
		PPCAnalyst::CodeOp *op;

		JitBlock *curBlock;
	};
	struct JitOptions
	{
		bool optimizeGatherPipe;
		bool enableBlocklink;
	};
	JitArmBlockCache blocks;

	JitArmAsmRoutineManager asm_routines;

	// TODO: Make arm specific versions of these, shouldn't be too hard to
	// make it so we allocate some space at the start(?) of code generation
	// and keep the registers in a cache. Will burn this bridge when we get to
	// it.
	ArmRegCache gpr;
	//FPURegCache fpr;

	PPCAnalyst::CodeBuffer code_buffer;
	void DoDownCount();

public:
	JitArm() : code_buffer(32000) {}
	~JitArm() {}

	JitState js;
	JitOptions jo;
	void Init();
	void Shutdown();

	// Jit!

	void Jit(u32 em_address);
	const u8* DoJit(u32 em_address, PPCAnalyst::CodeBuffer *code_buf, JitBlock *b);
	JitArmBlockCache *GetBlockCache() { return &blocks; }


	void Trace();

	void ClearCache();

	const u8 *GetDispatcher() {
		return 0; 
	}
	JitArmAsmRoutineManager *GetAsmRoutines() {
		return &asm_routines; 
	}

	const char *GetName() {
		return "JITARM";
	}
	// Run!

	void Run();
	void SingleStep();

	// Utilities for use by opcodes

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInR(ARMReg Reg);
	void WriteRfiExitDestInR(ARMReg Reg);
	void WriteExceptionExit();
	void WriteCallInterpreter(UGeckoInstruction _inst);
	void Cleanup();

	void GenerateRC(int cr = 0);
	void ComputeRC(int cr = 0);

	// OPCODES
	void unknown_instruction(UGeckoInstruction _inst);
	void Default(UGeckoInstruction _inst);
	void DoNothing(UGeckoInstruction _inst);
	void HLEFunction(UGeckoInstruction _inst);

	void DynaRunTable4(UGeckoInstruction _inst);
	void DynaRunTable19(UGeckoInstruction _inst);
	void DynaRunTable31(UGeckoInstruction _inst);
	void DynaRunTable59(UGeckoInstruction _inst);
	void DynaRunTable63(UGeckoInstruction _inst);

	// Breakin shit
	void Break(UGeckoInstruction _inst);
	// Branch
	void bx(UGeckoInstruction _inst);
	void bcx(UGeckoInstruction _inst);
	void bclrx(UGeckoInstruction _inst);
	void sc(UGeckoInstruction _inst);
	void rfi(UGeckoInstruction _inst);
	void bcctrx(UGeckoInstruction _inst);
		
	// Integer
	void addi(UGeckoInstruction _inst);
	void addis(UGeckoInstruction _inst);
	void addx(UGeckoInstruction _inst);
	void cmp (UGeckoInstruction _inst);
	void cmpi(UGeckoInstruction _inst);
	void cmpli(UGeckoInstruction _inst);
	void negx(UGeckoInstruction _inst);
	void ori(UGeckoInstruction _inst);	
	void orx(UGeckoInstruction _inst);
	void rlwinmx(UGeckoInstruction _inst);
	void extshx(UGeckoInstruction inst);
	void extsbx(UGeckoInstruction inst);

	// System Registers
	void mtmsr(UGeckoInstruction _inst);
	void mtspr(UGeckoInstruction _inst);
	void mfspr(UGeckoInstruction _inst);

	// LoadStore
	void icbi(UGeckoInstruction _inst);
};
void ArmJit(u32 *em_address);


#endif // _JIT64_H
