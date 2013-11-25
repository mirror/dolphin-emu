// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _PPCANALYST_H
#define _PPCANALYST_H

#include <algorithm>
#include <vector>
#include <map>

#include <cstdlib>
#include <string>

#include "Common.h"
#include "PPCTables.h"

class PPCSymbolDB;
struct Symbol;

namespace PPCAnalyst
{

struct CodeOp //16B
{
	UGeckoInstruction inst;
	GekkoOPInfo * opinfo;
	u32 address;
	u32 branchTo; //if 0, not a branch
	int branchToIndex; //index of target block
	s8 regsOut[2];
	s8 regsIn[3];
	s8 fregOut;
	s8 fregsIn[3];
	bool isBranchTarget;
	bool wantsCR0;
	bool wantsCR1;
	bool wantsPS1;
	bool outputCR0;
	bool outputCR1;
	bool outputPS1;
	bool skip;  // followed BL-s for example
};

struct BlockStats
{
	bool isFirstBlockOfFunction;
	bool isLastBlockOfFunction;
	int numCycles;
};

struct BlockRegStats
{
	short firstRead[32];
	short firstWrite[32];
	short lastRead[32];
	short lastWrite[32];
	short numReads[32];
	short numWrites[32];

	bool any;
	bool anyTimer;

	int GetTotalNumAccesses(int reg) {return numReads[reg] + numWrites[reg];}
	int GetUseRange(int reg) {
		return std::max(lastRead[reg], lastWrite[reg]) -
			   std::min(firstRead[reg], firstWrite[reg]);}

	inline void SetInputRegister(int reg, short opindex) {
		if (firstRead[reg] == -1)
			firstRead[reg] = (short)(opindex);
		lastRead[reg] = (short)(opindex);
		numReads[reg]++;
	}

	inline void SetOutputRegister(int reg, short opindex) {
		if (firstWrite[reg] == -1)
			firstWrite[reg] = (short)(opindex);
		lastWrite[reg] = (short)(opindex);
		numWrites[reg]++;
	}
};


class CodeBuffer
{
	int size_;
public:
	CodeBuffer(int size);
	~CodeBuffer();

	int GetSize() const { return size_; }

	PPCAnalyst::CodeOp *codebuffer;


};
class IBlock
{
	public:
	enum Type {
		SIMPLE,
		COMPLEX,
	};

	const static u32 FLAG_EXTERNAL_JUMP = 1 << 0;
	const static u32 FLAG_INTERNAL_JUMP = 1 << 1;
	const static u32 FLAG_IBLOCK_JUMP   = 1 << 2;
	const static u32 FLAG_FINAL_JUMP    = 1 << 3;
	const static u32 FLAG_INLINE_JUMP   = 1 << 4;
	struct Inst
	{
		u32 _hex;
		u32 _flags;
		u32 _target;
	};
	private:
	// Index of entry points
	std::vector<u32> _entrypoints;
	// Index of exit points
	// There will be only one exit point in a IBlock
	std::vector<u32> _exitpoints;
	// Index of instruction hexes
	std::vector<Inst> _instructions;
	// Index of codeOps
	std::map<u32, CodeOp> _code;
	BlockStats _stats;
	BlockRegStats _gpa;
	BlockRegStats _fpa;
	Type _type;
	u32 _blockStart;
	bool _endsBLR;
	public:
	IBlock() {}
	void Flatten(u32 address, u32 minAddress, u32 maxAddress, u32 *numInst, u32 blockSize, bool inlineJumps = true);
	bool EndsBLR() { return _endsBLR; }
	bool ContainsEntryPoint(u32 addr) { for (auto it : _entrypoints) if (it == addr) return true; return false; }
	std::vector<Inst>& GetInstructions() { return _instructions; }
	u32 GetStart() { return _blockStart; }
	u32 GetSize() { return _instructions.size(); }
	Type GetType() { return _type; }
	bool Merge(IBlock *block);
	
};
void FlattenNew(u32 address, std::map<u32, IBlock> &IBlocks, 
			bool &broken_block, CodeBuffer *buffer,
			int blockSize);

u32 Flatten(u32 address, int *realsize, BlockStats *st, BlockRegStats *gpa,
			BlockRegStats *fpa, bool &broken_block, CodeBuffer *buffer,
			int blockSize, u32* merged_addresses,
			int capacity_of_merged_addresses, int& size_of_merged_addresses);
void LogFunctionCall(u32 addr);
void FindFunctions(u32 startAddr, u32 endAddr, PPCSymbolDB *func_db);
bool AnalyzeFunction(u32 startAddr, Symbol &func, int max_size = 0);

}  // namespace

#endif

