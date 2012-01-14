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

// TODO(ector): Add EAX special casing, for ever so slightly smaller code.
struct NormalOpDef
{
	u8 toRm8, toRm32, fromRm8, fromRm32, imm8, imm32, simm8, ext;
};

static const NormalOpDef nops[11] = 
{
	{0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x83, 0}, //ADD
	{0x10, 0x11, 0x12, 0x13, 0x80, 0x81, 0x83, 2}, //ADC

	{0x28, 0x29, 0x2A, 0x2B, 0x80, 0x81, 0x83, 5}, //SUB
	{0x18, 0x19, 0x1A, 0x1B, 0x80, 0x81, 0x83, 3}, //SBB

	{0x20, 0x21, 0x22, 0x23, 0x80, 0x81, 0x83, 4}, //AND
	{0x08, 0x09, 0x0A, 0x0B, 0x80, 0x81, 0x83, 1}, //OR

	{0x30, 0x31, 0x32, 0x33, 0x80, 0x81, 0x83, 6}, //XOR
	{0x88, 0x89, 0x8A, 0x8B, 0xC6, 0xC7, 0xCC, 0}, //MOV

	{0x84, 0x85, 0x84, 0x85, 0xF6, 0xF7, 0xCC, 0}, //TEST (to == from)
	{0x38, 0x39, 0x3A, 0x3B, 0x80, 0x81, 0x83, 7}, //CMP

	{0x86, 0x87, 0x86, 0x87, 0xCC, 0xCC, 0xCC, 7}, //XCHG
};

enum NormalSSEOps
{
	sseCMP =         0xC2, 
	sseADD =         0x58, //ADD
	sseSUB =		 0x5C, //SUB
	sseAND =		 0x54, //AND
	sseANDN =		 0x55, //ANDN
	sseOR  =         0x56, 
	sseXOR  =        0x57,
	sseMUL =		 0x59, //MUL,
	sseDIV =		 0x5E, //DIV
	sseMIN =		 0x5D, //MIN
	sseMAX =		 0x5F, //MAX
	sseCOMIS =		 0x2F, //COMIS
	sseUCOMIS =		 0x2E, //UCOMIS
	sseSQRT =		 0x51, //SQRT
	sseRSQRT =		 0x52, //RSQRT (NO DOUBLE PRECISION!!!)
	sseMOVAPfromRM = 0x28, //MOVAP from RM
	sseMOVAPtoRM =	 0x29, //MOVAP to RM
	sseMOVUPfromRM = 0x10, //MOVUP from RM
	sseMOVUPtoRM =	 0x11, //MOVUP to RM
	sseMASKMOVDQU =  0xF7,
	sseLDDQU      =  0xF0,
	sseSHUF       =  0xC6,
	sseMOVNTDQ     = 0xE7,
	sseMOVNTP      = 0x2B,
};


void XEmitter::SetCodePtr(u8 *ptr)
{
	code = ptr;
}

const u8 *XEmitter::GetCodePtr() const
{
	return code;
}

u8 *XEmitter::GetWritableCodePtr()
{
	return code;
}

void XEmitter::ReserveCodeSpace(int bytes)
{
	for (int i = 0; i < bytes; i++)
		*code++ = 0xCC;
}

const u8 *XEmitter::AlignCode4()
{
	int c = int((u64)code & 3);
	if (c)
		ReserveCodeSpace(4-c);
	return code;
}

const u8 *XEmitter::AlignCode16()
{
	int c = int((u64)code & 15);
	if (c)
		ReserveCodeSpace(16-c);
	return code;
}

const u8 *XEmitter::AlignCodePage()
{
	int c = int((u64)code & 4095);
	if (c)
		ReserveCodeSpace(4096-c);
	return code;
}

void XEmitter::NOP(int count)
{
	// ARM Implementation
	int i;
	for (i = 0; i < count; i++) {
		Write16(0x5F80);
	}
}

