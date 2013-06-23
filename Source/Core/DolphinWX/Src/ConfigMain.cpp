// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <string> // System
#include <vector>
#include <algorithm>
#include <functional>
#include <wx/spinbutt.h>

#include "Common.h"
#include "CommonPaths.h"
#include "FileSearch.h"

#include "Core.h" // Core
#include "HW/EXI.h"
#include "HW/SI.h"
#include "HW/DSPHLE/DSPHLE.h"
#include "HW/DSPLLE/DSPLLE.h"
#include "HW/GCMemcard.h"
#include "IPC_HLE/WII_IPC_HLE.h"
#include "NANDContentLoader.h"

#include "WxUtils.h"
#include "Globals.h" // Local
#include "ConfigMain.h"
#include "ConfigManager.h"
#include "SysConf.h"
#include "Frame.h"
#include "HotkeyDlg.h"
#include "Main.h"
#include "VideoBackendBase.h"

#define TEXT_BOX(page, text) new wxStaticText(page, wxID_ANY, text, wxDefaultPosition, wxDefaultSize)

struct CPUCore
{
	int CPUid;
	const char *name;
};
const CPUCore CPUCores[] = {
	{0, wxTRANSLATE("Interpreter (VERY slow)")},
#ifdef _M_ARM
	{3, wxTRANSLATE("Arm JIT (experimental)")},
#else
	{1, wxTRANSLATE("JIT Recompiler (recommended)")},
	{2, wxTRANSLATE("JITIL experimental recompiler")},
#endif
};

extern CFrame* main_frame;

// keep these in sync with CConfigMain::InitializeGUILists
static const wxLanguage langIds[] =
{
	wxLANGUAGE_DEFAULT,
	wxLANGUAGE_ARABIC,
	wxLANGUAGE_CATALAN,
	wxLANGUAGE_CHINESE_SIMPLIFIED,
	wxLANGUAGE_CHINESE_TRADITIONAL,
	wxLANGUAGE_CZECH,
	wxLANGUAGE_DUTCH,
	wxLANGUAGE_ENGLISH,
	wxLANGUAGE_FARSI,
	wxLANGUAGE_FRENCH,
	wxLANGUAGE_GERMAN,
	wxLANGUAGE_GREEK,
	wxLANGUAGE_HEBREW,
	wxLANGUAGE_HUNGARIAN,
	wxLANGUAGE_ITALIAN,
	wxLANGUAGE_JAPANESE,
	wxLANGUAGE_KOREAN,
	wxLANGUAGE_NORWEGIAN_BOKMAL,
	wxLANGUAGE_POLISH,
	wxLANGUAGE_PORTUGUESE,
	wxLANGUAGE_PORTUGUESE_BRAZILIAN,
	wxLANGUAGE_RUSSIAN,
	wxLANGUAGE_SERBIAN,
	wxLANGUAGE_SPANISH,
	wxLANGUAGE_SWEDISH,
	wxLANGUAGE_TURKISH,
};

// Strings for Device Selections
#define DEV_NONE_STR		_trans("<Nothing>")
#define DEV_DUMMY_STR		_trans("Dummy")

#define SIDEV_STDCONT_STR	_trans("Standard Controller")
#define SIDEV_STEERING_STR	_trans("Steering Wheel")
#define SIDEV_DANCEMAT_STR	_trans("Dance Mat")
#define SIDEV_BONGO_STR		_trans("TaruKonga (Bongos)")
#define SIDEV_GBA_STR		"GBA"
#define SIDEV_AM_BB_STR		_trans("AM-Baseboard")

#define EXIDEV_MEMCARD_STR	_trans("Memory Card")
#define EXIDEV_MIC_STR		_trans("Mic")
#define EXIDEV_BBA_STR		"BBA"
#define EXIDEV_AM_BB_STR	_trans("AM-Baseboard")
#define EXIDEV_GECKO_STR	"USBGecko"

#define WXSTR_TRANS(a)		wxString(wxGetTranslation(wxT(a)))
#ifdef WIN32
//only used with xgettext to be picked up as translatable string.
//win32 does not have wx on its path, the provided wxALL_FILES 
//translation does not work there.
#define unusedALL_FILES wxTRANSLATE("All files (*.*)|*.*");
#endif

BEGIN_EVENT_TABLE(CConfigMain, wxDialog)

EVT_CLOSE(CConfigMain::OnClose)
EVT_BUTTON(wxID_OK, CConfigMain::OnOk)


EVT_CHECKBOX(ID_CPUTHREAD, CConfigMain::CoreSettingsChanged)
EVT_CHECKBOX(ID_IDLESKIP, CConfigMain::CoreSettingsChanged)
EVT_CHECKBOX(ID_ENABLECHEATS, CConfigMain::CoreSettingsChanged)
EVT_CHECKBOX(ID_INPUTSETTINGSISO, CConfigMain::CoreSettingsChanged)
EVT_CHOICE(ID_FRAMELIMIT, CConfigMain::CoreSettingsChanged)
EVT_CHECKBOX(ID_FRAMELIMIT_USEFPSFORLIMITING, CConfigMain::CoreSettingsChanged)

EVT_RADIOBOX(ID_CPUENGINE, CConfigMain::CoreSettingsChanged)
EVT_CHECKBOX(ID_NTSCJ, CConfigMain::CoreSettingsChanged)


EVT_RADIOBOX(ID_DSPENGINE, CConfigMain::AudioSettingsChanged)
EVT_CHECKBOX(ID_DSPTHREAD, CConfigMain::AudioSettingsChanged)
EVT_CHECKBOX(ID_ENABLE_THROTTLE, CConfigMain::AudioSettingsChanged)
EVT_CHECKBOX(ID_DUMP_AUDIO, CConfigMain::AudioSettingsChanged)
EVT_CHECKBOX(ID_DPL2DECODER, CConfigMain::AudioSettingsChanged)
EVT_CHOICE(ID_BACKEND, CConfigMain::AudioSettingsChanged)
EVT_SLIDER(ID_VOLUME, CConfigMain::AudioSettingsChanged)

EVT_CHECKBOX(ID_INTERFACE_CONFIRMSTOP, CConfigMain::DisplaySettingsChanged)
EVT_CHECKBOX(ID_INTERFACE_USEPANICHANDLERS, CConfigMain::DisplaySettingsChanged)
EVT_CHECKBOX(ID_INTERFACE_ONSCREENDISPLAYMESSAGES, CConfigMain::DisplaySettingsChanged)
EVT_CHOICE(ID_INTERFACE_LANG, CConfigMain::DisplaySettingsChanged)
EVT_BUTTON(ID_HOTKEY_CONFIG, CConfigMain::DisplaySettingsChanged)


EVT_CHOICE(ID_GC_SRAM_LNG, CConfigMain::GCSettingsChanged)
EVT_CHECKBOX(ID_GC_ALWAYS_HLE_BS2, CConfigMain::GCSettingsChanged)

EVT_CHOICE(ID_GC_EXIDEVICE_SLOTA, CConfigMain::GCSettingsChanged)
EVT_BUTTON(ID_GC_EXIDEVICE_SLOTA_PATH, CConfigMain::GCSettingsChanged)
EVT_CHOICE(ID_GC_EXIDEVICE_SLOTB, CConfigMain::GCSettingsChanged)
EVT_BUTTON(ID_GC_EXIDEVICE_SLOTB_PATH, CConfigMain::GCSettingsChanged)
EVT_CHOICE(ID_GC_EXIDEVICE_SP1, CConfigMain::GCSettingsChanged)
EVT_CHOICE(ID_GC_SIDEVICE0, CConfigMain::GCSettingsChanged)
EVT_CHOICE(ID_GC_SIDEVICE1, CConfigMain::GCSettingsChanged)
EVT_CHOICE(ID_GC_SIDEVICE2, CConfigMain::GCSettingsChanged)
EVT_CHOICE(ID_GC_SIDEVICE3, CConfigMain::GCSettingsChanged)


EVT_CHECKBOX(ID_WII_IPL_SSV, CConfigMain::WiiSettingsChanged)
EVT_CHECKBOX(ID_WII_IPL_E60, CConfigMain::WiiSettingsChanged)
EVT_CHOICE(ID_WII_IPL_AR, CConfigMain::WiiSettingsChanged)
EVT_CHOICE(ID_WII_IPL_LNG, CConfigMain::WiiSettingsChanged)

EVT_CHECKBOX(ID_WII_SD_CARD, CConfigMain::WiiSettingsChanged)
EVT_CHECKBOX(ID_WII_KEYBOARD, CConfigMain::WiiSettingsChanged)


