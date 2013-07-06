// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <map>
#include <string>
#include <vector>

#include "Common.h"
#include "FileUtil.h"

#include "../HW/Memmap.h"
#include "../PowerPC/PowerPC.h"
#include "../Host.h"
#include "StringUtil.h"
#include "PPCSymbolDB.h"
#include "SignatureDB.h"
#include "PPCAnalyst.h"

PPCSymbolDB g_symbolDB;

PPCSymbolDB::PPCSymbolDB()
{
	// Get access to the disasm() fgnction
	debugger = &PowerPC::debug_interface;
}

PPCSymbolDB::~PPCSymbolDB()
{
}

// Adds the function to the list, unless it's already there
Symbol *PPCSymbolDB::AddFunction(u32 startAddr)
{
	if (startAddr < 0x80000010)
		return 0;
	XFuncMap::iterator iter = functions.find(startAddr);
	if (iter != functions.end())
	{
		// it's already in the list
		return 0;
	}
	else
	{
		Symbol tempFunc; //the current one we're working on
		u32 targetEnd = PPCAnalyst::AnalyzeFunction(startAddr, tempFunc);
		if (targetEnd == 0)
			return 0;  //found a dud :(
		//LOG(OSHLE, "Symbol found at %08x", startAddr);
		functions[startAddr] = tempFunc;
		tempFunc.type = Symbol::SYMBOL_FUNCTION;
		checksumToFunction[tempFunc.hash] = &(functions[startAddr]);
		return &functions[startAddr];
	}
}

void PPCSymbolDB::AddKnownSymbol(u32 startAddr, u32 size, const char *name, int type)
{
	XFuncMap::iterator iter = functions.find(startAddr);
	if (iter != functions.end())
	{
		// already got it, let's just update name, checksum & size to be sure.
		Symbol *tempfunc = &iter->second;
		tempfunc->name = name;
		tempfunc->hash = SignatureDB::ComputeCodeChecksum(startAddr, startAddr + size);
		tempfunc->type = type;
		tempfunc->size = size;
	}
	else
	{
		// new symbol. run analyze.
		Symbol tf;
		tf.name = name;
		tf.type = type;
		tf.address = startAddr;
		if (tf.type == Symbol::SYMBOL_FUNCTION) {
			PPCAnalyst::AnalyzeFunction(startAddr, tf, size);
			checksumToFunction[tf.hash] = &(functions[startAddr]);
		}
		tf.size = size;
		functions[startAddr] = tf;
	}
}

Symbol *PPCSymbolDB::GetSymbolFromAddr(u32 addr)
{
	if (!Memory::IsRAMAddress(addr))
		return 0;

	XFuncMap::iterator it = functions.find(addr);
	if (it != functions.end())
	{
		return &it->second;
	}
	else
	{
		for (XFuncMap::iterator iter = functions.begin(); iter != functions.end(); ++iter)
		{
			if (addr >= iter->second.address && addr < iter->second.address + iter->second.size)
				return &iter->second;
		}
	}
	return 0;
}

const char *PPCSymbolDB::GetDescription(u32 addr)
{
	Symbol *symbol = GetSymbolFromAddr(addr);
	if (symbol)
		return symbol->name.c_str();
	else
		return " --- ";
}

void PPCSymbolDB::FillInCallers()
{
	for (XFuncMap::iterator iter = functions.begin(); iter != functions.end(); ++iter)
	{
		iter->second.callers.clear();
	}

	for (XFuncMap::iterator iter = functions.begin(); iter != functions.end(); ++iter)
	{
		Symbol &f = iter->second;
		for (size_t i = 0; i < f.calls.size(); i++)
		{
			SCall NewCall(iter->first, f.calls[i].callAddress);
			u32 FunctionAddress = f.calls[i].function;

			XFuncMap::iterator FuncIterator = functions.find(FunctionAddress);
			if (FuncIterator != functions.end())
			{
				Symbol& rCalledFunction = FuncIterator->second;
				rCalledFunction.callers.push_back(NewCall);
			}
			else
			{
				//LOG(OSHLE, "FillInCallers tries to fill data in an unknown function 0x%08x.", FunctionAddress);
				// TODO - analyze the function instead.
			}
		}
	}
}

void PPCSymbolDB::PrintCalls(u32 funcAddr) const
{
	XFuncMap::const_iterator iter = functions.find(funcAddr);
	if (iter != functions.end())
	{
		const Symbol &f = iter->second;
		INFO_LOG(OSHLE, "The function %s at %08x calls:", f.name.c_str(), f.address);
		for (std::vector<SCall>::const_iterator fiter = f.calls.begin(); fiter!=f.calls.end(); ++fiter)
		{
			XFuncMap::const_iterator n = functions.find(fiter->function);
			if (n != functions.end())
			{
				INFO_LOG(CONSOLE,"* %08x : %s", fiter->callAddress, n->second.name.c_str());
			}
		}
	}
	else
	{
		WARN_LOG(CONSOLE, "Symbol does not exist");
	}
}

void PPCSymbolDB::PrintCallers(u32 funcAddr) const
{
	XFuncMap::const_iterator iter = functions.find(funcAddr);
	if (iter != functions.end())
	{
		const Symbol &f = iter->second;
		INFO_LOG(CONSOLE,"The function %s at %08x is called by:",f.name.c_str(),f.address);
		for (std::vector<SCall>::const_iterator fiter = f.callers.begin(); fiter != f.callers.end(); ++fiter)
		{
			XFuncMap::const_iterator n = functions.find(fiter->function);
			if (n != functions.end())
			{
				INFO_LOG(CONSOLE,"* %08x : %s", fiter->callAddress, n->second.name.c_str());
			}
		}
	}
}

