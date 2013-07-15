// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Movie.h"

#include "BootManager.h"
#include "Core.h"
#include "ConfigManager.h"
#include "CPUDetect.h"
#include "Host.h"
#include "Thread.h"
#include "FileUtil.h"
#include "PowerPC/PowerPC.h"
#include "CoreTiming.h"
#include "HW/SI.h"
#include "HW/SystemTimers.h"
#include "HW/Wiimote.h"
#include "HW/WiimoteEmu/WiimoteEmu.h"
#include "HW/WiimoteEmu/WiimoteHid.h"
#include "IPC_HLE/WII_IPC_HLE_Device_usb.h"
#include "State.h"
#include "Timer.h"
#include "VideoConfig.h"
#include "HW/EXI.h"
#include "HW/EXI_Device.h"
#include "HW/EXI_Channel.h"
#include "HW/DVDInterface.h"
#include "../../Common/Src/NandPaths.h"
#include "Crypto/md5.h"
#include "scmrev.h"

// The chunk to allocate movie data in multiples of.
#define DTM_BASE_LENGTH (1024)

std::mutex cs_frameSkip;

namespace Movie {

bool g_bFrameStep = false;
bool g_bFrameStop = false;
bool g_bReadOnly = true;
u32 g_rerecords = 0;
PlayMode g_playMode = MODE_NONE;

u32 g_framesToSkip = 0, g_frameSkipCounter = 0;

u8 g_numPads = 0;
ControllerState g_padState;
DTMHeader tmpHeader;
u8* tmpInput = NULL;
size_t tmpInputAllocated = 0;
int et_IU;
u64 g_currentByte = 0, g_totalBytes = 0;
u64 g_currentFrame = 0, g_totalFrames = 0; // VI
u64 g_currentLagCount = 0, g_totalLagCount = 0; // just stats
u64 g_currentInputCount = 0, g_totalInputCount = 0; // just stats
u64 g_recordingStartTime; // seconds since 1970 that recording started
unsigned long g_startTime; // ms
std::vector<double> g_FPS;
bool bSaveConfig, bSkipIdle, bDualCore, bProgressive, bDSPHLE, bFastDiscSpeed = false;
bool bMemcard, g_bClearSave, bSyncGPU = false;
std::string videoBackend = "unknown";
int iCPUCore = 1;
bool g_bDiscChange = false;
std::string g_discChange = "";
std::string author = "";
u64 g_titleID = 0;
unsigned char MD5[16];
u8 bongos;
u8 revision[20];

bool g_bRecordingFromSaveState = false;
bool g_bPolled = false;
int g_currentSaveVersion = 0;

std::string tmpStateFilename = "dtm.sav";
std::string movieFilename;

std::string g_InputDisplay[8];

ManipFunction mfunc = NULL;

void EnsureTmpInputSize(size_t bound)
{
	if (tmpInputAllocated >= bound)
		return;
	// The buffer expands in powers of two of DTM_BASE_LENGTH
	// (standard exponential buffer growth).
	size_t newAlloc = DTM_BASE_LENGTH;
	while (newAlloc < bound)
		newAlloc *= 2;
	
	u8* newTmpInput = new u8[newAlloc];
	tmpInputAllocated = newAlloc;
	if (tmpInput != NULL)
	{
		if (g_totalBytes > 0)
			memcpy(newTmpInput, tmpInput, (size_t)g_totalBytes);
		delete[] tmpInput;
	}
	tmpInput = newTmpInput;
}

std::string GetInputDisplay()
{
	if (!IsPlayingInput() && !IsRecordingInput())
	{
		g_numPads = 0;
		for (int i = 0; i < 4; i++)
		{
			if (SConfig::GetInstance().m_SIDevice[i] == SIDEVICE_GC_CONTROLLER || SConfig::GetInstance().m_SIDevice[i] == SIDEVICE_GC_TARUKONGA)
				g_numPads |= (1 << i);
			if (g_wiimote_sources[i] != WIIMOTE_SRC_NONE)
				g_numPads |= (1 << (i + 4));
		}
	}

	std::string inputDisplay = "";
	for (int i = 0; i < 8; ++i)
		if ((g_numPads & (1 << i)) != 0)
			inputDisplay.append(g_InputDisplay[i]);
	
	return inputDisplay; 
}

void InputUpdate(u64 userdata, int cyclesLate)
{
	SerialInterface::UpdateDevices();

	auto s_Usb = GetUsbPointer();
	for (unsigned int i = 0; s_Usb && i < s_Usb->m_WiiMotes.size(); i++)
		if (s_Usb->m_WiiMotes[i].IsConnected())
			Wiimote::Update(i);
}

void FrameUpdate()
{
	g_currentFrame++;
	if(!g_bPolled) 
		g_currentLagCount++;

	if (IsRecordingInput())
	{
		g_totalFrames = g_currentFrame;
		g_totalLagCount = g_currentLagCount;
	}
	if (g_bFrameStep)
	{
		Core::SetState(Core::CORE_PAUSE);
		g_bFrameStep = false;
	}
	
	// ("framestop") the only purpose of this is to cause interpreter/jit Run() to return temporarily.
	// after that we set it back to CPU_RUNNING and continue as normal.
	if (g_bFrameStop)
		*PowerPC::GetStatePtr() = PowerPC::CPU_STEPPING;

	if(g_framesToSkip)
		FrameSkipping();
	
	g_bPolled = false;

	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bCPUThread && (IsRecordingInput() || IsPlayingInput()))
		CoreTiming::ScheduleEvent_Threadsafe_Immediate(et_IU);
}

// called when game is booting up, even if no movie is active,
// but potentially after BeginRecordingInput or PlayInput has been called.
void Init()
{
	g_bPolled = false;
	g_bFrameStep = false;
	g_bFrameStop = false;
	bSaveConfig = false;
	iCPUCore = SConfig::GetInstance().m_LocalCoreStartupParameter.iCPUCore;
	if (IsPlayingInput())
	{
		ReadHeader();
		Common::Thread md5thread;
		md5thread.Run(CheckMD5, "md5thread");
		if ((strncmp((char *)tmpHeader.gameID, Core::g_CoreStartupParameter.GetUniqueID().c_str(), 6)))
		{
			PanicAlert("The recorded game (%s) is not the same as the selected game (%s)", tmpHeader.gameID, Core::g_CoreStartupParameter.GetUniqueID().c_str());
			EndPlayInput(false);
		}
	}

	if (IsRecordingInput())
	{
		GetSettings();
		Common::Thread md5thread;
		md5thread.Run(CheckMD5, "md5thread");
	}

	g_frameSkipCounter = g_framesToSkip;
	memset(&g_padState, 0, sizeof(g_padState));

	for (int i = 0; i < 8; ++i)
		g_InputDisplay[i].clear();

	if (!IsPlayingInput() && !IsRecordingInput())
	{
		g_bRecordingFromSaveState = false;
		g_rerecords = 0;
		g_currentByte = 0;
		g_currentFrame = 0;
		g_currentLagCount = 0;
		g_currentInputCount = 0;
	}

	et_IU = CoreTiming::RegisterEvent("InputUpdate", InputUpdate);
}

