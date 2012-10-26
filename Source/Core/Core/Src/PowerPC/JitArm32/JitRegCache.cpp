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

#include "JitRegCache.h"

ArmRegCache::ArmRegCache()
{
	emit = 0;
}

static u32 EmitRegs[ARMREGS];
void ArmRegCache::Init(ARMXEmitter *emitter)
{
	emit = emitter;
	ARMReg *PPCRegs = GetPPCAllocationOrder(NUMPPCREG);
	ARMReg *Regs = GetAllocationOrder(NUMARMREG);
	for(u8 a = 0; a < 32; ++a)
	{
		// This gives us the memory locations of the gpr registers so we can
		// load them.
		regs[a].location = (u8*)&PowerPC::ppcState.gpr[a]; 	
		regs[a].UsesLeft = 0;
	}
	for(u8 a = 0; a < NUMPPCREG; ++a)
	{
		ArmCRegs[a].PPCReg = -1;
		ArmCRegs[a].Reg = PPCRegs[a];
		ArmCRegs[a].free = true;
	}
	for(u8 a = 0; a < NUMARMREG; ++a)
	{
		ArmRegs[a].Reg = Regs[a];
		ArmRegs[a].free = true;
	}
	// Set to a invalid number first so it will load w/e
	// value that is in if it hasn't ever loaded
	for(u8 a = 0; a < ARMREGS; ++a)
		EmitRegs[a] = 33;
}
void ArmRegCache::Start(PPCAnalyst::BlockRegStats &stats)
{
	int numUsedRegs = 0;
	for(u8 a = 0; a < 32; ++a)
	{
		if (stats.GetTotalNumAccesses(a) > 0)
			numUsedRegs++;
	}
	int count = 0;
	int CurrentSetReg = 0;
	ARMReg *PPCRegs = GetPPCAllocationOrder(count);
	// We have less used registers this block than the max we can offer.
	// We can just allocate them all.
	// MOV and LDR them all
	
	for(u8 a = 0; a < 32 && CurrentSetReg < NUMPPCREG; ++a)
	{
		if (stats.GetTotalNumAccesses(a) > 0)
		{
			// Right, we use this one.
			// Set the host side stuff first so we can track usage
			// while compiling as well
							
		// Now on the emitted side, let's check to see if that
			// register is loaded in already in this register location
			ARMReg GPRReg = R12;

			ARMReg CacheLoc = R14;
			emit->ARMABI_MOVI2R(CacheLoc, (u32)&EmitRegs);

			
			emit->ARMABI_MOVI2R(R11, (u32)&PowerPC::ppcState.gpr);
			// Need to check if this register exists anywhere else, if it
			// does, flush it.
			emit->MOV(R10, 0);
			for(int b = 0; b < NUMPPCREG; ++b)
			{
				emit->LDR(GPRReg, CacheLoc, b * 4); // A is the PPC reg
				// Check if the PPC variable we are going to use is in
				// this cache location.
				emit->CMP(GPRReg, a);
				FixupBranch Next = emit->B_CC(CC_NEQ);
				// The PPC Register is in /THIS/ cache location
				// Check if it is in the location we are currently in.
				emit->CMP(R10, CurrentSetReg);
				FixupBranch NotSame = emit->B_CC(CC_EQ);
				// Not in the same location, dump it.
				emit->LSL(GPRReg, GPRReg, 2);
				emit->STR(R11, PPCRegs[b], GPRReg, true, true);
				emit->MOV(R12, 33);
				emit->STR(CacheLoc, R12, b * 4); 
				
				emit->SetJumpTarget(NotSame);
				emit->SetJumpTarget(Next);	
				emit->ADD(R10, R10, 1);
			}
			emit->LDR(GPRReg, CacheLoc, CurrentSetReg * 4); // A is the PPC reg
			emit->CMP(GPRReg, a); // Now compare it to the reg
			FixupBranch Equal = emit->B_CC(CC_EQ);
			// Will jump over this loading if it contains the register

			emit->CMP(GPRReg, 33);
			FixupBranch DontStore = emit->B_CC(CC_EQ);
			// Alright, so it doesn't contain the register we want.
			// First let's store the register that is in there
			
			// Shift the register left two to get the memory address
			// offset in the array
			emit->LSL(GPRReg, GPRReg, 2);

			emit->STR(R11, PPCRegs[CurrentSetReg], GPRReg, true, true);
			emit->SetJumpTarget(DontStore);
			// Alright, let's load the register from memory location
			emit->LDR(PPCRegs[CurrentSetReg], R11, a * 4);

			// Alright, let's set the cache number to the PPC number
			emit->MOV(GPRReg, a);
			emit->STR(CacheLoc, GPRReg, CurrentSetReg * 4);
			 
			emit->SetJumpTarget(Equal);
			ArmCRegs[CurrentSetReg].PPCReg = a;
			ArmCRegs[CurrentSetReg].Reg = PPCRegs[CurrentSetReg]; 
			ArmCRegs[CurrentSetReg].free = false;
			++CurrentSetReg;
		}
	}

	for(u8 a = 0; a < 32; ++a)
		regs[a].UsesLeft = stats.GetTotalNumAccesses(a);	
}
ARMReg *ArmRegCache::GetPPCAllocationOrder(int &count)
{
	// This will return us the allocation order of the registers we can use on
	// the ppc side.
	static ARMReg allocationOrder[] = 
	{
		R0, R1, R2, R3, R4, R5, R6, R7, R8, R9
	};
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}
ARMReg *ArmRegCache::GetAllocationOrder(int &count)
{
	// This will return us the allocation order of the registers we can use on
	// the host side.
	static ARMReg allocationOrder[] = 
	{
		R14, R12, R11, R10
	};
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}

