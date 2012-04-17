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

enum ARMReg
{
	// GPRs
	R0 = 0, R1, R2, R3, R4, R5,
	R6, R7, R8, R9, R10, R11,

	// SPRs
	// R13 - R15 are SP, LR, and PC.
	// Almost always referred to by name instead of register number
	R12 = 12, R13 = 13, R14 = 14, R15 = 15,
	_IP = 12, _SP = 13, _LR = 14, _PC = 15,


	// VFP single precision registers
	S0 = 0, S1, S2, S3, S4, S5, S6,
	S7, S8, S9, S10, S11, S12, S13,
	S14, S15, S16, S17, S18, S19, S20,
	S21, S22, S23, S24, S25, S26, S27,
	S28, S29, S30, S31,

	// VFP Double Precision registers
	D0 = 0, D1, D2, D3, D4, D5, D6, D7,
	D8, D9, D10, D11, D12, D13, D14, D15,
	D16, D17, D18, D19, D20, D21, D22, D23,
	D24, D25, D26, D27, D28, D29, D30, D31,
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
	CC_AL, // Always (unconditional) 14
};
const u32 NO_COND = 0xE0000000;

enum ShiftType
{
	LSL = 0,
	ASL = 0,
	LSR = 1,
	ASR = 2,
	ROR = 3,
	RRX = 4
};

enum
{
	NUMGPRs = 13,
};

class ARMXEmitter;

struct Operand2
{
	u32 encoding;
	u8 size;
	Operand2() {}
	Operand2(u8 imm, u8 rotation)
	{
		_assert_msg_(DYNA_REC, (rotation & 0xE1) != 0, "Invalid Operand2: immediate rotation %u", rotation);
		encoding = (1 << 25) | (rotation << 7) | imm;
		size = 8;
	}
	Operand2(u16 imm)
	{
			encoding = ( (imm & 0xF000) << 4) | (imm & 0x0FFF);
			size = 16;
	}
	Operand2(ARMReg base, ShiftType type = LSL, u8 shift = 0)
	{
		switch (type)
		{
		case LSL:
			_assert_msg_(DYNA_REC, shift >= 32, "Invalid Operand2: LSL %u", shift);
			break;
		case LSR:
			_assert_msg_(DYNA_REC, shift > 32, "Invalid Operand2: LSR %u", shift);
			if (!shift)
				type = LSL;
			if (shift == 32)
				shift = 0;
			break;
		case ASR:
			_assert_msg_(DYNA_REC, shift > 32, "Invalid Operand2: LSR %u", shift);
			if (!shift)
				type = LSL;
			if (shift == 32)
				shift = 0;
			break;
		case ROR:
			_assert_msg_(DYNA_REC, shift >= 32, "Invalid Operand2: ROR %u", shift);
			if (!shift)
				type = LSL;
			break;
		case RRX:
			_assert_msg_(DYNA_REC, shift != 0, "Invalid Operand2: RRX does not take an immediate shift amount");
			type = ROR;
			break;
		}
		encoding = (shift << 7) | (type << 5) | base;
	}
	Operand2(ARMReg base, ShiftType type, ARMReg shift)
	{
		_assert_msg_(DYNA_REC, type == RRX, "Invalid Operand2: RRX does not take a register shift amount");
		encoding = (shift << 8) | (type << 5) | 0x10 | base;
	}
};

//usage: int a[]; ARRAY_OFFSET(a,10)
#define ARRAY_OFFSET(array,index) ((u32)((u64)&(array)[index]-(u64)&(array)[0]))
//usage: struct {int e;} s; STRUCT_OFFSET(s,e)
#define STRUCT_OFFSET(str,elem) ((u32)((u64)&(str).elem-(u64)&(str)))

typedef const u32* FixupBranch;
typedef const u32* JumpTarget;

class ARMXEmitter
{
	friend struct OpArg;  // for Write8 etc
private:
	u32 *code, *startcode;
	u32 condition;

	void WriteDataOp(u32 op, ARMReg dest, ARMReg src, Operand2 const &op2);
	void WriteDataOp(u32 op, ARMReg dest, ARMReg src);
	void WriteMoveOp(u32 op, ARMReg dest, Operand2 const &op2);
	void WriteStoreOp(u32 op, ARMReg dest, ARMReg src, Operand2 const &op2);

protected:
	inline void Write32(u32 value) {*code++ = value;}

public:
	ARMXEmitter() { code = NULL; startcode = NULL; condition = CC_AL << 28;}
	ARMXEmitter(u32 *code_ptr) { code = code_ptr; startcode = code_ptr; condition = CC_AL << 28;}
	virtual ~ARMXEmitter() {}