void InputUpdate()
{
	g_currentInputCount++;
	if (IsRecordingInput())
		g_totalInputCount = g_currentInputCount;

	if (IsPlayingInput() && g_currentInputCount == (g_totalInputCount -1) && SConfig::GetInstance().m_PauseMovie)
		Core::SetState(Core::CORE_PAUSE);
}

void SetFrameSkipping(unsigned int framesToSkip)
{
	std::lock_guard<std::mutex> lk(cs_frameSkip);
	
	g_framesToSkip = framesToSkip;
	g_frameSkipCounter = 0;
	
	// Don't forget to re-enable rendering in case it wasn't...
	// as this won't be changed anymore when frameskip is turned off
	if (framesToSkip == 0)
		g_video_backend->Video_SetRendering(true);
}

void SetPolledDevice()
{
	g_bPolled = true;
}

void DoFrameStep()
{
	if(Core::GetState() == Core::CORE_PAUSE)
	{
		// if already paused, frame advance for 1 frame
		Core::SetState(Core::CORE_RUN);
		Core::RequestRefreshInfo();
		g_bFrameStep = true;
	}
	else if(!g_bFrameStep)
	{
		// if not paused yet, pause immediately instead
		Core::SetState(Core::CORE_PAUSE);
	}
}

void SetFrameStopping(bool bEnabled)
{
	g_bFrameStop = bEnabled;
}

void SetReadOnly(bool bEnabled)
{
	if (g_bReadOnly != bEnabled)
		Core::DisplayMessage(bEnabled ? "Read-only mode." :  "Read+Write mode.", 1000);

	g_bReadOnly = bEnabled;
}

void SetStartTime()
{
	g_startTime = Common::Timer::GetTimeMs();
}

void FrameSkipping()
{
	// Frameskipping will desync movie playback
	if (!IsPlayingInput() && !IsRecordingInput())
	{
		std::lock_guard<std::mutex> lk(cs_frameSkip);

		g_frameSkipCounter++;
		if (g_frameSkipCounter > g_framesToSkip || Core::ShouldSkipFrame(g_frameSkipCounter) == false)
			g_frameSkipCounter = 0;

		g_video_backend->Video_SetRendering(!g_frameSkipCounter);
	}
}

bool IsRecordingInput()
{
	return (g_playMode == MODE_RECORDING);
}

bool IsRecordingInputFromSaveState()
{
	return g_bRecordingFromSaveState;
}

bool IsJustStartingRecordingInputFromSaveState()
{
	return IsRecordingInputFromSaveState() && g_currentFrame == 0;
}

bool IsJustStartingPlayingInputFromSaveState()
{
	return IsRecordingInputFromSaveState() && g_currentFrame == 1 && IsPlayingInput();
}

bool IsPlayingInput()
{
	return (g_playMode == MODE_PLAYING);
}

bool IsReadOnly()
{
	return g_bReadOnly;
}

u64 GetRecordingStartTime()
{
	return g_recordingStartTime;
}

// get ISO ID
bool GetISOID(std::string &isoID_, std::string filename)
{
	DTMHeader header;

	if (!filename.empty())
	{
		File::IOFile g_recordfd;
		if (!g_recordfd.Open(filename, "rb"))
		{
			PanicAlertT("Can't open %s.", filename.c_str());
			isoID_ = "";
			return false;
		}
		g_recordfd.ReadArray(&header, 1);
	}
	else
	{
		header = tmpHeader;
	}

	char isoID[7];
	memcpy(isoID, (char*)header.gameID, 6);
	isoID[6] = '\0';
	isoID_ = std::string(isoID);

	if (isoID_.empty())
	{
		PanicAlertT("Can't find ISO ID in %s.", filename.c_str());
		return false;
	}

	return true;
}

bool IsUsingPad(int controller)
{
	return ((g_numPads & (1 << controller)) != 0);
}

bool IsUsingBongo(int controller)
{
	return ((bongos & (1 << controller)) != 0);
}

bool IsUsingWiimote(int wiimote)
{
	return ((g_numPads & (1 << (wiimote + 4))) != 0);
}

bool IsConfigSaved()
{
	return bSaveConfig;
}
bool IsDualCore()
{
	return bDualCore;
}

bool IsProgressive()
{
	return bProgressive;
}

bool IsSkipIdle()
{
	return bSkipIdle;
}

bool IsDSPHLE()
{
	return bDSPHLE;
}

bool IsFastDiscSpeed()
{
	return bFastDiscSpeed;
}

int GetCPUMode()
{
	return iCPUCore;
}

bool IsStartingFromClearSave()
{
	return g_bClearSave;
}

bool IsUsingMemcard()
{
	return bMemcard;
}
bool IsSyncGPU()
{
	return bSyncGPU;
}

void ChangePads(bool instantly)
{
	if (Core::GetState() == Core::CORE_UNINITIALIZED)
		return;

	int controllers = 0;

	for (int i = 0; i < 4; i++)
		if (SConfig::GetInstance().m_SIDevice[i] == SIDEVICE_GC_CONTROLLER || SConfig::GetInstance().m_SIDevice[i] == SIDEVICE_GC_TARUKONGA)
			controllers |= (1 << i);

	if (instantly && (g_numPads & 0x0F) == controllers)
		return;

	for (int i = 0; i < 4; i++)
		if (instantly) // Changes from savestates need to be instantaneous
			SerialInterface::AddDevice(IsUsingPad(i) ? (IsUsingBongo(i) ? SIDEVICE_GC_TARUKONGA : SIDEVICE_GC_CONTROLLER) : SIDEVICE_NONE, i);
		else
			SerialInterface::ChangeDevice(IsUsingPad(i) ? (IsUsingBongo(i) ? SIDEVICE_GC_TARUKONGA : SIDEVICE_GC_CONTROLLER) : SIDEVICE_NONE, i);
}

void ChangeWiiPads(bool instantly)
{
	int controllers = 0;

	for (int i = 0; i < 4; i++)
		if (g_wiimote_sources[i] != WIIMOTE_SRC_NONE)
			controllers |= (1 << i);

	// This is important for Wiimotes, because they can desync easily if they get re-activated
	if (instantly && (g_numPads >> 4) == controllers)
		return;

	for (int i = 0; i < MAX_BBMOTES; i++)
	{
		g_wiimote_sources[i] = IsUsingWiimote(i) ? WIIMOTE_SRC_EMU : WIIMOTE_SRC_NONE;
		GetUsbPointer()->AccessWiiMote(i | 0x100)->Activate(IsUsingWiimote(i));
	}
}

