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

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#include "JitInterface.h"
#include "JitCommon/JitBase.h"

#ifndef _M_GENERIC
#include "Jit64IL/JitIL.h"
#include "Jit64/Jit.h"
#include "Jit64/Jit64_Tables.h"
#include "Jit64IL/JitIL_Tables.h"
#endif

#include "Profiler.h"
#include "PPCSymbolDB.h"

namespace JitInterface
{
	void DoState(PointerWrap &p)
	{
		if (jit && p.GetMode() == PointerWrap::MODE_READ)
			jit->GetBlockCache()->ClearSafe();
	}
	CPUCoreBase *InitJitCore(int core)
	{
		CPUCoreBase *ptr = NULL;
		switch(core)
		{
			#ifndef _M_GENERIC
			case 1:
			{
				ptr = new Jit64();
				break;
			}
			case 2:
			{
				ptr = new JitIL();
				break;
			}
			#endif
			default:
			{
				PanicAlert("Unrecognizable cpu_core: %d", core);
				return NULL;
				break;
			}
		}
		jit = static_cast<JitBase*>(ptr);
		jit->Init();
		return ptr;
	}
	void InitTables(int core)
	{
		switch(core)
		{
			#ifndef _M_GENERIC
			case 1:
			{
				Jit64Tables::InitTables();
				break;
			}
			case 2:
			{
				JitILTables::InitTables();
				break;
			}
			#endif
			default:
			{
				PanicAlert("Unrecognizable cpu_core: %d", core);
				break;
			}
		}
	}
	CPUCoreBase *GetCore()
	{
		return jit;
	}

	void WriteProfileResults(const char *filename)
	{
		// Can't really do this with no jit core available
		#ifndef _M_GENERIC
		
		std::vector<BlockStat> stats;
		stats.reserve(jit->GetBlockCache()->GetNumBlocks());
		u64 cost_sum = 0;
	#ifdef _WIN32
		u64 timecost_sum = 0;
		u64 countsPerSec;
		QueryPerformanceFrequency((LARGE_INTEGER *)&countsPerSec);
	#endif
		for (int i = 0; i < jit->GetBlockCache()->GetNumBlocks(); i++)
		{
			const JitBlock *block = jit->GetBlockCache()->GetBlock(i);
			// Rough heuristic.  Mem instructions should cost more.
			u64 cost = block->originalSize * (block->runCount / 4);
	#ifdef _WIN32
			u64 timecost = block->ticCounter;
	#endif
			// Todo: tweak.
			if (block->runCount >= 1)
				stats.push_back(BlockStat(i, cost));
			cost_sum += cost;
	#ifdef _WIN32
			timecost_sum += timecost;
	#endif
		}

		sort(stats.begin(), stats.end());
		File::IOFile f(filename, "w");
		if (!f)
		{
			PanicAlert("failed to open %s", filename);
			return;
		}
		fprintf(f.GetHandle(), "origAddr\tblkName\tcost\ttimeCost\tpercent\ttimePercent\tOvAllinBlkTime(ms)\tblkCodeSize\n");
		for (unsigned int i = 0; i < stats.size(); i++)
		{
			const JitBlock *block = jit->GetBlockCache()->GetBlock(stats[i].blockNum);
			if (block)
			{
				std::string name = g_symbolDB.GetDescription(block->originalAddress);
				double percent = 100.0 * (double)stats[i].cost / (double)cost_sum;
	#ifdef _WIN32 
				double timePercent = 100.0 * (double)block->ticCounter / (double)timecost_sum;
				fprintf(f.GetHandle(), "%08x\t%s\t%llu\t%llu\t%.2lf\t%llf\t%lf\t%i\n", 
						block->originalAddress, name.c_str(), stats[i].cost,
						block->ticCounter, percent, timePercent,
						(double)block->ticCounter*1000.0/(double)countsPerSec, block->codeSize);
	#else
				fprintf(f.GetHandle(), "%08x\t%s\t%llu\t???\t%.2lf\t???\t???\t%i\n", 
						block->originalAddress, name.c_str(), stats[i].cost,  percent, block->codeSize);
	#endif
			}
		}
		#endif
	}
	
	void Shutdown()
	{
		if (jit)
		{
			jit->Shutdown();
			delete jit;
			jit = NULL;
		}
	}
}
