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
#include "ABI.h"
#include "CPUDetect.h"

namespace Gen
{

void XEmitter::SetCodePtr(u32 *ptr)
{
	code = ptr;
}

const u32 *XEmitter::GetCodePtr() const
{
	return code;
}

u32 *XEmitter::GetWritableCodePtr()
{
	return code;
}

void XEmitter::ReserveCodeSpace(int bytes)
{
	for (int i = 0; i < bytes/4; i++)
		*code++ = 0xE1200070; //bkpt 0
}

const u32 *XEmitter::AlignCode16()
{
	ReserveCodeSpace((-(s32)code) & 15);
	return code;
}

const u32 *XEmitter::AlignCodePage()
{
	ReserveCodeSpace((-(s32)code) & 4095);
	return code;
}

void XEmitter::SetCC(CCFlags cond)
{
	condition = cond << 28;
}

void XEmitter::NOP(int count)
{
	for (int i = 0; i < count; i++) {
		Write32(condition | 0x01A00000);
	}
}

void XEmitter::BKPT(u16 arg)
{
	Write32(condition | 0x01200070 | (arg << 4 & 0x000FFF00) | (arg & 0x0000000F));
}
void XEmitter::YIELD()
{
	Write32(condition | 0x0320F001);
}

void XEmitter::BL(const void *fnptr)
{
	s32 distance = (s32)fnptr - (s32(code) + 8);
	_assert_msg_(DYNA_REC, distance < -33554432
		     || distance >=  33554432,
		     "BL out of range (%p calls %p)", code, fnptr);
	Write32(condition | 0x0B000000 | (distance >> 2 & 0x00FFFFFF));
}

void XEmitter::WriteDataOp(u32 op, ARMReg dest, ARMReg src, Operand2 const &op2)
{
	Write32(condition | (op << 20) | (src << 16) | (dest << 12) | op2.encoding);
}

// Data Operations
void XEmitter::AND (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 0, dest, src, op2);}
void XEmitter::ANDS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 1, dest, src, op2);}
void XEmitter::EOR (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 2, dest, src, op2);}
void XEmitter::EORS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 3, dest, src, op2);}
void XEmitter::SUB (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 4, dest, src, op2);}
void XEmitter::SUBS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 5, dest, src, op2);}
void XEmitter::RSB (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 6, dest, src, op2);}
void XEmitter::RSBS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 7, dest, src, op2);}
void XEmitter::ADD (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 8, dest, src, op2);}
void XEmitter::ADDS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp( 9, dest, src, op2);}
void XEmitter::ADC (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(10, dest, src, op2);}
void XEmitter::ADCS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(11, dest, src, op2);}
void XEmitter::SBC (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(12, dest, src, op2);}
void XEmitter::SBCS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(13, dest, src, op2);}
void XEmitter::RSC (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(14, dest, src, op2);}
void XEmitter::RSCS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(15, dest, src, op2);}
void XEmitter::TST (             ARMReg src, Operand2 const &op2) { WriteDataOp(17, R0  , src, op2);}
void XEmitter::TEQ (             ARMReg src, Operand2 const &op2) { WriteDataOp(19, R0  , src, op2);}
void XEmitter::CMP (             ARMReg src, Operand2 const &op2) { WriteDataOp(21, R0  , src, op2);}
void XEmitter::CMN (             ARMReg src, Operand2 const &op2) { WriteDataOp(23, R0  , src, op2);}
void XEmitter::ORR (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(24, dest, src, op2);}
void XEmitter::ORRS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(25, dest, src, op2);}
void XEmitter::MOV (ARMReg dest,             Operand2 const &op2) { WriteDataOp(26, dest, R0 , op2);}
void XEmitter::MOVS(ARMReg dest,             Operand2 const &op2) { WriteDataOp(27, dest, R0 , op2);}
void XEmitter::BIC (ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(28, dest, src, op2);}
void XEmitter::BICS(ARMReg dest, ARMReg src, Operand2 const &op2) { WriteDataOp(29, dest, src, op2);}
void XEmitter::MVN (ARMReg dest,             Operand2 const &op2) { WriteDataOp(30, dest, R0 , op2);}
void XEmitter::MVNS(ARMReg dest,             Operand2 const &op2) { WriteDataOp(31, dest, R0 , op2);}
	
// helper routines for setting pointers
void XEmitter::CallCdeclFunction3(void* fnptr, u32 arg0, u32 arg1, u32 arg2)
{
}

void XEmitter::CallCdeclFunction4(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3)
{
}

void XEmitter::CallCdeclFunction5(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
}

void XEmitter::CallCdeclFunction6(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
}
}
