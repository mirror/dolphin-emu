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

// Enable define below to enable oprofile integration. For this to work,
// it requires at least oprofile version 0.9.4, and changing the build
// system to link the Dolphin executable against libopagent.  Since the
// dependency is a little inconvenient and this is possibly a slight
// performance hit, it's not enabled by default, but it's useful for
// locating performance issues.

#include "Common.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "../../Core.h"
#include "MemoryUtil.h"

#include "../../HW/Memmap.h"
#include "../JitInterface.h"
#include "../../CoreTiming.h"

#include "../PowerPC.h"
#include "../PPCTables.h"
#include "../PPCAnalyst.h"

#include "JitArmCache.h"

// Only included for JIT_OPCODE define
#include "../JitCommon/JitBase.h"

#include "disasm.h"

#if defined USE_OPROFILE && USE_OPROFILE
#include <opagent.h>
#endif

#if defined USE_OPROFILE && USE_OPROFILE
	op_agent_t agent;
#endif

using namespace ArmGen;

#define INVALID_EXIT 0xFFFFFFFF

	bool JitArmBlockCache::IsFull() const 
	{
		return GetNumBlocks() >= MAX_NUM_BLOCKS - 1;
	}

	void JitArmBlockCache::Init()
	{
		MAX_NUM_BLOCKS = 65536*2;

#if defined USE_OPROFILE && USE_OPROFILE
		agent = op_open_agent();
#endif
		blocks = new JitBlock[MAX_NUM_BLOCKS];
		blockCodePointers = new const u8*[MAX_NUM_BLOCKS];
#ifdef JIT_UNLIMITED_ICACHE
		if (iCache == 0 && iCacheEx == 0 && iCacheVMEM == 0)
		{
			iCache = new u8[JIT_ICACHE_SIZE];
			iCacheEx = new u8[JIT_ICACHEEX_SIZE];
			iCacheVMEM = new u8[JIT_ICACHE_SIZE];
		}
		else
		{
			PanicAlert("JitArmBlockCache::Init() - iCache is already initialized");
		}
		if (iCache == 0 || iCacheEx == 0 || iCacheVMEM == 0)
		{
			PanicAlert("JitArmBlockCache::Init() - unable to allocate iCache");
		}
		memset(iCache, JIT_ICACHE_INVALID_BYTE, JIT_ICACHE_SIZE);
		memset(iCacheEx, JIT_ICACHE_INVALID_BYTE, JIT_ICACHEEX_SIZE);
		memset(iCacheVMEM, JIT_ICACHE_INVALID_BYTE, JIT_ICACHE_SIZE);
#endif
		Clear();
	}

	void JitArmBlockCache::Shutdown()
	{
		delete[] blocks;
		delete[] blockCodePointers;
#ifdef JIT_UNLIMITED_ICACHE		
		if (iCache != 0)
			delete[] iCache;
		iCache = 0;
		if (iCacheEx != 0)
			delete[] iCacheEx;
		iCacheEx = 0;
		if (iCacheVMEM != 0)
			delete[] iCacheVMEM;
		iCacheVMEM = 0;
#endif
		blocks = 0;
		blockCodePointers = 0;
		num_blocks = 0;
#if defined USE_OPROFILE && USE_OPROFILE
		op_close_agent(agent);
#endif
	}
	
	// This clears the JIT cache. It's called from JitCache.cpp when the JIT cache
	// is full and when saving and loading states.
	void JitArmBlockCache::Clear()
	{
		Core::DisplayMessage("Clearing code cache.", 3000);
		for (int i = 0; i < num_blocks; i++)
		{
			DestroyBlock(i, false);
		}
		links_to.clear();
		block_map.clear();
		num_blocks = 0;
		memset(blockCodePointers, 0, sizeof(u8*)*MAX_NUM_BLOCKS);
	}

	void JitArmBlockCache::ClearSafe()
	{
#ifdef JIT_UNLIMITED_ICACHE
		memset(iCache, JIT_ICACHE_INVALID_BYTE, JIT_ICACHE_SIZE);
		memset(iCacheEx, JIT_ICACHE_INVALID_BYTE, JIT_ICACHEEX_SIZE);
		memset(iCacheVMEM, JIT_ICACHE_INVALID_BYTE, JIT_ICACHE_SIZE);
#endif
	}

	/*void JitArmBlockCache::DestroyBlocksWithFlag(BlockFlag death_flag)
	{
		for (int i = 0; i < num_blocks; i++)
		{
			if (blocks[i].flags & death_flag)
			{
				DestroyBlock(i, false);
			}
		}
	}*/

	void JitArmBlockCache::Reset()
	{
		Shutdown();
		Init();
	}

	JitBlock *JitArmBlockCache::GetBlock(int no)
	{
		return &blocks[no];
	}

	int JitArmBlockCache::GetNumBlocks() const
	{
		return num_blocks;
	}

	bool JitArmBlockCache::RangeIntersect(int s1, int e1, int s2, int e2) const
	{
		// check if any endpoint is inside the other range
		if ((s1 >= s2 && s1 <= e2) ||
			(e1 >= s2 && e1 <= e2) ||
			(s2 >= s1 && s2 <= e1) ||
			(e2 >= s1 && e2 <= e1)) 
			return true;
		else
			return false;
	}

	int JitArmBlockCache::AllocateBlock(u32 em_address)
	{
		JitBlock &b = blocks[num_blocks];
		b.invalid = false;
		b.originalAddress = em_address;
		b.exitAddress[0] = INVALID_EXIT;
		b.exitAddress[1] = INVALID_EXIT;
		b.exitPtrs[0] = 0;
		b.exitPtrs[1] = 0;
		b.linkStatus[0] = false;
		b.linkStatus[1] = false;
		b.blockNum = num_blocks;
		num_blocks++; //commit the current block
		return num_blocks - 1;
	}

	void JitArmBlockCache::FinalizeBlock(int block_num, bool block_link, const u8 *code_ptr)
	{
		blockCodePointers[block_num] = code_ptr;
		JitBlock &b = blocks[block_num];
		b.originalFirstOpcode = JitInterface::Read_Opcode_JIT(b.originalAddress);
		static bool did = false;
		if(!did)
		{
			did = true;
			printf("Finishing block as %08x. Original Opcode: %08x\n",
			(JIT_OPCODE << 26) | block_num, b.originalFirstOpcode);
		}
		JitInterface::Write_Opcode_JIT(b.originalAddress, (JIT_OPCODE << 26) |
		block_num);
		block_map[std::make_pair(b.originalAddress + 4 * b.originalSize - 1, b.originalAddress)] = block_num;
		if (block_link)
		{
			for (int i = 0; i < 2; i++)
			{
				if (b.exitAddress[i] != INVALID_EXIT) 
					links_to.insert(std::pair<u32, int>(b.exitAddress[i], block_num));
			}
			
			LinkBlock(block_num);
			LinkBlockExits(block_num);
		}

#if defined USE_OPROFILE && USE_OPROFILE
		char buf[100];
		sprintf(buf, "EmuCode%x", b.originalAddress);
		const u8* blockStart = blockCodePointers[block_num];
		op_write_native_code(agent, buf, (uint64_t)blockStart,
		                     blockStart, b.codeSize);
#endif
	}

	const u8 **JitArmBlockCache::GetCodePointers()
	{
		return blockCodePointers;
	}

