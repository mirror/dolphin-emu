// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"
#include "VideoCommon.h"
#include "VideoConfig.h"
#include "MathUtil.h"
#include "Thread.h"
#include "Atomic.h"
#include "OpcodeDecoding.h"
#include "Fifo.h"
#include "ChunkFile.h"
#include "CommandProcessor.h"
#include "PixelEngine.h"
#include "CoreTiming.h"
#include "ConfigManager.h"
#include "HW/ProcessorInterface.h"
#include "HW/GPFifo.h"
#include "HW/Memmap.h"
#include "DLCache.h"
#include "HW/SystemTimers.h"
#include "Core.h"

namespace CommandProcessor
{

int et_UpdateInterrupts;

// TODO(ector): Warn on bbox read/write

// STATE_TO_SAVE
// Note that gpuFifo == &cpuFifo except when bSyncGPUAtIdleOnly is on.
SCPFifoStruct cpuFifo, _gpuFifo;
SCPFifoStruct *gpuFifo;
UCPStatusReg m_CPStatusReg;
UCPCtrlReg	m_CPCtrlReg;
UCPClearReg	m_CPClearReg;

int m_bboxleft;
int m_bboxtop;
int m_bboxright;
int m_bboxbottom;
u16 m_tokenReg;

volatile bool isPossibleWaitingSetDrawDone = false;
volatile bool isHiWatermarkActive = false;
volatile bool isLoWatermarkActive = false;
volatile bool interruptSet= false;
volatile bool interruptWaiting= false;
volatile bool interruptTokenWaiting = false;
u32 interruptTokenData;
volatile bool interruptFinishWaiting = false;
volatile u32 gpuBusy = 0;

volatile u32 VITicks = CommandProcessor::m_cpClockOrigin;

bool IsOnThread()
{
	return SConfig::GetInstance().m_LocalCoreStartupParameter.bCPUThread;
}

void UpdateInterrupts_Wrapper(u64 userdata, int cyclesLate)
{
	UpdateInterrupts(userdata);
}

void DoState(PointerWrap &p)
{
	p.DoPOD(m_CPStatusReg);
	p.DoPOD(m_CPCtrlReg);
	p.DoPOD(m_CPClearReg);
	p.Do(m_bboxleft);
	p.Do(m_bboxtop);
	p.Do(m_bboxright);
	p.Do(m_bboxbottom);
	p.Do(m_tokenReg);
	p.Do(cpuFifo);

	p.Do(isHiWatermarkActive);
	p.Do(isLoWatermarkActive);
	p.Do(isPossibleWaitingSetDrawDone);
	p.Do(interruptSet);
	p.Do(interruptWaiting);
	p.Do(interruptTokenWaiting);
	p.Do(interruptFinishWaiting);
}

inline void WriteLow (volatile u32& _reg, u16 lowbits)  {Common::AtomicStore(_reg,(_reg & 0xFFFF0000) | lowbits);}
inline void WriteHigh(volatile u32& _reg, u16 highbits) {Common::AtomicStore(_reg,(_reg & 0x0000FFFF) | ((u32)highbits << 16));}

inline u16 ReadLow  (u32 _reg)  {return (u16)(_reg & 0xFFFF);}
inline u16 ReadHigh (u32 _reg)  {return (u16)(_reg >> 16);}

void Init()
{
	m_CPStatusReg.Hex = 0;
	m_CPStatusReg.CommandIdle = 1;
	m_CPStatusReg.ReadIdle = 1;

	m_CPCtrlReg.Hex = 0;

	m_CPClearReg.Hex = 0;

	m_bboxleft = 0;
	m_bboxtop  = 0;
	m_bboxright = 640;
	m_bboxbottom = 480;

	m_tokenReg = 0;
	
	memset(&cpuFifo,0,sizeof(cpuFifo));
	cpuFifo.bFF_Breakpoint = 0;
	cpuFifo.bFF_HiWatermark = 0;
	cpuFifo.bFF_HiWatermarkInt = 0;
	cpuFifo.bFF_LoWatermark = 0;
	cpuFifo.bFF_LoWatermarkInt = 0;


	if (Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
	{
		_gpuFifo = cpuFifo;
		gpuFifo = &_gpuFifo;
	}
	else
	{
		gpuFifo = &cpuFifo;
	}

	interruptSet = false;
	interruptWaiting = false;
	interruptFinishWaiting = false;
	interruptTokenWaiting = false;

	isPossibleWaitingSetDrawDone = false;
	isHiWatermarkActive = false;
	isLoWatermarkActive = false;

	et_UpdateInterrupts = CoreTiming::RegisterEvent("CPInterrupt", UpdateInterrupts_Wrapper);
}

static void SyncGPU() {
	if (IsOnThread())
	{
		while (Common::AtomicLoad(CommandProcessor::gpuBusy))
			Common::YieldCPU();
	}
	if (Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
	{
		// need a barrier here for ARM
		if (interruptTokenWaiting)
		{
			PixelEngine::SetToken_OnMainThread(interruptTokenData, 0);
		}
		if (interruptFinishWaiting)
		{
			PixelEngine::SetFinish_OnMainThread(0, 0);
		}
		cpuFifo.CPReadPointer = _gpuFifo.CPReadPointer;
		cpuFifo.CPReadWriteDistance = _gpuFifo.CPReadWriteDistance;
		_gpuFifo = cpuFifo;
		// need another barrier here
		if (interruptWaiting) {
			// normally, UpdateInterruptsFromVideoBackend does this
			UpdateInterrupts(1);
		}
	}
}

void SyncGPUIfIdleOnly() {
	if (Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
	{
		SyncGPU();
	}
}

static u32 GetFakeRwDistance()
{
	u32 prev;
	if (cpuFifo.CPWritePointer >= cpuFifo.CPReadPointer)
		prev = cpuFifo.CPWritePointer - cpuFifo.CPReadPointer;
	else
		// why 32?
		prev = cpuFifo.CPEnd - cpuFifo.CPReadPointer + cpuFifo.CPWritePointer - cpuFifo.CPBase + 32;
	return std::min(prev, (u32) 16);
}

static u32 GetFakeReadPointer()
{
	u32 result = cpuFifo.CPWritePointer - GetFakeRwDistance();
	if (result < cpuFifo.CPBase)
		result += cpuFifo.CPEnd - cpuFifo.CPBase;
	return 0;
}

void Read16(u16& _rReturnValue, const u32 _Address)
{
	INFO_LOG(COMMANDPROCESSOR, "(r): 0x%08x", _Address);
	switch (_Address & 0xFFF)
	{
	case STATUS_REGISTER:
		SetCpStatusRegister();
		_rReturnValue = m_CPStatusReg.Hex;
		return;
	case CTRL_REGISTER:		_rReturnValue = m_CPCtrlReg.Hex; return;
	case CLEAR_REGISTER:
		_rReturnValue = m_CPClearReg.Hex;
		PanicAlert("CommandProcessor:: CPU reads from CLEAR_REGISTER!");
		ERROR_LOG(COMMANDPROCESSOR, "(r) clear: 0x%04x", _rReturnValue);
		return;
	case FIFO_TOKEN_REGISTER:		_rReturnValue = m_tokenReg; return;
	case FIFO_BOUNDING_BOX_LEFT:	_rReturnValue = m_bboxleft; return;
	case FIFO_BOUNDING_BOX_RIGHT:	_rReturnValue = m_bboxright; return;
	case FIFO_BOUNDING_BOX_TOP:		_rReturnValue = m_bboxtop; return;
	case FIFO_BOUNDING_BOX_BOTTOM:	_rReturnValue = m_bboxbottom; return;

	case FIFO_BASE_LO:			_rReturnValue = ReadLow (cpuFifo.CPBase); return;
	case FIFO_BASE_HI:			_rReturnValue = ReadHigh(cpuFifo.CPBase); return;
	case FIFO_END_LO:			_rReturnValue = ReadLow (cpuFifo.CPEnd);  return;
	case FIFO_END_HI:			_rReturnValue = ReadHigh(cpuFifo.CPEnd);  return;
	case FIFO_HI_WATERMARK_LO:	_rReturnValue = ReadLow (cpuFifo.CPHiWatermark); return;
	case FIFO_HI_WATERMARK_HI:	_rReturnValue = ReadHigh(cpuFifo.CPHiWatermark); return;
	case FIFO_LO_WATERMARK_LO:	_rReturnValue = ReadLow (cpuFifo.CPLoWatermark); return;
	case FIFO_LO_WATERMARK_HI:	_rReturnValue = ReadHigh(cpuFifo.CPLoWatermark); return;

	case FIFO_RW_DISTANCE_LO:
		if (IsOnThread())
		{
			if (Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
			{
				_rReturnValue = ReadLow (GetFakeRwDistance());
			}
			else
			{
				if(cpuFifo.CPWritePointer >= cpuFifo.SafeCPReadPointer)
					_rReturnValue = ReadLow (cpuFifo.CPWritePointer - cpuFifo.SafeCPReadPointer);
				else
					_rReturnValue = ReadLow (cpuFifo.CPEnd - cpuFifo.SafeCPReadPointer + cpuFifo.CPWritePointer - cpuFifo.CPBase + 32);
			}
		}
		else
		{
			_rReturnValue = ReadLow (cpuFifo.CPReadWriteDistance);
		}
		DEBUG_LOG(COMMANDPROCESSOR, "Read FIFO_RW_DISTANCE_LO : %04x", _rReturnValue);
		return;
	case FIFO_RW_DISTANCE_HI:
		if (IsOnThread())
		{
			if (Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
			{
				_rReturnValue = ReadHigh (GetFakeRwDistance());
			}
			else
			{
				if(cpuFifo.CPWritePointer >= cpuFifo.SafeCPReadPointer)
					_rReturnValue = ReadHigh (cpuFifo.CPWritePointer - cpuFifo.SafeCPReadPointer);
				else
					_rReturnValue = ReadHigh (cpuFifo.CPEnd - cpuFifo.SafeCPReadPointer + cpuFifo.CPWritePointer - cpuFifo.CPBase + 32);
			}
		}
		else
		{
			_rReturnValue = ReadHigh(cpuFifo.CPReadWriteDistance);
		}
		DEBUG_LOG(COMMANDPROCESSOR, "Read FIFO_RW_DISTANCE_HI : %04x", _rReturnValue);
		return;
	case FIFO_WRITE_POINTER_LO:
		_rReturnValue = ReadLow (cpuFifo.CPWritePointer);
		DEBUG_LOG(COMMANDPROCESSOR, "Read FIFO_WRITE_POINTER_LO : %04x", _rReturnValue);
		return;
	case FIFO_WRITE_POINTER_HI:
		_rReturnValue = ReadHigh(cpuFifo.CPWritePointer);
		DEBUG_LOG(COMMANDPROCESSOR, "Read FIFO_WRITE_POINTER_HI : %04x", _rReturnValue);
		return;
	case FIFO_READ_POINTER_LO:
		if (IsOnThread())
		{
			if (Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
				_rReturnValue = ReadLow (GetFakeReadPointer());
			else
				_rReturnValue = ReadLow (cpuFifo.SafeCPReadPointer);
		}
		else
			_rReturnValue = ReadLow (cpuFifo.CPReadPointer);
		DEBUG_LOG(COMMANDPROCESSOR, "Read FIFO_READ_POINTER_LO : %04x", _rReturnValue);
		return;
	case FIFO_READ_POINTER_HI:
		if (IsOnThread())
		{
			if (Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
				_rReturnValue = ReadHigh (GetFakeReadPointer());
			else
				_rReturnValue = ReadHigh (cpuFifo.SafeCPReadPointer);
		}
		else
		{
			_rReturnValue = ReadHigh (cpuFifo.CPReadPointer);
		}
		DEBUG_LOG(COMMANDPROCESSOR, "Read FIFO_READ_POINTER_HI : %04x", _rReturnValue);
		return;

	case FIFO_BP_LO: _rReturnValue = ReadLow (cpuFifo.CPBreakpoint); return;
	case FIFO_BP_HI: _rReturnValue = ReadHigh(cpuFifo.CPBreakpoint); return;

	case XF_RASBUSY_L:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from XF_RASBUSY_L: %04x", _rReturnValue);
		return;
	case XF_RASBUSY_H:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from XF_RASBUSY_H: %04x", _rReturnValue);
		return;

	case XF_CLKS_L:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from XF_CLKS_L: %04x", _rReturnValue);
		return;
	case XF_CLKS_H:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from XF_CLKS_H: %04x", _rReturnValue);
		return;

	case XF_WAIT_IN_L:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from XF_WAIT_IN_L: %04x", _rReturnValue);
		return;
	case XF_WAIT_IN_H:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from XF_WAIT_IN_H: %04x", _rReturnValue);
		return;

	case XF_WAIT_OUT_L:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from XF_WAIT_OUT_L: %04x", _rReturnValue);
		return;
	case XF_WAIT_OUT_H:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from XF_WAIT_OUT_H: %04x", _rReturnValue);
		return;

	case VCACHE_METRIC_CHECK_L:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from VCACHE_METRIC_CHECK_L: %04x", _rReturnValue);
		return;
	case VCACHE_METRIC_CHECK_H:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from VCACHE_METRIC_CHECK_H: %04x", _rReturnValue);
		return;

	case VCACHE_METRIC_MISS_L:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from VCACHE_METRIC_MISS_L: %04x", _rReturnValue);
		return;
	case VCACHE_METRIC_MISS_H:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from VCACHE_METRIC_MISS_H: %04x", _rReturnValue);
		return;

	case VCACHE_METRIC_STALL_L:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from VCACHE_METRIC_STALL_L: %04x", _rReturnValue);
		return;
	case VCACHE_METRIC_STALL_H:
		_rReturnValue = 0;	// TODO: Figure out the true value
		DEBUG_LOG(COMMANDPROCESSOR, "Read from VCACHE_METRIC_STALL_H: %04x", _rReturnValue);
		return;

	case CLKS_PER_VTX_OUT:
		_rReturnValue = 4; //Number of clocks per vertex.. TODO: Calculate properly
		DEBUG_LOG(COMMANDPROCESSOR, "Read from CLKS_PER_VTX_OUT: %04x", _rReturnValue);
		return;
	default:
		_rReturnValue = 0;
		WARN_LOG(COMMANDPROCESSOR, "(r16) unknown CP reg @ %08x", _Address);
		return;
	}

	return;
}

void Write16(const u16 _Value, const u32 _Address)
{
	INFO_LOG(COMMANDPROCESSOR, "(write16): 0x%04x @ 0x%08x",_Value,_Address);

	switch (_Address & 0xFFF)
	{
	case STATUS_REGISTER:
		{
			// This should be Read-Only
			ERROR_LOG(COMMANDPROCESSOR,"\t write to STATUS_REGISTER : %04x", _Value);
			PanicAlert("CommandProcessor:: CPU writes to STATUS_REGISTER!");
		}
		break;

	case CTRL_REGISTER:
		{
			UCPCtrlReg tmpCtrl(_Value);
			m_CPCtrlReg.Hex = tmpCtrl.Hex;
			INFO_LOG(COMMANDPROCESSOR,"\t Write to CTRL_REGISTER : %04x", _Value);
			SetCpControlRegister();
		}
		break;

	case CLEAR_REGISTER:
		{
			UCPClearReg tmpCtrl(_Value);
			m_CPClearReg.Hex = tmpCtrl.Hex;
			DEBUG_LOG(COMMANDPROCESSOR,"\t Write to CLEAR_REGISTER : %04x", _Value);
			SetCpClearRegister();
		}
		break;

	case PERF_SELECT:
		// Seems to select which set of perf registers should be exposed.
		DEBUG_LOG(COMMANDPROCESSOR, "Write to PERF_SELECT: %04x", _Value);
		break;

	// Fifo Registers
	case FIFO_TOKEN_REGISTER:
		m_tokenReg = _Value;
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_TOKEN_REGISTER : %04x", _Value);
		break;
	case FIFO_BASE_LO:
		WriteLow ((u32 &)cpuFifo.CPBase, _Value & 0xFFE0);
		SyncGPUIfIdleOnly();
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_BASE_LO : %04x", _Value);
		break;
	case FIFO_BASE_HI:
		WriteHigh((u32 &)cpuFifo.CPBase, _Value);
		SyncGPUIfIdleOnly();
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_BASE_HI : %04x", _Value);
		break;

	case FIFO_END_LO:
		WriteLow ((u32 &)cpuFifo.CPEnd,  _Value & 0xFFE0);
		SyncGPUIfIdleOnly();
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_END_LO : %04x", _Value);
		break;
	case FIFO_END_HI:
		WriteHigh((u32 &)cpuFifo.CPEnd,  _Value);
		SyncGPUIfIdleOnly();
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_END_HI : %04x", _Value);
		break;

	case FIFO_WRITE_POINTER_LO:
		WriteLow ((u32 &)cpuFifo.CPWritePointer, _Value & 0xFFE0);
		SyncGPUIfIdleOnly();
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_WRITE_POINTER_LO : %04x", _Value);
		break;
	case FIFO_WRITE_POINTER_HI:
		WriteHigh((u32 &)cpuFifo.CPWritePointer, _Value);
		SyncGPUIfIdleOnly();
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_WRITE_POINTER_HI : %04x", _Value);
		break;

	case FIFO_READ_POINTER_LO:
		SyncGPUIfIdleOnly();
		WriteLow ((u32 &)cpuFifo.CPReadPointer, _Value & 0xFFE0);
		gpuFifo->CPReadPointer = cpuFifo.CPReadPointer;
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_READ_POINTER_LO : %04x", _Value);
		break;
	case FIFO_READ_POINTER_HI:
		SyncGPUIfIdleOnly();
		WriteHigh((u32 &)cpuFifo.CPReadPointer, _Value);
		cpuFifo.SafeCPReadPointer = cpuFifo.CPReadPointer;
		gpuFifo->CPReadPointer = cpuFifo.CPReadPointer;
		gpuFifo->SafeCPReadPointer = cpuFifo.SafeCPReadPointer;
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_READ_POINTER_HI : %04x", _Value);
		break;

	case FIFO_HI_WATERMARK_LO:
		WriteLow ((u32 &)cpuFifo.CPHiWatermark, _Value);
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_HI_WATERMARK_LO : %04x", _Value);
		break;
	case FIFO_HI_WATERMARK_HI:
		WriteHigh((u32 &)cpuFifo.CPHiWatermark, _Value);
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_HI_WATERMARK_HI : %04x", _Value);
		break;

	case FIFO_LO_WATERMARK_LO:
		WriteLow ((u32 &)cpuFifo.CPLoWatermark, _Value);
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_LO_WATERMARK_LO : %04x", _Value);
		break;
	case FIFO_LO_WATERMARK_HI:
		WriteHigh((u32 &)cpuFifo.CPLoWatermark, _Value);
		DEBUG_LOG(COMMANDPROCESSOR,"\t Write to FIFO_LO_WATERMARK_HI : %04x", _Value);
		break;

	case FIFO_BP_LO:
		WriteLow ((u32 &)cpuFifo.CPBreakpoint, _Value & 0xFFE0);
		SyncGPUIfIdleOnly();
		DEBUG_LOG(COMMANDPROCESSOR,"Write to FIFO_BP_LO : %04x", _Value);
		break;
	case FIFO_BP_HI:
		WriteHigh((u32 &)cpuFifo.CPBreakpoint, _Value);
		SyncGPUIfIdleOnly();
		DEBUG_LOG(COMMANDPROCESSOR,"Write to FIFO_BP_HI : %04x", _Value);
		break;

	case FIFO_RW_DISTANCE_HI:
		WriteHigh((u32 &)cpuFifo.CPReadWriteDistance, _Value);
		SyncGPU();
		if (cpuFifo.CPReadWriteDistance == 0)
		{
			GPFifo::ResetGatherPipe();
			ResetVideoBuffer();
		}
		else
		{
			ResetVideoBuffer();
		}
		IncrementCheckContextId();
		DEBUG_LOG(COMMANDPROCESSOR,"Try to write to FIFO_RW_DISTANCE_HI : %04x", _Value);
		break;
	case FIFO_RW_DISTANCE_LO:
		WriteLow((u32 &)cpuFifo.CPReadWriteDistance, _Value & 0xFFE0);
		DEBUG_LOG(COMMANDPROCESSOR,"Try to write to FIFO_RW_DISTANCE_LO : %04x", _Value);
		break;

	default:
		WARN_LOG(COMMANDPROCESSOR, "(w16) unknown CP reg write %04x @ %08x", _Value, _Address);
	}

	if (!IsOnThread())
		RunGpu();
}

void Read32(u32& _rReturnValue, const u32 _Address)
{
	_rReturnValue = 0;
	_dbg_assert_msg_(COMMANDPROCESSOR, 0, "Read32 from CommandProccessor at 0x%08x", _Address);
}

void Write32(const u32 _Data, const u32 _Address)
{
	_dbg_assert_msg_(COMMANDPROCESSOR, 0, "Write32 at CommandProccessor at 0x%08x", _Address);
}

void STACKALIGN GatherPipeBursted()
{
	ProcessFifoEvents();
	// if we aren't linked, we don't care about gather pipe data
	if (!m_CPCtrlReg.GPLinkEnable)
	{
		if (!IsOnThread())
		{
			RunGpu();
		}
		else
		{
			// In multibuffer mode is not allowed write in the same FIFO attached to the GPU.
			// Fix Pokemon XD in DC mode.
			if((ProcessorInterface::Fifo_CPUEnd == cpuFifo.CPEnd) && (ProcessorInterface::Fifo_CPUBase == cpuFifo.CPBase)
				 && gpuFifo->CPReadWriteDistance > 0)
			{
				SyncGPU();
			}
		}
		return;
	}

	if (IsOnThread())
		SetCpStatus(true);

	// update the fifo pointer
	if (cpuFifo.CPWritePointer >= cpuFifo.CPEnd)
		cpuFifo.CPWritePointer = cpuFifo.CPBase;
	else
		cpuFifo.CPWritePointer += GATHER_PIPE_SIZE;

	Common::AtomicAdd(gpuFifo->CPReadWriteDistance, GATHER_PIPE_SIZE);
	if (Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
	{
		cpuFifo.CPReadWriteDistance += GATHER_PIPE_SIZE;
	}

	if (!IsOnThread())
		RunGpu();

	if (cpuFifo.CPWritePointer == cpuFifo.CPReadPointer)
	{
		// Overflow; block until the GPU thread is ready.
		SyncGPU();
	}

	// check if we are in sync
	_assert_msg_(COMMANDPROCESSOR, cpuFifo.CPWritePointer	== ProcessorInterface::Fifo_CPUWritePointer, "FIFOs linked but out of sync");
	_assert_msg_(COMMANDPROCESSOR, cpuFifo.CPBase			== ProcessorInterface::Fifo_CPUBase, "FIFOs linked but out of sync");
	_assert_msg_(COMMANDPROCESSOR, cpuFifo.CPEnd			== ProcessorInterface::Fifo_CPUEnd, "FIFOs linked but out of sync");
}

void UpdateInterrupts(u64 userdata)
{
	if (userdata)
	{
		interruptSet = true;
		INFO_LOG(COMMANDPROCESSOR,"Interrupt set");
		ProcessorInterface::SetInterrupt(INT_CAUSE_CP, true);
	}
	else
	{
		interruptSet = false;
		INFO_LOG(COMMANDPROCESSOR,"Interrupt cleared");
		ProcessorInterface::SetInterrupt(INT_CAUSE_CP, false);
	}
	interruptWaiting = false;
}

void UpdateInterruptsFromVideoBackend(u64 userdata)
{
	CoreTiming::ScheduleEvent_Threadsafe(0, et_UpdateInterrupts, userdata);
}

// This is called by the ProcessorInterface when PI_FIFO_RESET is written to.
void AbortFrame()
{

}

void SetCpStatus(bool isCPUThread)
{
	if (Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
	{
		if (!isCPUThread)
		{
			return;
		}
		// We don't care.
		cpuFifo.bFF_HiWatermark = 0;
		cpuFifo.bFF_LoWatermark = 0;
	}
	else
	{
		// overflow & underflow check
		cpuFifo.bFF_HiWatermark = (cpuFifo.CPReadWriteDistance > cpuFifo.CPHiWatermark);
		cpuFifo.bFF_LoWatermark = (cpuFifo.CPReadWriteDistance < cpuFifo.CPLoWatermark);
	}

	// breakpoint
	if (!isCPUThread)
	{
		if (cpuFifo.bFF_BPEnable)
		{
			if (cpuFifo.CPBreakpoint == cpuFifo.CPReadPointer)
			{
				if (!cpuFifo.bFF_Breakpoint)
				{
					INFO_LOG(COMMANDPROCESSOR, "Hit breakpoint at %i", cpuFifo.CPReadPointer);
					cpuFifo.bFF_Breakpoint = true;
					IncrementCheckContextId();
				}
			}
			else
			{
				if (cpuFifo.bFF_Breakpoint)
					INFO_LOG(COMMANDPROCESSOR, "Cleared breakpoint at %i", cpuFifo.CPReadPointer);
				cpuFifo.bFF_Breakpoint = false;
			}
		}
		else
		{
			if (cpuFifo.bFF_Breakpoint)
				INFO_LOG(COMMANDPROCESSOR, "Cleared breakpoint at %i", cpuFifo.CPReadPointer);
			cpuFifo.bFF_Breakpoint = false;
		}
	}

	bool bpInt = cpuFifo.bFF_Breakpoint && cpuFifo.bFF_BPInt;
	bool ovfInt = cpuFifo.bFF_HiWatermark && cpuFifo.bFF_HiWatermarkInt;
	bool undfInt = cpuFifo.bFF_LoWatermark && cpuFifo.bFF_LoWatermarkInt;

	bool interrupt = (bpInt || ovfInt || undfInt) && m_CPCtrlReg.GPReadEnable;

	isHiWatermarkActive = ovfInt && m_CPCtrlReg.GPReadEnable;
	isLoWatermarkActive = undfInt && m_CPCtrlReg.GPReadEnable;

	if (interrupt != interruptSet && !interruptWaiting)
	{
		u64 userdata = interrupt?1:0;
		if (IsOnThread())
		{
			if (!interrupt || bpInt || undfInt || ovfInt)
			{
				if (!isCPUThread)
				{
					// GPU thread:
					interruptWaiting = true;
					if (!Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly)
					{
						CommandProcessor::UpdateInterruptsFromVideoBackend(userdata);
					}
				}
				else
				{
					// CPU thread:
					interruptSet = interrupt;
					INFO_LOG(COMMANDPROCESSOR,"Interrupt set");
					ProcessorInterface::SetInterrupt(INT_CAUSE_CP, interrupt);
				}
			}
		}
		else
		{
			CommandProcessor::UpdateInterrupts(userdata);
		}
	}
}

void ProcessFifoEvents()
{
	if (IsOnThread() && !Core::g_CoreStartupParameter.bSyncGPUAtIdleOnly &&
	    (interruptWaiting || interruptFinishWaiting || interruptTokenWaiting))
		CoreTiming::ProcessFifoWaitEvents();
}

void Shutdown()
{
 
}

void SetCpStatusRegister()
{
	// Here always there is one fifo attached to the GPU
	m_CPStatusReg.Breakpoint = cpuFifo.bFF_Breakpoint;
	m_CPStatusReg.ReadIdle = !cpuFifo.CPReadWriteDistance || cpuFifo.CPReadPointer == cpuFifo.CPWritePointer || AtBreakpointCpu();
	m_CPStatusReg.CommandIdle = !cpuFifo.CPReadWriteDistance || AtBreakpointCpu() || !cpuFifo.bFF_GPReadEnable;
	m_CPStatusReg.UnderflowLoWatermark = cpuFifo.bFF_LoWatermark;
	m_CPStatusReg.OverflowHiWatermark = cpuFifo.bFF_HiWatermark;

	INFO_LOG(COMMANDPROCESSOR,"\t Read from STATUS_REGISTER : %04x", m_CPStatusReg.Hex);
	DEBUG_LOG(COMMANDPROCESSOR, "(r) status: iBP %s | fReadIdle %s | fCmdIdle %s | iOvF %s | iUndF %s"
		, m_CPStatusReg.Breakpoint ?			"ON" : "OFF"
		, m_CPStatusReg.ReadIdle ?				"ON" : "OFF"
		, m_CPStatusReg.CommandIdle ?			"ON" : "OFF"
		, m_CPStatusReg.OverflowHiWatermark ?	"ON" : "OFF"
		, m_CPStatusReg.UnderflowLoWatermark ?	"ON" : "OFF"
			);
}

void SetCpControlRegister()
{
	// If the new fifo is being attached, force an exception check
	// This fixes the hang while booting Eternal Darkness
	if (!cpuFifo.bFF_GPReadEnable && m_CPCtrlReg.GPReadEnable && !m_CPCtrlReg.BPEnable)
	{
		CoreTiming::ForceExceptionCheck(0);
	}

	cpuFifo.bFF_BPInt = m_CPCtrlReg.BPInt;
	cpuFifo.bFF_BPEnable = m_CPCtrlReg.BPEnable;
	cpuFifo.bFF_HiWatermarkInt = m_CPCtrlReg.FifoOverflowIntEnable;
	cpuFifo.bFF_LoWatermarkInt = m_CPCtrlReg.FifoUnderflowIntEnable;
	cpuFifo.bFF_GPLinkEnable = m_CPCtrlReg.GPLinkEnable;

	if(m_CPCtrlReg.GPReadEnable && m_CPCtrlReg.GPLinkEnable)
	{
		ProcessorInterface::Fifo_CPUWritePointer = cpuFifo.CPWritePointer;
		ProcessorInterface::Fifo_CPUBase = cpuFifo.CPBase;
		ProcessorInterface::Fifo_CPUEnd = cpuFifo.CPEnd;
	}
			
	cpuFifo.bFF_GPReadEnable = m_CPCtrlReg.GPReadEnable;
	SyncGPU();

	DEBUG_LOG(COMMANDPROCESSOR, "\t GPREAD %s | BP %s | Int %s | OvF %s | UndF %s | LINK %s"
		, cpuFifo.bFF_GPReadEnable ?				"ON" : "OFF"
		, cpuFifo.bFF_BPEnable ?					"ON" : "OFF"
		, cpuFifo.bFF_BPInt ?						"ON" : "OFF"
		, m_CPCtrlReg.FifoOverflowIntEnable ?	"ON" : "OFF"
		, m_CPCtrlReg.FifoUnderflowIntEnable ?	"ON" : "OFF"
		, m_CPCtrlReg.GPLinkEnable ?			"ON" : "OFF"
		);

}

// NOTE: The implementation of this function should be correct, but we intentionally aren't using it at the moment.
// We don't emulate proper GP timing anyway at the moment, so this code would just slow down emulation.
void SetCpClearRegister()
{
//	if (IsOnThread())
//	{
//		if (!m_CPClearReg.ClearFifoUnderflow && m_CPClearReg.ClearFifoOverflow)
//			bProcessFifoToLoWatermark = true;
//	}
}

void Update()
{
	// called only when bSyncGPU is true
	while (VITicks > m_cpClockOrigin && gpuBusy && IsOnThread())
		Common::YieldCPU();

	if (gpuBusy)
		Common::AtomicAdd(VITicks, SystemTimers::GetTicksPerSecond() / 10000);
}
} // end of namespace CommandProcessor
