// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <map>

#include "Common.h"
#include "../../HLE/HLE.h"
#include "../../Core.h"
#include "../../PatchEngine.h"
#include "../../CoreTiming.h"
#include "../../ConfigManager.h"
#include "../PowerPC.h"
#include "../Profiler.h"
#include "../PPCTables.h"
#include "../PPCAnalyst.h"
#include "../../HW/Memmap.h"
#include "../../HW/GPFifo.h"
#include "JitIL.h"
#include "JitIL_Tables.h"
#include "ArmEmitter.h"
#include "../JitInterface.h"

using namespace ArmGen;
using namespace PowerPC;

static int CODE_SIZE = 1024*1024*32;
namespace CPUCompare
{
	extern u32 m_BlockStart;
}
void JitArmIL::Init()
{
	AllocCodeSpace(CODE_SIZE);
	blocks.Init();
	asm_routines.Init();
}

void JitArmIL::ClearCache()
{
	ClearCodeSpace();
	blocks.Clear();
}

void JitArmIL::Shutdown()
{
	FreeCodeSpace();
	blocks.Shutdown();
	asm_routines.Shutdown();
}
void JitArmIL::unknown_instruction(UGeckoInstruction inst)
{
	//	CCPU::Break();
	PanicAlert("unknown_instruction %08x - Fix me ;)", inst.hex);
}

void JitArmIL::Default(UGeckoInstruction _inst)
{
	ibuild.EmitInterpreterFallback(
		ibuild.EmitIntConst(_inst.hex),
		ibuild.EmitIntConst(js.compilerPC));
}

void JitArmIL::HLEFunction(UGeckoInstruction _inst)
{
	// XXX
}

void JitArmIL::DoNothing(UGeckoInstruction _inst)
{
	// Yup, just don't do anything.
}
void JitArmIL::Break(UGeckoInstruction _inst)
{
	ibuild.EmitINT3();
}

void JitArmIL::DoDownCount()
{
	ARMReg rA = R14;
	ARMReg rB = R12;
	MOVI2R(rA, (u32)&CoreTiming::downcount);
	LDR(rB, rA);
	if(js.downcountAmount < 255) // We can enlarge this if we used rotations
	{
		SUBS(rB, rB, js.downcountAmount);
		STR(rB, rA);
	}
	else
	{
		ARMReg rC = R11;
		MOVI2R(rC, js.downcountAmount);
		SUBS(rB, rB, rC);
		STR(rB, rA);
	}
}

void JitArmIL::WriteExitDestInReg(ARMReg Reg)
{
	STR(Reg, R9, PPCSTATE_OFF(pc));
	DoDownCount();
	MOVI2R(Reg, (u32)asm_routines.dispatcher);
	B(Reg);
}

void JitArmIL::WriteRfiExitDestInR(ARMReg Reg)
{
	STR(Reg, R9, PPCSTATE_OFF(pc));
	DoDownCount();
	MOVI2R(Reg, (u32)asm_routines.testExceptions);
	B(Reg);
}
void JitArmIL::WriteExceptionExit()
{
	DoDownCount();

	MOVI2R(R14, (u32)asm_routines.testExceptions);
	B(R14);
}
void JitArmIL::WriteExit(u32 destination)
{
	DoDownCount();
	//If nobody has taken care of this yet (this can be removed when all branches are done)
	JitBlock *b = js.curBlock;
	JitBlock::LinkData linkData;
	linkData.exitAddress = destination;
	linkData.exitPtrs = GetWritableCodePtr();

	// Link opportunity!
	int block = blocks.GetBlockNumberFromStartAddress(destination);
	if (block >= 0 && jo.enableBlocklink)
	{
		// It exists! Joy of joy!
		B(blocks.GetBlock(block)->checkedEntry);
		linkData.linkStatus = true;
	}
	else
	{
		MOVI2R(R14, destination);
		STR(R14, R9, PPCSTATE_OFF(pc));
		MOVI2R(R14, (u32)asm_routines.dispatcher);
		B(R14);
	}

	b->linkData.push_back(linkData);
}
void JitArmIL::PrintDebug(UGeckoInstruction inst, u32 level)
{
	if (level > 0)
		printf("Start: %08x OP '%s' Info\n", (u32)GetCodePtr(),  PPCTables::GetInstructionName(inst));
	if (level > 1)
	{
		GekkoOPInfo* Info = GetOpInfo(inst.hex);
		printf("\tOuts\n");
		if (Info->flags & FL_OUT_A)
			printf("\t-OUT_A: %x\n", inst.RA);
		if(Info->flags & FL_OUT_D)
			printf("\t-OUT_D: %x\n", inst.RD);
		printf("\tIns\n");
		// A, AO, B, C, S
		if(Info->flags & FL_IN_A)
			printf("\t-IN_A: %x\n", inst.RA);
		if(Info->flags & FL_IN_A0)
			printf("\t-IN_A0: %x\n", inst.RA);
		if(Info->flags & FL_IN_B)
			printf("\t-IN_B: %x\n", inst.RB);
		if(Info->flags & FL_IN_C)
			printf("\t-IN_C: %x\n", inst.RC);
		if(Info->flags & FL_IN_S)
			printf("\t-IN_S: %x\n", inst.RS);
	}
}