void XEmitter::BKPT()
{
	// ARM Implementation
	Write16(0xBE03);
}
void XEmitter::YIELD()
{
	// ARM Implementation
	Write16(0xBF10);
}
void XEmitter::CALL(const void *fnptr)
{
	// ARM Implementation
	s64 distance = s64(fnptr) - (s32(code) + 4);
	_assert_msg_(DYNA_REC, distance < -33554432
		     || distance >=  33554428,
		     "CALL out of range (%p calls %p)", code, fnptr);
	Write8(0xCB);
	Write24(s32(distance));
}
//operand can either be immediate or register
void OpArg::WriteNormalOp(XEmitter *emit, bool toRM, NormalOp op, const OpArg &operand, int bits) const
{
	if (IsImm())
	{
		_assert_msg_(DYNA_REC, 0, "WriteNormalOp - Imm argument, wrong order");
	}
	if(bits == 64)
		_assert_msg_(DYNA_REC, 0, "WriteNormalOp - Can't handle 64bit on ARM platform");

	if (operand.IsImm())
	{

		if (operand.scale == SCALE_IMM8 && bits == 8) 
		{
			// Writing an eight bit value
			emit->Write16(0xE3A0);
			emit->Write8(GetSimpleReg() << 4);
			emit->Write8((u8)operand.offset);
			
		}
		else if ((operand.scale == SCALE_IMM16 && bits == 16))
		{
			emit->Write8(0xE3);
			emit->Write8((u16)operand.offset >> 12);
			emit->Write16((u16)operand.offset | GetSimpleReg() << 12);
		}
		else if (operand.scale == SCALE_IMM32 && bits == 32)
		{
			// Must do two moves to get the full 32bits in the register
			// First do the regular 16bit move
			emit->Write8(0xE3);
			emit->Write8((u16)(operand.offset >> 12));
			emit->Write16(GetSimpleReg() << 12 | (u16)operand.offset);

			// Now do a MOVT to get the top bits in
			emit->Write8(0xE3);
			emit->Write8(0x40 | operand.offset >> 20);
			emit->Write16((operand.offset >> 16 & 0xF000) | GetSimpleReg() << 12);
		}
		else
		{
			_assert_msg_(DYNA_REC, 0, "WriteNormalOp - Unhandled case");
		}
	}
	else
	{
		// Register to Register
		emit->Write16(0xE1A0);
		emit->Write8(GetSimpleReg() << 4);
		emit->Write8(operand.GetSimpleReg());
	}
}

void XEmitter::WriteNormalOp(XEmitter *emit, int bits, NormalOp op, const OpArg &a1, const OpArg &a2)
{
	if (a1.IsImm())
	{
		//Booh! Can't write to an imm
		_assert_msg_(DYNA_REC, 0, "WriteNormalOp - a1 cannot be imm");
		return;
	}

	a1.WriteNormalOp(emit, true, op, a2, bits);
}
// Bit Operations
void XEmitter::ADD (int bits, const OpArg &a1, const OpArg &a2) {}
void XEmitter::ADC (int bits, const OpArg &a1, const OpArg &a2) {}
void XEmitter::SUB (int bits, const OpArg &a1, const OpArg &a2) {}
void XEmitter::SBB (int bits, const OpArg &a1, const OpArg &a2) {}
void XEmitter::AND (int bits, const OpArg &a1, const OpArg &a2) {}
void XEmitter::OR  (int bits, const OpArg &a1, const OpArg &a2) {}
void XEmitter::XOR (int bits, const OpArg &a1, const OpArg &a2) {}
void XEmitter::MOV (int bits, const OpArg &a1, const OpArg &a2) 
{
	#ifdef _DEBUG
	_assert_msg_(DYNA_REC, !a1.IsSimpleReg() || !a2.IsSimpleReg() || a1.GetSimpleReg() != a2.GetSimpleReg(), "Redundant MOV @ %p - bug in JIT?", 
				 code); 
	#endif
	WriteNormalOp(this, bits, nrmMOV, a1, a2);
}

// Bunch of X86 BS down here





void XEmitter::JMP(const u8 *addr, bool force5Bytes)
{
}

void XEmitter::JMPptr(const OpArg &arg2)
{
}
void XEmitter::CALLptr(OpArg arg)
{
}

FixupBranch XEmitter::J(bool force5bytes)
{
}

FixupBranch XEmitter::J_CC(CCFlags conditionCode, bool force5bytes)
{
}

void XEmitter::J_CC(CCFlags conditionCode, const u8 * addr, bool force5Bytes)
{
}

void XEmitter::SetJumpTarget(const FixupBranch &branch)
{
}


//Single byte opcodes
//There is no PUSHAD/POPAD in 64-bit mode.
void XEmitter::INT3() {}
void XEmitter::RET()  {}
void XEmitter::RET_FAST()  {} //two-byte return (rep ret) - recommended by AMD optimization manual for the case of jumping to a ret


