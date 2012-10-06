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
	ARMReg *PPCRegs = GetPPCAllocationOrder(count);
	int CurrentSetReg = 0;
	// Reset our registers
	for (u8 a = 0; a < NUMPPCREG; ++a)
	{
		ArmCRegs[a].PPCReg = -1;
		ArmCRegs[a].free = true;
	}
	// Alright, load in the GPR location
	emit->MOVW(R14, (u32)&PowerPC::ppcState.gpr); // Load in our location
	emit->MOVT(R14, (u32)&PowerPC::ppcState.gpr, true);
		// We have less used registers this block than the max we can offer.
		// We can just allocate them all.
		// MOV and LDR them all
		for(u8 a = 0; a < 32 && CurrentSetReg < NUMPPCREG; ++a)
		{
			if (stats.GetTotalNumAccesses(a) > 0)
			{
				// Right, we use this one.
				ArmCRegs[CurrentSetReg].PPCReg = a;
				ArmCRegs[CurrentSetReg].Reg = PPCRegs[CurrentSetReg]; 
				ArmCRegs[CurrentSetReg].free = false;
				// Let's load up that register
				ARMReg tReg = ArmCRegs[CurrentSetReg].Reg;
				emit->LDR(tReg, R14, a * 4); // Load the values
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
	
	for(u8 a = 0; a < NUMPPCREG; ++a)
	{
		if(!ArmCRegs[a].free)
		{
			emit->STR(R14, ArmCRegs[a].Reg, ArmCRegs[a].PPCReg * 4); 
		}
	}
}

void ArmRegCache::FlushAndStore(int OldReg, int NewReg)
{
	emit->MOVW(R14, (u32)&PowerPC::ppcState.gpr);
	emit->MOVT(R14, (u32)&PowerPC::ppcState.gpr, true);
	for (u8 a = 0; a < NUMPPCREG; ++a)
	{
		if(ArmCRegs[a].PPCReg == OldReg)
		{
			emit->STR(R14, ArmCRegs[a].Reg, ArmCRegs[a].PPCReg * 4);
			emit->LDR(ArmCRegs[a].Reg, R14, NewReg * 4);
			ArmCRegs[a].PPCReg = NewReg;
			return;
		}
	}
}

void ArmRegCache::ReloadPPC()
{
	emit->MOVW(R14, (u32)&PowerPC::ppcState.gpr);
	emit->MOVT(R14, (u32)&PowerPC::ppcState.gpr, true);
	for(u8 a = 0; a < NUMPPCREG; ++a)
	{
		if(!ArmCRegs[a].free)
		{
			emit->LDR(ArmCRegs[a].Reg, R14, ArmCRegs[a].PPCReg * 4); 
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
