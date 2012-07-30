#ifndef ARM_INTERFACE_H
#define ARM_INTERFACE_H
#include "Jit.h"
#include "../JitInterface.h"
#include "HW/Memmap.h"

namespace ArmInterface
{
	// Memory functions
	u32 Read_Opcode_JIT_Uncached(const u32 _Address);

	u32 Read_Opcode_JIT(u32 _Address);
	// The following function is deprecated in favour of FAST_ICACHE
	u32 Read_Opcode_JIT_LC(const u32 _Address);

	// WARNING! No checks!
	// We assume that _Address is cached
	void Write_Opcode_JIT(const u32 _Address, const u32 _Value);

}

#endif
