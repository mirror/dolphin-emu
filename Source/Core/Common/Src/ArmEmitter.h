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

// WARNING - THIS LIBRARY IS NOT THREAD SAFE!!!

#ifndef _DOLPHIN_INTEL_CODEGEN_
#define _DOLPHIN_INTEL_CODEGEN_

#include "Common.h"
#include "MemoryUtil.h"

namespace Gen
{

enum X64Reg
{
	// GPRs
	R0 = 0, R1, R2, R3, R4, R5,
	R6, R7, R8, R9, R10, R11, R12,

	// SPRs
	// R13 - R15 are SP, LR, and PC.
	// Almost always referred to by name instead of register number
	R13 = 13, R14 = 14, R15 = 15,
	SP = 13, LR = 14, PC = 15,


	// VFP single precision registers
	S0 = 0, S1, S2, S3, S4, S5, S6,
	S7, S8, S9, S10, S11, S12, S13,
	S14, S15, S16, S17, S18, S19, S20,
	S21, S22, S23, S24, S25, S26, S27,
	S28, S29, S30, S31,

	// VFP Double Precision registers
	D0 = 0, D1, D2, D3, D4, D5, D6, D7,
	D8, D9, D10, D11, D12, D13, D14, D15
	INVALID_REG = 0xFFFFFFFF
};

enum CCFlags
{
	CC_EQ = 0, // Equal
	CC_NEQ, // Not equal
	CC_CS, // Carry Set
	CC_CC, // Carry Clear
	CC_MI, // Minus
	CC_PL, // Plus
	CC_VS, // Overflow
	CC_VC, // No Overflow
	CC_HI, // Unsigned higher
	CC_LS, // Unsigned lower
	CC_GE, // Signed greater than or equal
	CC_LT, // Signed less than
	CC_GT, // Signed greater than
	CC_LE, // Signed less than or equal
	CC_AL, // Always (unconditional)
	
};

enum
{
	NUMGPRs = 13,
};

enum
{
	SCALE_NONE = 0,
	SCALE_1 = 1,
	SCALE_2 = 2,
	SCALE_4 = 4,
	SCALE_8 = 8,
	SCALE_ATREG = 16,
	//SCALE_NOBASE_1 is not supported and can be replaced with SCALE_ATREG
	SCALE_NOBASE_2 = 34,
	SCALE_NOBASE_4 = 36,
	SCALE_NOBASE_8 = 40,
	SCALE_RIP = 0xFF,
	SCALE_IMM8  = 0xF0,
	SCALE_IMM16 = 0xF1,
	SCALE_IMM32 = 0xF2,
	SCALE_IMM64 = 0xF3,
};

enum NormalOp {
	nrmADD,
	nrmADC,
	nrmSUB,
	nrmSBB,
	nrmAND,
	nrmOR ,
	nrmXOR,
	nrmMOV,
	nrmTEST,
	nrmCMP,
	nrmXCHG,
};

class XEmitter;

// RIP addressing does not benefit from micro op fusion on Core arch
struct OpArg
{
	OpArg() {}  // dummy op arg, used for storage
	OpArg(u64 _offset, int _scale, X64Reg rmReg = RAX, X64Reg scaledReg = RAX)
	{
		operandReg = 0;
		scale = (u8)_scale;
		offsetOrBaseReg = (u16)rmReg;
		indexReg = (u16)scaledReg;
		//if scale == 0 never mind offseting
		offset = _offset;
	}
	void WriteRex(XEmitter *emit, int opBits, int bits, int customOp = -1) const;
	void WriteRest(XEmitter *emit, int extraBytes=0, X64Reg operandReg=(X64Reg)0xFF, bool warn_64bit_offset = true) const;
	void WriteSingleByteOp(XEmitter *emit, u8 op, X64Reg operandReg, int bits);
	// This one is public - must be written to
	u64 offset;  // use RIP-relative as much as possible - 64-bit immediates are not available.
	u16 operandReg;

	void WriteNormalOp(XEmitter *emit, bool toRM, NormalOp op, const OpArg &operand, int bits) const;
	bool IsImm() const {return scale == SCALE_IMM8 || scale == SCALE_IMM16 || scale == SCALE_IMM32 || scale == SCALE_IMM64;}
	bool IsSimpleReg() const {return scale == SCALE_NONE;}
	bool IsSimpleReg(X64Reg reg) const {
		if (!IsSimpleReg())
			return false;
		return GetSimpleReg() == reg;
	}

	bool CanDoOpWith(const OpArg &other) const
	{
		if (IsSimpleReg()) return true;
		if (!IsSimpleReg() && !other.IsSimpleReg() && !other.IsImm()) return false;
		return true;
	}

	int GetImmBits() const
	{
		switch (scale)
		{
		case SCALE_IMM8: return 8;
		case SCALE_IMM16: return 16;
		case SCALE_IMM32: return 32;
		case SCALE_IMM64: return 64;
		default: return -1;
		}
	}