ARMReg ArmRegCache::GetReg(bool AutoLock)
{
	for(u8 a = 0; a < NUMARMREG; ++a)
		if(ArmRegs[a].free)
		{
			// Alright, this one is free
			if (AutoLock)
				ArmRegs[a].free = false;
			return ArmRegs[a].Reg;
		}
	// Uh Oh, we have all them locked....
	_assert_msg_(_DYNA_REC_, false, "All available registers are locked dumb dumb");
}
void ArmRegCache::Lock(ARMReg Reg)
{
	for(u8 RegNum = 0; RegNum < NUMARMREG; ++RegNum)
		if(ArmRegs[RegNum].Reg == Reg)
		{
			_assert_msg_(_DYNA_REC, ArmRegs[RegNum].free, "This register is already locked");
			ArmRegs[RegNum].free = false;
		}
	_assert_msg_(_DYNA_REC, false, "Register %d can't be used with lock", Reg);
}
void ArmRegCache::Unlock(ARMReg R0, ARMReg R1, ARMReg R2, ARMReg R3)
{
	for(u8 RegNum = 0; RegNum < NUMARMREG; ++RegNum)
	{
		if(ArmRegs[RegNum].Reg == R0)
		{
			_assert_msg_(_DYNA_REC, !ArmRegs[RegNum].free, "This register is already unlocked");
			ArmRegs[RegNum].free = true;
		}
		if( R1 != INVALID_REG && ArmRegs[RegNum].Reg == R1) ArmRegs[RegNum].free = true;
		if( R2 != INVALID_REG && ArmRegs[RegNum].Reg == R2) ArmRegs[RegNum].free = true;
		if( R3 != INVALID_REG && ArmRegs[RegNum].Reg == R3) ArmRegs[RegNum].free = true;
	}
}

ARMReg ArmRegCache::R(int preg)
{
	for(u8 a = 0; a < NUMPPCREG; ++a){
		if (ArmCRegs[a].PPCReg == preg)
			return ArmCRegs[a].Reg;
	}
	_assert_msg_(_DYNA_REC, false, "Can't handle overflow yet! Tried loading PREG: %d", preg);
	exit(0);
}

void ArmRegCache::Flush()
{
	emit->MOVW(R14, (u32)&PowerPC::ppcState.gpr);
	emit->MOVT(R14, (u32)&PowerPC::ppcState.gpr, true);
	emit->ARMABI_MOVI2R(R12, (u32)&EmitRegs);
	
	for(u8 a = 0; a < NUMPPCREG; ++a)
	{
		if (!ArmCRegs[a].free)
		{
			emit->LDR(R11, R12, a * 4);
			emit->CMP(R11, 33);
			FixupBranch Invalid = emit->B_CC(CC_EQ);
			// Not in the same location, dump it.
			emit->STR(R14, ArmCRegs[a].Reg, a * 4);
			emit->SetJumpTarget(Invalid);
		}
	}
}

void ArmRegCache::FlushAndStore(int OldReg, int NewReg)
{
	emit->MOVW(R14, (u32)&PowerPC::ppcState.gpr);
	emit->MOVT(R14, (u32)&PowerPC::ppcState.gpr, true);
	emit->ARMABI_MOVI2R(R11, (u32)&EmitRegs);

	for (u8 a = 0; a < NUMPPCREG; ++a)
	{
		if(ArmCRegs[a].PPCReg == OldReg)
		{
			emit->STR(R14, ArmCRegs[a].Reg, ArmCRegs[a].PPCReg * 4);
			emit->LDR(ArmCRegs[a].Reg, R14, NewReg * 4);
			ArmCRegs[a].PPCReg = NewReg;

			// Alright, make sure to set the emit array to the current reg we
			// are switching too as well
			emit->MOV(R12, NewReg);
			emit->STR(R11, R12, a * 4);
			return;
		}
	}
}