EVT_LISTBOX(ID_ISOPATHS, CConfigMain::ISOPathsSelectionChanged)
EVT_CHECKBOX(ID_RECURSIVEISOPATH, CConfigMain::RecursiveDirectoryChanged)
EVT_BUTTON(ID_ADDISOPATH, CConfigMain::AddRemoveISOPaths)
EVT_BUTTON(ID_REMOVEISOPATH, CConfigMain::AddRemoveISOPaths)

EVT_FILEPICKER_CHANGED(ID_DEFAULTISO, CConfigMain::DefaultISOChanged)
EVT_DIRPICKER_CHANGED(ID_DVDROOT, CConfigMain::DVDRootChanged)
EVT_FILEPICKER_CHANGED(ID_APPLOADERPATH, CConfigMain::ApploaderPathChanged)
EVT_DIRPICKER_CHANGED(ID_NANDROOT, CConfigMain::NANDRootChanged)


END_EVENT_TABLE()

CConfigMain::CConfigMain(wxWindow* parent, wxWindowID id, const wxString& title,
		const wxPoint& position, const wxSize& size, long style)
	: wxDialog(parent, id, title, position, size, style)
{
	// Control refreshing of the ISOs list
	bRefreshList = false;

	CreateGUIControls();

	// Update selected ISO paths
	for(u32 i = 0; i < SConfig::GetInstance().m_ISOFolder.size(); i++)
	{
		ISOPaths->Append(StrToWxStr(SConfig::GetInstance().m_ISOFolder[i]));
	}
}

CConfigMain::~CConfigMain()
{
}

void CConfigMain::SetSelectedTab(int tab)
{
	// TODO : this is just a quick and dirty way to do it, possible cleanup

	switch (tab)
	{
	case ID_AUDIOPAGE:
		Notebook->SetSelection(2);
		break;
	}
}

// Used to restrict changing of some options while emulator is running
void CConfigMain::UpdateGUI()
{
	if(Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		// Disable the Core stuff on GeneralPage
		CPUThread->Disable();
		SkipIdle->Disable();
		EnableCheats->Disable();
		InputSettingsISO->Disable();
		
		CPUEngine->Disable();
		_NTSCJ->Disable();

		// Disable stuff on AudioPage
		DSPEngine->Disable();
		DSPThread->Disable();
		DPL2Decoder->Disable();
		Latency->Disable();

		// Disable stuff on GamecubePage
		GCSystemLang->Disable();
		GCAlwaysHLE_BS2->Disable();

		// Disable stuff on WiiPage
		WiiScreenSaver->Disable();
		WiiEuRGB60->Disable();
		WiiAspectRatio->Disable();
		WiiSystemLang->Disable();

		// Disable stuff on PathsPage
		PathsPage->Disable();
	}
}
void CConfigMain::InitializeGUILists()
{
	// General page
	// Framelimit
	arrayStringFor_Framelimit.Add(_("Off"));
	arrayStringFor_Framelimit.Add(_("Auto"));
	arrayStringFor_Framelimit.Add(_("Audio"));
	for (int i = 10; i <= 120; i += 5)	// from 10 to 120
		arrayStringFor_Framelimit.Add(wxString::Format(wxT("%i"), i));

	// Emulator Engine
	for (unsigned int a = 0; a < (sizeof(CPUCores) / sizeof(CPUCore)); ++a)
		arrayStringFor_CPUEngine.Add(wxGetTranslation(CPUCores[a].name));
		
	// DSP Engine 
	arrayStringFor_DSPEngine.Add(_("DSP HLE emulation (fast)"));
	arrayStringFor_DSPEngine.Add(_("DSP LLE recompiler"));
	arrayStringFor_DSPEngine.Add(_("DSP LLE interpreter (slow)"));
	
	// Gamecube page
	// GC Language arrayStrings
	arrayStringFor_GCSystemLang.Add(_("English"));
	arrayStringFor_GCSystemLang.Add(_("German"));
	arrayStringFor_GCSystemLang.Add(_("French"));
	arrayStringFor_GCSystemLang.Add(_("Spanish"));
	arrayStringFor_GCSystemLang.Add(_("Italian"));
	arrayStringFor_GCSystemLang.Add(_("Dutch"));

	
	// Wii page
	// Sensorbar Position
	arrayStringFor_WiiSensBarPos.Add(_("Bottom"));
	arrayStringFor_WiiSensBarPos.Add(_("Top"));
	
	// Aspect ratio
	arrayStringFor_WiiAspectRatio.Add(wxT("4:3"));
	arrayStringFor_WiiAspectRatio.Add(wxT("16:9"));
	
	// Wii Language arrayStrings
	arrayStringFor_WiiSystemLang = arrayStringFor_GCSystemLang;
	arrayStringFor_WiiSystemLang.Insert(_("Japanese"), 0);
	arrayStringFor_WiiSystemLang.Add(_("Simplified Chinese"));
	arrayStringFor_WiiSystemLang.Add(_("Traditional Chinese"));
	arrayStringFor_WiiSystemLang.Add(_("Korean"));

	// GUI language arrayStrings
	// keep these in sync with the langIds array at the beginning of this file
	arrayStringFor_InterfaceLang.Add(_("<System>"));
	arrayStringFor_InterfaceLang.Add(_("Arabic"));
	arrayStringFor_InterfaceLang.Add(_("Catalan"));
	arrayStringFor_InterfaceLang.Add(_("Chinese (Simplified)"));
	arrayStringFor_InterfaceLang.Add(_("Chinese (Traditional)"));
	arrayStringFor_InterfaceLang.Add(_("Czech"));
	arrayStringFor_InterfaceLang.Add(_("Dutch"));
	arrayStringFor_InterfaceLang.Add(_("English"));
	arrayStringFor_InterfaceLang.Add(_("Farsi"));
	arrayStringFor_InterfaceLang.Add(_("French"));
	arrayStringFor_InterfaceLang.Add(_("German"));
	arrayStringFor_InterfaceLang.Add(_("Greek"));
	arrayStringFor_InterfaceLang.Add(_("Hebrew"));
	arrayStringFor_InterfaceLang.Add(_("Hungarian"));
	arrayStringFor_InterfaceLang.Add(_("Italian"));
	arrayStringFor_InterfaceLang.Add(_("Japanese"));
	arrayStringFor_InterfaceLang.Add(_("Korean"));
	arrayStringFor_InterfaceLang.Add(_("Norwegian Bokmaal"));
	arrayStringFor_InterfaceLang.Add(_("Polish"));
	arrayStringFor_InterfaceLang.Add(_("Portuguese"));
	arrayStringFor_InterfaceLang.Add(_("Portuguese (Brazilian)"));
	arrayStringFor_InterfaceLang.Add(_("Russian"));
	arrayStringFor_InterfaceLang.Add(_("Serbian"));
	arrayStringFor_InterfaceLang.Add(_("Spanish"));
	arrayStringFor_InterfaceLang.Add(_("Swedish"));
	arrayStringFor_InterfaceLang.Add(_("Turkish"));
}

