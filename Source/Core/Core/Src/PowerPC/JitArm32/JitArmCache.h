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

#ifndef _JITARMCACHE_H
#define _JITARMCACHE_H

#include <map>
#include <vector>

#include "../Gekko.h"
#include "../PPCAnalyst.h"
#include "../JitCommon/JitCache.h"


typedef void (*CompiledCode)();

class JitArmBlockCache
{
	const u8 **blockCodePointers;
	JitBlock *blocks;
	int num_blocks;
	std::multimap<u32, int> links_to;
	std::map<std::pair<u32,u32>, u32> block_map; // (end_addr, start_addr) -> number
#ifdef JIT_UNLIMITED_ICACHE
	u8 *iCache;
	u8 *iCacheEx;
	u8 *iCacheVMEM;
#endif
	int MAX_NUM_BLOCKS;

	bool RangeIntersect(int s1, int e1, int s2, int e2) const;
	void LinkBlockExits(int i);
	void LinkBlock(int i);

public:
	JitArmBlockCache() :
		blockCodePointers(0), blocks(0), num_blocks(0),
#ifdef JIT_UNLIMITED_ICACHE	
		iCache(0), iCacheEx(0), iCacheVMEM(0), 
#endif
		MAX_NUM_BLOCKS(0) { }
	int AllocateBlock(u32 em_address);
	void FinalizeBlock(int block_num, bool block_link, const u8 *code_ptr);

	void Clear();
	void ClearSafe();
	void Init();
	void Shutdown();
	void Reset();

	bool IsFull() const;

	// Code Cache
	JitBlock *GetBlock(int block_num);
	int GetNumBlocks() const;
	const u8 **GetCodePointers();
#ifdef JIT_UNLIMITED_ICACHE
	u8 *GetICache();
	u8 *GetICacheEx();
	u8 *GetICacheVMEM();
#endif

	// Fast way to get a block. Only works on the first ppc instruction of a block.
	int GetBlockNumberFromStartAddress(u32 em_address);

    // slower, but can get numbers from within blocks, not just the first instruction.
	// WARNING! WILL NOT WORK WITH INLINING ENABLED (not yet a feature but will be soon)
	// Returns a list of block numbers - only one block can start at a particular address, but they CAN overlap.
	// This one is slow so should only be used for one-shots from the debugger UI, not for anything during runtime.
	void GetBlockNumbersFromAddress(u32 em_address, std::vector<int> *block_numbers);

	u32 GetOriginalFirstOp(int block_num);
	CompiledCode GetCompiledCodeFromBlock(int block_num);

	// DOES NOT WORK CORRECTLY WITH INLINING
	void InvalidateICache(u32 em_address);
	void DestroyBlock(int block_num, bool invalidate);

	// Not currently used
	//void DestroyBlocksWithFlag(BlockFlag death_flag);
};

#endif
