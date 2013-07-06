// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


// -----------------------------------------------------------------------------------------
// Partial Action Replay code system implementation.
// Will never be able to support some AR codes - specifically those that patch the running
// Action Replay engine itself - yes they do exist!!!
// Action Replay actually is a small virtual machine with a limited number of commands.
// It probably is Turing complete - but what does that matter when AR codes can write
// actual PowerPC code...
// -----------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------------------
// Code Types:
// (Unconditional) Normal Codes (0): this one has subtypes inside
// (Conditional) Normal Codes (1 - 7): these just compare values and set the line skip info
// Zero Codes: any code with no address.  These codes are used to do special operations like memory copy, etc
// -------------------------------------------------------------------------------------------------------------

#include <string>
#include <vector>

#include "Common.h"
#include "StringUtil.h"
#include "HW/Memmap.h"
#include "ActionReplay.h"
#include "Core.h"
#include "ARDecrypt.h"
#include "LogManager.h"
#include "ConfigManager.h"

namespace ActionReplay
{

enum
{
	// Zero Code Types
	ZCODE_END	= 0x00,
	ZCODE_NORM	= 0x02, 
	ZCODE_ROW	= 0x03, 
	ZCODE_04	= 0x04,

	// Conditional Codes
	CONDTIONAL_EQUAL				= 0x01,
	CONDTIONAL_NOT_EQUAL			= 0x02, 
	CONDTIONAL_LESS_THAN_SIGNED		= 0x03,
	CONDTIONAL_GREATER_THAN_SIGNED	= 0x04,
	CONDTIONAL_LESS_THAN_UNSIGNED	= 0x05,
	CONDTIONAL_GREATER_THAN_UNSIGNED	= 0x06,
	CONDTIONAL_AND					= 0x07,	// bitwise AND

	// Conditional Line Counts
	CONDTIONAL_ONE_LINE		= 0x00,
	CONDTIONAL_TWO_LINES	= 0x01,
	CONDTIONAL_ALL_LINES_UNTIL	= 0x02,
	CONDTIONAL_ALL_LINES	= 0x03,

	// Data Types
	DATATYPE_8BIT		= 0x00,
	DATATYPE_16BIT		= 0x01, 
	DATATYPE_32BIT		= 0x02, 
	DATATYPE_32BIT_FLOAT	= 0x03,

	// Normal Code 0 Subtypes
	SUB_RAM_WRITE		= 0x00,
	SUB_WRITE_POINTER	= 0x01, 
	SUB_ADD_CODE		= 0x02, 
	SUB_MASTER_CODE		= 0x03,
};

// pointer to the code currently being run, (used by log messages that include the code name)
static ARCode const* current_code = NULL;

static bool b_RanOnce = false;
std::vector<ARCode> arCodes;
static std::vector<ARCode> activeCodes;
static bool logSelf = false;
static std::vector<std::string> arLog;

struct ARAddr
{
	union
	{
		u32 address;
		struct
		{
			u32 gcaddr : 25;
			u32 size : 2;
			u32 type : 3;
			u32 subtype : 2;
		};
	};