bool BeginRecordingInput(int controllers)
{
	if(g_playMode != MODE_NONE || controllers == 0)
		return false;

	g_numPads = controllers;
	g_currentFrame = g_totalFrames = 0;
	g_currentLagCount = g_totalLagCount = 0;
	g_currentInputCount = g_totalInputCount = 0;
	g_recordingStartTime = Common::Timer::GetLocalTimeSinceJan1970();
	g_rerecords = 0;

	for (int i = 0; i < 4; i++)
		if (SConfig::GetInstance().m_SIDevice[i] == SIDEVICE_GC_TARUKONGA)
			bongos |= (1 << i);

	if (Core::IsRunning())
	{
		if(File::Exists(tmpStateFilename))
			File::Delete(tmpStateFilename);

		State::SaveAs(tmpStateFilename.c_str());
		g_bRecordingFromSaveState = true;

		// This is only done here if starting from save state because otherwise we won't have the titleid. Otherwise it's set in WII_IPC_HLE_Device_es.cpp.
		// TODO: find a way to GetTitleDataPath() from Movie::Init()
		if (Core::g_CoreStartupParameter.bWii)
		{
			if (File::Exists((Common::GetTitleDataPath(g_titleID) + "banner.bin").c_str()))
				Movie::g_bClearSave = false;
			else
				Movie::g_bClearSave = true;
		}

		Common::Thread md5thread;
		md5thread.Run(GetMD5, "md5thread");
		GetSettings();
	}
	g_playMode = MODE_RECORDING;
	author = SConfig::GetInstance().m_strMovieAuthor;
	EnsureTmpInputSize(1);

	g_currentByte = g_totalBytes = 0;

	Core::DisplayMessage("Starting movie recording", 2000);
	return true;
}

static void Analog2DToString(u8 x, u8 y, const char* prefix, char* str)
{
	if((x <= 1 || x == 128 || x >= 255)
	&& (y <= 1 || y == 128 || y >= 255))
	{
		if(x != 128 || y != 128)
		{
			if(x != 128 && y != 128)
			{
				sprintf(str, "%s:%s,%s", prefix, x<128?"LEFT":"RIGHT", y<128?"DOWN":"UP");
			}
			else if(x != 128)
			{
				sprintf(str, "%s:%s", prefix, x<128?"LEFT":"RIGHT");
			}
			else
			{
				sprintf(str, "%s:%s", prefix, y<128?"DOWN":"UP");
			}
		}
		else
		{
			str[0] = '\0';
		}
	}
	else
	{
		sprintf(str, "%s:%d,%d", prefix, x, y);
	}
}

static void Analog1DToString(u8 v, const char* prefix, char* str)
{
	if(v > 0)
	{
		if(v == 255)
		{
			strcpy(str, prefix);
		}
		else
		{
			sprintf(str, "%s:%d", prefix, v);
		}
	}
	else
	{
		str[0] = '\0';
	}
}

void SetInputDisplayString(ControllerState padState, int controllerID)
{
	char inp[70];
	sprintf(inp, "P%d:", controllerID + 1);
	g_InputDisplay[controllerID] = inp;

	if(g_padState.A)
		g_InputDisplay[controllerID].append(" A");
	if(g_padState.B)
		g_InputDisplay[controllerID].append(" B");
	if(g_padState.X)
		g_InputDisplay[controllerID].append(" X");
	if(g_padState.Y)
		g_InputDisplay[controllerID].append(" Y");
	if(g_padState.Z)
		g_InputDisplay[controllerID].append(" Z");
	if(g_padState.Start)
		g_InputDisplay[controllerID].append(" START");

	if(g_padState.DPadUp)
		g_InputDisplay[controllerID].append(" UP");
	if(g_padState.DPadDown)
		g_InputDisplay[controllerID].append(" DOWN");
	if(g_padState.DPadLeft)
		g_InputDisplay[controllerID].append(" LEFT");
	if(g_padState.DPadRight)
		g_InputDisplay[controllerID].append(" RIGHT");

	Analog1DToString(g_padState.TriggerL, " L", inp);
	g_InputDisplay[controllerID].append(inp);

	Analog1DToString(g_padState.TriggerR, " R", inp);
	g_InputDisplay[controllerID].append(inp);

	Analog2DToString(g_padState.AnalogStickX, g_padState.AnalogStickY, " ANA", inp);
	g_InputDisplay[controllerID].append(inp);

	Analog2DToString(g_padState.CStickX, g_padState.CStickY, " C", inp);
	g_InputDisplay[controllerID].append(inp);

	g_InputDisplay[controllerID].append("\n");
}

void SetWiiInputDisplayString(int remoteID, u8* const coreData, u8* const accelData, u8* const irData)
{
	int controllerID = remoteID + 4;

	char inp[70];
	sprintf(inp, "R%d:", remoteID + 1);
	g_InputDisplay[controllerID] = inp;

	if(coreData)
	{
		wm_core buttons = *(wm_core*)coreData;
		if(buttons & WiimoteEmu::Wiimote::PAD_LEFT)
			g_InputDisplay[controllerID].append(" LEFT");
		if(buttons & WiimoteEmu::Wiimote::PAD_RIGHT)
			g_InputDisplay[controllerID].append(" RIGHT");
		if(buttons & WiimoteEmu::Wiimote::PAD_DOWN)
			g_InputDisplay[controllerID].append(" DOWN");
		if(buttons & WiimoteEmu::Wiimote::PAD_UP)
			g_InputDisplay[controllerID].append(" UP");
		if(buttons & WiimoteEmu::Wiimote::BUTTON_A)
			g_InputDisplay[controllerID].append(" A");
		if(buttons & WiimoteEmu::Wiimote::BUTTON_B)
			g_InputDisplay[controllerID].append(" B");
		if(buttons & WiimoteEmu::Wiimote::BUTTON_PLUS)
			g_InputDisplay[controllerID].append(" +");
		if(buttons & WiimoteEmu::Wiimote::BUTTON_MINUS)
			g_InputDisplay[controllerID].append(" -");
		if(buttons & WiimoteEmu::Wiimote::BUTTON_ONE)
			g_InputDisplay[controllerID].append(" 1");
		if(buttons & WiimoteEmu::Wiimote::BUTTON_TWO)
			g_InputDisplay[controllerID].append(" 2");
		if(buttons & WiimoteEmu::Wiimote::BUTTON_HOME)
			g_InputDisplay[controllerID].append(" HOME");
	}

	if(accelData)
	{
		wm_accel* dt = (wm_accel*)accelData;
		sprintf(inp, " ACC:%d,%d,%d", dt->x, dt->y, dt->z);
		g_InputDisplay[controllerID].append(inp);
	}

	if(irData) // incomplete
	{
		sprintf(inp, " IR:%d,%d", ((u8*)irData)[0], ((u8*)irData)[1]);
		g_InputDisplay[controllerID].append(inp);
	}

	g_InputDisplay[controllerID].append("\n");
}