	void SetCodePtr(u32 *ptr);
	void ReserveCodeSpace(int bytes);
	const u32 *AlignCode16();
	const u32 *AlignCodePage();
	const u32 *GetCodePtr() const;
	void Flush();
	u32 *GetWritableCodePtr();

	void SetCC(CCFlags cond = CC_AL);

	// Debug Breakpoint
	void BKPT(u16 arg);

	// Hint instruction
	void YIELD();
	
	// Do nothing
	void NOP(int count = 1); //nop padding - TODO: fast nop slides, for amd and intel (check their manuals)
	
#ifdef CALL
#undef CALL
#endif

	void BL(const void *fnptr);
	void BLX(ARMReg src);
	void PUSH(const int num, ...);
	void POP(const int num, ...);
	
	// Data operations
	void AND (ARMReg dest, ARMReg src, Operand2 const &op2);
	void ANDS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void EOR (ARMReg dest, ARMReg src, Operand2 const &op2);
	void EORS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void SUB (ARMReg dest, ARMReg src, Operand2 const &op2);
	void SUBS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void RSB (ARMReg dest, ARMReg src, Operand2 const &op2);
	void RSBS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void ADD (ARMReg dest, ARMReg src, Operand2 const &op2);
	void ADDS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void ADC (ARMReg dest, ARMReg src, Operand2 const &op2);
	void ADCS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void SBC (ARMReg dest, ARMReg src, Operand2 const &op2);
	void SBCS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void RSC (ARMReg dest, ARMReg src, Operand2 const &op2);
	void RSCS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void TST (             ARMReg src, Operand2 const &op2);
	void TEQ (             ARMReg src, Operand2 const &op2);
	void CMP (             ARMReg src, Operand2 const &op2);
	void CMN (             ARMReg src, Operand2 const &op2);
	void ORR (ARMReg dest, ARMReg src, Operand2 const &op2);
	void ORRS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void MOV (ARMReg dest,             Operand2 const &op2);
	void MOV (ARMReg dest, ARMReg src					  );
	void MOVS(ARMReg dest, ARMReg src					  );
	void MOVS(ARMReg dest,             Operand2 const &op2);
	void BIC (ARMReg dest, ARMReg src, Operand2 const &op2);
	void BICS(ARMReg dest, ARMReg src, Operand2 const &op2);
	void MVN (ARMReg dest,             Operand2 const &op2);
	void MVNS(ARMReg dest,             Operand2 const &op2);

	// Memory load/store operations
	void MOVT(ARMReg dest, 			   Operand2 const &op2);
	void MOVW(ARMReg dest, 			   Operand2 const &op2);
	void LDR (ARMReg dest, ARMReg src, Operand2 const &op2);
	void LDRB(ARMReg dest, ARMReg src, Operand2 const &op2);
	void STR (ARMReg dest, ARMReg src, Operand2 const &op2);
	void STRB(ARMReg dest, ARMReg src, Operand2 const &op2);
	void STMFD(ARMReg dest, bool WriteBack, const int Regnum, ...);
	void LDMFD(ARMReg dest, bool WriteBack, const int Regnum, ...);
	
	// Strange call wrappers.
	void CallCdeclFunction3(void* fnptr, u32 arg0, u32 arg1, u32 arg2);
	void CallCdeclFunction4(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3);
	void CallCdeclFunction5(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4);
	void CallCdeclFunction6(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5);
	#define CallCdeclFunction3_I(a,b,c,d) CallCdeclFunction3((void *)(a), (b), (c), (d))
	#define CallCdeclFunction4_I(a,b,c,d,e) CallCdeclFunction4((void *)(a), (b), (c), (d), (e)) 
	#define CallCdeclFunction5_I(a,b,c,d,e,f) CallCdeclFunction5((void *)(a), (b), (c), (d), (e), (f)) 
	#define CallCdeclFunction6_I(a,b,c,d,e,f,g) CallCdeclFunction6((void *)(a), (b), (c), (d), (e), (f), (g)) 

	#define DECLARE_IMPORT(x)

};  // class ARMXEmitter


// Everything that needs to generate X86 code should inherit from this.
// You get memory management for free, plus, you can use all the MOV etc functions without
// having to prefix them with gen-> or something similar.
class XCodeBlock : public ARMXEmitter
{
protected:
	u32 *region;
	size_t region_size;

public:
	XCodeBlock() : region(NULL), region_size(0) {}
	virtual ~XCodeBlock() { if (region) FreeCodeSpace(); }

	// Call this before you generate any code.
	void AllocCodeSpace(int size)
	{
		region_size = size;
		region = (u32*)AllocateExecutableMemory(region_size);
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

	bool IsInCodeSpace(u32 *ptr)
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