	ARAddr(const u32 addr) : address(addr) {}
	u32 GCAddress() const { return gcaddr | 0x80000000; }
	operator u32() const { return address; }
};

void LogInfo(const char *format, ...);
bool Subtype_RamWriteAndFill(const ARAddr addr, const u32 data);
bool Subtype_WriteToPointer(const ARAddr addr, const u32 data);
bool Subtype_AddCode(const ARAddr addr, const u32 data);
bool Subtype_MasterCodeAndWriteToCCXXXXXX(const ARAddr addr, const u32 data);
bool ZeroCode_FillAndSlide(const u32 val_last, const ARAddr addr, const u32 data);
bool ZeroCode_MemoryCopy(const u32 val_last, const ARAddr addr, const u32 data);
bool NormalCode(const ARAddr addr, const u32 data);
bool ConditionalCode(const ARAddr addr, const u32 data, int* const pSkipCount);
bool CompareValues(const u32 val1, const u32 val2, const int type);

// ----------------------
// AR Remote Functions
void LoadCodes(IniFile &ini, bool forceLoad)
{
	// Parses the Action Replay section of a game ini file.
	if (!SConfig::GetInstance().m_LocalCoreStartupParameter.bEnableCheats 
		&& !forceLoad) 
		return;

	std::vector<std::string> lines;
	std::vector<std::string> encryptedLines;
	ARCode currentCode;
	arCodes.clear();

	if (!ini.GetLines("ActionReplay", lines))
		return;  // no codes found.

	std::vector<std::string>::const_iterator
		it = lines.begin(),
		lines_end = lines.end();
	for (; it != lines_end; ++it)
	{
		const std::string line = *it;
		
		if (line.empty())
			continue;

		std::vector<std::string> pieces;

		// Check if the line is a name of the code
		if (line[0] == '+' || line[0] == '$')
		{
			if (currentCode.ops.size())
			{
				arCodes.push_back(currentCode);
				currentCode.ops.clear();
			}
			if (encryptedLines.size())
			{
				DecryptARCode(encryptedLines, currentCode.ops);
				arCodes.push_back(currentCode);
				currentCode.ops.clear();
				encryptedLines.clear();
			}

			if (line.size() > 1)
			{
				if (line[0] == '+')
				{
					currentCode.active = true;
					currentCode.name = line.substr(2, line.size() - 2);;
					if (!forceLoad)
						Core::DisplayMessage("AR code active: " + currentCode.name, 5000);
				}
				else
				{
					currentCode.active = false;
					currentCode.name = line.substr(1, line.size() - 1);
				}
			}
			continue;
		}

		SplitString(line, ' ', pieces);

		// Check if the AR code is decrypted
		if (pieces.size() == 2 && pieces[0].size() == 8 && pieces[1].size() == 8)
		{
			AREntry op;
			bool success_addr = TryParse(std::string("0x") + pieces[0], &op.cmd_addr);
			bool success_val = TryParse(std::string("0x") + pieces[1], &op.value);
			if (!(success_addr | success_val)) {
				PanicAlertT("Action Replay Error: invalid AR code line: %s", line.c_str());
				if (!success_addr) PanicAlertT("The address is invalid");
				if (!success_val) PanicAlertT("The value is invalid");
			}
			else
			{
				currentCode.ops.push_back(op);
			}
		}
		else
		{
			SplitString(line, '-', pieces);
			if (pieces.size() == 3 && pieces[0].size() == 4 && pieces[1].size() == 4 && pieces[2].size() == 5) 
			{
				// Encrypted AR code
				// Decryption is done in "blocks", so we must push blocks into a vector,
				//	then send to decrypt when a new block is encountered, or if it's the last block.
				encryptedLines.push_back(pieces[0]+pieces[1]+pieces[2]);
			}
		}
	}

	// Handle the last code correctly.
	if (currentCode.ops.size())
	{
		arCodes.push_back(currentCode);
	}
	if (encryptedLines.size())
	{
		DecryptARCode(encryptedLines, currentCode.ops);
		arCodes.push_back(currentCode);
	}

	UpdateActiveList();
}

void LoadCodes(std::vector<ARCode> &_arCodes, IniFile &ini)
{
	LoadCodes(ini, true);
	_arCodes = arCodes;
}


void LogInfo(const char *format, ...)
{
	if (!b_RanOnce) 
	{
		if (LogManager::GetMaxLevel() >= LogTypes::LINFO || logSelf)
		{
			char* temp = (char*)alloca(strlen(format)+512);
			va_list args;
			va_start(args, format);
			CharArrayFromFormatV(temp, 512, format, args);
			va_end(args);
			INFO_LOG(ACTIONREPLAY, "%s", temp);

			if (logSelf)
			{
				std::string text = temp;
				text += '\n';
				arLog.push_back(text.c_str());
			}
		}
	}
}


void RunAllActive()
{
	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bEnableCheats)
	{
		for (std::vector<ARCode>::iterator i = activeCodes.begin(); i != activeCodes.end(); ++i) 
		{
			if (i->active)
			{
				i->active = RunCode(*i);
				LogInfo("\n");
			}
		}

		b_RanOnce = true;
	}
}