void XEmitter::PAUSE() {} //use in tight spinloops for energy saving on some cpu
void XEmitter::CLC()  {} //clear carry
void XEmitter::CMC()  {} //flip carry
void XEmitter::STC()  {} //set carry

//TODO: xchg ah, al ???
void XEmitter::XCHG_AHAL()
{
}

//These two can not be executed on early Intel 64-bit CPU:s, only on AMD!
void XEmitter::LAHF() {}
void XEmitter::SAHF() {}

void XEmitter::PUSHF() {}
void XEmitter::POPF()  {}

void XEmitter::LFENCE() {}
void XEmitter::MFENCE() {}
void XEmitter::SFENCE() {}

void XEmitter::CWD(int bits)
{
}

void XEmitter::CBW(int bits)
{
}

//Simple opcodes


//push/pop do not need wide to be 64-bit
void XEmitter::PUSH(X64Reg reg) {}
void XEmitter::POP(X64Reg reg)  {}

void XEmitter::PUSH(int bits, const OpArg &reg) 
{ 
}

void XEmitter::POP(int /*bits*/, const OpArg &reg)
{ 
}

void XEmitter::BSWAP(int bits, X64Reg reg)
{

}

// Undefined opcode - reserved
// If we ever need a way to always cause a non-breakpoint hard exception...
void XEmitter::UD2()
{
}

void XEmitter::PREFETCH(PrefetchLevel level, OpArg arg)
{
}

void XEmitter::SETcc(CCFlags flag, OpArg dest)
{
}

void XEmitter::CMOVcc(int bits, X64Reg dest, OpArg src, CCFlags flag)
{
}


void XEmitter::MUL(int bits, OpArg src)  {}
void XEmitter::DIV(int bits, OpArg src)  {}
void XEmitter::IMUL(int bits, OpArg src) {}
void XEmitter::IDIV(int bits, OpArg src) {}
void XEmitter::NEG(int bits, OpArg src)  {}
void XEmitter::NOT(int bits, OpArg src)  {}


void XEmitter::MOVNTI(int bits, OpArg dest, X64Reg src)
{
}

void XEmitter::BSF(int bits, X64Reg dest, OpArg src) {} //bottom bit to top bit
void XEmitter::BSR(int bits, X64Reg dest, OpArg src) {} //top bit to bottom bit

void XEmitter::MOVSX(int dbits, int sbits, X64Reg dest, OpArg src)
{
}

void XEmitter::MOVZX(int dbits, int sbits, X64Reg dest, OpArg src)
{
}


void XEmitter::LEA(int bits, X64Reg dest, OpArg src)
{
}


// large rotates and shift are slower on intel than amd
// intel likes to rotate by 1, and the op is smaller too
void XEmitter::ROL(int bits, OpArg dest, OpArg shift) {}
void XEmitter::ROR(int bits, OpArg dest, OpArg shift) {}
void XEmitter::RCL(int bits, OpArg dest, OpArg shift) {}
void XEmitter::RCR(int bits, OpArg dest, OpArg shift) {}
void XEmitter::SHL(int bits, OpArg dest, OpArg shift) {}
void XEmitter::SHR(int bits, OpArg dest, OpArg shift) {}
void XEmitter::SAR(int bits, OpArg dest, OpArg shift) {}


void XEmitter::BT(int bits, OpArg dest, OpArg index)  {}
void XEmitter::BTS(int bits, OpArg dest, OpArg index) {}
void XEmitter::BTR(int bits, OpArg dest, OpArg index) {}
void XEmitter::BTC(int bits, OpArg dest, OpArg index) {}

//shift can be either imm8 or cl
void XEmitter::SHRD(int bits, OpArg dest, OpArg src, OpArg shift)
{
}

void XEmitter::SHLD(int bits, OpArg dest, OpArg src, OpArg shift)
{
}

void XEmitter::TEST(int bits, const OpArg &a1, const OpArg &a2) {}
void XEmitter::CMP (int bits, const OpArg &a1, const OpArg &a2) {}
void XEmitter::XCHG(int bits, const OpArg &a1, const OpArg &a2) {}

void XEmitter::IMUL(int bits, X64Reg regOp, OpArg a1, OpArg a2)
{
}

void XEmitter::IMUL(int bits, X64Reg regOp, OpArg a)
{
}