void CheckPadStatus(SPADStatus *PadStatus, int controllerID)
{
	g_padState.A		 = ((PadStatus->button & PAD_BUTTON_A) != 0);
	g_padState.B		 = ((PadStatus->button & PAD_BUTTON_B) != 0);
	g_padState.X		 = ((PadStatus->button & PAD_BUTTON_X) != 0);
	g_padState.Y		 = ((PadStatus->button & PAD_BUTTON_Y) != 0);
	g_padState.Z		 = ((PadStatus->button & PAD_TRIGGER_Z) != 0);
	g_padState.Start     = ((PadStatus->button & PAD_BUTTON_START) != 0);

	g_padState.DPadUp	 = ((PadStatus->button & PAD_BUTTON_UP) != 0);
	g_padState.DPadDown  = ((PadStatus->button & PAD_BUTTON_DOWN) != 0);
	g_padState.DPadLeft  = ((PadStatus->button & PAD_BUTTON_LEFT) != 0);
	g_padState.DPadRight = ((PadStatus->button & PAD_BUTTON_RIGHT) != 0);

	g_padState.L = ((PadStatus->button & PAD_TRIGGER_L) != 0);
	g_padState.R = ((PadStatus->button & PAD_TRIGGER_R) != 0);
	g_padState.TriggerL = PadStatus->triggerLeft;
	g_padState.TriggerR = PadStatus->triggerRight;

	g_padState.AnalogStickX = PadStatus->stickX;
	g_padState.AnalogStickY = PadStatus->stickY;

	g_padState.CStickX = PadStatus->substickX;
	g_padState.CStickY = PadStatus->substickY;

	SetInputDisplayString(g_padState, controllerID);
}

void RecordInput(SPADStatus *PadStatus, int controllerID)
{
	if (!IsRecordingInput() || !IsUsingPad(controllerID))
		return;

	CheckPadStatus(PadStatus, controllerID);

	if (g_bDiscChange)
	{
		g_padState.disc = g_bDiscChange;
		g_bDiscChange = false;
	}


	DTMData d;
	d.frame = g_currentFrame;
	d.wii = 0;
	d.size = 8;

	EnsureTmpInputSize((size_t)(g_currentByte + sizeof(DTMData) + d.size));
	memcpy(&(tmpInput[g_currentByte]), &d, sizeof(DTMData));
	memcpy(&(tmpInput[g_currentByte + sizeof(DTMData)]), &g_padState, d.size);
	g_currentByte += sizeof(DTMData) + d.size;
	g_totalBytes = g_currentByte;
}

void CheckWiimoteStatus(int wiimote, u8 *data, const WiimoteEmu::ReportFeatures& rptf, int irMode)
{
	u8* const coreData = rptf.core?(data+rptf.core):NULL;
	u8* const accelData = rptf.accel?(data+rptf.accel):NULL;
	u8* const irData = rptf.ir?(data+rptf.ir):NULL;
	u8 size = rptf.size;
	SetWiiInputDisplayString(wiimote, coreData, accelData, irData);

	if (IsRecordingInput())
		RecordWiimote(wiimote, data, size);
}

void RecordWiimote(int wiimote, u8 *data, u8 size)
{
	if(!IsRecordingInput() || !IsUsingWiimote(wiimote))
		return;

	DTMData d;
	d.frame = g_currentFrame;
	d.wii = 1;
	d.size = size;

	InputUpdate();
	EnsureTmpInputSize((size_t)(g_currentByte + sizeof(DTMData) + d.size));
	memcpy(&(tmpInput[g_currentByte]), &d, sizeof(DTMData));
	memcpy(&(tmpInput[g_currentByte + sizeof(DTMData)]), data, size);

	g_currentByte += sizeof(DTMData) + d.size;
	g_totalBytes = g_currentByte;
}

void ReadHeader()
{
	g_numPads = tmpHeader.numControllers;
	g_recordingStartTime = tmpHeader.recordingStartTime;
	if (g_rerecords < tmpHeader.numRerecords)
		g_rerecords = tmpHeader.numRerecords;

	if (tmpHeader.bSaveConfig)
	{
		bSaveConfig = true;
		bSkipIdle = tmpHeader.bSkipIdle;
		bDualCore = tmpHeader.bDualCore;
		bProgressive = tmpHeader.bProgressive;
		bDSPHLE = tmpHeader.bDSPHLE;
		bFastDiscSpeed = tmpHeader.bFastDiscSpeed;
		iCPUCore = tmpHeader.CPUCore;
		g_bClearSave = tmpHeader.bClearSave;
		bMemcard = tmpHeader.bMemcard;
		bongos = tmpHeader.bongos;
		bSyncGPU = tmpHeader.bSyncGPU;
		memcpy(revision, tmpHeader.revision, ARRAYSIZE(revision));
	}
	else
	{
		GetSettings();
	}

	videoBackend = (char*) tmpHeader.videoBackend;
	g_discChange = (char*) tmpHeader.discChange;
	author = (char*) tmpHeader.author;
	memcpy(MD5, tmpHeader.md5, 16);
}

bool PlayInput(const char *filename)
{
	if(!filename || g_playMode != MODE_NONE)
		return false;

	if(!File::Exists(filename))
		return false;

	File::IOFile g_recordfd;

	if (!g_recordfd.Open(filename, "rb"))
		return false;

	std::string fn, ext;
	SplitPath(filename, NULL, &fn, &ext);
	movieFilename = fn + ext;

	g_recordfd.ReadArray(&tmpHeader, 1);
	
	if(tmpHeader.filetype[0] != 'D' || tmpHeader.filetype[1] != 'T' || tmpHeader.filetype[2] != 'M' || tmpHeader.filetype[3] != 0x1A) {
		PanicAlertT("Invalid recording file");
		goto cleanup;
	}

	ReadHeader();
	g_totalFrames = tmpHeader.frameCount;
	g_totalLagCount = tmpHeader.lagCount;
	g_totalInputCount = tmpHeader.inputCount;
	g_currentFrame = 0;
	g_currentLagCount = 0;
	g_currentInputCount = 0;

	g_playMode = MODE_PLAYING;
	
	g_totalBytes = g_recordfd.GetSize() - 256;
	EnsureTmpInputSize((size_t)g_totalBytes);
	g_recordfd.ReadArray(tmpInput, (size_t)g_totalBytes);
	g_currentByte = 0;
	g_recordfd.Close();

	// Load savestate (and skip to frame data)
	if(tmpHeader.bFromSaveState)
	{
		const std::string stateFilename = std::string(filename) + ".sav";
		if(File::Exists(stateFilename))
			Core::SetStateFileName(stateFilename);
		g_bRecordingFromSaveState = true;
		Movie::LoadInput(filename);
	}

	return true;

cleanup:
	g_recordfd.Close();
	return false;
}