bool RunCode(const ARCode &arcode)
{
	// The mechanism is different than what the real AR uses, so there may be compatibility problems.

	bool doFillNSlide = false;
	bool doMemoryCopy = false;

	// used for conditional codes
	int skip_count = 0;

	u32 val_last = 0;

	current_code = &arcode;

	LogInfo("Code Name: %s", arcode.name.c_str());
	LogInfo("Number of codes: %i", arcode.ops.size());

	std::vector<AREntry>::const_iterator
		iter = arcode.ops.begin(),
		ops_end = arcode.ops.end();
	for (; iter != ops_end; ++iter)
	{
		const ARAddr& addr = *(ARAddr*)&iter->cmd_addr;
		const u32 data = iter->value;

		// after a conditional code, skip lines if needed
		if (skip_count)
		{
			if (skip_count > 0)	// skip x lines
			{
				LogInfo("Line skipped");
				--skip_count;
			}
			else if (-CONDTIONAL_ALL_LINES == skip_count)
			{
				// skip all lines
				LogInfo("All Lines skipped");
				return true;	// don't need to iterate through the rest of the ops
			}
			else if (-CONDTIONAL_ALL_LINES_UNTIL == skip_count)
			{
				// skip until a "00000000 40000000" line is reached
				LogInfo("Line skipped");
				if (0 == addr && 0x40000000 == data)	// check for an endif line
					skip_count = 0;
			}

			continue;
		}
		
		LogInfo("--- Running Code: %08x %08x ---", addr.address, data);
		//LogInfo("Command: %08x", cmd);

		// Do Fill & Slide
		if (doFillNSlide)
		{
			doFillNSlide = false;
			LogInfo("Doing Fill And Slide");
			if (false == ZeroCode_FillAndSlide(val_last, addr, data))
				return false;
			continue;
		}

		// Memory Copy
		if (doMemoryCopy)
		{
			doMemoryCopy = false;
			LogInfo("Doing Memory Copy");
			if (false == ZeroCode_MemoryCopy(val_last, addr, data))
				return false;
			continue;
		}

		// ActionReplay program self modification codes
		if (addr >= 0x00002000 && addr < 0x00003000)
		{
			LogInfo("This action replay simulator does not support codes that modify Action Replay itself.");
			PanicAlertT("This action replay simulator does not support codes that modify Action Replay itself.");
			return false;
		}

		// skip these weird init lines
		// TODO: Where are the "weird init lines"?
		//if (iter == code.ops.begin() && cmd == 1)
			//continue;

		// Zero codes
		if (0x0 == addr) // Check if the code is a zero code
		{
			const u8 zcode = (data >> 29);

			LogInfo("Doing Zero Code %08x", zcode);

			switch (zcode)
			{
				case ZCODE_END: // END OF CODES
					LogInfo("ZCode: End Of Codes");
					return true;
					break;

				// TODO: the "00000000 40000000"(end if) codes fall into this case, I don't think that is correct
				case ZCODE_NORM: // Normal execution of codes
					// Todo: Set register 1BB4 to 0
					LogInfo("ZCode: Normal execution of codes, set register 1BB4 to 0 (zcode not supported)");
					break;

				case ZCODE_ROW: // Executes all codes in the same row
					// Todo: Set register 1BB4 to 1
					LogInfo("ZCode: Executes all codes in the same row, Set register 1BB4 to 1 (zcode not supported)");
					PanicAlertT("Zero 3 code not supported");
					return false;
					break;

				case ZCODE_04: // Fill & Slide or Memory Copy
					if (0x3 == ((data >> 25) & 0x03))
					{
						LogInfo("ZCode: Memory Copy");
						doMemoryCopy = true;
						val_last = data;
					}
					else 
					{
						LogInfo("ZCode: Fill And Slide");
						doFillNSlide = true;
						val_last = data;
					}
					break;

				default: 
					LogInfo("ZCode: Unknown");
					PanicAlertT("Zero code unknown to dolphin: %08x", zcode); 
					return false;
					break;
			}

			// done handling zero codes
			continue;
		}

		// Normal codes
		LogInfo("Doing Normal Code %08x", addr.type);
		LogInfo("Subtype: %08x", addr.subtype);

		switch (addr.type)
		{
		case 0x00:
			if (false == NormalCode(addr, data))
				return false;
			break;
			
		default:
			LogInfo("This Normal Code is a Conditional Code");
			if (false == ConditionalCode(addr, data, &skip_count))
				return false;
			break;
		}
	}

	b_RanOnce = true;

	return true;
}
size_t GetCodeListSize()
{
	return arCodes.size();
}