void XEmitter::MOVD_xmm(X64Reg dest, const OpArg &arg) {}
void XEmitter::MOVD_xmm(const OpArg &arg, X64Reg src) {}

void XEmitter::MOVQ_xmm(X64Reg dest, OpArg arg) {
}

void XEmitter::MOVQ_xmm(OpArg arg, X64Reg src) {
}

void XEmitter::STMXCSR(OpArg memloc) {}
void XEmitter::LDMXCSR(OpArg memloc) {}

void XEmitter::MOVNTDQ(OpArg arg, X64Reg regOp)   {}
void XEmitter::MOVNTPS(OpArg arg, X64Reg regOp)   {}
void XEmitter::MOVNTPD(OpArg arg, X64Reg regOp)   {}

void XEmitter::ADDSS(X64Reg regOp, OpArg arg)   {}
void XEmitter::ADDSD(X64Reg regOp, OpArg arg)   {}
void XEmitter::SUBSS(X64Reg regOp, OpArg arg)   {}
void XEmitter::SUBSD(X64Reg regOp, OpArg arg)   {}
void XEmitter::CMPSS(X64Reg regOp, OpArg arg, u8 compare)   {}
void XEmitter::CMPSD(X64Reg regOp, OpArg arg, u8 compare)   {}
void XEmitter::MULSS(X64Reg regOp, OpArg arg)   {}
void XEmitter::MULSD(X64Reg regOp, OpArg arg)   {}
void XEmitter::DIVSS(X64Reg regOp, OpArg arg)   {}
void XEmitter::DIVSD(X64Reg regOp, OpArg arg)   {}
void XEmitter::MINSS(X64Reg regOp, OpArg arg)   {}
void XEmitter::MINSD(X64Reg regOp, OpArg arg)  {}
void XEmitter::MAXSS(X64Reg regOp, OpArg arg)   {}
void XEmitter::MAXSD(X64Reg regOp, OpArg arg)   {}
void XEmitter::SQRTSS(X64Reg regOp, OpArg arg)  {}
void XEmitter::SQRTSD(X64Reg regOp, OpArg arg)  {}
void XEmitter::RSQRTSS(X64Reg regOp, OpArg arg) {}

void XEmitter::ADDPS(X64Reg regOp, OpArg arg)   {}
void XEmitter::ADDPD(X64Reg regOp, OpArg arg)   {}
void XEmitter::SUBPS(X64Reg regOp, OpArg arg)   {}
void XEmitter::SUBPD(X64Reg regOp, OpArg arg)   {}
void XEmitter::CMPPS(X64Reg regOp, OpArg arg, u8 compare)   {}
void XEmitter::CMPPD(X64Reg regOp, OpArg arg, u8 compare)   {}
void XEmitter::ANDPS(X64Reg regOp, OpArg arg)   {}
void XEmitter::ANDPD(X64Reg regOp, OpArg arg)   {}
void XEmitter::ANDNPS(X64Reg regOp, OpArg arg)  {}
void XEmitter::ANDNPD(X64Reg regOp, OpArg arg)  {}
void XEmitter::ORPS(X64Reg regOp, OpArg arg)    {}
void XEmitter::ORPD(X64Reg regOp, OpArg arg)    {}
void XEmitter::XORPS(X64Reg regOp, OpArg arg)   {}
void XEmitter::XORPD(X64Reg regOp, OpArg arg)   {}
void XEmitter::MULPS(X64Reg regOp, OpArg arg)   {}
void XEmitter::MULPD(X64Reg regOp, OpArg arg)   {}
void XEmitter::DIVPS(X64Reg regOp, OpArg arg)   {}
void XEmitter::DIVPD(X64Reg regOp, OpArg arg)   {}
void XEmitter::MINPS(X64Reg regOp, OpArg arg)   {}
void XEmitter::MINPD(X64Reg regOp, OpArg arg)  {}
void XEmitter::MAXPS(X64Reg regOp, OpArg arg)   {}
void XEmitter::MAXPD(X64Reg regOp, OpArg arg)   {}
void XEmitter::SQRTPS(X64Reg regOp, OpArg arg)  {}
void XEmitter::SQRTPD(X64Reg regOp, OpArg arg)  {}
void XEmitter::RSQRTPS(X64Reg regOp, OpArg arg) {}
void XEmitter::SHUFPS(X64Reg regOp, OpArg arg, u8 shuffle) {}
void XEmitter::SHUFPD(X64Reg regOp, OpArg arg, u8 shuffle) {}

