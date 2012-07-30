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

#ifndef _JITARMREGCACHE_H
#define _JITARMREGCACHE_H

#include "ArmEmitter.h"
#include "ArmABI.h"

using namespace ArmGen;
struct PPCCachedReg
{
	const u32 *location;
};

class ArmRegCache
{
private:
	const u32 *StartLocation;
	PPCCachedReg regs[32];

protected:
	
	ARMXEmitter *emit;
	
public:

	ArmRegCache();

	~ArmRegCache() {}
	void Start();

	void SetEmitter(ARMXEmitter *emitter) {emit = emitter;}

	void Flush();
	const u32* R(int preg) const {return regs[preg].location;}
};




#endif