ARCode GetARCode(size_t index)
{
	if (index >= arCodes.size())
	{
		PanicAlertT("GetARCode: Index %u is >= ar code list size %u", index, arCodes.size());
		return ARCode();
	}
	return arCodes[index];
}

void SetARCode_IsActive(bool active, size_t index)
{
	if (index >= arCodes.size())
	{
		PanicAlertT("SetARCode_IsActive: Index %u is >= ar code list size %u", index, arCodes.size());
		return;
	}

	arCodes[index].active = active;
	UpdateActiveList();
}

void UpdateActiveList()
{
	bool old_value = SConfig::GetInstance().m_LocalCoreStartupParameter.bEnableCheats;
	SConfig::GetInstance().m_LocalCoreStartupParameter.bEnableCheats = false;
	b_RanOnce = false;
	activeCodes.clear();
	for (size_t i = 0; i < arCodes.size(); i++)
	{
		if (arCodes[i].active)
			activeCodes.push_back(arCodes[i]);
	}
	SConfig::GetInstance().m_LocalCoreStartupParameter.bEnableCheats = old_value;
}

void EnableSelfLogging(bool enable)
{
	logSelf = enable;
}

const std::vector<std::string> &GetSelfLog()
{
	return arLog;
}

bool IsSelfLogging()
{
	return logSelf;
}

// ----------------------
// Code Functions
bool Subtype_RamWriteAndFill(const ARAddr addr, const u32 data)
{
	const u32 new_addr = addr.GCAddress();

	LogInfo("Hardware Address: %08x", new_addr);
	LogInfo("Size: %08x", addr.size);

	switch (addr.size)
	{
	case DATATYPE_8BIT:
	{
		LogInfo("8-bit Write");
		LogInfo("--------");
		u32 repeat = data >> 8;
		for (u32 i = 0; i <= repeat; ++i)
		{
			Memory::Write_U8(data & 0xFF, new_addr + i);
			LogInfo("Wrote %08x to address %08x", data & 0xFF, new_addr + i);
		}
		LogInfo("--------");
		break;
	}

	case DATATYPE_16BIT:
	{
		LogInfo("16-bit Write");
		LogInfo("--------");
		u32 repeat = data >> 16;
		for (u32 i = 0; i <= repeat; ++i)
		{
			Memory::Write_U16(data & 0xFFFF, new_addr + i * 2);
			LogInfo("Wrote %08x to address %08x", data & 0xFFFF, new_addr + i * 2);
		}
		LogInfo("--------");
		break;
	}

	case DATATYPE_32BIT_FLOAT:
	case DATATYPE_32BIT: // Dword write
		LogInfo("32-bit Write");
		LogInfo("--------");
		Memory::Write_U32(data, new_addr);
		LogInfo("Wrote %08x to address %08x", data, new_addr);
		LogInfo("--------");
		break;

	default:
		LogInfo("Bad Size");
		PanicAlertT("Action Replay Error: Invalid size "
			"(%08x : address = %08x) in Ram Write And Fill (%s)",
			addr.size, addr.gcaddr, current_code->name.c_str());
		return false;
	}

	return true;
}