void DoState(PointerWrap &p)
{
	static const int MOVIE_STATE_VERSION = 1;
	g_currentSaveVersion = MOVIE_STATE_VERSION;
	p.Do(g_currentSaveVersion);
	// many of these could be useful to save even when no movie is active,
	// and the data is tiny, so let's just save it regardless of movie state.
	p.Do(g_currentFrame);
	p.Do(g_currentByte);
	p.Do(g_currentLagCount);
	p.Do(g_currentInputCount);
	p.Do(g_bPolled);
	// other variables (such as g_totalBytes and g_totalFrames) are set in LoadInput
}

void LoadInput(const char *filename)
{
	File::IOFile t_record;
	if (!t_record.Open(filename, "r+b"))
	{
		PanicAlertT("Failed to read %s", filename);
		EndPlayInput(false);
		return;
	}

	t_record.ReadArray(&tmpHeader, 1);

	if(tmpHeader.filetype[0] != 'D' || tmpHeader.filetype[1] != 'T' || tmpHeader.filetype[2] != 'M' || tmpHeader.filetype[3] != 0x1A)
	{
		PanicAlertT("Savestate movie %s is corrupted, movie recording stopping...", filename);
		EndPlayInput(false);
		return;
	}
	ReadHeader();
	if (!g_bReadOnly)
	{
		g_rerecords++;
		tmpHeader.numRerecords = g_rerecords;
		t_record.Seek(0, SEEK_SET);
		t_record.WriteArray(&tmpHeader, 1);
	}

	ChangePads(true);
	if (Core::g_CoreStartupParameter.bWii)
		ChangeWiiPads(true);

	u64 totalSavedBytes = t_record.GetSize() - 256;

	bool afterEnd = false;
	if (g_currentByte > totalSavedBytes)
	{
		//PanicAlertT("Warning: You loaded a save whose movie ends before the current frame in the save (byte %u < %u) (frame %u < %u). You should load another save before continuing.", (u32)totalSavedBytes+256, (u32)g_currentByte+256, (u32)tmpHeader.frameCount, (u32)g_currentFrame);
		afterEnd = true;
	}

	if (!g_bReadOnly || tmpInput == NULL)
	{
		g_totalFrames = tmpHeader.frameCount;
		g_totalLagCount = tmpHeader.lagCount;
		g_totalInputCount = tmpHeader.inputCount;

		EnsureTmpInputSize((size_t)totalSavedBytes);
		g_totalBytes = totalSavedBytes;
		t_record.ReadArray(tmpInput, (size_t)g_totalBytes);
	}
	else if (g_currentByte > 0)
	{
		if (g_currentByte > totalSavedBytes)
		{
		}
		else if (g_currentByte > g_totalBytes)
		{
			PanicAlertT("Warning: You loaded a save that's after the end of the current movie. (byte %u > %u) (frame %u > %u). You should load another save before continuing, or load this state with read-only mode off.", (u32)g_currentByte+256, (u32)g_totalBytes+256, (u32)g_currentFrame, (u32)g_totalFrames);
		}
		else if(g_currentByte > 0 && g_totalBytes > 0)
		{
			// verify identical from movie start to the save's current frame
			u32 len = (u32)g_currentByte;
			u8* movInput = new u8[len];
			t_record.ReadArray(movInput, (size_t)len);
			for (u32 i = 0; i < len; ++i)
			{
				if (movInput[i] != tmpInput[i])
				{
					// this is a "you did something wrong" alert for the user's benefit.
					// we'll try to say what's going on in excruciating detail, otherwise the user might not believe us.
					if(IsUsingWiimote(0))
					{ 
						// TODO: more detail
						PanicAlertT("Warning: You loaded a save whose movie mismatches on byte %d (0x%X). You should load another save before continuing, or load this state with read-only mode off. Otherwise you'll probably get a desync.", i+256, i+256);
						memcpy(tmpInput, movInput, g_currentByte);
					}
					else
					{
						int frame = i/8;
						ControllerState curPadState;
						memcpy(&curPadState, &(tmpInput[frame*8]), 8);
						ControllerState movPadState;
						memcpy(&movPadState, &(movInput[frame*8]), 8);
						PanicAlertT("Warning: You loaded a save whose movie mismatches on frame %d. You should load another save before continuing, or load this state with read-only mode off. Otherwise you'll probably get a desync.\n\n"
							"More information: The current movie is %d frames long and the savestate's movie is %d frames long.\n\n"
							"On frame %d, the current movie presses:\n"
							"Start=%d, A=%d, B=%d, X=%d, Y=%d, Z=%d, DUp=%d, DDown=%d, DLeft=%d, DRight=%d, L=%d, R=%d, LT=%d, RT=%d, AnalogX=%d, AnalogY=%d, CX=%d, CY=%d"
							"\n\n"
							"On frame %d, the savestate's movie presses:\n"
							"Start=%d, A=%d, B=%d, X=%d, Y=%d, Z=%d, DUp=%d, DDown=%d, DLeft=%d, DRight=%d, L=%d, R=%d, LT=%d, RT=%d, AnalogX=%d, AnalogY=%d, CX=%d, CY=%d",
							(int)frame,
							(int)g_totalFrames, (int)tmpHeader.frameCount,
							(int)frame,
							(int)curPadState.Start, (int)curPadState.A, (int)curPadState.B, (int)curPadState.X, (int)curPadState.Y, (int)curPadState.Z, (int)curPadState.DPadUp, (int)curPadState.DPadDown, (int)curPadState.DPadLeft, (int)curPadState.DPadRight, (int)curPadState.L, (int)curPadState.R, (int)curPadState.TriggerL, (int)curPadState.TriggerR, (int)curPadState.AnalogStickX, (int)curPadState.AnalogStickY, (int)curPadState.CStickX, (int)curPadState.CStickY,
							(int)frame,
							(int)movPadState.Start, (int)movPadState.A, (int)movPadState.B, (int)movPadState.X, (int)movPadState.Y, (int)movPadState.Z, (int)movPadState.DPadUp, (int)movPadState.DPadDown, (int)movPadState.DPadLeft, (int)movPadState.DPadRight, (int)movPadState.L, (int)movPadState.R, (int)movPadState.TriggerL, (int)movPadState.TriggerR, (int)movPadState.AnalogStickX, (int)movPadState.AnalogStickY, (int)movPadState.CStickX, (int)movPadState.CStickY);

						memcpy(tmpInput, movInput, g_currentByte);
					}
					break;
				}
			}
			delete [] movInput;
		}
	}
	t_record.Close();

	bSaveConfig = tmpHeader.bSaveConfig;

	if (!afterEnd)
	{
		if (g_bReadOnly)
		{
			if(g_playMode != MODE_PLAYING)
			{
				g_playMode = MODE_PLAYING;
				Core::DisplayMessage("Switched to playback", 2000);
			}
		}
		else
		{
			if(g_playMode != MODE_RECORDING)
			{
				g_playMode = MODE_RECORDING;
				Core::DisplayMessage("Switched to recording", 2000);
			}
		}
	}
	else
	{
		EndPlayInput(false);
	}
}