	X64Reg GetSimpleReg() const
	{
		if (scale == SCALE_NONE)
			return (X64Reg)offsetOrBaseReg;
		else
			return INVALID_REG;
	}
private:
	u8 scale;
	u16 offsetOrBaseReg;
	u16 indexReg;
};

inline OpArg M(void *ptr)	    {return OpArg((u64)ptr, (int)SCALE_RIP);}
inline OpArg R(X64Reg value)	{return OpArg(0, SCALE_NONE, value);}
inline OpArg MatR(X64Reg value) {return OpArg(0, SCALE_ATREG, value);}
inline OpArg MDisp(X64Reg value, int offset) {
	return OpArg((u32)offset, SCALE_ATREG, value);
}
inline OpArg MComplex(X64Reg base, X64Reg scaled, int scale, int offset) {
	return OpArg(offset, scale, base, scaled);
}
inline OpArg MScaled(X64Reg scaled, int scale, int offset) {
	if (scale == SCALE_1)
		return OpArg(offset, SCALE_ATREG, scaled);
	else
		return OpArg(offset, scale | 0x20, RAX, scaled);
}
inline OpArg MRegSum(X64Reg base, X64Reg offset) {
	return MComplex(base, offset, 1, 0);
}
inline OpArg Imm8 (u8 imm)  {return OpArg(imm, SCALE_IMM8);}
inline OpArg Imm16(u16 imm) {return OpArg(imm, SCALE_IMM16);} //rarely used
inline OpArg Imm32(u32 imm) {return OpArg(imm, SCALE_IMM32);}
inline OpArg Imm64(u64 imm) {return OpArg(imm, SCALE_IMM64);}
#ifdef _M_X64
inline OpArg ImmPtr(void* imm) {return Imm64((u64)imm);}
#else
inline OpArg ImmPtr(void* imm) {return Imm32((u32)imm);}
#endif
inline u32 PtrOffset(void* ptr, void* base) {
#ifdef _M_X64
	s64 distance = (s64)ptr-(s64)base;
	if (distance >= 0x80000000LL ||
	    distance < -0x80000000LL) {
		_assert_msg_(DYNA_REC, 0, "pointer offset out of range");
		return 0;
	}
	return (u32)distance;
#else
	return (u32)ptr-(u32)base;
#endif
}

//usage: int a[]; ARRAY_OFFSET(a,10)
#define ARRAY_OFFSET(array,index) ((u32)((u64)&(array)[index]-(u64)&(array)[0]))
//usage: struct {int e;} s; STRUCT_OFFSET(s,e)
#define STRUCT_OFFSET(str,elem) ((u32)((u64)&(str).elem-(u64)&(str)))

struct FixupBranch
{
	u8 *ptr;
	int type; //0 = 8bit 1 = 32bit
};

enum SSECompare
{
	EQ = 0,
	LT,
	LE,
	UNORD,
	NEQ,
	NLT,
	NLE,
	ORD,
};

typedef const u8* JumpTarget;

class XEmitter
{
	friend struct OpArg;  // for Write8 etc
private:
	u8 *code;

protected:
	inline void Write8(u8 value)   {*code++ = value;}
	inline void Write16(u16 value) {*(u16*)code = (value); code += 2;}
	inline void Write32(u32 value) {*(u32*)code = (value); code += 4;}
	inline void Write64(u64 value) {*(u64*)code = (value); code += 8;}

public:
	XEmitter() { code = NULL; }
	XEmitter(u8 *code_ptr) { code = code_ptr; }
	virtual ~XEmitter() {}

	void SetCodePtr(u8 *ptr);
	void ReserveCodeSpace(int bytes);
	const u8 *AlignCode4();
	const u8 *AlignCode16();
	const u8 *AlignCodePage();
	const u8 *GetCodePtr() const;
	u8 *GetWritableCodePtr();

	// Do nothing
	void NOP(int count = 1); //nop padding - TODO: fast nop slides, for amd and intel (check their manuals)

};  // class XEmitter


// Everything that needs to generate X86 code should inherit from this.
// You get memory management for free, plus, you can use all the MOV etc functions without
// having to prefix them with gen-> or something similar.
class XCodeBlock : public XEmitter
{
protected:
	u8 *region;
	size_t region_size;

public:
	XCodeBlock() : region(NULL), region_size(0) {}
	virtual ~XCodeBlock() { if (region) FreeCodeSpace(); }

	// Call this before you generate any code.
	void AllocCodeSpace(int size)
	{
		region_size = size;
		region = (u8*)AllocateExecutableMemory(region_size);
		SetCodePtr(region);
	}

	// Always clear code space with breakpoints, so that if someone accidentally executes
	// uninitialized, it just breaks into the debugger.
	void ClearCodeSpace() 
	{
		// x86/64: 0xCC = breakpoint
		memset(region, 0xCC, region_size);
		ResetCodePtr();
	}

	// Call this when shutting down. Don't rely on the destructor, even though it'll do the job.
	void FreeCodeSpace()
	{
		FreeMemoryPages(region, region_size);
		region = NULL;
		region_size = 0;
	}

	bool IsInCodeSpace(u8 *ptr)
	{
		return ptr >= region && ptr < region + region_size;
	}

	// Cannot currently be undone. Will write protect the entire code region.
	// Start over if you need to change the code (call FreeCodeSpace(), AllocCodeSpace()).
	void WriteProtect()
	{
		WriteProtectMemory(region, region_size, true);		
	}

	void ResetCodePtr()
	{
		SetCodePtr(region);
	}

	size_t GetSpaceLeft() const
	{
		return region_size - (GetCodePtr() - region);
	}
};

}  // namespace

#endif // _DOLPHIN_INTEL_CODEGEN_