bool Subtype_WriteToPointer(const ARAddr addr, const u32 data)
{
	const u32 new_addr = addr.GCAddress();
	const u32 ptr = Memory::Read_U32(new_addr);

	LogInfo("Hardware Address: %08x", new_addr);
	LogInfo("Size: %08x", addr.size);

	switch (addr.size)
	{
	case DATATYPE_8BIT:
	{
		LogInfo("Write 8-bit to pointer");
		LogInfo("--------");
		const u8 thebyte = data & 0xFF;
		const u32 offset = data >> 8;
		LogInfo("Pointer: %08x", ptr);
		LogInfo("Byte: %08x", thebyte);
		LogInfo("Offset: %08x", offset);
		Memory::Write_U8(thebyte, ptr + offset);
		LogInfo("Wrote %08x to address %08x", thebyte, ptr + offset);
		LogInfo("--------");
		break;
	}

	case DATATYPE_16BIT:
	{
		LogInfo("Write 16-bit to pointer");
		LogInfo("--------");
		const u16 theshort = data & 0xFFFF;
		const u32 offset = (data >> 16) << 1;
		LogInfo("Pointer: %08x", ptr);
		LogInfo("Byte: %08x", theshort);
		LogInfo("Offset: %08x", offset);
		Memory::Write_U16(theshort, ptr + offset);
		LogInfo("Wrote %08x to address %08x", theshort, ptr + offset);
		LogInfo("--------");
		break;
	}

	case DATATYPE_32BIT_FLOAT:
	case DATATYPE_32BIT:
		LogInfo("Write 32-bit to pointer");
		LogInfo("--------");
		Memory::Write_U32(data, ptr);
		LogInfo("Wrote %08x to address %08x", data, ptr);
		LogInfo("--------");
		break;

	default:
		LogInfo("Bad Size");
		PanicAlertT("Action Replay Error: Invalid size "
			"(%08x : address = %08x) in Write To Pointer (%s)",
			addr.size, addr.gcaddr, current_code->name.c_str());
		return false;
	}
	return true;
}

bool Subtype_AddCode(const ARAddr addr, const u32 data)
{
	// Used to increment/decrement a value in memory
	const u32 new_addr = addr.GCAddress();

	LogInfo("Hardware Address: %08x", new_addr);
	LogInfo("Size: %08x", addr.size);

	switch (addr.size)
	{
	case DATATYPE_8BIT:
		LogInfo("8-bit Add");
		LogInfo("--------");
		Memory::Write_U8(Memory::Read_U8(new_addr) + data, new_addr);
		LogInfo("Wrote %08x to address %08x", Memory::Read_U8(new_addr) + (data & 0xFF), new_addr);
		LogInfo("--------");
		break;

	case DATATYPE_16BIT:
		LogInfo("16-bit Add");
		LogInfo("--------");
		Memory::Write_U16(Memory::Read_U16(new_addr) + data, new_addr);
		LogInfo("Wrote %08x to address %08x", Memory::Read_U16(new_addr) + (data & 0xFFFF), new_addr);
		LogInfo("--------");
		break;

	case DATATYPE_32BIT:
		LogInfo("32-bit Add");
		LogInfo("--------");
		Memory::Write_U32(Memory::Read_U32(new_addr) + data, new_addr);
		LogInfo("Wrote %08x to address %08x", Memory::Read_U32(new_addr) + data, new_addr);
		LogInfo("--------");
		break;

	case DATATYPE_32BIT_FLOAT:
	{
		LogInfo("32-bit floating Add");
		LogInfo("--------");

		const u32 read = Memory::Read_U32(new_addr);
		const float fread = *((float*)&read) + (float)data;	// data contains an integer value
		const u32 newval = *((u32*)&fread);
		Memory::Write_U32(newval, new_addr);
		LogInfo("Old Value %08x", read);
		LogInfo("Increment %08x", data);
		LogInfo("New value %08x", newval);
		LogInfo("--------");
		break;
	}

	default:
		LogInfo("Bad Size");
		PanicAlertT("Action Replay Error: Invalid size "
			"(%08x : address = %08x) in Add Code (%s)",
			addr.size, addr.gcaddr, current_code->name.c_str());
		return false;
	}
	return true;
}

bool Subtype_MasterCodeAndWriteToCCXXXXXX(const ARAddr addr, const u32 data)
{
	// code not yet implemented - TODO
	// u32 new_addr = (addr & 0x01FFFFFF) | 0x80000000;
	// u8  mcode_type = (data & 0xFF0000) >> 16;
	// u8  mcode_count = (data & 0xFF00) >> 8;
	// u8  mcode_number = data & 0xFF;
	PanicAlertT("Action Replay Error: Master Code and Write To CCXXXXXX not implemented (%s)\n"
		"Master codes are not needed. Do not use master codes.", current_code->name.c_str());
	return false;
}