void XEmitter::COMISS(X64Reg regOp, OpArg arg)  {} //weird that these should be packed
void XEmitter::COMISD(X64Reg regOp, OpArg arg)  {} //ordered
void XEmitter::UCOMISS(X64Reg regOp, OpArg arg) {} //unordered
void XEmitter::UCOMISD(X64Reg regOp, OpArg arg) {}

void XEmitter::MOVAPS(X64Reg regOp, OpArg arg)  {}
void XEmitter::MOVAPD(X64Reg regOp, OpArg arg)  {}
void XEmitter::MOVAPS(OpArg arg, X64Reg regOp) {}
void XEmitter::MOVAPD(OpArg arg, X64Reg regOp)  {}

void XEmitter::MOVUPS(X64Reg regOp, OpArg arg)  {}
void XEmitter::MOVUPD(X64Reg regOp, OpArg arg)  {}
void XEmitter::MOVUPS(OpArg arg, X64Reg regOp)  {}
void XEmitter::MOVUPD(OpArg arg, X64Reg regOp)  {}

void XEmitter::MOVSS(X64Reg regOp, OpArg arg)   {}
void XEmitter::MOVSD(X64Reg regOp, OpArg arg)   {}
void XEmitter::MOVSS(OpArg arg, X64Reg regOp)   {}
void XEmitter::MOVSD(OpArg arg, X64Reg regOp)   {}

void XEmitter::CVTPS2PD(X64Reg regOp, OpArg arg) {}
void XEmitter::CVTPD2PS(X64Reg regOp, OpArg arg){}

void XEmitter::CVTSD2SS(X64Reg regOp, OpArg arg) {}
void XEmitter::CVTSS2SD(X64Reg regOp, OpArg arg) {}
void XEmitter::CVTSD2SI(X64Reg regOp, OpArg arg) {}

void XEmitter::CVTDQ2PD(X64Reg regOp, OpArg arg) {}
void XEmitter::CVTDQ2PS(X64Reg regOp, OpArg arg) {}
void XEmitter::CVTPD2DQ(X64Reg regOp, OpArg arg) {}
void XEmitter::CVTPS2DQ(X64Reg regOp, OpArg arg) {}

void XEmitter::CVTTSS2SI(X64Reg xregdest, OpArg arg) {}
void XEmitter::CVTTPS2DQ(X64Reg xregdest, OpArg arg) {}

void XEmitter::MASKMOVDQU(X64Reg dest, X64Reg src)  {}

void XEmitter::MOVMSKPS(X64Reg dest, OpArg arg) {}
void XEmitter::MOVMSKPD(X64Reg dest, OpArg arg) {}

void XEmitter::LDDQU(X64Reg dest, OpArg arg)    {}// For integer data only

// THESE TWO ARE UNTESTED.
void XEmitter::UNPCKLPS(X64Reg dest, OpArg arg) {}
void XEmitter::UNPCKHPS(X64Reg dest, OpArg arg) {}

void XEmitter::UNPCKLPD(X64Reg dest, OpArg arg) {}
void XEmitter::UNPCKHPD(X64Reg dest, OpArg arg){}

void XEmitter::MOVDDUP(X64Reg regOp, OpArg arg) 
{
}

//There are a few more left

// Also some integer instrucitons are missing
void XEmitter::PACKSSDW(X64Reg dest, OpArg arg) {}
void XEmitter::PACKSSWB(X64Reg dest, OpArg arg) {}
//void PACKUSDW(X64Reg dest, OpArg arg) {WriteSSEOp(64, 0x66, true, dest, arg);} // WRONG
void XEmitter::PACKUSWB(X64Reg dest, OpArg arg) {}

void XEmitter::PUNPCKLBW(X64Reg dest, const OpArg &arg){}
void XEmitter::PUNPCKLWD(X64Reg dest, const OpArg &arg) {}
void XEmitter::PUNPCKLDQ(X64Reg dest, const OpArg &arg) {}
//void PUNPCKLQDQ(X64Reg dest, OpArg arg) {WriteSSEOp(64, 0x60, true, dest, arg);}

void XEmitter::PSRLW(X64Reg reg, int shift) {

}

void XEmitter::PSRLD(X64Reg reg, int shift) {

}

