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

#ifndef _MMIO_H
#define _MMIO_H

#include "Common.h"
#include <array>
#include <string>
#include <type_traits>

#include "MMIOHandlers.h"

// HACK: Remove when the new MMIO interface is used.
#include "Memmap.h"

namespace MMIO
{

// There are three main MMIO blocks on the Wii (only one on the GameCube):
//  - 0xCC00xxxx: GameCube MMIOs (CP, PE, VI, PI, MI, DSP, DVD, SI, EI, AI, GP)
//  - 0xCD00xxxx: Wii MMIOs and GC mirrors (IPC, DVD, SI, EI, AI)
//  - 0xCD80xxxx: Mirror of 0xCD00xxxx.
//
// In practice, since the third block is a mirror of the second one, we can
// assume internally that there are only two blocks: one for GC, one for Wii.
enum Block
{
	GC_BLOCK = 0,
	WII_BLOCK = 1,

	NUM_BLOCKS
};
const u32 BLOCK_SIZE = 0x10000;
const u32 NUM_MMIOS = NUM_BLOCKS * BLOCK_SIZE;

// Compute the internal unique ID for a given MMIO address. This ID is computed
// from a very simple formula: (1 + block_id) * lower_16_bits(address).
//
// The block ID can easily be computed by simply checking bit 24 (CC vs. CD).
inline u32 UniqueID(u32 address)
{
	_dbg_assert_msg_(MEMMAP, ((address & 0xFFFF0000) == 0xCC000000) ||
	                         ((address & 0xFFFF0000) == 0xCD000000) ||
	                         ((address & 0xFFFF0000) == 0xCD800000),
	                 "Trying to get the ID of a non-existing MMIO address.");

	return (address & 0xFFFF) * (1 + ((address >> 24) & 1));
}


class Mapping
{
public:
	// MMIO registration interface. Use this to register new MMIO handlers.
#define REGISTER_FUNC(Size) \
	void Register(u32 addr, ReadHandlingMethod<u##Size>* read, \
	              WriteHandlingMethod<u##Size>* write) \
	{ \
		u32 id = UniqueID(addr) / sizeof (u##Size); \
		m_Read##Size##Handlers[id].ResetMethod(read); \
		m_Write##Size##Handlers[id].ResetMethod(write); \
	}
	REGISTER_FUNC(8) REGISTER_FUNC(16) REGISTER_FUNC(32)
#undef REGISTER_FUNC

	// Direct read/write interface.
	//
	// These functions allow reading/writing an MMIO register at a given
	// address. They are used by the Memory:: access functions, which are
	// called in interpreter mode, from Dolphin's own code, or from JIT'd code
	// where the access address could not be predicted.
	//
	// Note that for reads we cannot simply return the read value because C++
	// allows overloading only with parameter types, not return types.
#define READ_FUNC(Size) \
	void Read(u32 addr, u##Size& val) const \
	{ \
		u32 id = UniqueID(addr) / sizeof (u##Size); \
		val = m_Read##Size##Handlers[id].Read(addr); \
	}
	READ_FUNC(8) READ_FUNC(16) READ_FUNC(32)
#undef READ_FUNC

#define WRITE_FUNC(Size) \
	void Write(u32 addr, u##Size val) const \
	{ \
		u32 id = UniqueID(addr) / sizeof (u##Size); \
		m_Write##Size##Handlers[id].Write(addr, val); \
	}
	WRITE_FUNC(8) WRITE_FUNC(16) WRITE_FUNC(32)
#undef WRITE_FUNC

	// Dummy 64 bits variants of these functions. While 64 bits MMIO access is
	// not supported, we need these in order to make the code compile.
	void Read(u32 addr, u64& val) const { _dbg_assert_(MEMMAP, 0); }
	void Write(u32 addr, u64 val) const { _dbg_assert_(MEMMAP, 0); }

private:
	// These arrays contain the handlers for each MMIO access type: read/write
	// to 8/16/32 bits. They are indexed using the UniqueID(addr) function
	// defined earlier, which maps an MMIO address to a unique ID by using the
	// MMIO block ID.
	//
	// Each array contains NUM_MMIOS / sizeof (AccessType) because larger
	// access types mean less possible adresses (assuming aligned only
	// accesses).
#define HANDLERS(Size) \
	std::array<ReadHandler<u##Size>, NUM_MMIOS / sizeof (u##Size)> m_Read##Size##Handlers; \
	std::array<WriteHandler<u##Size>, NUM_MMIOS / sizeof (u##Size)> m_Write##Size##Handlers;
	HANDLERS(8) HANDLERS(16) HANDLERS(32)
#undef HANDLERS
};

}

#endif
