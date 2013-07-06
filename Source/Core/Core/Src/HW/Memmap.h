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

// Official Git repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#ifndef _MEMMAP_H
#define _MEMMAP_H

// Includes
#include <string>
#include "Common.h"

// Global declarations
class PointerWrap;

typedef void (*writeFn8 )(const u8, const u32);
typedef void (*writeFn16)(const u16,const u32);
typedef void (*writeFn32)(const u32,const u32);
typedef void (*writeFn64)(const u64,const u32);

typedef void (*readFn8 )(u8&,  const u32);
typedef void (*readFn16)(u16&, const u32);
typedef void (*readFn32)(u32&, const u32);
typedef void (*readFn64)(u64&, const u32);

namespace Memory
{
// Base is a pointer to the base of the memory map. Yes, some MMU tricks
// are used to set up a full GC or Wii memory map in process memory.  on
// 32-bit, you have to mask your offsets with 0x3FFFFFFF. This means that
// some things are mirrored too many times, but eh... it works.

// In 64-bit, this might point to "high memory" (above the 32-bit limit),
// so be sure to load it into a 64-bit register.
extern u8 *base; 

// These are guaranteed to point to "low memory" addresses (sub-32-bit).
extern u8 *m_pRAM;
extern u8 *m_pEXRAM;
extern u8 *m_pL1Cache;
extern u8 *m_pVirtualFakeVMEM;

enum
{
	// RAM_SIZE is the amount allocated by the emulator, whereas REALRAM_SIZE is
	// what will be reported in lowmem, and thus used by emulated software.
	// Note: Writing to lowmem is done by IPL. If using retail IPL, it will
	// always be set to 24MB.
	REALRAM_SIZE	= 0x1800000,
	RAM_SIZE		= ROUND_UP_POW2(REALRAM_SIZE),
	RAM_MASK		= RAM_SIZE - 1,
	FAKEVMEM_SIZE	= 0x2000000,
	FAKEVMEM_MASK	= FAKEVMEM_SIZE - 1,
	L1_CACHE_SIZE	= 0x40000,
	L1_CACHE_MASK	= L1_CACHE_SIZE - 1,
	EFB_SIZE		= 0x200000,
	EFB_MASK		= EFB_SIZE - 1,
	IO_SIZE			= 0x10000,
	EXRAM_SIZE		= 0x4000000,
	EXRAM_MASK		= EXRAM_SIZE - 1,

	ADDR_MASK_HW_ACCESS	= 0x0c000000,
	ADDR_MASK_MEM1		= 0x20000000,

#ifdef _M_IX86
	MEMVIEW32_MASK  = 0x3FFFFFFF,
#endif
};

// Init and Shutdown
bool IsInitialized();
void Init();
void Shutdown();
void DoState(PointerWrap &p);

void Clear();

#ifdef _WIN32
void ArtMoneyPointer();
#endif

// ONLY for use by GUI
u8 ReadUnchecked_U8(const u32 _Address);
u32 ReadUnchecked_U32(const u32 _Address);

void WriteUnchecked_U8(const u8 _Data, const u32 _Address);
void WriteUnchecked_U32(const u32 _Data, const u32 _Address);

void InitHWMemFuncs();
void InitHWMemFuncsWii();

bool IsRAMAddress(const u32 addr, bool allow_locked_cache = false, bool allow_fake_vmem = false);
writeFn32 GetHWWriteFun32(const u32 _Address);

inline u8* GetCachePtr() {return m_pL1Cache;}
inline u8* GetMainRAMPtr() {return m_pRAM;}
inline u32 ReadFast32(const u32 _Address)
{
#ifdef _M_IX86
	return Common::swap32(*(u32 *)(base + (_Address & MEMVIEW32_MASK)));  // ReadUnchecked_U32(_Address);
#elif defined(_M_X64)
	return Common::swap32(*(u32 *)(base + _Address));
#endif
}

// used by interpreter to read instructions, uses iCache
u32 Read_Opcode(const u32 _Address);
// this is used by Debugger a lot. 
// For now, just reads from memory!
u32 Read_Instruction(const u32 _Address);


// For use by emulator

// Read and write functions
#define NUMHWMEMFUN 64
#define HWSHIFT 10
#define HW_MASK 0x3FF

u8  Read_U8(const u32 _Address);
u16 Read_U16(const u32 _Address);
u32 Read_U32(const u32 _Address);
u64 Read_U64(const u32 _Address);

// used by JIT. Return zero-extended 32bit values
u32 Read_U8_ZX(const u32 _Address);
u32 Read_U16_ZX(const u32 _Address);

// used by JIT (Jit64::lXz)
u32 EFB_Read(const u32 addr);

void Write_U8(const u8 _Data, const u32 _Address);
void Write_U16(const u16 _Data, const u32 _Address);
void Write_U32(const u32 _Data, const u32 _Address);
void Write_U64(const u64 _Data, const u32 _Address);

void Write_U16_Swap(const u16 _Data, const u32 _Address);
void Write_U32_Swap(const u32 _Data, const u32 _Address);
void Write_U64_Swap(const u64 _Data, const u32 _Address);

void WriteHW_U32(const u32 _Data, const u32 _Address);
void GetString(std::string& _string, const u32 _Address);

void WriteBigEData(const u8 *_pData, const u32 _Address, const u32 size);
void ReadBigEData(u8 *_pDest, const u32 _Address, const u32 size);
u8* GetPointer(const u32 _Address);
void DMA_LCToMemory(const u32 _iMemAddr, const u32 _iCacheAddr, const u32 _iNumBlocks);
void DMA_MemoryToLC(const u32 _iCacheAddr, const u32 _iMemAddr, const u32 _iNumBlocks);
void Memset(const u32 _Address, const u8 _Data, const u32 _iLength);

// TLB functions
void SDRUpdated();
enum XCheckTLBFlag
{
	FLAG_NO_EXCEPTION,
	FLAG_READ,
	FLAG_WRITE,
	FLAG_OPCODE,
};
u32 TranslateAddress(u32 _Address, XCheckTLBFlag _Flag);
void InvalidateTLBEntry(u32 _Address);
void GenerateDSIException(u32 _EffectiveAdress, bool _bWrite);
void GenerateISIException(u32 _EffectiveAdress);
extern u32 pagetable_base;
extern u32 pagetable_hashmask;

};

#endif