void CConfigMain::InitializeGUIValues()
{
	const SCoreStartupParameter& startup_params = SConfig::GetInstance().m_LocalCoreStartupParameter;
	
	// General - Basic
	CPUThread->SetValue(startup_params.bCPUThread);
	SkipIdle->SetValue(startup_params.bSkipIdle);
	EnableCheats->SetValue(startup_params.bEnableCheats);
	InputSettingsISO->SetValue(startup_params.bInputSettingsISO);
	Framelimit->SetSelection(SConfig::GetInstance().m_Framelimit);
	UseFPSForLimiting->SetValue(SConfig::GetInstance().b_UseFPS);

	// General - Advanced
	for (unsigned int a = 0; a < (sizeof(CPUCores) / sizeof(CPUCore)); ++a)
		if (CPUCores[a].CPUid == startup_params.iCPUCore)
			CPUEngine->SetSelection(a);
	_NTSCJ->SetValue(startup_params.bForceNTSCJ);


	// Display - Interface
	ConfirmStop->SetValue(startup_params.bConfirmStop);
	UsePanicHandlers->SetValue(startup_params.bUsePanicHandlers);
	OnScreenDisplayMessages->SetValue(startup_params.bOnScreenDisplayMessages);
	// need redesign
	for (unsigned int i = 0; i < sizeof(langIds) / sizeof(wxLanguage); i++)
	{
		if (langIds[i] == SConfig::GetInstance().m_InterfaceLanguage)
		{
			InterfaceLang->SetSelection(i);
			break;
		}
	}

	// Audio DSP Engine
	if (startup_params.bDSPHLE)
		DSPEngine->SetSelection(0);
	else
		DSPEngine->SetSelection(SConfig::GetInstance().m_EnableJIT ? 1 : 2);

	// Audio
	VolumeSlider->Enable(SupportsVolumeChanges(SConfig::GetInstance().sBackend));
	VolumeSlider->SetValue(SConfig::GetInstance().m_Volume);
	VolumeText->SetLabel(wxString::Format(wxT("%d %%"), SConfig::GetInstance().m_Volume));
	DSPThread->SetValue(startup_params.bDSPThread);
	DumpAudio->SetValue(SConfig::GetInstance().m_DumpAudio ? true : false);
	DPL2Decoder->Enable(std::string(SConfig::GetInstance().sBackend) == BACKEND_OPENAL);
	DPL2Decoder->SetValue(startup_params.bDPL2Decoder);
	Latency->Enable(std::string(SConfig::GetInstance().sBackend) == BACKEND_OPENAL);
	Latency->SetValue(startup_params.iLatency);
	// add backends to the list
	AddAudioBackends();


	// Gamecube - IPL
	GCSystemLang->SetSelection(startup_params.SelectedLanguage);
	GCAlwaysHLE_BS2->SetValue(startup_params.bHLE_BS2);

	// Gamecube - Devices
	wxArrayString SlotDevices;
		SlotDevices.Add(_(DEV_NONE_STR));
		SlotDevices.Add(_(DEV_DUMMY_STR));
		SlotDevices.Add(_(EXIDEV_MEMCARD_STR));
		SlotDevices.Add(_(EXIDEV_GECKO_STR));
#if HAVE_PORTAUDIO
		SlotDevices.Add(_(EXIDEV_MIC_STR));
#endif

	wxArrayString SP1Devices;
		SP1Devices.Add(_(DEV_NONE_STR));
		SP1Devices.Add(_(DEV_DUMMY_STR));
		SP1Devices.Add(_(EXIDEV_BBA_STR));
		SP1Devices.Add(_(EXIDEV_AM_BB_STR));

	wxArrayString SIDevices;
		SIDevices.Add(_(DEV_NONE_STR));
		SIDevices.Add(_(SIDEV_STDCONT_STR));
		SIDevices.Add(_(SIDEV_STEERING_STR));
		SIDevices.Add(_(SIDEV_DANCEMAT_STR));
		SIDevices.Add(_(SIDEV_BONGO_STR));
		SIDevices.Add(_(SIDEV_GBA_STR));
		SIDevices.Add(_(SIDEV_AM_BB_STR));

	for (int i = 0; i < 3; ++i)
	{
		bool isMemcard = false;

		// Add strings to the wxChoice list, the third wxChoice is the SP1 slot
		if (i == 2)
			GCEXIDevice[i]->Append(SP1Devices);
		else
			GCEXIDevice[i]->Append(SlotDevices);

		switch (SConfig::GetInstance().m_EXIDevice[i])
		{
		case EXIDEVICE_NONE:
			GCEXIDevice[i]->SetStringSelection(SlotDevices[0]);
			break;
		case EXIDEVICE_MEMORYCARD:
			isMemcard = GCEXIDevice[i]->SetStringSelection(SlotDevices[2]);
			break;
		case EXIDEVICE_MIC:
			GCEXIDevice[i]->SetStringSelection(SlotDevices[4]);
			break;
		case EXIDEVICE_ETH:
			GCEXIDevice[i]->SetStringSelection(SP1Devices[2]);
			break;
		case EXIDEVICE_AM_BASEBOARD:
			GCEXIDevice[i]->SetStringSelection(SP1Devices[3]);
			break;
		case EXIDEVICE_GECKO:
			GCEXIDevice[i]->SetStringSelection(SlotDevices[3]);
			break;
		case EXIDEVICE_DUMMY:
		default:
			GCEXIDevice[i]->SetStringSelection(SlotDevices[1]);
			break;
		}
		if (!isMemcard && i < 2)
			GCMemcardPath[i]->Disable();
	}
	for (int i = 0; i < 4; ++i)
	{
		// Add string to the wxChoice list
		GCSIDevice[i]->Append(SIDevices);

		switch (SConfig::GetInstance().m_SIDevice[i])
		{
		case SIDEVICE_GC_CONTROLLER:
			GCSIDevice[i]->SetStringSelection(SIDevices[1]);
			break;
		case SIDEVICE_GC_STEERING:
			GCSIDevice[i]->SetStringSelection(SIDevices[2]);
			break;
		case SIDEVICE_DANCEMAT:
			GCSIDevice[i]->SetStringSelection(SIDevices[3]);
			break;
		case SIDEVICE_GC_TARUKONGA:
			GCSIDevice[i]->SetStringSelection(SIDevices[4]);
			break;
		case SIDEVICE_GC_GBA:
			GCSIDevice[i]->SetStringSelection(SIDevices[5]);
			break;
		case SIDEVICE_AM_BASEBOARD:
			GCSIDevice[i]->SetStringSelection(SIDevices[6]);
			break;
		default:
			GCSIDevice[i]->SetStringSelection(SIDevices[0]);
			break;
		}
		// Remove the AM baseboard from the list, only the first list can select it
		if (i == 0)
			SIDevices.RemoveAt(SIDevices.GetCount() - 1);
	}

	// Wii - Misc
	WiiScreenSaver->SetValue(!!SConfig::GetInstance().m_SYSCONF->GetData<u8>("IPL.SSV"));
	WiiEuRGB60->SetValue(!!SConfig::GetInstance().m_SYSCONF->GetData<u8>("IPL.E60"));
	WiiAspectRatio->SetSelection(SConfig::GetInstance().m_SYSCONF->GetData<u8>("IPL.AR"));
	WiiSystemLang->SetSelection(SConfig::GetInstance().m_SYSCONF->GetData<u8>("IPL.LNG"));
	
	// Wii - Devices
	WiiSDCard->SetValue(SConfig::GetInstance().m_WiiSDCard);
	WiiKeyboard->SetValue(SConfig::GetInstance().m_WiiKeyboard);


	// Paths
	RecursiveISOPath->SetValue(SConfig::GetInstance().m_RecursiveISOFolder);
	DefaultISO->SetPath(StrToWxStr(startup_params.m_strDefaultGCM));
	DVDRoot->SetPath(StrToWxStr(startup_params.m_strDVDRoot));
	ApploaderPath->SetPath(StrToWxStr(startup_params.m_strApploader));
	NANDRoot->SetPath(StrToWxStr(SConfig::GetInstance().m_NANDPath));
}

void CConfigMain::InitializeGUITooltips()
{
	// General - Basic
	CPUThread->SetToolTip(_("This splits the Video and CPU threads, so they can be run on separate cores.\nCauses major speed improvements on PCs with more than one core, but can also cause occasional crashes/glitches."));
	Framelimit->SetToolTip(_("This limits the game speed to the specified number of frames per second (full speed is 60 for NTSC and 50 for PAL). Alternatively, use Audio to throttle using the DSP (might fix audio clicks but can also cause constant noise depending on the game)."));

	// General - Advanced
	_NTSCJ->SetToolTip(_("Forces NTSC-J mode for using the Japanese ROM font.\nLeft unchecked, dolphin defaults to NTSC-U and automatically enables this setting when playing Japanese games."));

	// Display - Interface
	ConfirmStop->SetToolTip(_("Show a confirmation box before stopping a game."));
	UsePanicHandlers->SetToolTip(_("Show a message box when a potentially serious error has occurred.\nDisabling this may avoid annoying and non-fatal messages, but it may also mean that Dolphin suddenly crashes without any explanation at all."));
	OnScreenDisplayMessages->SetToolTip(_("Show messages on the emulation screen area.\nThese messages include memory card writes, video backend and CPU information, and JIT cache clearing."));

	InterfaceLang->SetToolTip(_("Change the language of the user interface.\nRequires restart."));

	// Audio tooltips
	DSPThread->SetToolTip(_("Run DSP HLE and LLE on a dedicated thread (not recommended: might cause audio glitches with HLE and freezes with LLE)."));
	BackendSelection->SetToolTip(_("Changing this will have no effect while the emulator is running!"));

	// Gamecube - Devices
	GCEXIDevice[2]->SetToolTip(_("Serial Port 1 - This is the port which devices such as the net adapter use"));

	// Wii - Devices
	WiiKeyboard->SetToolTip(_("This could cause slow down in Wii Menu and some games."));

#if defined(__APPLE__)
	DPL2Decoder->SetToolTip(_("Enables Dolby Pro Logic II emulation using 5.1 surround. Not available on OSX."));
#elif defined(__linux__)
	DPL2Decoder->SetToolTip(_("Enables Dolby Pro Logic II emulation using 5.1 surround. OpenAL backend only."));
#elif defined(_WIN32)
	DPL2Decoder->SetToolTip(_("Enables Dolby Pro Logic II emulation using 5.1 surround. OpenAL backend only. May need to rename soft_oal.dll to OpenAL32.dll to make it work."));
#endif

	Latency->SetToolTip(_("Sets the latency (in ms).  Higher values may reduce audio crackling. OpenAL backend only."));
}

