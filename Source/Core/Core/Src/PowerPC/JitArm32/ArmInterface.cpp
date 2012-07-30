#include "ArmInterface.h"

namespace ArmInterface
{
	// Memory functions
	u32 Read_Opcode_JIT_Uncached(const u32 _Address)
	{
		u8* iCache;
		u32 addr;
		if (_Address & JIT_ICACHE_VMEM_BIT)
		{
			iCache = jitarm->GetBlockCache()->GetICacheVMEM();
			addr = _Address & JIT_ICACHE_MASK;
		}
		else if (_Address & JIT_ICACHE_EXRAM_BIT)
		{
			iCache = jitarm->GetBlockCache()->GetICacheEx();
			addr = _Address & JIT_ICACHEEX_MASK;
		}
		else
		{
			iCache = jitarm->GetBlockCache()->GetICache();
			addr = _Address & JIT_ICACHE_MASK;
		}
		u32 inst = *(u32*)(iCache + addr);
		if (inst == JIT_ICACHE_INVALID_WORD)
		{
			u32 cache_block_start = addr & ~0x1f;
			u32 mem_block_start = _Address & ~0x1f;
			u8 *pMem = Memory::GetPointer(mem_block_start);
			memcpy(iCache + cache_block_start, pMem, 32);
			inst = *(u32*)(iCache + addr);
		}
		inst = Common::swap32(inst);

		if ((inst & 0xfc000000) == 0)
		{
			inst = jitarm->GetBlockCache()->GetOriginalFirstOp(inst);
		}

		return inst;
	}

	u32 Read_Opcode_JIT(u32 _Address)
	{
	#ifdef FAST_ICACHE	
		if (bMMU && !bFakeVMEM && (_Address & Memory::ADDR_MASK_MEM1))
		{
			_Address = Memory::TranslateAddress(_Address, Memory::FLAG_OPCODE);
			if (_Address == 0)
			{
				return 0;
			}
		}
		u32 inst = 0;

		// Bypass the icache for the external interrupt exception handler
		if ( (_Address & 0x0FFFFF00) == 0x00000500 )
			inst = Read_Opcode_JIT_Uncached(_Address);
		else
			inst = PowerPC::ppcState.iCache.ReadInstruction(_Address);
	#else
		u32 inst = Memory::ReadUnchecked_U32(_Address);
	#endif
		return inst;
	}

	// The following function is deprecated in favour of FAST_ICACHE
	u32 Read_Opcode_JIT_LC(const u32 _Address)
	{
	#ifdef JIT_UNLIMITED_ICACHE	
		if ((_Address & ~JIT_ICACHE_MASK) != 0x80000000 && (_Address & ~JIT_ICACHE_MASK) != 0x00000000 &&
			(_Address & ~JIT_ICACHE_MASK) != 0x7e000000 && // TLB area
			(_Address & ~JIT_ICACHEEX_MASK) != 0x90000000 && (_Address & ~JIT_ICACHEEX_MASK) != 0x10000000)
		{
			PanicAlertT("iCacheJIT: Reading Opcode from %x. Please report.", _Address);
			ERROR_LOG(MEMMAP, "iCacheJIT: Reading Opcode from %x. Please report.", _Address);
			return 0;
		}
		u8* iCache;
		u32 addr;
		if (_Address & JIT_ICACHE_VMEM_BIT)
		{		
			iCache = jitarm->GetBlockCache()->GetICacheVMEM();
			addr = _Address & JIT_ICACHE_MASK;
		}
		else if (_Address & JIT_ICACHE_EXRAM_BIT)
		{		
			iCache = jitarm->GetBlockCache()->GetICacheEx();
			addr = _Address & JIT_ICACHEEX_MASK;
		}
		else
		{
			iCache = jitarm->GetBlockCache()->GetICache();
			addr = _Address & JIT_ICACHE_MASK;
		}
		u32 inst = *(u32*)(iCache + addr);
		if (inst == JIT_ICACHE_INVALID_WORD)
			inst = Memory::ReadUnchecked_U32(_Address);
		else
			inst = Common::swap32(inst);
	#else
		u32 inst = Memory::ReadUnchecked_U32(_Address);
	#endif
		if ((inst & 0xfc000000) == 0)
		{
			inst = jitarm->GetBlockCache()->GetOriginalFirstOp(inst);
		}	
		return inst;
	}

	// WARNING! No checks!
	// We assume that _Address is cached
	void Write_Opcode_JIT(const u32 _Address, const u32 _Value)
	{
	#ifdef JIT_UNLIMITED_ICACHE
		if (_Address & JIT_ICACHE_VMEM_BIT)
		{
			*(u32*)(jitarm->GetBlockCache()->GetICacheVMEM() + (_Address & JIT_ICACHE_MASK)) = Common::swap32(_Value);		
		}
		else if (_Address & JIT_ICACHE_EXRAM_BIT)
		{
			*(u32*)(jitarm->GetBlockCache()->GetICacheEx() + (_Address & JIT_ICACHEEX_MASK)) = Common::swap32(_Value);		
		}
		else
			*(u32*)(jitarm->GetBlockCache()->GetICache() + (_Address & JIT_ICACHE_MASK)) = Common::swap32(_Value);
	#else
		Memory::WriteUnchecked_U32(_Value, _Address);
	#endif	
	}

}

