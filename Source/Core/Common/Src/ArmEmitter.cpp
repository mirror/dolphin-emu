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

#include "Common.h"
#include "ArmEmitter.h"
#include "CPUDetect.h"

#include <assert.h>
#include <stdarg.h>

namespace Gen
{

void ARMXEmitter::SetCodePtr(u32 *ptr)
{
	code = ptr;
	startcode = code;
}

const u32 *ARMXEmitter::GetCodePtr() const
{
	return code;
}

u32 *ARMXEmitter::GetWritableCodePtr()
{
	return code;
}

void ARMXEmitter::ReserveCodeSpace(int bytes)
{
	for (int i = 0; i < bytes/4; i++)
		*code++ = 0xE1200070; //bkpt 0
}

const u32 *ARMXEmitter::AlignCode16()
{
	ReserveCodeSpace((-(s32)code) & 15);
	return code;
}

const u32 *ARMXEmitter::AlignCodePage()
{
	ReserveCodeSpace((-(s32)code) & 4095);
	return code;
}

void ARMXEmitter::Flush()
{
	__clear_cache (startcode, code);
	SLEEP(0);
}
void ARMXEmitter::SetCC(CCFlags cond)
{
	condition = cond << 28;
}

void ARMXEmitter::NOP(int count)
{
	for (int i = 0; i < count; i++) {
		Write32(condition | 0x01A00000);
	}
}

void ARMXEmitter::BKPT(u16 arg)
{
	Write32(condition | 0x01200070 | (arg << 4 & 0x000FFF00) | (arg & 0x0000000F));
}
void ARMXEmitter::YIELD()
{
	Write32(condition | 0x0320F001);
}

void ARMXEmitter::BL(const void *fnptr)
{
	s32 distance = (s32)fnptr - (s32(code) + 8);
        _assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "BL out of range (%p calls %p)", code, fnptr);
	Write32(condition | 0x0B000000 | ((distance >> 2) & 0x00FFFFFF));
}
void ARMXEmitter::BLX(ARMReg src)
{
	Write32(condition | 0x12FFF30 | src);
}

void ARMXEmitter::PUSH(const int num, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, num);
	for (i=0;i<num;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	Write32(condition | (2349 << 16) | RegList);
}
void ARMXEmitter::POP(const int num, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, num);
	for (i=0;i<num;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	Write32(condition | (2237 << 16) | RegList);
}

void ARMXEmitter::WriteDataOp(u32 op, ARMReg dest, ARMReg src, Operand2 const &op2)
{
	assert(op2.size == 8);
	Write32(condition | (op << 20) | (src << 16) | (dest << 12) | op2.encoding);
}
void ARMXEmitter::WriteDataOp(u32 op, ARMReg dest, ARMReg src)
{
	Write32(condition | (op << 20) | (dest << 12) | src);
}