void CConfigMain::CreateGUIControls()
{
	InitializeGUILists();
	
	// Create the notebook and pages
	Notebook = new wxNotebook(this, ID_NOTEBOOK, wxDefaultPosition, wxDefaultSize);
	wxPanel* const GeneralPage = new wxPanel(Notebook, ID_GENERALPAGE, wxDefaultPosition, wxDefaultSize);
	wxPanel* const DisplayPage = new wxPanel(Notebook, ID_DISPLAYPAGE, wxDefaultPosition, wxDefaultSize);
	wxPanel* const AudioPage = new wxPanel(Notebook, ID_AUDIOPAGE, wxDefaultPosition, wxDefaultSize);
	wxPanel* const GamecubePage = new wxPanel(Notebook, ID_GAMECUBEPAGE, wxDefaultPosition, wxDefaultSize);
	wxPanel* const WiiPage = new wxPanel(Notebook, ID_WIIPAGE, wxDefaultPosition, wxDefaultSize);
	PathsPage = new wxPanel(Notebook, ID_PATHSPAGE, wxDefaultPosition, wxDefaultSize);

	Notebook->AddPage(GeneralPage, _("General"));
	Notebook->AddPage(DisplayPage, _("Interface"));
	Notebook->AddPage(AudioPage, _("Audio"));
	Notebook->AddPage(GamecubePage, _("Gamecube"));
	Notebook->AddPage(WiiPage, _("Wii"));
	Notebook->AddPage(PathsPage, _("Paths"));

	// General page
	// Core Settings - Basic
	CPUThread = new wxCheckBox(GeneralPage, ID_CPUTHREAD, _("Enable Dual Core (speedup)"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	SkipIdle = new wxCheckBox(GeneralPage, ID_IDLESKIP, _("Enable Idle Skipping (speedup)"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	EnableCheats = new wxCheckBox(GeneralPage, ID_ENABLECHEATS, _("Enable Cheats"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	InputSettingsISO = new wxCheckBox(GeneralPage, ID_INPUTSETTINGSISO, _("Save Input Settings to ISO Settings"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	// Framelimit
	Framelimit = new wxChoice(GeneralPage, ID_FRAMELIMIT, wxDefaultPosition, wxDefaultSize, arrayStringFor_Framelimit, 0, wxDefaultValidator);
	UseFPSForLimiting = new wxCheckBox(GeneralPage, ID_FRAMELIMIT_USEFPSFORLIMITING, _("Limit by FPS"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	// Core Settings - Advanced
	CPUEngine = new wxRadioBox(GeneralPage, ID_CPUENGINE, _("CPU Emulator Engine"), wxDefaultPosition, wxDefaultSize, arrayStringFor_CPUEngine, 0, wxRA_SPECIFY_ROWS);
	_NTSCJ = new wxCheckBox(GeneralPage, ID_NTSCJ, _("Force Console as NTSC-J"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);

	// Populate the General settings
	wxBoxSizer* sFramelimit = new wxBoxSizer(wxHORIZONTAL);
	sFramelimit->Add(TEXT_BOX(GeneralPage, _("Framelimit:")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	sFramelimit->Add(Framelimit, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);
	sFramelimit->Add(UseFPSForLimiting, 0, wxALL | wxEXPAND, 5);
	wxStaticBoxSizer* const sbBasic = new wxStaticBoxSizer(wxVERTICAL, GeneralPage, _("Basic Settings"));
	sbBasic->Add(CPUThread, 0, wxALL, 5);
	sbBasic->Add(SkipIdle, 0, wxALL, 5);
	sbBasic->Add(EnableCheats, 0, wxALL, 5);
	sbBasic->Add(InputSettingsISO, 0, wxALL, 5);
	sbBasic->Add(sFramelimit);

	wxStaticBoxSizer* const sbAdvanced = new wxStaticBoxSizer(wxVERTICAL, GeneralPage, _("Advanced Settings"));
	sbAdvanced->Add(CPUEngine, 0, wxALL, 5);
	sbAdvanced->Add(_NTSCJ, 0, wxALL, 5);

	wxBoxSizer* const sGeneralPage = new wxBoxSizer(wxVERTICAL);
	sGeneralPage->Add(sbBasic, 0, wxEXPAND | wxALL, 5);
	sGeneralPage->Add(sbAdvanced, 0, wxEXPAND | wxALL, 5);
	GeneralPage->SetSizer(sGeneralPage);

	// Interface Language
	InterfaceLang = new wxChoice(DisplayPage, ID_INTERFACE_LANG, wxDefaultPosition,
			wxDefaultSize, arrayStringFor_InterfaceLang, 0, wxDefaultValidator);
	// Hotkey configuration
	HotkeyConfig = new wxButton(DisplayPage, ID_HOTKEY_CONFIG, _("Hotkeys"),
			wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT, wxDefaultValidator);
	// Interface settings
	ConfirmStop = new wxCheckBox(DisplayPage, ID_INTERFACE_CONFIRMSTOP, _("Confirm on Stop"),
			wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	UsePanicHandlers = new wxCheckBox(DisplayPage, ID_INTERFACE_USEPANICHANDLERS,
			_("Use Panic Handlers"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	OnScreenDisplayMessages = new wxCheckBox(DisplayPage, ID_INTERFACE_ONSCREENDISPLAYMESSAGES,
			_("On-Screen Display Messages"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);

	wxBoxSizer* sInterface = new wxBoxSizer(wxHORIZONTAL);
	sInterface->Add(TEXT_BOX(DisplayPage, _("Language:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sInterface->Add(InterfaceLang, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sInterface->AddStretchSpacer();
	sInterface->Add(HotkeyConfig, 0, wxALIGN_RIGHT | wxALL, 5);

	// theme selection
	auto const theme_selection = new wxChoice(DisplayPage, wxID_ANY);

	CFileSearch::XStringVector theme_dirs;
	theme_dirs.push_back(File::GetUserPath(D_THEMES_IDX));
#if !defined(_WIN32)
	theme_dirs.push_back(SHARED_USER_DIR THEMES_DIR);
#endif

	CFileSearch cfs(CFileSearch::XStringVector(1, "*"), theme_dirs);
	auto const& sv = cfs.GetFileNames();
	std::for_each(sv.begin(), sv.end(), [theme_selection](const std::string& filename)
	{
		std::string name, ext;
		SplitPath(filename, NULL, &name, &ext);

		name += ext;
		auto const wxname = StrToWxStr(name);
		if (-1 == theme_selection->FindString(wxname))
			theme_selection->Append(wxname);
	});
	
	theme_selection->SetStringSelection(StrToWxStr(SConfig::GetInstance().m_LocalCoreStartupParameter.theme_name));

	// std::function = avoid error on msvc
	theme_selection->Bind(wxEVT_COMMAND_CHOICE_SELECTED, std::function<void(wxEvent&)>([theme_selection](wxEvent&)
	{
		SConfig::GetInstance().m_LocalCoreStartupParameter.theme_name = WxStrToStr(theme_selection->GetStringSelection());
		main_frame->InitBitmaps();
		main_frame->UpdateGameList();
	}));

	auto const scInterface = new wxBoxSizer(wxHORIZONTAL);
	scInterface->Add(TEXT_BOX(DisplayPage, _("Theme:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	scInterface->Add(theme_selection, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	scInterface->AddStretchSpacer();

	sbInterface = new wxStaticBoxSizer(wxVERTICAL, DisplayPage, _("Interface Settings"));
	sbInterface->Add(ConfirmStop, 0, wxALL, 5);
	sbInterface->Add(UsePanicHandlers, 0, wxALL, 5);
	sbInterface->Add(OnScreenDisplayMessages, 0, wxALL, 5);
	sbInterface->Add(scInterface, 0, wxEXPAND | wxALL, 5);
	sbInterface->Add(sInterface, 0, wxEXPAND | wxALL, 5);
	sDisplayPage = new wxBoxSizer(wxVERTICAL);
	sDisplayPage->Add(sbInterface, 0, wxEXPAND | wxALL, 5);
	DisplayPage->SetSizer(sDisplayPage);

	
	// Audio page
	DSPEngine = new wxRadioBox(AudioPage, ID_DSPENGINE, _("DSP Emulator Engine"),
				wxDefaultPosition, wxDefaultSize, arrayStringFor_DSPEngine, 0, wxRA_SPECIFY_ROWS);
	DSPThread = new wxCheckBox(AudioPage, ID_DSPTHREAD, _("DSP on Dedicated Thread"));
	DumpAudio = new wxCheckBox(AudioPage, ID_DUMP_AUDIO, _("Dump Audio"),
				wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	DPL2Decoder = new wxCheckBox(AudioPage, ID_DPL2DECODER, _("Dolby Pro Logic II decoder"));
	VolumeSlider = new wxSlider(AudioPage, ID_VOLUME, 0, 1, 100,
				wxDefaultPosition, wxDefaultSize, wxSL_VERTICAL|wxSL_INVERSE);
	VolumeText = new wxStaticText(AudioPage, wxID_ANY, wxT(""),
				wxDefaultPosition, wxDefaultSize, 0);
	BackendSelection = new wxChoice(AudioPage, ID_BACKEND, wxDefaultPosition,
				wxDefaultSize, wxArrayBackends, 0, wxDefaultValidator, wxEmptyString);
	Latency = new wxSpinCtrl(AudioPage, ID_LATENCY, "", wxDefaultPosition, wxDefaultSize,
		wxSP_ARROW_KEYS, 0, 30);

	Latency->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED, &CConfigMain::AudioSettingsChanged, this);

	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		Latency->Disable();
		BackendSelection->Disable();
		DPL2Decoder->Disable();
	}

	// Create sizer and add items to dialog
	wxStaticBoxSizer *sbAudioSettings = new wxStaticBoxSizer(wxVERTICAL, AudioPage, _("Sound Settings"));
	sbAudioSettings->Add(DSPEngine, 0, wxALL | wxEXPAND, 5);
	sbAudioSettings->Add(DSPThread, 0, wxALL, 5);
	sbAudioSettings->Add(DumpAudio, 0, wxALL, 5);
	sbAudioSettings->Add(DPL2Decoder, 0, wxALL, 5);

	wxStaticBoxSizer *sbVolume = new wxStaticBoxSizer(wxVERTICAL, AudioPage, _("Volume"));
	sbVolume->Add(VolumeSlider, 1, wxLEFT|wxRIGHT, 13);
	sbVolume->Add(VolumeText, 0, wxALIGN_CENTER|wxALL, 5);

	wxGridBagSizer *sBackend = new wxGridBagSizer();
	sBackend->Add(TEXT_BOX(AudioPage, _("Audio Backend:")), wxGBPosition(0, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxALL, 5);
	sBackend->Add(BackendSelection, wxGBPosition(0, 1), wxDefaultSpan, wxALL, 5);
	sBackend->Add(TEXT_BOX(AudioPage, _("Latency:")), wxGBPosition(1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxALL, 5);
	sBackend->Add(Latency, wxGBPosition(1, 1), wxDefaultSpan, wxALL, 5);
	wxStaticBoxSizer *sbBackend = new wxStaticBoxSizer(wxHORIZONTAL, AudioPage, _("Backend Settings"));
	sbBackend->Add(sBackend, 0, wxEXPAND);

	wxBoxSizer *sAudio = new wxBoxSizer(wxHORIZONTAL);
	sAudio->Add(sbAudioSettings, 1, wxEXPAND|wxALL, 5);
	sAudio->Add(sbVolume, 0, wxEXPAND|wxALL, 5);

	sAudioPage = new wxBoxSizer(wxVERTICAL);
	sAudioPage->Add(sAudio, 0, wxALL|wxEXPAND);
	sAudioPage->Add(sbBackend, 0, wxALL|wxEXPAND, 5);
	AudioPage->SetSizerAndFit(sAudioPage);


	// Gamecube page
	// IPL settings
	GCSystemLang = new wxChoice(GamecubePage, ID_GC_SRAM_LNG, wxDefaultPosition,
			wxDefaultSize, arrayStringFor_GCSystemLang, 0, wxDefaultValidator);
	GCAlwaysHLE_BS2 = new wxCheckBox(GamecubePage, ID_GC_ALWAYS_HLE_BS2, _("Skip BIOS"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	// Device settings
	// EXI Devices
	wxStaticText* GCEXIDeviceText[3];
	GCEXIDeviceText[0] = TEXT_BOX(GamecubePage, _("Slot A"));
	GCEXIDeviceText[1] = TEXT_BOX(GamecubePage, _("Slot B"));
	GCEXIDeviceText[2] = TEXT_BOX(GamecubePage, wxT("SP1"));
	GCEXIDevice[0] = new wxChoice(GamecubePage, ID_GC_EXIDEVICE_SLOTA);
	GCEXIDevice[1] = new wxChoice(GamecubePage, ID_GC_EXIDEVICE_SLOTB);
	GCEXIDevice[2] = new wxChoice(GamecubePage, ID_GC_EXIDEVICE_SP1);
	GCMemcardPath[0] = new wxButton(GamecubePage, ID_GC_EXIDEVICE_SLOTA_PATH, wxT("..."),
			wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT, wxDefaultValidator);
	GCMemcardPath[1] = new wxButton(GamecubePage, ID_GC_EXIDEVICE_SLOTB_PATH, wxT("..."),
			wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT, wxDefaultValidator);

	//SI Devices
	wxStaticText* GCSIDeviceText[4];
	GCSIDeviceText[0] = TEXT_BOX(GamecubePage, _("Port 1"));
	GCSIDeviceText[1] = TEXT_BOX(GamecubePage, _("Port 2"));
	GCSIDeviceText[2] = TEXT_BOX(GamecubePage, _("Port 3"));
	GCSIDeviceText[3] = TEXT_BOX(GamecubePage, _("Port 4"));
	GCSIDevice[0] = new wxChoice(GamecubePage, ID_GC_SIDEVICE0);
	GCSIDevice[1] = new wxChoice(GamecubePage, ID_GC_SIDEVICE1);
	GCSIDevice[2] = new wxChoice(GamecubePage, ID_GC_SIDEVICE2);
	GCSIDevice[3] = new wxChoice(GamecubePage, ID_GC_SIDEVICE3);

	// Populate the Gamecube page
	sGamecubeIPLSettings = new wxGridBagSizer();
	sGamecubeIPLSettings->Add(GCAlwaysHLE_BS2, wxGBPosition(0, 0), wxGBSpan(1, 2), wxALL, 5);
	sGamecubeIPLSettings->Add(TEXT_BOX(GamecubePage, _("System Language:")),
			wxGBPosition(1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxLEFT|wxRIGHT|wxBOTTOM, 5);
	sGamecubeIPLSettings->Add(GCSystemLang, wxGBPosition(1, 1), wxDefaultSpan, wxLEFT|wxRIGHT|wxBOTTOM, 5);

	sbGamecubeIPLSettings = new wxStaticBoxSizer(wxVERTICAL, GamecubePage, _("IPL Settings"));
	sbGamecubeIPLSettings->Add(sGamecubeIPLSettings);
	wxStaticBoxSizer *sbGamecubeDeviceSettings = new wxStaticBoxSizer(wxVERTICAL, GamecubePage, _("Device Settings"));
	wxGridBagSizer* sbGamecubeEXIDevSettings = new wxGridBagSizer(10, 10);
	for (int i = 0; i < 3; ++i)
	{
		sbGamecubeEXIDevSettings->Add(GCEXIDeviceText[i], wxGBPosition(i, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
		sbGamecubeEXIDevSettings->Add(GCEXIDevice[i], wxGBPosition(i, 1), wxGBSpan(1, (i < 2)?1:2), wxALIGN_CENTER_VERTICAL);
		if (i < 2) sbGamecubeEXIDevSettings->Add(GCMemcardPath[i], wxGBPosition(i, 2), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
	}
	sbGamecubeDeviceSettings->Add(sbGamecubeEXIDevSettings, 0, wxALL, 5);

	wxFlexGridSizer* sbGamecubeDevSettings = new wxFlexGridSizer(2, 10, 10);
	for (int i = 0; i < 4; ++i)
	{
		sbGamecubeDevSettings->Add(GCSIDeviceText[i], 1, wxALIGN_CENTER_VERTICAL|wxLEFT|wxRIGHT|wxBOTTOM, 0);
		sbGamecubeDevSettings->Add(GCSIDevice[i], 1, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 0);
	}
	sbGamecubeDeviceSettings->Add(sbGamecubeDevSettings, 0, wxALL, 5);

	sGamecubePage = new wxBoxSizer(wxVERTICAL);
	sGamecubePage->Add(sbGamecubeIPLSettings, 0, wxEXPAND|wxALL, 5);
	sGamecubePage->Add(sbGamecubeDeviceSettings, 0, wxEXPAND|wxALL, 5);
	GamecubePage->SetSizer(sGamecubePage);


	// Wii page
	// Misc Settings
	WiiScreenSaver = new wxCheckBox(WiiPage, ID_WII_IPL_SSV, _("Enable Screen Saver"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	WiiEuRGB60 = new wxCheckBox(WiiPage, ID_WII_IPL_E60, _("Use EuRGB60 Mode (PAL60)"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	WiiAspectRatio = new wxChoice(WiiPage, ID_WII_IPL_AR, wxDefaultPosition, wxDefaultSize, arrayStringFor_WiiAspectRatio, 0, wxDefaultValidator);
	WiiSystemLang = new wxChoice(WiiPage, ID_WII_IPL_LNG, wxDefaultPosition, wxDefaultSize, arrayStringFor_WiiSystemLang, 0, wxDefaultValidator);

	// Device Settings
	WiiSDCard = new wxCheckBox(WiiPage, ID_WII_SD_CARD, _("Insert SD Card"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	WiiKeyboard = new wxCheckBox(WiiPage, ID_WII_KEYBOARD, _("Connect USB Keyboard"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);

	// Populate the Wii Page
	sWiiIPLSettings = new wxGridBagSizer();
	sWiiIPLSettings->Add(WiiScreenSaver, wxGBPosition(0, 0), wxGBSpan(1, 2), wxALL, 5);
	sWiiIPLSettings->Add(WiiEuRGB60, wxGBPosition(1, 0), wxGBSpan(1, 2), wxALL, 5);
	sWiiIPLSettings->Add(TEXT_BOX(WiiPage, _("Aspect Ratio:")),
			wxGBPosition(2, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxALL, 5);
	sWiiIPLSettings->Add(WiiAspectRatio, wxGBPosition(2, 1), wxDefaultSpan, wxALL, 5);
	sWiiIPLSettings->Add(TEXT_BOX(WiiPage, _("System Language:")),
			wxGBPosition(3, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxALL, 5);
	sWiiIPLSettings->Add(WiiSystemLang, wxGBPosition(3, 1), wxDefaultSpan, wxALL, 5);
	sbWiiIPLSettings = new wxStaticBoxSizer(wxVERTICAL, WiiPage, _("Misc Settings"));
	sbWiiIPLSettings->Add(sWiiIPLSettings);

	sbWiiDeviceSettings = new wxStaticBoxSizer(wxVERTICAL, WiiPage, _("Device Settings"));
	sbWiiDeviceSettings->Add(WiiSDCard, 0, wxALL, 5);
	sbWiiDeviceSettings->Add(WiiKeyboard, 0, wxALL, 5);

	sWiiPage = new wxBoxSizer(wxVERTICAL);
	sWiiPage->Add(sbWiiIPLSettings, 0, wxEXPAND|wxALL, 5);
	sWiiPage->Add(sbWiiDeviceSettings, 0, wxEXPAND|wxALL, 5);
	WiiPage->SetSizer(sWiiPage);

	
	// Paths page
	ISOPaths = new wxListBox(PathsPage, ID_ISOPATHS, wxDefaultPosition, wxDefaultSize, arrayStringFor_ISOPaths, wxLB_SINGLE, wxDefaultValidator);
	RecursiveISOPath = new wxCheckBox(PathsPage, ID_RECURSIVEISOPATH, _("Search Subfolders"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator);
	AddISOPath = new wxButton(PathsPage, ID_ADDISOPATH, _("Add..."), wxDefaultPosition, wxDefaultSize, 0);
	RemoveISOPath = new wxButton(PathsPage, ID_REMOVEISOPATH, _("Remove"), wxDefaultPosition, wxDefaultSize, 0);
	RemoveISOPath->Enable(false);

	DefaultISO = new wxFilePickerCtrl(PathsPage, ID_DEFAULTISO, wxEmptyString, _("Choose a default ISO:"),
		_("All GC/Wii images (gcm, iso, wbfs, ciso, gcz)") + wxString::Format(wxT("|*.gcm;*.iso;*.wbfs;*.ciso;*.gcz|%s"), wxGetTranslation(wxALL_FILES)),
		wxDefaultPosition, wxDefaultSize, wxFLP_USE_TEXTCTRL|wxFLP_OPEN);
	DVDRoot = new wxDirPickerCtrl(PathsPage, ID_DVDROOT, wxEmptyString, _("Choose a DVD root directory:"), wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL);
	ApploaderPath = new wxFilePickerCtrl(PathsPage, ID_APPLOADERPATH, wxEmptyString, _("Choose file to use as apploader: (applies to discs constructed from directories only)"),
		_("apploader (.img)") + wxString::Format(wxT("|*.img|%s"), wxGetTranslation(wxALL_FILES)),
		wxDefaultPosition, wxDefaultSize, wxFLP_USE_TEXTCTRL|wxFLP_OPEN);
	NANDRoot = new wxDirPickerCtrl(PathsPage, ID_NANDROOT, wxEmptyString, _("Choose a NAND root directory:"), wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL);

	// Populate the settings
	wxBoxSizer* sISOButtons = new wxBoxSizer(wxHORIZONTAL);
	sISOButtons->Add(RecursiveISOPath, 0, wxALL|wxALIGN_CENTER, 0);
	sISOButtons->AddStretchSpacer();
	sISOButtons->Add(AddISOPath, 0, wxALL, 0);
	sISOButtons->Add(RemoveISOPath, 0, wxALL, 0);
	sbISOPaths = new wxStaticBoxSizer(wxVERTICAL, PathsPage, _("ISO Directories"));
	sbISOPaths->Add(ISOPaths, 1, wxEXPAND|wxALL, 0);
	sbISOPaths->Add(sISOButtons, 0, wxEXPAND|wxALL, 5);

	sOtherPaths = new wxGridBagSizer();
	sOtherPaths->Add(TEXT_BOX(PathsPage, _("Default ISO:")),
			wxGBPosition(0, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxALL, 5);
	sOtherPaths->Add(DefaultISO, wxGBPosition(0, 1), wxDefaultSpan, wxEXPAND|wxALL, 5);
	sOtherPaths->Add(TEXT_BOX(PathsPage, _("DVD Root:")),
			wxGBPosition(1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxALL, 5);
	sOtherPaths->Add(DVDRoot, wxGBPosition(1, 1), wxDefaultSpan, wxEXPAND|wxALL, 5);
	sOtherPaths->Add(TEXT_BOX(PathsPage, _("Apploader:")),
			wxGBPosition(2, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxALL, 5);
	sOtherPaths->Add(ApploaderPath, wxGBPosition(2, 1), wxDefaultSpan, wxEXPAND|wxALL, 5);
	sOtherPaths->Add(TEXT_BOX(PathsPage, _("Wii NAND Root:")),
			wxGBPosition(3, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxALL, 5);
	sOtherPaths->Add(NANDRoot, wxGBPosition(3, 1), wxDefaultSpan, wxEXPAND|wxALL, 5);
	sOtherPaths->AddGrowableCol(1);

	// Populate the Paths page
	sPathsPage = new wxBoxSizer(wxVERTICAL);
	sPathsPage->Add(sbISOPaths, 1, wxEXPAND|wxALL, 5);
	sPathsPage->Add(sOtherPaths, 0, wxEXPAND|wxALL, 5);
	PathsPage->SetSizer(sPathsPage);

	wxBoxSizer* sMain = new wxBoxSizer(wxVERTICAL);
	sMain->Add(Notebook, 1, wxEXPAND|wxALL, 5);
	sMain->Add(CreateButtonSizer(wxOK), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	InitializeGUIValues();
	InitializeGUITooltips();

	UpdateGUI();

	SetSizerAndFit(sMain);
	Center();
	SetFocus();
}

void CConfigMain::OnClose(wxCloseEvent& WXUNUSED (event))
{
	EndModal((bRefreshList) ? wxID_OK : wxID_CANCEL);
}

void CConfigMain::OnOk(wxCommandEvent& WXUNUSED (event))
{
	Close();

	// Save the config. Dolphin crashes too often to only save the settings on closing
	SConfig::GetInstance().SaveSettings();
}

// Core settings
void CConfigMain::CoreSettingsChanged(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	// Core - Basic
	case ID_CPUTHREAD:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bCPUThread = CPUThread->IsChecked();
		break;
	case ID_IDLESKIP:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bSkipIdle = SkipIdle->IsChecked();
		break;
	case ID_ENABLECHEATS:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bEnableCheats = EnableCheats->IsChecked();
		break;
	case ID_INPUTSETTINGSISO:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bInputSettingsISO = InputSettingsISO->IsChecked();
		break;
	case ID_FRAMELIMIT:
		SConfig::GetInstance().m_Framelimit = Framelimit->GetSelection();
		AudioCommon::UpdateSoundStream();
		break;
	case ID_FRAMELIMIT_USEFPSFORLIMITING:
		SConfig::GetInstance().b_UseFPS = UseFPSForLimiting->IsChecked();
		break;
	// Core - Advanced
	case ID_CPUENGINE:
		SConfig::GetInstance().m_LocalCoreStartupParameter.iCPUCore = CPUCores[CPUEngine->GetSelection()].CPUid;
		if (main_frame->g_pCodeWindow)
			main_frame->g_pCodeWindow->GetMenuBar()->Check(IDM_INTERPRETER,
				SConfig::GetInstance().m_LocalCoreStartupParameter.iCPUCore?false:true);
		break;
	case ID_NTSCJ:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bForceNTSCJ = _NTSCJ->IsChecked();
		break;
	}
}

// Display and Interface settings
void CConfigMain::DisplaySettingsChanged(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	// Display - Interface
	case ID_INTERFACE_CONFIRMSTOP:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bConfirmStop = ConfirmStop->IsChecked();
		break;
	case ID_INTERFACE_USEPANICHANDLERS:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bUsePanicHandlers = UsePanicHandlers->IsChecked();
		SetEnableAlert(UsePanicHandlers->IsChecked());
		break;
	case ID_INTERFACE_ONSCREENDISPLAYMESSAGES:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bOnScreenDisplayMessages = OnScreenDisplayMessages->IsChecked();
		SetEnableAlert(OnScreenDisplayMessages->IsChecked());
		break;
	case ID_INTERFACE_LANG:
		if (SConfig::GetInstance().m_InterfaceLanguage != langIds[InterfaceLang->GetSelection()])
			SuccessAlertT("You must restart Dolphin in order for the change to take effect.");
		SConfig::GetInstance().m_InterfaceLanguage = langIds[InterfaceLang->GetSelection()];
		break;
	case ID_HOTKEY_CONFIG:
		{
			HotkeyConfigDialog m_HotkeyDialog(this);
			m_HotkeyDialog.ShowModal();
		}
		// Update the GUI in case menu accelerators were changed
		main_frame->UpdateGUI();
		break;
	}
}

void CConfigMain::AudioSettingsChanged(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case ID_DSPENGINE:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bDSPHLE = DSPEngine->GetSelection() == 0;
		if (!DSPEngine->GetSelection() == 0)
			SConfig::GetInstance().m_EnableJIT = DSPEngine->GetSelection() == 1;
		AudioCommon::UpdateSoundStream();
		break;

	case ID_VOLUME:
		SConfig::GetInstance().m_Volume = VolumeSlider->GetValue();
		AudioCommon::UpdateSoundStream();
		VolumeText->SetLabel(wxString::Format(wxT("%d %%"), VolumeSlider->GetValue()));
		break;

	case ID_DSPTHREAD:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bDSPThread = DSPThread->IsChecked();
		break;

	case ID_DPL2DECODER:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bDPL2Decoder = DPL2Decoder->IsChecked();
		break;

	case ID_BACKEND:
		VolumeSlider->Enable(SupportsVolumeChanges(WxStrToStr(BackendSelection->GetStringSelection())));
		Latency->Enable(WxStrToStr(BackendSelection->GetStringSelection()) == BACKEND_OPENAL);
		DPL2Decoder->Enable(WxStrToStr(BackendSelection->GetStringSelection()) == BACKEND_OPENAL);
		// Don't save the translated BACKEND_NULLSOUND string
		SConfig::GetInstance().sBackend = BackendSelection->GetSelection() ?
			WxStrToStr(BackendSelection->GetStringSelection()) : BACKEND_NULLSOUND;
		AudioCommon::UpdateSoundStream();
		break;

	case ID_LATENCY:
		SConfig::GetInstance().m_LocalCoreStartupParameter.iLatency = Latency->GetValue();
		break;

	default:
		SConfig::GetInstance().m_DumpAudio = DumpAudio->GetValue();
		break;
	}
}

void CConfigMain::AddAudioBackends()
{
	std::vector<std::string> backends = AudioCommon::GetSoundBackends();
	// I'm sure Billiard will change this into an auto sometimes soon :P
	for (std::vector<std::string>::const_iterator iter = backends.begin(); 
		 iter != backends.end(); ++iter)
	{
		BackendSelection->Append(wxGetTranslation(StrToWxStr(*iter)));
		int num = BackendSelection->
			FindString(StrToWxStr(SConfig::GetInstance().sBackend));
		BackendSelection->SetSelection(num);
	}
}

bool CConfigMain::SupportsVolumeChanges(std::string backend)
{
	//FIXME: this one should ask the backend whether it supports it.
	//       but getting the backend from string etc. is probably
	//       too much just to enable/disable a stupid slider...
	return (backend == BACKEND_DIRECTSOUND ||
			backend == BACKEND_COREAUDIO ||
			backend == BACKEND_OPENAL ||
			backend == BACKEND_XAUDIO2);
}


// GC settings
// -----------------------
void CConfigMain::GCSettingsChanged(wxCommandEvent& event)
{
	int sidevice = 0;
	int exidevice = 0;
	switch (event.GetId())
	{
	// Gamecube - IPL
	case ID_GC_SRAM_LNG:
		SConfig::GetInstance().m_LocalCoreStartupParameter.SelectedLanguage = GCSystemLang->GetSelection();
		bRefreshList = true;
		break;
	// Gamecube - IPL Settings
	case ID_GC_ALWAYS_HLE_BS2:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bHLE_BS2 = GCAlwaysHLE_BS2->IsChecked();
		break;
	// Gamecube - Devices
	case ID_GC_EXIDEVICE_SP1:
		exidevice++;
	case ID_GC_EXIDEVICE_SLOTB:
		exidevice++;
	case ID_GC_EXIDEVICE_SLOTA:
		ChooseEXIDevice(event.GetString(), exidevice);
		break;
	case ID_GC_EXIDEVICE_SLOTA_PATH:
		ChooseMemcardPath(SConfig::GetInstance().m_strMemoryCardA, true);
		break;
	case ID_GC_EXIDEVICE_SLOTB_PATH:
		ChooseMemcardPath(SConfig::GetInstance().m_strMemoryCardB, false);
		break;
	case ID_GC_SIDEVICE3:
		sidevice++;
	case ID_GC_SIDEVICE2:
		sidevice++;
	case ID_GC_SIDEVICE1:
		sidevice++;
	case ID_GC_SIDEVICE0:
		ChooseSIDevice(event.GetString(), sidevice);
		break;
	}
}

void CConfigMain::ChooseMemcardPath(std::string& strMemcard, bool isSlotA)
{
	std::string filename = WxStrToStr(wxFileSelector(
		_("Choose a file to open"),
		StrToWxStr(File::GetUserPath(D_GCUSER_IDX)),
		isSlotA ? wxT(GC_MEMCARDA) : wxT(GC_MEMCARDB),
		wxEmptyString,
		_("Gamecube Memory Cards (*.raw,*.gcp)") + wxString(wxT("|*.raw;*.gcp"))));

	if (!filename.empty())
	{
		if (File::Exists(filename))
		{
			GCMemcard memorycard(filename.c_str());
			if (!memorycard.IsValid())
			{
				PanicAlertT("Cannot use that file as a memory card.\n%s\n" \
							"is not a valid gamecube memory card file", filename.c_str());
				return;
			}
		}
		#ifdef _WIN32
			if (!strncmp(File::GetExeDirectory().c_str(), filename.c_str(), File::GetExeDirectory().size()))
			{
				filename.erase(0, File::GetExeDirectory().size() +1);
				filename = "./" + filename;
			}
		#endif

		// also check that the path isn't used for the other memcard...
		if (filename.compare(isSlotA ? SConfig::GetInstance().m_strMemoryCardB
			: SConfig::GetInstance().m_strMemoryCardA) != 0)
		{
			strMemcard = filename;

			if (Core::GetState() != Core::CORE_UNINITIALIZED)
			{
				// Change memcard to the new file
				ExpansionInterface::ChangeDevice(
					isSlotA ? 0 : 1, // SlotA: channel 0, SlotB channel 1
					EXIDEVICE_MEMORYCARD,
					0);	// SP1 is device 2, slots are device 0
			}
		}
		else
		{
			PanicAlertT("Cannot use that file as a memory card.\n" \
					"Are you trying to use the same file in both slots?");
		}
	}
}

void CConfigMain::ChooseSIDevice(wxString deviceName, int deviceNum)
{
	SIDevices tempType;
	if (!deviceName.compare(WXSTR_TRANS(SIDEV_STDCONT_STR)))
		tempType = SIDEVICE_GC_CONTROLLER;
	else if (!deviceName.compare(WXSTR_TRANS(SIDEV_STEERING_STR)))
		tempType = SIDEVICE_GC_STEERING;
	else if (!deviceName.compare(WXSTR_TRANS(SIDEV_DANCEMAT_STR)))
		tempType = SIDEVICE_DANCEMAT;
	else if (!deviceName.compare(WXSTR_TRANS(SIDEV_BONGO_STR)))
		tempType = SIDEVICE_GC_TARUKONGA;
	else if (!deviceName.compare(wxT(SIDEV_GBA_STR)))
		tempType = SIDEVICE_GC_GBA;
	else if (!deviceName.compare(WXSTR_TRANS(SIDEV_AM_BB_STR)))
		tempType = SIDEVICE_AM_BASEBOARD;
	else
		tempType = SIDEVICE_NONE;

	SConfig::GetInstance().m_SIDevice[deviceNum] = tempType;

	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		// Change plugged device! :D
		SerialInterface::ChangeDevice(tempType, deviceNum);
	}
}

void CConfigMain::ChooseEXIDevice(wxString deviceName, int deviceNum)
{
	TEXIDevices tempType;

	if (!deviceName.compare(WXSTR_TRANS(EXIDEV_MEMCARD_STR)))
		tempType = EXIDEVICE_MEMORYCARD;
	else if (!deviceName.compare(WXSTR_TRANS(EXIDEV_MIC_STR)))
		tempType = EXIDEVICE_MIC;
	else if (!deviceName.compare(wxT(EXIDEV_BBA_STR)))
		tempType = EXIDEVICE_ETH;
	else if (!deviceName.compare(WXSTR_TRANS(EXIDEV_AM_BB_STR)))
		tempType = EXIDEVICE_AM_BASEBOARD;
	else if (!deviceName.compare(wxT(EXIDEV_GECKO_STR)))
		tempType = EXIDEVICE_GECKO;
	else if (!deviceName.compare(WXSTR_TRANS(DEV_NONE_STR)))
		tempType = EXIDEVICE_NONE;
	else
		tempType = EXIDEVICE_DUMMY;

	// Gray out the memcard path button if we're not on a memcard
	if (tempType == EXIDEVICE_MEMORYCARD)
		GCMemcardPath[deviceNum]->Enable();
	else if (deviceNum == 0 || deviceNum == 1)
		GCMemcardPath[deviceNum]->Disable();

	SConfig::GetInstance().m_EXIDevice[deviceNum] = tempType;

	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		// Change plugged device! :D
		ExpansionInterface::ChangeDevice(
			(deviceNum == 1) ? 1 : 0,	// SlotB is on channel 1, slotA and SP1 are on 0
			tempType,					// The device enum to change to
			(deviceNum == 2) ? 2 : 0);	// SP1 is device 2, slots are device 0
	}
}




// Wii settings
// -------------------
void CConfigMain::WiiSettingsChanged(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	// Wii - SYSCONF settings
	case ID_WII_IPL_SSV:
		SConfig::GetInstance().m_SYSCONF->SetData("IPL.SSV", WiiScreenSaver->IsChecked());
		break;
	case ID_WII_IPL_E60:
		SConfig::GetInstance().m_SYSCONF->SetData("IPL.E60", WiiEuRGB60->IsChecked());
		break;
	case ID_WII_IPL_AR:
		SConfig::GetInstance().m_SYSCONF->SetData("IPL.AR", WiiAspectRatio->GetSelection());
		break;
	case ID_WII_IPL_LNG:
	{
		int wii_system_lang = WiiSystemLang->GetSelection();
		SConfig::GetInstance().m_SYSCONF->SetData("IPL.LNG", wii_system_lang);
		u8 country_code = GetSADRCountryCode(wii_system_lang);
		if(!SConfig::GetInstance().m_SYSCONF->SetArrayData("IPL.SADR", &country_code, 1))
		{
			PanicAlert("Failed to update country code in SYSCONF");
		}
		break;
	}
	// Wii - Devices
	case ID_WII_SD_CARD:
		SConfig::GetInstance().m_WiiSDCard = WiiSDCard->IsChecked();
		WII_IPC_HLE_Interface::SDIO_EventNotify();
		break;
	case ID_WII_KEYBOARD:
		SConfig::GetInstance().m_WiiKeyboard = WiiKeyboard->IsChecked();
		break;
	}
}


// Paths settings
// -------------------
void CConfigMain::ISOPathsSelectionChanged(wxCommandEvent& WXUNUSED (event))
{
	if (!ISOPaths->GetStringSelection().empty())
	{
		RemoveISOPath->Enable(true);
	}
	else
	{
		RemoveISOPath->Enable(false);
	}
}

void CConfigMain::AddRemoveISOPaths(wxCommandEvent& event)
{
	if (event.GetId() == ID_ADDISOPATH)
	{
		wxDirDialog dialog(this, _("Choose a directory to add"), wxGetHomeDir(),
				wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

		if (dialog.ShowModal() == wxID_OK)
		{
			if (ISOPaths->FindString(dialog.GetPath()) != -1)
			{
				wxMessageBox(_("The chosen directory is already in the list"), _("Error"), wxOK);
			}
			else
			{
				bRefreshList = true;
				ISOPaths->Append(dialog.GetPath());
			}
		}
	}
	else
	{
		bRefreshList = true;
		ISOPaths->Delete(ISOPaths->GetSelection());
	}

	// Save changes right away
	SConfig::GetInstance().m_ISOFolder.clear();

	for (unsigned int i = 0; i < ISOPaths->GetCount(); i++)
		SConfig::GetInstance().m_ISOFolder.push_back(WxStrToStr(ISOPaths->GetStrings()[i]));
}

void CConfigMain::RecursiveDirectoryChanged(wxCommandEvent& WXUNUSED (event))
{
	SConfig::GetInstance().m_RecursiveISOFolder = RecursiveISOPath->IsChecked();
	bRefreshList = true;
}

void CConfigMain::DefaultISOChanged(wxFileDirPickerEvent& WXUNUSED (event))
{
	SConfig::GetInstance().m_LocalCoreStartupParameter.m_strDefaultGCM = WxStrToStr(DefaultISO->GetPath());
}

void CConfigMain::DVDRootChanged(wxFileDirPickerEvent& WXUNUSED (event))
{
	SConfig::GetInstance().m_LocalCoreStartupParameter.m_strDVDRoot = WxStrToStr(DVDRoot->GetPath());
}

void CConfigMain::ApploaderPathChanged(wxFileDirPickerEvent& WXUNUSED (event))
{
	SConfig::GetInstance().m_LocalCoreStartupParameter.m_strApploader = WxStrToStr(ApploaderPath->GetPath());
}

void CConfigMain::NANDRootChanged(wxFileDirPickerEvent& WXUNUSED (event))
{
	std::string NANDPath =
		SConfig::GetInstance().m_NANDPath =
			File::GetUserPath(D_WIIROOT_IDX, WxStrToStr(NANDRoot->GetPath()));
	NANDRoot->SetPath(StrToWxStr(NANDPath));
	SConfig::GetInstance().m_SYSCONF->UpdateLocation();
	DiscIO::cUIDsys::AccessInstance().UpdateLocation();
	DiscIO::CSharedContent::AccessInstance().UpdateLocation();
	main_frame->UpdateWiiMenuChoice();
}

// GFX backend selection
void CConfigMain::OnSelectionChanged(wxCommandEvent& ev)
{
	g_video_backend = g_available_video_backends[ev.GetInt()];
	SConfig::GetInstance().m_LocalCoreStartupParameter.m_strVideoBackend = g_video_backend->GetName();
}

void CConfigMain::OnConfig(wxCommandEvent&)
{
	if (g_video_backend)
		g_video_backend->ShowConfig(this);
}

// Change from IPL.LNG value to IPL.SADR country code
inline u8 CConfigMain::GetSADRCountryCode(int language)
{
	//http://wiibrew.org/wiki/Country_Codes
	u8 countrycode = language;
	switch (countrycode)
	{
	case 0: //Japanese
		countrycode = 1; //Japan
		break;
	case 1: //English
		countrycode = 49; // United States
		break;
	case 2:	//German
		countrycode = 78; //Germany
		break;
	case 3: //French
		countrycode = 77; //France
		break;
	case 4: //Spanish
		countrycode = 105; //Spain
		break;
	case 5: //Italian
		countrycode = 83; //Italy
		break;
	case 6:	//Dutch
		countrycode = 94; //Netherlands
		break;
	case 7: //Simplified Chinese
	case 8: //Traditional Chinese
		countrycode = 157; //China
		break;
	case 9: //Korean
		countrycode = 136; //Korea
		break;
	}
	return countrycode;
}