static void CheckInputEnd()
{
	if (g_currentFrame > g_totalFrames || g_currentByte >= g_totalBytes)
	{
		EndPlayInput(!g_bReadOnly);
	}
}

bool TestEnd(u64 current, u64 total)
{
	if (current > total)
	{
		PanicAlertT("Premature movie end because next byte %llu > total bytes %llu", g_currentByte, g_totalBytes);
		EndPlayInput(!g_bReadOnly);
		return false;
	}
	else
		return true;
}

bool TestSize(u8 recorded, u8 current)
{
	if (recorded != current)
	{
		WARN_LOG(PAD, "Size at input %6llu: rec. %u, cur. %u", g_currentInputCount, recorded, current);
		g_currentByte += sizeof(DTMData) + recorded;
		return false;
	}
	else
		return true;
}

bool TestType(u8 recorded, u8 current)
{
	if (recorded != current)
	{
		PanicAlertT("Type at input %6llu: rec. %d, cur. %d", g_currentInputCount, recorded, current);
		EndPlayInput(!g_bReadOnly);
		return false;
	}
	else
		return true;
}

void TestSync(u64 recorded)
{
	if (recorded != g_currentFrame)
	{
		WARN_LOG(PAD, "Frame at input %6llu: dif. %3d, rec. %6d, cur. %6d", g_currentInputCount, g_currentFrame - recorded, recorded, g_currentFrame);
	}
}

void PlayController(SPADStatus *PadStatus, int controllerID)
{
	// Correct playback is entirely dependent on the emulator polling the controllers
	// in the same order done during recording
	if (!IsPlayingInput() || !IsUsingPad(controllerID) || tmpInput == NULL)
		return;

	if (!TestEnd(g_currentByte + 8, g_totalBytes))
		return;

	// dtm files don't save the mic button or error bit. not sure if they're actually used, but better safe than sorry
	signed char e = PadStatus->err;
	memset(PadStatus, 0, sizeof(SPADStatus));
	PadStatus->err = e;

	DTMData d;
	memcpy(&d, &(tmpInput[g_currentByte]), sizeof(DTMData));

	if (!TestType(d.wii, 0))
		return;

	if (!TestSize(d.size, 8))
		return;

	TestSync(d.frame);

	memcpy(&g_padState, &(tmpInput[g_currentByte + sizeof(DTMData)]), d.size);
	g_currentByte += sizeof(DTMData) + d.size;
	
	PadStatus->triggerLeft = g_padState.TriggerL;
	PadStatus->triggerRight = g_padState.TriggerR;

	PadStatus->stickX = g_padState.AnalogStickX;
	PadStatus->stickY = g_padState.AnalogStickY;

	PadStatus->substickX = g_padState.CStickX;
	PadStatus->substickY = g_padState.CStickY;

	PadStatus->button |= PAD_USE_ORIGIN;
	
	if(g_padState.A)
	{
		PadStatus->button |= PAD_BUTTON_A;
		PadStatus->analogA = 0xFF;
	}
	if(g_padState.B)
	{
		PadStatus->button |= PAD_BUTTON_B;
		PadStatus->analogB = 0xFF;
	}
	if(g_padState.X)
		PadStatus->button |= PAD_BUTTON_X;
	if(g_padState.Y)
		PadStatus->button |= PAD_BUTTON_Y;
	if(g_padState.Z)
		PadStatus->button |= PAD_TRIGGER_Z;
	if(g_padState.Start)
		PadStatus->button |= PAD_BUTTON_START;
	
	if(g_padState.DPadUp)
		PadStatus->button |= PAD_BUTTON_UP;
	if(g_padState.DPadDown)
		PadStatus->button |= PAD_BUTTON_DOWN;
	if(g_padState.DPadLeft)
		PadStatus->button |= PAD_BUTTON_LEFT;
	if(g_padState.DPadRight)
		PadStatus->button |= PAD_BUTTON_RIGHT;
	
	if(g_padState.L)
		PadStatus->button |= PAD_TRIGGER_L;
	if(g_padState.R)
		PadStatus->button |= PAD_TRIGGER_R;
	if (g_padState.disc)
	{
		// This implementation assumes the disc change will only happen once. Trying to change more than that will cause
		// it to load the last disc every time. As far as i know though, there are no 3+ disc games, so this should be fine.
		Core::SetState(Core::CORE_PAUSE);
		int numPaths = (int)SConfig::GetInstance().m_ISOFolder.size();
		bool found = false;
		std::string path;
		for (int i = 0; i < numPaths; i++)
		{
			path = SConfig::GetInstance().m_ISOFolder[i];
			if (File::Exists((path + '/' + g_discChange.c_str()).c_str()))
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			DVDInterface::ChangeDisc((path + '/' + g_discChange.c_str()).c_str());
			Core::SetState(Core::CORE_RUN);
		}
		else
		{
			PanicAlert("Change the disc to %s", g_discChange.c_str());
		}
	}

	SetInputDisplayString(g_padState, controllerID);
	CheckInputEnd();
}

bool PlayWiimote(int wiimote, u8 *data, const WiimoteEmu::ReportFeatures& rptf, int irMode)
{
	if(!IsPlayingInput() || !IsUsingWiimote(wiimote) || tmpInput == NULL)
		return false;

	if (!TestEnd(g_currentByte, g_totalBytes))
		return false;

	u8* const coreData = rptf.core?(data+rptf.core):NULL;
	u8* const accelData = rptf.accel?(data+rptf.accel):NULL;
	u8* const irData = rptf.ir?(data+rptf.ir):NULL;
	u8 size = rptf.size;

	if (!TestEnd(g_currentByte + size, g_totalBytes))
		return false;

	DTMData d;
	memcpy(&d, &(tmpInput[g_currentByte]), sizeof(DTMData));

	if (!TestType(d.wii, 1))
		return false;
	
	// send neutral instead of recorded data if it belong to another reporting mode
	if (!TestSize(d.size, size))
		return false;

	TestSync(d.frame);

	memcpy(data, &(tmpInput[g_currentByte + sizeof(DTMData)]), size);
	g_currentByte += sizeof(DTMData) + size;
	
	SetWiiInputDisplayString(wiimote, coreData, accelData, irData);

	g_currentInputCount++;
	
	CheckInputEnd();
	return true;
}