void XEmitter::PSRLQ(X64Reg reg, int shift) {

}

void XEmitter::PSLLW(X64Reg reg, int shift) {
}

void XEmitter::PSLLD(X64Reg reg, int shift) {
}

void XEmitter::PSLLQ(X64Reg reg, int shift) {
}

// WARNING not REX compatible
void XEmitter::PSRAW(X64Reg reg, int shift) {

}

// WARNING not REX compatible
void XEmitter::PSRAD(X64Reg reg, int shift) {

}

void XEmitter::PSHUFB(X64Reg dest, OpArg arg) {
}

void XEmitter::PAND(X64Reg dest, OpArg arg)    {}
void XEmitter::PANDN(X64Reg dest, OpArg arg)    {}
void XEmitter::PXOR(X64Reg dest, OpArg arg)     {}
void XEmitter::POR(X64Reg dest, OpArg arg)     {}

void XEmitter::PADDB(X64Reg dest, OpArg arg)    {}
void XEmitter::PADDW(X64Reg dest, OpArg arg)    {}
void XEmitter::PADDD(X64Reg dest, OpArg arg)    {}
void XEmitter::PADDQ(X64Reg dest, OpArg arg)    {}

void XEmitter::PADDSB(X64Reg dest, OpArg arg)  {}
void XEmitter::PADDSW(X64Reg dest, OpArg arg)   {}
void XEmitter::PADDUSB(X64Reg dest, OpArg arg)  {}
void XEmitter::PADDUSW(X64Reg dest, OpArg arg) {}

void XEmitter::PSUBB(X64Reg dest, OpArg arg)    {}
void XEmitter::PSUBW(X64Reg dest, OpArg arg)    {}
void XEmitter::PSUBD(X64Reg dest, OpArg arg)    {}
void XEmitter::PSUBQ(X64Reg dest, OpArg arg)    {}

void XEmitter::PSUBSB(X64Reg dest, OpArg arg)   {}
void XEmitter::PSUBSW(X64Reg dest, OpArg arg)   {}
void XEmitter::PSUBUSB(X64Reg dest, OpArg arg)  {}
void XEmitter::PSUBUSW(X64Reg dest, OpArg arg)  {}

void XEmitter::PAVGB(X64Reg dest, OpArg arg)    {}
void XEmitter::PAVGW(X64Reg dest, OpArg arg)    {}

void XEmitter::PCMPEQB(X64Reg dest, OpArg arg)  {}
void XEmitter::PCMPEQW(X64Reg dest, OpArg arg)  {}
void XEmitter::PCMPEQD(X64Reg dest, OpArg arg)  {}

void XEmitter::PCMPGTB(X64Reg dest, OpArg arg)  {}
void XEmitter::PCMPGTW(X64Reg dest, OpArg arg)  {}
void XEmitter::PCMPGTD(X64Reg dest, OpArg arg)  {}

void XEmitter::PEXTRW(X64Reg dest, OpArg arg, u8 subreg)   {}
void XEmitter::PINSRW(X64Reg dest, OpArg arg, u8 subreg)   {}

void XEmitter::PMADDWD(X64Reg dest, OpArg arg) {}
void XEmitter::PSADBW(X64Reg dest, OpArg arg)  {}

void XEmitter::PMAXSW(X64Reg dest, OpArg arg)  {}
void XEmitter::PMAXUB(X64Reg dest, OpArg arg)   {}
void XEmitter::PMINSW(X64Reg dest, OpArg arg)   {}
void XEmitter::PMINUB(X64Reg dest, OpArg arg)   {}

void XEmitter::PMOVMSKB(X64Reg dest, OpArg arg)    {}

void XEmitter::PSHUFLW(X64Reg regOp, OpArg arg, u8 shuffle)   {}

// Prefixes

void XEmitter::LOCK()  {}
void XEmitter::REP()   {}
void XEmitter::REPNE() {}

void XEmitter::FWAIT()
{
}

void XEmitter::RTDSC() {}
	
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

#ifdef _M_X64

// See header
void XEmitter::___CallCdeclImport3(void* impptr, u32 arg0, u32 arg1, u32 arg2) {
}
void XEmitter::___CallCdeclImport4(void* impptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3) {
}
void XEmitter::___CallCdeclImport5(void* impptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4) {
}
void XEmitter::___CallCdeclImport6(void* impptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5) {
}

#endif

}