// Data Operations
void ARMXEmitter::AND (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 0, dest, src, op2);}
void ARMXEmitter::ANDS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 1, dest, src, op2);}
void ARMXEmitter::EOR (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 2, dest, src, op2);}
void ARMXEmitter::EORS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 3, dest, src, op2);}
void ARMXEmitter::SUB (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 4, dest, src, op2);}
void ARMXEmitter::SUBS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 5, dest, src, op2);}
void ARMXEmitter::RSB (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 6, dest, src, op2);}
void ARMXEmitter::RSBS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 7, dest, src, op2);}
void ARMXEmitter::ADD (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 8, dest, src, op2);}
void ARMXEmitter::ADDS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 9, dest, src, op2);}
void ARMXEmitter::ADC (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(10, dest, src, op2);}
void ARMXEmitter::ADCS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(11, dest, src, op2);}
void ARMXEmitter::SBC (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(12, dest, src, op2);}
void ARMXEmitter::SBCS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(13, dest, src, op2);}
void ARMXEmitter::RSC (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(14, dest, src, op2);}
void ARMXEmitter::RSCS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(15, dest, src, op2);}
void ARMXEmitter::TST (             ARMReg src, Operand2 const &op2) { WriteDataOp(17, R0  , src, op2);}
void ARMXEmitter::TEQ (             ARMReg src, Operand2 const &op2) { WriteDataOp(19, R0  , src, op2);}
void ARMXEmitter::CMP (             ARMReg src, Operand2 const &op2) { WriteDataOp(21, R0  , src, op2);}
void ARMXEmitter::CMN (             ARMReg src, Operand2 const &op2) { WriteDataOp(23, R0  , src, op2);}
void ARMXEmitter::ORR (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(24, dest, src, op2);}
void ARMXEmitter::ORRS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(25, dest, src, op2);}
void ARMXEmitter::MOV (ARMReg dest,             Operand2 const &op2) { WriteDataOp(26, dest, R0 , op2);}
void ARMXEmitter::MOVS(ARMReg dest,             Operand2 const &op2) { WriteDataOp(27, dest, R0 , op2);}
void ARMXEmitter::MOV (ARMReg dest, ARMReg src					   ) { WriteDataOp(26, dest, src); }
void ARMXEmitter::MOVS (ARMReg dest, ARMReg src					   ) { WriteDataOp(27, dest, src); }
void ARMXEmitter::BIC (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(28, dest, src, op2);}
void ARMXEmitter::BICS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(29, dest, src, op2);}
void ARMXEmitter::MVN (ARMReg dest,             Operand2 const &op2) { WriteDataOp(30, dest, R0 , op2);}
void ARMXEmitter::MVNS(ARMReg dest,             Operand2 const &op2) { WriteDataOp(31, dest, R0 , op2);}

// Memory Load/Store operations
void ARMXEmitter::WriteMoveOp(u32 op, ARMReg dest, Operand2 const &op2)
{
	assert(op2.size == 16);
	Write32(condition | (op << 20) | (dest << 12) | op2.encoding);
}
void ARMXEmitter::MOVT(ARMReg dest, 			Operand2 const &op2) { WriteMoveOp( 52, dest, op2);}
void ARMXEmitter::MOVW(ARMReg dest, 			Operand2 const &op2) { WriteMoveOp( 48, dest, op2);}

void ARMXEmitter::WriteStoreOp(u32 op, ARMReg dest, ARMReg src, Operand2 const &op2)
{
	Write32(condition | (op << 20) | (dest << 16) | (src << 12) | (op2.encoding & 0x00000FFF));
}
void ARMXEmitter::STR (ARMReg dest, ARMReg src, Operand2 const &op) { WriteStoreOp(0x40, dest, src, op);}
void ARMXEmitter::STRB(ARMReg dest, ARMReg src, Operand2 const &op) { WriteStoreOp(0x44, dest, src, op);}
void ARMXEmitter::LDR (ARMReg dest, ARMReg src, Operand2 const &op) { WriteStoreOp(0x41, dest, src, op);}
void ARMXEmitter::LDRB(ARMReg dest, ARMReg src, Operand2 const &op) { WriteStoreOp(0x45, dest, src, op);}

void ArmXEmitter::WriteRegStoreOp(u32 op, ARMReg dest, bool WriteBack, u16 RegList);
{
    Write32(condition | (op << 20) | (WriteBack << 21) | (dest << 16) | RegList);
}
void ARMXEmitter::STMFD(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
    _assert_msg_(DYNA_REC, Regnum > 1, "Doesn't support only one register");
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, Regnum);
	for (i=0;i<Regnum;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
    WriteRegStoreOp(0x90, dest, Writeback, RegList);
}
void ARMXEmitter::LDMFD(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
    _assert_msg_(DYNA_REC, Regnum > 1, "Doesn't support only one register");
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, Regnum);
	for (i=0;i<Regnum;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
    WriteRegStoreOp(0x89, dest, Writeback, RegList);
}
// helper routines for setting pointers
void ARMXEmitter::CallCdeclFunction3(void* fnptr, u32 arg0, u32 arg1, u32 arg2)
{
}

void ARMXEmitter::CallCdeclFunction4(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3)
{
}

void ARMXEmitter::CallCdeclFunction5(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
}

void ARMXEmitter::CallCdeclFunction6(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
}
}