void ArmRegCache::ReloadPPC()
{
	emit->MOVW(R14, (u32)&PowerPC::ppcState.gpr);
	emit->MOVT(R14, (u32)&PowerPC::ppcState.gpr, true);
	emit->ARMABI_MOVI2R(R12, (u32)&EmitRegs);
		
	for(u8 a = 0; a < NUMPPCREG; ++a)
	{
		if (!ArmCRegs[a].free)
		{
			emit->LDR(R11, R12, a * 4);
			emit->CMP(R11, 33);
			FixupBranch Invalid = emit->B_CC(CC_EQ);
			// Not in the same location, dump it.
			emit->LDR(ArmCRegs[a].Reg, R14, a * 4);	
			emit->SetJumpTarget(Invalid);
		}
	}
}
void ArmRegCache::Analyze(UGeckoInstruction inst)
{
	// This function basically checks the instruction provided to see if it uses a register that we
	// don't have in the register cache yet, and if it isn't in it, it will flush a register that we
	// won't be using, and then load in a new one that the next instruction will be using. Hopefully
	// in the future we can make this more efficient to use a down count to flush registers that
	// don't have anymore uses at all in them. But for now, we will just flush a register that we
	// won't be using.
	GekkoOPInfo *Info = GetOpInfo(inst);
	int flags = Info->flags;
	s8 Regs[3];
	Regs[0] = Regs[1] = Regs[2] = -1;
	u8 CurrentReg = 0;

	if ((flags & FL_IN_A) || ((flags & FL_IN_A0) && inst.RA != 0))
		Regs[CurrentReg++] = inst.RA; 
	if(flags & FL_IN_B)
		Regs[CurrentReg++] = inst.RB;	
	if(flags & FL_IN_C)
		Regs[CurrentReg++] = inst.RC;
	if(flags & FL_IN_S)
		Regs[CurrentReg++] = inst.RS;
	if(flags & FL_OUT_D) 
		Regs[CurrentReg++] = inst.RD;
	if(flags & FL_OUT_A)
		Regs[CurrentReg++] = inst.RA;
	// We can only have three registers per instruction max
	_assert_msg_(_DYNA_REC, !(CurrentReg > 3), "Somehow got more registers analyzed then we can have");
	// let's check to make sure all of these are loaded.
	if (CurrentReg == 0) // No registers this instruction
		return;
	u8 LoadedReg = 0;
	s8 URegs[3];
	
	for (u8 b = 0; b < 3; ++b){
		URegs[b] = -1;
		if (Regs[b] != -1)
			for( u8 a = 0; a < NUMPPCREG; ++a){
				if (ArmCRegs[a].PPCReg == Regs[b]) // Alright, it is already loaded
				{
					URegs[b] = ArmCRegs[a].PPCReg;
					LoadedReg++;
					goto NextOne;
				}
			}
		else
			URegs[b] = -2;
		NextOne:
		;
	}
	if (LoadedReg == CurrentReg) // Oh, we already have all these ones cache
		return;

	// Check to make sure we aren't trying to load multiple of the same ones
	if (URegs[0] > 0){
		if (URegs[0] == URegs[1]) // 0 is 1
			URegs[1] = -2; 
		if (URegs[0] == URegs[2]) // 0 is 2
			URegs[2] = -2;
	}
	if (URegs[1] > 0) {
		if(URegs[1] == URegs[2])
			URegs[2] = -2;
	}
	for (u8 b = 0; b < 3; ++b)
	{
		if (URegs[b] == -1) // We don't have this one loaded
		{
			// We want to make sure not to unload the other two if they are already loaded.
			switch(b){
				case 0: //check for 1 and 2
				for (u8 a = 0; a < NUMPPCREG; ++a)
					if(ArmCRegs[a].PPCReg != URegs[1] && ArmCRegs[a].PPCReg != URegs[2])
					{
						// Alright, w/e PPC reg this is, it doesn't contain our new registers
						// First we've got to flush the old one, and then load the new one.
						FlushAndStore(ArmCRegs[a].PPCReg, Regs[b]);
						break;
					}
					break;
				case 1: // check for 0 and 2
				for (u8 a = 0; a < NUMPPCREG; ++a)
					if(ArmCRegs[a].PPCReg != URegs[0] && ArmCRegs[a].PPCReg != URegs[2])
					{
						// Alright, w/e PPC reg this is, it doesn't contain our new registers
						// First we've got to flush the old one, and then load the new one.
						FlushAndStore(ArmCRegs[a].PPCReg, Regs[b]);
						break;
					}
					break;
				case 2: // check for 0 and 1
				for (u8 a = 0; a < NUMPPCREG; ++a)
					if(ArmCRegs[a].PPCReg != URegs[0] && ArmCRegs[a].PPCReg != URegs[1])
					{
						// Alright, w/e PPC reg this is, it doesn't contain our new registers
						// First we've got to flush the old one, and then load the new one.
						FlushAndStore(ArmCRegs[a].PPCReg, Regs[b]);
						break;
					}
					break;
			}
			URegs[b] = Regs[b];
		}
	}
	// Should be all loaded at this point
}