void PPCSymbolDB::LogFunctionCall(u32 addr)
{
	//u32 from = PC;
	XFuncMap::iterator iter = functions.find(addr);
	if (iter != functions.end())
	{
		Symbol &f = iter->second;
		f.numCalls++;
	}
}

// This one can load both leftover map files on game discs (like Zelda), and mapfiles
// produced by SaveSymbolMap below.
bool PPCSymbolDB::LoadMap(const char *filename)
{
	File::IOFile f(filename, "r");
	if (!f)
		return false;

	bool started = false;

	char line[512];
	while (fgets(line, 512, f.GetHandle()))
	{
		if (strlen(line) < 4)
			continue;

		char temp[256];
		sscanf(line, "%s", temp);

		if (strcmp(temp, "UNUSED")==0) continue;
		if (strcmp(temp, ".text")==0)  {started = true; continue;};
		if (strcmp(temp, ".init")==0)  {started = true; continue;};
		if (strcmp(temp, "Starting")==0) continue;
		if (strcmp(temp, "extab")==0) continue;
		if (strcmp(temp, ".ctors")==0) break; //uh?
		if (strcmp(temp, ".dtors")==0) break;
		if (strcmp(temp, ".rodata")==0) continue;
		if (strcmp(temp, ".data")==0) continue;
		if (strcmp(temp, ".sbss")==0) continue;
		if (strcmp(temp, ".sdata")==0) continue;
		if (strcmp(temp, ".sdata2")==0) continue;
		if (strcmp(temp, "address")==0)  continue;
		if (strcmp(temp, "-----------------------")==0)  continue;
		if (strcmp(temp, ".sbss2")==0) break;
		if (temp[1] == ']') continue;

		if (!started) continue;

		u32 address, vaddress, size, unknown;
		char name[512];
		sscanf(line, "%08x %08x %08x %i %s", &address, &size, &vaddress, &unknown, name);

		const char *namepos = strstr(line, name);
		if (namepos != 0) //would be odd if not :P
			strcpy(name, namepos);
		name[strlen(name) - 1] = 0;

		// we want the function names only .... TODO: or do we really? aren't we wasting information here?
		for (size_t i = 0; i < strlen(name); i++)
		{
			if (name[i] == ' ') name[i] = 0x00;
			if (name[i] == '(') name[i] = 0x00;
		}

		// Check if this is a valid entry.
		if (strcmp(name, ".text") != 0 || strcmp(name, ".init") != 0 || strlen(name) > 0)
		{
			AddKnownSymbol(vaddress | 0x80000000, size, name); // ST_FUNCTION
		}
	}

	Index();
	return true;
}


// ===================================================
/* Save the map file and save a code file */
// ----------------
bool PPCSymbolDB::SaveMap(const char *filename, bool WithCodes) const
{
	// Format the name for the codes version
	std::string mapFile = filename;
	if (WithCodes) mapFile = mapFile.substr(0, mapFile.find_last_of(".")) + "_code.map";

	// Check size
	const int wxYES_NO = 0x00000002 | 0x00000008;
	if (IsEmpty())
	{
		if(!AskYesNo(StringFromFormat(
			"No symbol names are generated. Do you want to replace '%s' with a blank file?",
			mapFile.c_str()).c_str(), "Confirm", wxYES_NO)) return false;
	}
	if (WithCodes) Host_UpdateStatusBar("Saving code, please stand by ...");

	// Make a file
	File::IOFile f(mapFile, "w");
	if (!f)
		return false;

	// --------------------------------------------------------------------
	// Walk through every code row
	// -------------------------
	fprintf(f.GetHandle(), ".text\n"); // Write ".text" at the top
	XFuncMap::const_iterator itr = functions.begin();
	u32 LastAddress = 0x80004000;
	std::string LastSymbolName;
	while (itr != functions.end())
	{
		// Save a map file
		const Symbol &rSymbol = itr->second;
		if (!WithCodes)
		{
			fprintf(f.GetHandle(),"%08x %08x %08x %i %s\n", rSymbol.address, rSymbol.size, rSymbol.address,
			0, rSymbol.name.c_str());
			++itr;
		}

		// Save a code file
		else
		{
			// Get the current and next address
			LastAddress = rSymbol.address;
			LastSymbolName = rSymbol.name;	
			++itr;

			/* To make nice straight lines we fill out the name with spaces, we also cut off
			   all names longer than 25 letters */
			std::string TempSym;
			for (u32 i = 0; i < 25; i++)
			{
				if (i < LastSymbolName.size())
					TempSym += LastSymbolName[i];
				else
					TempSym += " ";
			}

			// We currently skip the last block because we don't know how long it goes
			int space;
			if (itr != functions.end())
				space = itr->second.address - LastAddress;
			else
				space = 0;
			
			for (int i = 0; i < space; i += 4)
			{
				int Address = LastAddress + i;
				char disasm[256];
				debugger->disasm(Address, disasm, 256);
				fprintf(f.GetHandle(),"%08x %i %20s %s\n", Address, 0, TempSym.c_str(), disasm);
			}
			// Write a blank line after each block
			fprintf(f.GetHandle(), "\n");
		}
	}
	// ---------------
	Host_UpdateStatusBar(StringFromFormat("Saved %s", mapFile.c_str()).c_str());

	return true;
}
// ===========