#ifdef JIT_UNLIMITED_ICACHE
	u8* JitArmBlockCache::GetICache()
	{
		return iCache;
	}

	u8* JitArmBlockCache::GetICacheEx()
	{
		return iCacheEx;
	}

	u8* JitArmBlockCache::GetICacheVMEM()
	{
		return iCacheVMEM;
	}
#endif

	int JitArmBlockCache::GetBlockNumberFromStartAddress(u32 addr)
	{
		if (!blocks)
			return -1;		
#ifdef JIT_UNLIMITED_ICACHE
		u32 inst;
		if (addr & JIT_ICACHE_VMEM_BIT)
		{
			inst = *(u32*)(iCacheVMEM + (addr & JIT_ICACHE_MASK));
		}
		else if (addr & JIT_ICACHE_EXRAM_BIT)
		{
			inst = *(u32*)(iCacheEx + (addr & JIT_ICACHEEX_MASK));
		}
		else
		{
			inst = *(u32*)(iCache + (addr & JIT_ICACHE_MASK));
		}
		inst = Common::swap32(inst);
#else
		u32 inst = Memory::ReadFast32(addr);
#endif
		if (inst & 0xfc000000) // definitely not a JIT block
			return -1;
		if ((int)inst >= num_blocks)
			return -1;
		if (blocks[inst].originalAddress != addr)
			return -1;		
		return inst;
	}

	void JitArmBlockCache::GetBlockNumbersFromAddress(u32 em_address, std::vector<int> *block_numbers)
	{
		for (int i = 0; i < num_blocks; i++)
			if (blocks[i].ContainsAddress(em_address))
				block_numbers->push_back(i);
	}

	u32 JitArmBlockCache::GetOriginalFirstOp(int block_num)
	{
		if (block_num >= num_blocks)
		{
			//PanicAlert("JitArmBlockCache::GetOriginalFirstOp - block_num = %u is out of range", block_num);
			return block_num;
		}
		return blocks[block_num].originalFirstOpcode;
	}

	CompiledCode JitArmBlockCache::GetCompiledCodeFromBlock(int block_num)
	{		
		return (CompiledCode)blockCodePointers[block_num];
	}

	//Block linker
	//Make sure to have as many blocks as possible compiled before calling this
	//It's O(N), so it's fast :)
	//Can be faster by doing a queue for blocks to link up, and only process those
	//Should probably be done

	void JitArmBlockCache::LinkBlockExits(int i)
	{
		JitBlock &b = blocks[i];
		if (b.invalid)
		{
			// This block is dead. Don't relink it.
			return;
		}
		for (int e = 0; e < 2; e++)
		{
			if (b.exitAddress[e] != INVALID_EXIT && !b.linkStatus[e])
			{
				int destinationBlock = GetBlockNumberFromStartAddress(b.exitAddress[e]);
				if (destinationBlock != -1)
				{
					#warning FIXME: Block linking won't work without this!
					//XEmitter emit(b.exitPtrs[e]);
					//emit.JMP(blocks[destinationBlock].checkedEntry, true);
					b.linkStatus[e] = true;
				}
			}
		}
	}

	using namespace std;

	void JitArmBlockCache::LinkBlock(int i)
	{
		LinkBlockExits(i);
		JitBlock &b = blocks[i];
		std::map<u32, int>::iterator iter;
		pair<multimap<u32, int>::iterator, multimap<u32, int>::iterator> ppp;
		// equal_range(b) returns pair<iterator,iterator> representing the range
		// of element with key b
		ppp = links_to.equal_range(b.originalAddress);
		if (ppp.first == ppp.second)
			return;
		for (multimap<u32, int>::iterator iter2 = ppp.first; iter2 != ppp.second; ++iter2) {
			// PanicAlert("Linking block %i to block %i", iter2->second, i);
			LinkBlockExits(iter2->second);
		}
	}

	void JitArmBlockCache::DestroyBlock(int block_num, bool invalidate)
	{
		if (block_num < 0 || block_num >= num_blocks)
		{
			PanicAlert("DestroyBlock: Invalid block number %d", block_num);
			return;
		}
		JitBlock &b = blocks[block_num];
		if (b.invalid)
		{
			if (invalidate)
				PanicAlert("Invalidating invalid block %d", block_num);
			return;
		}
		b.invalid = true;
#ifdef JIT_UNLIMITED_ICACHE
		JitInterface::Write_Opcode_JIT(b.originalAddress, b.originalFirstOpcode?b.originalFirstOpcode:JIT_ICACHE_INVALID_WORD);
#else
		if (Memory::ReadFast32(b.originalAddress) == block_num)
			Memory::WriteUnchecked_U32(b.originalFirstOpcode, b.originalAddress);
#endif

		// We don't unlink blocks, we just send anyone who tries to run them back to the dispatcher.
		// Not entirely ideal, but .. pretty good.
		// Spurious entrances from previously linked blocks can only come through checkedEntry
		#warning FIXME! Block destroying won't work right with this!
		//XEmitter emit((u8 *)b.checkedEntry);
		//emit.MOV(32, M(&PC), Imm32(b.originalAddress));
		//emit.JMP(jit->GetAsmRoutines()->dispatcher, true);
		// this is not needed really
		/*
		emit.SetCodePtr((u8 *)blockCodePointers[blocknum]);
		emit.MOV(32, M(&PC), Imm32(b.originalAddress));
		emit.JMP(asm_routines.dispatcher, true);
		*/
	}


	void JitArmBlockCache::InvalidateICache(u32 address)
	{
		address &= ~0x1f;
		// destroy JIT blocks
		// !! this works correctly under assumption that any two overlapping blocks end at the same address
		std::map<pair<u32,u32>, u32>::iterator it1 = block_map.lower_bound(std::make_pair(address, 0)), it2 = it1, it;
		while (it2 != block_map.end() && it2->first.second < address + 0x20)
		{
			DestroyBlock(it2->second, true);
			it2++;
		}
		if (it1 != it2)
		{
			block_map.erase(it1, it2);
		}

#ifdef JIT_UNLIMITED_ICACHE
		// invalidate iCache.
		// icbi can be called with any address, so we should check
		if ((address & ~JIT_ICACHE_MASK) != 0x80000000 && (address & ~JIT_ICACHE_MASK) != 0x00000000 &&
			(address & ~JIT_ICACHE_MASK) != 0x7e000000 && // TLB area
			(address & ~JIT_ICACHEEX_MASK) != 0x90000000 && (address & ~JIT_ICACHEEX_MASK) != 0x10000000)
		{
			return;
		}
		if (address & JIT_ICACHE_VMEM_BIT)
		{
			u32 cacheaddr = address & JIT_ICACHE_MASK;
			memset(iCacheVMEM + cacheaddr, JIT_ICACHE_INVALID_BYTE, 32);
		}
		else if (address & JIT_ICACHE_EXRAM_BIT)
		{
			u32 cacheaddr = address & JIT_ICACHEEX_MASK;
			memset(iCacheEx + cacheaddr, JIT_ICACHE_INVALID_BYTE, 32);
		}
		else
		{
			u32 cacheaddr = address & JIT_ICACHE_MASK;
			memset(iCache + cacheaddr, JIT_ICACHE_INVALID_BYTE, 32);
		}
#endif
	}