void EndPlayInput(bool cont) 
{
	if (cont)
	{
		g_playMode = MODE_RECORDING;
		Core::DisplayMessage("Reached movie end. Resuming recording.", 2000);
	}
	else if(g_playMode != MODE_NONE)
	{
		g_rerecords = 0;
		g_currentByte = 0;
		g_playMode = MODE_NONE;
		Core::DisplayMessage("Movie End.", 2000);
		g_bRecordingFromSaveState = false;
		// we don't clear these things because otherwise we can't resume playback if we load a movie state later
		//g_totalFrames = g_totalBytes = 0;
		//delete tmpInput;
		//tmpInput = NULL;

		// report benchmark result
		if (SConfig::GetInstance().m_LocalCoreStartupParameter.bBenchmark)
		{
			double totalTime = double(Common::Timer::GetTimeMs() - g_startTime) * 0.001;
			// state loading and recompilation makes the first frames unfairly low
			for(int i=0; i<3; i++) if (!g_FPS.empty()) g_FPS.erase(g_FPS.begin(),g_FPS.begin()+1);

			fprintf(stderr, "Performance\n");
			fprintf(stderr, "Frames:  %10llu\n", g_totalFrames);
			fprintf(stderr, "Seconds: %10.2f\n", totalTime);
			fprintf(stderr, "FPS:     %10.2f\n", g_totalFrames/totalTime);
			if (!g_FPS.empty()) fprintf(stderr, "Min FPS: %10.2f\n", *std::min_element(g_FPS.begin(),g_FPS.end())); else fprintf(stderr, "Min FPS: %10s\n", "N/A");
			if (!g_FPS.empty()) fprintf(stderr, "Max FPS: %10.2f\n", *std::max_element(g_FPS.begin(),g_FPS.end())); else fprintf(stderr, "Max FPS: %10s\n", "N/A");
			fprintf(stderr, "\n");

			fprintf(stderr, "Movie\n");
			fprintf(stderr, "File: %s\n", movieFilename.c_str());
			fprintf(stderr, "Program: %s\n", Core::g_CoreStartupParameter.m_strUniqueID.c_str());
			fprintf(stderr, "\n");

			fprintf(stderr, "System\n");
			fprintf(stderr, "CPU: %s\n", cpu_info.brand_string);
			fprintf(stderr, "\n");

			fprintf(stderr, "CPU\n");
			fprintf(stderr, "Recompile: %d\n", Core::g_CoreStartupParameter.iCPUCore);
			fprintf(stderr, "Skip idle: %d\n", Core::g_CoreStartupParameter.bSkipIdle);
			fprintf(stderr, "\n");

			fprintf(stderr, "Video\n");
			fprintf(stderr, "Backend: %s\n", Core::g_CoreStartupParameter.m_strVideoBackend.c_str());
			fprintf(stderr, "Thread: %d\n", Core::g_CoreStartupParameter.bCPUThread);
			fprintf(stderr, "Synchronized: %d\n", Core::g_CoreStartupParameter.bSyncGPU);
			fprintf(stderr, "Resolution: %d\n", g_ActiveConfig.iEFBScale);
			fprintf(stderr, "AA: %d\n", g_ActiveConfig.iMultisampleMode);
			fprintf(stderr, "AF: %d\n", g_ActiveConfig.iMaxAnisotropy);
			fprintf(stderr, "EFB scale: %d\n", g_ActiveConfig.bCopyEFBScaled);
			fprintf(stderr, "\n");

			fprintf(stderr, "EFB\n");
			fprintf(stderr, "Access from CPU: %d\n", g_ActiveConfig.bEFBAccessEnable);
			fprintf(stderr, "Emulate format changes: %d\n", g_ActiveConfig.bEFBEmulateFormatChanges);
			fprintf(stderr, "\n");

			fprintf(stderr, "EFB copy\n");
			fprintf(stderr, "Enabled: %d\n", g_ActiveConfig.bEFBCopyEnable);
			fprintf(stderr, "To texture: %d\n", g_ActiveConfig.bCopyEFBToTexture);
			fprintf(stderr, "Cache: %d\n", g_ActiveConfig.bEFBCopyCacheEnable);
			fprintf(stderr, "\n");

			fprintf(stderr, "Texture cache\n");
			fprintf(stderr, "Accuracy: %d\n", g_ActiveConfig.iSafeTextureCache_ColorSamples);
			fprintf(stderr, "\n");

			fprintf(stderr, "XFB\n");
			fprintf(stderr, "Enabled: %d\n", g_ActiveConfig.bUseXFB);
			fprintf(stderr, "Real: %d\n", g_ActiveConfig.bUseRealXFB);
			fprintf(stderr, "\n");

			fprintf(stderr, "Other\n");
			fprintf(stderr, "Cache display list: %d\n", g_ActiveConfig.bDlistCachingEnable);
			fprintf(stderr, "Destination alpha: %d\n", g_ActiveConfig.bDstAlphaPass);
			fprintf(stderr, "OpenCL: %d\n", g_ActiveConfig.bEnableOpenCL);
			fprintf(stderr, "OpenMP: %d\n", g_ActiveConfig.bOMPDecoder);
			fprintf(stderr, "Hacked buffer upload: %d\n", g_ActiveConfig.bHackedBufferUpload);
			fprintf(stderr, "Fast depth calculation: %d\n", g_ActiveConfig.bFastDepthCalc);
			fprintf(stderr, "\n");

			fprintf(stderr, "Audio\n");
			fprintf(stderr, "Backend: %s\n", SConfig::GetInstance().sBackend.c_str());
			fprintf(stderr, "Thread: %d\n", Core::g_CoreStartupParameter.bDSPThread);
			fprintf(stderr, "HLE: %d\n", SConfig::GetInstance().m_LocalCoreStartupParameter.bDSPHLE);
			fprintf(stderr, "Recompile: %d\n", SConfig::GetInstance().m_EnableJIT);
			fprintf(stderr, "\n");

			Host_Message(WM_USER_STOP);
		}
	}
}