bool ZeroCode_FillAndSlide(const u32 val_last, const ARAddr addr, const u32 data) // This needs more testing
{
	const u32 new_addr = ((ARAddr*)&val_last)->GCAddress();
	const u8 size = ((ARAddr*)&val_last)->size;

	const s16 addr_incr = (s16)(data & 0xFFFF);
	const s8  val_incr = (s8)(data >> 24);
	const u8  write_num = (data & 0xFF0000) >> 16;
	
	u32 val = addr;
	u32 curr_addr = new_addr;

	LogInfo("Current Hardware Address: %08x", new_addr);
	LogInfo("Size: %08x", addr.size);
	LogInfo("Write Num: %08x", write_num);
	LogInfo("Address Increment: %i", addr_incr);
	LogInfo("Value Increment: %i", val_incr);

	switch (size)
	{
	case DATATYPE_8BIT:
		LogInfo("8-bit Write");
		LogInfo("--------");
		for (int i = 0; i < write_num; ++i) 
		{
			Memory::Write_U8(val & 0xFF, curr_addr);
			curr_addr += addr_incr;
			val += val_incr;
			LogInfo("Write %08x to address %08x", val & 0xFF, curr_addr);

			LogInfo("Value Update: %08x", val);
			LogInfo("Current Hardware Address Update: %08x", curr_addr);
		}
		LogInfo("--------");
		break;

	case DATATYPE_16BIT:
		LogInfo("16-bit Write");
		LogInfo("--------");
		for (int i=0; i < write_num; ++i)
		{
			Memory::Write_U16(val & 0xFFFF, curr_addr);
			LogInfo("Write %08x to address %08x", val & 0xFFFF, curr_addr);
			curr_addr += addr_incr * 2;
			val += val_incr;
			LogInfo("Value Update: %08x", val);
			LogInfo("Current Hardware Address Update: %08x", curr_addr);
		}
		LogInfo("--------");
		break;

	case DATATYPE_32BIT:
		LogInfo("32-bit Write");
		LogInfo("--------");
		for (int i = 0; i < write_num; ++i)
		{
			Memory::Write_U32(val, curr_addr);
			LogInfo("Write %08x to address %08x", val, curr_addr);
			curr_addr += addr_incr * 4;
			val += val_incr;
			LogInfo("Value Update: %08x", val);
			LogInfo("Current Hardware Address Update: %08x", curr_addr);
		}
		LogInfo("--------");
		break;

	default:
		LogInfo("Bad Size");
		PanicAlertT("Action Replay Error: Invalid size (%08x : address = %08x) in Fill and Slide (%s)", size, new_addr, current_code->name.c_str());
		return false;
	}
	return true;
}

// Looks like this is new?? - untested
bool ZeroCode_MemoryCopy(const u32 val_last, const ARAddr addr, const u32 data)
{
	const u32 addr_dest = val_last | 0x06000000;
	const u32 addr_src = addr.GCAddress();

	const u8 num_bytes = data & 0x7FFF;

	LogInfo("Dest Address: %08x", addr_dest);
	LogInfo("Src Address: %08x", addr_src);
	LogInfo("Size: %08x", num_bytes);

	if ((data & ~0x7FFF) == 0x0000)
	{ 
		if ((data >> 24) != 0x0)
		{ // Memory Copy With Pointers Support
			LogInfo("Memory Copy With Pointers Support");
			LogInfo("--------");
			for (int i = 0; i < 138; ++i)
			{
				Memory::Write_U8(Memory::Read_U8(addr_src + i), addr_dest + i);
				LogInfo("Wrote %08x to address %08x", Memory::Read_U8(addr_src + i), addr_dest + i);
			}
			LogInfo("--------");
		}
		else
		{ // Memory Copy Without Pointer Support
			LogInfo("Memory Copy Without Pointers Support");
			LogInfo("--------");
			for (int i=0; i < num_bytes; ++i)
			{
				Memory::Write_U32(Memory::Read_U32(addr_src + i), addr_dest + i);
				LogInfo("Wrote %08x to address %08x", Memory::Read_U32(addr_src + i), addr_dest + i);
			}
			LogInfo("--------");
			return true;
		}
	}
	else
	{
		LogInfo("Bad Value");
		PanicAlertT("Action Replay Error: Invalid value (%08x) in Memory Copy (%s)", (data & ~0x7FFF), current_code->name.c_str());
		return false;
	}
	return true;
}