void STACKALIGN JitArmIL::Run()
{
	CompiledCode pExecAddr = (CompiledCode)asm_routines.enterCode;
	pExecAddr();
}

void JitArmIL::SingleStep()
{
	CompiledCode pExecAddr = (CompiledCode)asm_routines.enterCode;
	pExecAddr();
}
void STACKALIGN JitArmIL::Jit(u32 em_address)
{
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull() || Core::g_CoreStartupParameter.bJITNoBlockCache)
	{
		ClearCache();
	}
	int num_inst = 0;
	PPCAnalyst::SuperBlock Block;
	int blockSize = code_buffer.GetSize();
	if (Core::g_CoreStartupParameter.bEnableDebugging)
	{
		// Comment out the following to disable breakpoints (speed-up)
		blockSize = 1;
	}
	FlattenNew(PowerPC::ppcState.pc, Block, num_inst, blockSize, 1); 

	int block_num = blocks.AllocateBlock(PowerPC::ppcState.pc);
	JitBlock *b = blocks.GetBlock(block_num);
	DoJit(PowerPC::ppcState.pc, b, Block, blockSize);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink, Block);
}

void JitArmIL::DoJit(u32 em_address, JitBlock *b, PPCAnalyst::SuperBlock &Block, int blockSize)
{
	// Memory exception on instruction fetch
	bool memory_exception = false;

	if (em_address == 0)
	{
		Core::SetState(Core::CORE_PAUSE);
		PanicAlert("ERROR: Compiling at 0. LR=%08x CTR=%08x", LR, CTR);
	}

	if (Core::g_CoreStartupParameter.bMMU && (em_address & JIT_ICACHE_VMEM_BIT))
	{
		if (!Memory::TranslateAddress(em_address, Memory::FLAG_OPCODE))
		{
			// Memory exception occurred during instruction fetch
			memory_exception = true;
		}
	}

	// Conditionally add profiling code.
	if (Profiler::g_ProfileBlocks) {
		// XXX
	}
	// Start up IR builder (structure that collects the
	// instruction processed by the JIT routines)
	ibuild.Reset();

	u64 codeHash = -1;
	const u8 *normalEntry = GetCodePtr();

	for (auto it = Block.IBlocks.begin(); it != Block.IBlocks.end(); ++it)
	{
		// Translate instructions
		js.currentSuper = &Block;
		js.downcountAmount = 0;
		js.isLastInstruction = false;
		js.blockStart = it->second._blockStart;
		js.fifoBytesThisBlock = 0;
		js.curBlock = b;
		js.currentIBlock = &it->second;
		js.block_flags = 0;
		js.cancel = false;

		js.skipnext = false;
		js.blockSize = it->second.GetSize();
		js.compilerPC = it->second._blockStart;

		js.fpa = &it->second._fpa;
		js.gpa = &it->second._gpa;
		js.st = &it->second._stats;	

		Block._codeEntrypoints[js.blockStart] = (u32)normalEntry;
		js.rewriteStart = (u8*)GetCodePtr();

		// For profiling and IR Writer
		for (int i = 0; i < js.blockSize; i++)
		{
			const u64 inst = it->second._code[i].inst.hex;
			// Ported from boost::hash
			codeHash ^= inst + (codeHash << 6) + (codeHash >> 2);
		}

		for (int i = 0; i < js.blockSize; i++)
		{
			js.compilerPC = it->second._code[i].address;
			js.op = &it->second._code[i];
			js.instructionNumber = i;
			const GekkoOPInfo *opinfo = it->second._code[i].opinfo;
			js.downcountAmount += (opinfo->numCyclesMinusOne + 1);

			if (i == (int)js.blockSize - 1)
			{
				// WARNING - cmp->branch merging will screw this up.
				js.isLastInstruction = true;
				js.next_inst = 0;
				if (Profiler::g_ProfileBlocks) {
					// CAUTION!!! push on stack regs you use, do your stuff, then pop
					PROFILER_VPUSH;
					// get end tic
					PROFILER_QUERY_PERFORMANCE_COUNTER(&b->ticStop);
					// tic counter += (end tic - start tic)
					PROFILER_ADD_DIFF_LARGE_INTEGER(&b->ticCounter, &b->ticStop, &b->ticStart);
					PROFILER_VPOP;
				}
			}
			else
			{
				// help peephole optimizations
				js.next_inst = it->second._code[i + 1].inst;
				js.next_compilerPC = it->second._code[i + 1].address;
			}
			if (!it->second._code[i].skip)
			{
				if (js.memcheck && (opinfo->flags & FL_USE_FPU))
				{
					// Don't do this yet
					BKPT(0x7777);
				}
				JitArmILTables::CompileInstruction(it->second._code[i]);
				if (js.memcheck && (opinfo->flags & FL_LOADSTORE))
				{
					// Don't do this yet
					BKPT(0x666);
				}
			}
		}
		if (memory_exception)
			BKPT(0x500);
		if (!it->second._endsBranch)
		{
			printf("Broken Block going to 0x%08x\n", js.compilerPC + 4);
			WriteExit(js.compilerPC + 4);
		}
	}

	// Perform actual code generation

	WriteCode(em_address);
	b->flags = js.block_flags;
	b->codeSize = (u32)(GetCodePtr() - normalEntry);
	b->originalSize = js.blockSize;

	FlushIcache();
}