void SaveRecording(const char *filename)
{
	File::IOFile save_record(filename, "wb");
	// Create the real header now and write it
	DTMHeader header;
	memset(&header, 0, sizeof(DTMHeader));
	
	header.filetype[0] = 'D'; header.filetype[1] = 'T'; header.filetype[2] = 'M'; header.filetype[3] = 0x1A;
	strncpy((char *)header.gameID, Core::g_CoreStartupParameter.GetUniqueID().c_str(), 6);
	header.bWii = Core::g_CoreStartupParameter.bWii;
	header.numControllers = g_numPads & (Core::g_CoreStartupParameter.bWii ? 0xFF : 0x0F);
	
	header.bFromSaveState = g_bRecordingFromSaveState;
	header.frameCount = g_totalFrames;
	header.lagCount = g_totalLagCount;
	header.inputCount = g_totalInputCount;
	header.numRerecords = g_rerecords;
	header.recordingStartTime = g_recordingStartTime;

	header.bSaveConfig = true;
	header.bSkipIdle = bSkipIdle;
	header.bDualCore = bDualCore;
	header.bProgressive = bProgressive;
	header.bDSPHLE = bDSPHLE;
	header.bFastDiscSpeed = bFastDiscSpeed;
	strncpy((char *)header.videoBackend, videoBackend.c_str(),ARRAYSIZE(header.videoBackend));
	header.CPUCore = iCPUCore;
	header.bEFBAccessEnable = g_ActiveConfig.bEFBAccessEnable;
	header.bEFBCopyEnable = g_ActiveConfig.bEFBCopyEnable;
	header.bCopyEFBToTexture = g_ActiveConfig.bCopyEFBToTexture;
	header.bEFBCopyCacheEnable = g_ActiveConfig.bEFBCopyCacheEnable;
	header.bEFBEmulateFormatChanges = g_ActiveConfig.bEFBEmulateFormatChanges;
	header.bUseXFB = g_ActiveConfig.bUseXFB;
	header.bUseRealXFB = g_ActiveConfig.bUseRealXFB;
	header.bMemcard = bMemcard;
	header.bClearSave = g_bClearSave;
	header.bSyncGPU = bSyncGPU;
	strncpy((char *)header.discChange, g_discChange.c_str(),ARRAYSIZE(header.discChange));
	strncpy((char *)header.author, author.c_str(),ARRAYSIZE(header.author));
	memcpy(header.md5,MD5,16);
	header.bongos = bongos;
	memcpy(header.revision, revision, ARRAYSIZE(header.revision));

	// TODO
	header.uniqueID = 0; 
	// header.audioEmulator;

	save_record.WriteArray(&header, 1);

	bool success = save_record.WriteArray(tmpInput, (size_t)g_totalBytes);

	if (success && g_bRecordingFromSaveState)
	{
		std::string stateFilename = filename;
		stateFilename.append(".sav");
		success = File::Copy(tmpStateFilename, stateFilename);
	}
	
	if (success)
		Core::DisplayMessage(StringFromFormat("DTM %s saved", filename).c_str(), 2000);
	else
		Core::DisplayMessage(StringFromFormat("Failed to save %s", filename).c_str(), 2000);
}

void SetInputManip(ManipFunction func)
{
	mfunc = func;
}

void CallInputManip(SPADStatus *PadStatus, int controllerID)
{
	if (mfunc)
		(*mfunc)(PadStatus, controllerID);
}

void SetGraphicsConfig()
{
	g_Config.bEFBAccessEnable = tmpHeader.bEFBAccessEnable;
	g_Config.bEFBCopyEnable = tmpHeader.bEFBCopyEnable;
	g_Config.bCopyEFBToTexture = tmpHeader.bCopyEFBToTexture;
	g_Config.bEFBCopyCacheEnable = tmpHeader.bEFBCopyCacheEnable;
	g_Config.bEFBEmulateFormatChanges = tmpHeader.bEFBEmulateFormatChanges;
	g_Config.bUseXFB = tmpHeader.bUseXFB;
	g_Config.bUseRealXFB = tmpHeader.bUseRealXFB;
}

void GetSettings()
{
	bSaveConfig = true;
	bSkipIdle = SConfig::GetInstance().m_LocalCoreStartupParameter.bSkipIdle;
	bDualCore = SConfig::GetInstance().m_LocalCoreStartupParameter.bCPUThread;
	bProgressive = SConfig::GetInstance().m_LocalCoreStartupParameter.bProgressive;
	bDSPHLE = SConfig::GetInstance().m_LocalCoreStartupParameter.bDSPHLE;
	bFastDiscSpeed = SConfig::GetInstance().m_LocalCoreStartupParameter.bFastDiscSpeed;
	videoBackend = g_video_backend->GetName();
	bSyncGPU = SConfig::GetInstance().m_LocalCoreStartupParameter.bSyncGPU;
	iCPUCore = SConfig::GetInstance().m_LocalCoreStartupParameter.iCPUCore;
	if (!Core::g_CoreStartupParameter.bWii)
		g_bClearSave = !File::Exists(SConfig::GetInstance().m_strMemoryCardA);
	bMemcard = SConfig::GetInstance().m_EXIDevice[0] == EXIDEVICE_MEMORYCARD;
	u8 tmp[21];
	for (int i = 0; i < 20; ++i)
	{
		sscanf(SCM_REV_STR + 2 * i, "%02hhx", &tmp[i]);
		revision[i] = tmp[i];
	}
}

void CheckMD5()
{
	for (int i=0, n=0; i<16; i++)
	{
		if (tmpHeader.md5[i] != 0)
			continue;
		n++;
		if (n == 16)
			return;
	}
	Core::DisplayMessage("Verifying checksum...", 2000);

	unsigned char gameMD5[16];
	char game[255];
	memcpy(game, SConfig::GetInstance().m_LocalCoreStartupParameter.m_strFilename.c_str(), SConfig::GetInstance().m_LocalCoreStartupParameter.m_strFilename.size());
	md5_file(game, gameMD5);

	if (memcmp(gameMD5,MD5,16) == 0)
		Core::DisplayMessage("Checksum of current game matches the recorded game.", 2000);
	else
		PanicAlert("Checksum of current game does not match the recorded game!");
}

void GetMD5()
{
	Core::DisplayMessage("Calculating checksum of game file...", 2000);
	memset(MD5, 0, sizeof(MD5));
	char game[255];
	memcpy(game, SConfig::GetInstance().m_LocalCoreStartupParameter.m_strFilename.c_str(),SConfig::GetInstance().m_LocalCoreStartupParameter.m_strFilename.size());
	md5_file(game, MD5);
	Core::DisplayMessage("Finished calculating checksum.", 2000);
}

void Shutdown()
{
	g_currentInputCount = g_totalInputCount = g_totalFrames = g_totalBytes = 0;
	delete [] tmpInput;
	tmpInput = NULL;
	tmpInputAllocated = 0;
}
};