bool NormalCode(const ARAddr addr, const u32 data)
{
	switch (addr.subtype)
	{
	case SUB_RAM_WRITE: // Ram write (and fill)
		LogInfo("Doing Ram Write And Fill");
		if (!Subtype_RamWriteAndFill(addr, data))
			return false;
		break;

	case SUB_WRITE_POINTER: // Write to pointer
		LogInfo("Doing Write To Pointer");
		if (!Subtype_WriteToPointer(addr, data))
			return false;
		break;

	case SUB_ADD_CODE: // Increment Value
		LogInfo("Doing Add Code");
		if (!Subtype_AddCode(addr, data))
			return false;
		break;

	case SUB_MASTER_CODE: // Master Code & Write to CCXXXXXX
		LogInfo("Doing Master Code And Write to CCXXXXXX (ncode not supported)");
		if (!Subtype_MasterCodeAndWriteToCCXXXXXX(addr, data))
			return false;
		break;

	default:
		LogInfo("Bad Subtype");
		PanicAlertT("Action Replay: Normal Code 0: Invalid Subtype %08x (%s)", addr.subtype, current_code->name.c_str());
		return false;
		break;
	}

	return true;
}

bool ConditionalCode(const ARAddr addr, const u32 data, int* const pSkipCount)
{
	const u32 new_addr = addr.GCAddress();

	LogInfo("Size: %08x", addr.size);
	LogInfo("Hardware Address: %08x", new_addr);

	bool result = true;

	switch (addr.size)
	{
	case DATATYPE_8BIT:
		result = CompareValues((u32)Memory::Read_U8(new_addr), (data & 0xFF), addr.type);
		break;

	case DATATYPE_16BIT:
		result = CompareValues((u32)Memory::Read_U16(new_addr), (data & 0xFFFF), addr.type);
		break;

	case DATATYPE_32BIT_FLOAT:
	case DATATYPE_32BIT:
		result = CompareValues(Memory::Read_U32(new_addr), data, addr.type);
		break;

	default:
		LogInfo("Bad Size");
		PanicAlertT("Action Replay: Conditional Code: Invalid Size %08x (%s)", addr.size, current_code->name.c_str());
		return false;
		break;
	}

	// if the comparison failed we need to skip some lines
	if (false == result)
	{
		switch (addr.subtype)
		{
		case CONDTIONAL_ONE_LINE:
		case CONDTIONAL_TWO_LINES:
			*pSkipCount = addr.subtype + 1; // Skip 1 or 2 lines
			break;

		// Skip all lines,
		// Skip lines until a "00000000 40000000" line is reached
		case CONDTIONAL_ALL_LINES:
		case CONDTIONAL_ALL_LINES_UNTIL:
			*pSkipCount = -addr.subtype;
			break;

		default:
			LogInfo("Bad Subtype");
			PanicAlertT("Action Replay: Normal Code %i: Invalid subtype %08x (%s)", 1, addr.subtype, current_code->name.c_str());
			return false;
			break;
		}
	}

	return true;
}

bool CompareValues(const u32 val1, const u32 val2, const int type)
{
	switch(type)
	{
	case CONDTIONAL_EQUAL:
		LogInfo("Type 1: If Equal");
		return (val1 == val2);
		break;

	case CONDTIONAL_NOT_EQUAL:
		LogInfo("Type 2: If Not Equal");
		return (val1 != val2);
		break;

	case CONDTIONAL_LESS_THAN_SIGNED:
		LogInfo("Type 3: If Less Than (Signed)");
		return ((int)val1 < (int)val2);
		break;

	case CONDTIONAL_GREATER_THAN_SIGNED:
		LogInfo("Type 4: If Greater Than (Signed)");
		return ((int)val1 > (int)val2);
		break;

	case CONDTIONAL_LESS_THAN_UNSIGNED:
		LogInfo("Type 5: If Less Than (Unsigned)");
		return (val1 < val2);
		break;

	case CONDTIONAL_GREATER_THAN_UNSIGNED:
		LogInfo("Type 6: If Greater Than (Unsigned)");
		return (val1 > val2);
		break;

	case CONDTIONAL_AND:
		LogInfo("Type 7: If And");
		return !!(val1 & val2);	// bitwise AND
		break;

	default: LogInfo("Unknown Compare type");
		PanicAlertT("Action Replay: Invalid Normal Code Type %08x (%s)", type, current_code->name.c_str());
		return false;
		break;
	}
}

} // namespace ActionReplay
