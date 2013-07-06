// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "ConfigManager.h"
#include "Debugger_SymbolMap.h"
#include "DebugInterface.h"
#include "PPCDebugInterface.h"
#include "PowerPCDisasm.h"
#include "../Host.h"
#include "../Core.h"
#include "../HW/CPU.h"
#include "../HW/DSP.h"
#include "../HW/Memmap.h"
#include "../PowerPC/PowerPC.h"
#include "../PowerPC/JitCommon/JitBase.h"
#include "../PowerPC/PPCSymbolDB.h"

void PPCDebugInterface::disasm(unsigned int address, char *dest, int max_size) 
{
	// Memory::ReadUnchecked_U32 seemed to crash on shutdown
	if (PowerPC::GetState() == PowerPC::CPU_POWERDOWN) return;

	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		if (Memory::IsRAMAddress(address, true, true))
		{
			u32 op = Memory::Read_Instruction(address);
			DisassembleGekko(op, address, dest, max_size);
			UGeckoInstruction inst;
			inst.hex = Memory::ReadUnchecked_U32(address);
			if (inst.OPCD == 1) {
				strcat(dest, " (hle)");
			}
		}
		else
		{
			strcpy(dest, "(No RAM here)");
		}
	}
	else
	{
		strcpy(dest, "<unknown>");
	}
}

void PPCDebugInterface::getRawMemoryString(int memory, unsigned int address, char *dest, int max_size)
{
	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		if (memory || Memory::IsRAMAddress(address, true, true))
		{
			snprintf(dest, max_size, "%d %08X", memory, readExtraMemory(memory, address));
		}
		else
		{
			snprintf(dest, max_size, "%d %s", memory,  "---");
		}
	}
	else
	{
		strcpy(dest, "<unknwn>");  // bad spelling - 8 chars
	}
}

unsigned int PPCDebugInterface::readMemory(unsigned int address)
{
	return Memory::ReadUnchecked_U32(address);
}

unsigned int PPCDebugInterface::readExtraMemory(int memory, unsigned int address)
{
	switch (memory)
	{
	case 0:
		return Memory::ReadUnchecked_U32(address);
	case 1:
		return (DSP::ReadARAM(address)     << 24) |
			   (DSP::ReadARAM(address + 1) << 16) |
			   (DSP::ReadARAM(address + 2) << 8) |
			   (DSP::ReadARAM(address + 3));
	default:
		return 0;
	}
}

unsigned int PPCDebugInterface::readInstruction(unsigned int address)
{
	return Memory::Read_Instruction(address);
}

bool PPCDebugInterface::isAlive()
{
	return Core::GetState() != Core::CORE_UNINITIALIZED;
}

bool PPCDebugInterface::isBreakpoint(unsigned int address) 
{
	return PowerPC::breakpoints.IsAddressBreakPoint(address);
}

void PPCDebugInterface::setBreakpoint(unsigned int address)
{
	PowerPC::breakpoints.Add(address);
}

void PPCDebugInterface::clearBreakpoint(unsigned int address)
{
	PowerPC::breakpoints.Remove(address);
}

void PPCDebugInterface::clearAllBreakpoints() {}

void PPCDebugInterface::toggleBreakpoint(unsigned int address)
{
	if (PowerPC::breakpoints.IsAddressBreakPoint(address))
		PowerPC::breakpoints.Remove(address);
	else
		PowerPC::breakpoints.Add(address);
}

bool PPCDebugInterface::isMemCheck(unsigned int address)
{
	return (PowerPC::memchecks.GetMemCheck(address, 4));
}

void PPCDebugInterface::toggleMemCheck(unsigned int address)
{
	if (!PowerPC::memchecks.GetMemCheck(address, 4))
	{
		// Add Memory Check
		TMemCheck MemCheck;
		MemCheck.StartAddress = address;
		MemCheck.EndAddress = address;
		MemCheck.OnRead = true;
		MemCheck.OnWrite = true;

		MemCheck.Log = true;
		MemCheck.Break = true;

		PowerPC::memchecks.Add(MemCheck);

	}
	else
		PowerPC::memchecks.Remove(address);
}

void PPCDebugInterface::insertBLR(unsigned int address, unsigned int value) 
{
	Memory::Write_U32(value, address);
}

void PPCDebugInterface::breakNow()
{
	CCPU::Break();
}


// =======================================================
// Separate the blocks with colors.
// -------------
int PPCDebugInterface::getColor(unsigned int address)
{
	if (!Memory::IsRAMAddress(address, true, true))
		return 0xeeeeee;
	static const int colors[6] =
	{ 
		0xd0FFFF,  // light cyan
		0xFFd0d0,  // light red
		0xd8d8FF,  // light blue
		0xFFd0FF,  // light purple
		0xd0FFd0,  // light green
		0xFFFFd0,  // light yellow
	};
	Symbol *symbol = g_symbolDB.GetSymbolFromAddr(address);
	if (!symbol)
		return 0xFFFFFF;
	if (symbol->type != Symbol::SYMBOL_FUNCTION)
		return 0xEEEEFF;
	return colors[symbol->index % 6];
}
// =============


std::string PPCDebugInterface::getDescription(unsigned int address) 
{
	return g_symbolDB.GetDescription(address);
}

unsigned int PPCDebugInterface::getPC() 
{
	return PowerPC::ppcState.pc;
}

void PPCDebugInterface::setPC(unsigned int address) 
{
	PowerPC::ppcState.pc = address;
}

void PPCDebugInterface::showJitResults(unsigned int address) 
{
	Host_ShowJitResults(address);
}

void PPCDebugInterface::runToBreakpoint()
{

}
