// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


// CFrame is the main parent window. Inside CFrame there is an m_Panel that is
// the parent for the rendering window (when we render to the main window). In
// Windows the rendering window is created by giving CreateWindow()
// m_Panel->GetHandle() as parent window and creating a new child window to
// m_Panel. The new child window handle that is returned by CreateWindow() can
// be accessed from Core::GetWindowHandle().

#include "Common.h" // Common
#include "FileUtil.h"
#include "Timer.h"

#include "Globals.h" // Local
#include "Frame.h"
#include "ConfigMain.h"
#include "CheatsWindow.h"
#include "GameListCtrl.h"
#include "BootManager.h"
#include "ConsoleListener.h"

#include "ConfigManager.h" // Core
#include "Core.h"
#include "HW/DVDInterface.h"
#include "HW/GCPad.h"
#include "IPC_HLE/WII_IPC_HLE_Device_usb.h"
#include "State.h"
#include "VolumeHandler.h"
#include "Movie.h"

#include "VideoBackendBase.h"

#include <wx/datetime.h> // wxWidgets

// Resources

extern "C" {
#include "../resources/Dolphin.c" // Dolphin icon
};


#ifdef _WIN32
// I could not use FindItemByHWND() instead of this, it crashed on that occasion I used it */
HWND MSWGetParent_(HWND Parent)
{
	return GetParent(Parent);
}
#endif

// ---------------
// The CPanel class to receive MSWWindowProc messages from the video backend.

extern CFrame* main_frame;


BEGIN_EVENT_TABLE(CPanel, wxPanel)
END_EVENT_TABLE()

CPanel::CPanel(
			wxWindow *parent,
			wxWindowID id
			)
	: wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, 0) // disables wxTAB_TRAVERSAL because it was breaking hotkeys
{
}

#ifdef _WIN32
	WXLRESULT CPanel::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
	{
		switch (nMsg)
		{
		case WM_USER:
			switch(wParam)
			{
			case WM_USER_STOP:
				main_frame->DoStop();
				break;

			case WM_USER_SETCURSOR:
				if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor &&
						main_frame->RendererHasFocus() && Core::GetState() == Core::CORE_RUN)
					SetCursor(wxCURSOR_BLANK);
				else
					SetCursor(wxNullCursor);
				break;
			}
			break;

		default:
			// By default let wxWidgets do what it normally does with this event
			return wxPanel::MSWWindowProc(nMsg, wParam, lParam);
		}
		return 0;
	}
#endif

CRenderFrame::CRenderFrame(wxFrame* parent, wxWindowID id, const wxString& title,
		const wxPoint& pos, const wxSize& size,	long style)
	: wxFrame(parent, id, title, pos, size, style)
{
	// Give it an icon
	wxIcon IconTemp;
	IconTemp.CopyFromBitmap(wxGetBitmapFromMemory(dolphin_ico32x32));
	SetIcon(IconTemp);
}

#ifdef _WIN32
WXLRESULT CRenderFrame::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
	switch (nMsg)
	{
		case WM_SYSCOMMAND:
			switch (wParam)
			{
				case SC_SCREENSAVE:
				case SC_MONITORPOWER:
					if (Core::GetState() == Core::CORE_RUN && SConfig::GetInstance().m_LocalCoreStartupParameter.bDisableScreenSaver)
						break;
				default:
					return wxFrame::MSWWindowProc(nMsg, wParam, lParam);
			}
			break;

		case WM_CLOSE:
			// Let Core finish initializing before accepting any WM_CLOSE messages
			if (Core::GetState() == Core::CORE_UNINITIALIZED) break;
			// Use default action otherwise

		default:
			// By default let wxWidgets do what it normally does with this event
			return wxFrame::MSWWindowProc(nMsg, wParam, lParam);
	}
	return 0;
}
#endif

// event tables
// Notice that wxID_HELP will be processed for the 'About' menu and the toolbar
// help button.

const wxEventType wxEVT_HOST_COMMAND = wxNewEventType();

BEGIN_EVENT_TABLE(CFrame, CRenderFrame)

// Menu bar
EVT_MENU(wxID_OPEN, CFrame::OnOpen)
EVT_MENU(wxID_EXIT, CFrame::OnQuit)
EVT_MENU(IDM_HELPWEBSITE, CFrame::OnHelp)
EVT_MENU(IDM_HELPGOOGLECODE, CFrame::OnHelp)
EVT_MENU(wxID_ABOUT, CFrame::OnHelp)
EVT_MENU(wxID_REFRESH, CFrame::OnRefresh)
EVT_MENU(IDM_PLAY, CFrame::OnPlay)
EVT_MENU(IDM_STOP, CFrame::OnStop)
EVT_MENU(IDM_RESET, CFrame::OnReset)
EVT_MENU(IDM_RECORD, CFrame::OnRecord)
EVT_MENU(IDM_PLAYRECORD, CFrame::OnPlayRecording)
EVT_MENU(IDM_RECORDEXPORT, CFrame::OnRecordExport)
EVT_MENU(IDM_RECORDREADONLY, CFrame::OnRecordReadOnly)
EVT_MENU(IDM_TASINPUT, CFrame::OnTASInput)
EVT_MENU(IDM_TOGGLE_PAUSEMOVIE, CFrame::OnTogglePauseMovie)
EVT_MENU(IDM_SHOWLAG, CFrame::OnShowLag)
EVT_MENU(IDM_FRAMESTEP, CFrame::OnFrameStep)
EVT_MENU(IDM_SCREENSHOT, CFrame::OnScreenshot)
EVT_MENU(wxID_PREFERENCES, CFrame::OnConfigMain)
EVT_MENU(IDM_CONFIG_GFX_BACKEND, CFrame::OnConfigGFX)
EVT_MENU(IDM_CONFIG_DSP_EMULATOR, CFrame::OnConfigDSP)
EVT_MENU(IDM_CONFIG_PAD_PLUGIN, CFrame::OnConfigPAD)
EVT_MENU(IDM_CONFIG_WIIMOTE_PLUGIN, CFrame::OnConfigWiimote)
EVT_MENU(IDM_CONFIG_HOTKEYS, CFrame::OnConfigHotkey)

EVT_MENU(IDM_SAVE_PERSPECTIVE, CFrame::OnToolBar)
EVT_AUITOOLBAR_TOOL_DROPDOWN(IDM_SAVE_PERSPECTIVE, CFrame::OnDropDownToolbarItem)
EVT_MENU(IDM_EDIT_PERSPECTIVES, CFrame::OnToolBar)
EVT_AUITOOLBAR_TOOL_DROPDOWN(IDM_EDIT_PERSPECTIVES, CFrame::OnDropDownSettingsToolbar)
// Drop down
EVT_MENU(IDM_PERSPECTIVES_ADD_PANE, CFrame::OnToolBar)
EVT_MENU_RANGE(IDM_PERSPECTIVES_0, IDM_PERSPECTIVES_100, CFrame::OnSelectPerspective)
EVT_MENU(IDM_ADD_PERSPECTIVE, CFrame::OnDropDownToolbarSelect)
EVT_MENU(IDM_TAB_SPLIT, CFrame::OnDropDownToolbarSelect)
EVT_MENU(IDM_NO_DOCKING, CFrame::OnDropDownToolbarSelect)
// Drop down float
EVT_MENU_RANGE(IDM_FLOAT_LOGWINDOW, IDM_FLOAT_CODEWINDOW, CFrame::OnFloatWindow)

EVT_MENU(IDM_NETPLAY, CFrame::OnNetPlay)
EVT_MENU(IDM_BROWSE, CFrame::OnBrowse)
EVT_MENU(IDM_MEMCARD, CFrame::OnMemcard)
EVT_MENU(IDM_IMPORTSAVE, CFrame::OnImportSave)
EVT_MENU(IDM_CHEATS, CFrame::OnShow_CheatsWindow)
EVT_MENU(IDM_CHANGEDISC, CFrame::OnChangeDisc)
EVT_MENU(IDM_MENU_INSTALLWAD, CFrame::OnInstallWAD)
EVT_MENU(IDM_LIST_INSTALLWAD, CFrame::OnInstallWAD)
EVT_MENU(IDM_LOAD_WII_MENU, CFrame::OnLoadWiiMenu)
EVT_MENU(IDM_FIFOPLAYER, CFrame::OnFifoPlayer)

EVT_MENU(IDM_TOGGLE_FULLSCREEN, CFrame::OnToggleFullscreen)
EVT_MENU(IDM_TOGGLE_DUALCORE, CFrame::OnToggleDualCore)
EVT_MENU(IDM_TOGGLE_SKIPIDLE, CFrame::OnToggleSkipIdle)
EVT_MENU(IDM_TOGGLE_TOOLBAR, CFrame::OnToggleToolbar)
EVT_MENU(IDM_TOGGLE_STATUSBAR, CFrame::OnToggleStatusbar)
EVT_MENU_RANGE(IDM_LOGWINDOW, IDM_VIDEOWINDOW, CFrame::OnToggleWindow)

EVT_MENU(IDM_PURGECACHE, CFrame::GameListChanged)

EVT_MENU(IDM_SAVEFIRSTSTATE, CFrame::OnSaveFirstState)
EVT_MENU(IDM_UNDOLOADSTATE,     CFrame::OnUndoLoadState)
EVT_MENU(IDM_UNDOSAVESTATE,     CFrame::OnUndoSaveState)
EVT_MENU(IDM_LOADSTATEFILE, CFrame::OnLoadStateFromFile)
EVT_MENU(IDM_SAVESTATEFILE, CFrame::OnSaveStateToFile)

EVT_MENU_RANGE(IDM_LOADSLOT1, IDM_LOADSLOT8, CFrame::OnLoadState)
EVT_MENU_RANGE(IDM_LOADLAST1, IDM_LOADLAST8, CFrame::OnLoadLastState)
EVT_MENU_RANGE(IDM_SAVESLOT1, IDM_SAVESLOT8, CFrame::OnSaveState)
EVT_MENU_RANGE(IDM_FRAMESKIP0, IDM_FRAMESKIP9, CFrame::OnFrameSkip)
EVT_MENU_RANGE(IDM_DRIVE1, IDM_DRIVE24, CFrame::OnBootDrive)
EVT_MENU_RANGE(IDM_CONNECT_WIIMOTE1, IDM_CONNECT_WIIMOTE4, CFrame::OnConnectWiimote)
EVT_MENU_RANGE(IDM_LISTWAD, IDM_LISTDRIVES, CFrame::GameListChanged)

// Other
EVT_ACTIVATE(CFrame::OnActive)
EVT_CLOSE(CFrame::OnClose)
EVT_SIZE(CFrame::OnResize)
EVT_MOVE(CFrame::OnMove)
EVT_LIST_ITEM_ACTIVATED(LIST_CTRL, CFrame::OnGameListCtrl_ItemActivated)
EVT_HOST_COMMAND(wxID_ANY, CFrame::OnHostMessage)

EVT_AUI_PANE_CLOSE(CFrame::OnPaneClose)
EVT_AUINOTEBOOK_PAGE_CLOSE(wxID_ANY, CFrame::OnNotebookPageClose)
EVT_AUINOTEBOOK_ALLOW_DND(wxID_ANY, CFrame::OnAllowNotebookDnD)
EVT_AUINOTEBOOK_PAGE_CHANGED(wxID_ANY, CFrame::OnNotebookPageChanged)
EVT_AUINOTEBOOK_TAB_RIGHT_UP(wxID_ANY, CFrame::OnTab)

// Post events to child panels
EVT_MENU_RANGE(IDM_INTERPRETER, IDM_ADDRBOX, CFrame::PostEvent)
EVT_TEXT(IDM_ADDRBOX, CFrame::PostEvent)

END_EVENT_TABLE()

// ---------------
// Creation and close, quit functions

CFrame::CFrame(wxFrame* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos,
		const wxSize& size,
		bool _UseDebugger,
		bool _BatchMode,
		bool ShowLogWindow,
		long style)
	: CRenderFrame(parent, id, title, pos, size, style)
	, g_pCodeWindow(NULL), g_NetPlaySetupDiag(NULL), g_CheatsWindow(NULL)
	, m_ToolBar(NULL), m_ToolBarDebug(NULL), m_ToolBarAui(NULL)
	, m_GameListCtrl(NULL), m_Panel(NULL)
	, m_RenderFrame(NULL), m_RenderParent(NULL)
	, m_LogWindow(NULL), m_LogConfigWindow(NULL)
	, m_FifoPlayerDlg(NULL), UseDebugger(_UseDebugger)
	, m_bBatchMode(_BatchMode), m_bEdit(false), m_bTabSplit(false), m_bNoDocking(false)
	, m_bGameLoading(false)
{
	for (int i = 0; i <= IDM_CODEWINDOW - IDM_LOGWINDOW; i++)
		bFloatWindow[i] = false;

	if (ShowLogWindow) SConfig::GetInstance().m_InterfaceLogWindow = true;

	// Give it a console early to show potential messages from this onward
	ConsoleListener *Console = LogManager::GetInstance()->GetConsoleListener();
	if (SConfig::GetInstance().m_InterfaceConsole) Console->Open();

	// Start debugging maximized
	if (UseDebugger) this->Maximize(true);
	// Debugger class
	if (UseDebugger)
	{
		g_pCodeWindow = new CCodeWindow(SConfig::GetInstance().m_LocalCoreStartupParameter, this, IDM_CODEWINDOW);
		LoadIniPerspectives();
		g_pCodeWindow->Load();
	}

	// Create toolbar bitmaps
	InitBitmaps();

	// Give it a status bar
	SetStatusBar(CreateStatusBar(2, wxST_SIZEGRIP, ID_STATUSBAR));
	if (!SConfig::GetInstance().m_InterfaceStatusbar)
		GetStatusBar()->Hide();

	// Give it a menu bar
	CreateMenu();

	// ---------------
	// Main panel
	// This panel is the parent for rendering and it holds the gamelistctrl
	m_Panel = new CPanel(this, IDM_MPANEL);

	m_GameListCtrl = new CGameListCtrl(m_Panel, LIST_CTRL,
			wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxSUNKEN_BORDER | wxLC_ALIGN_LEFT);

	wxBoxSizer *sizerPanel = new wxBoxSizer(wxHORIZONTAL);
	sizerPanel->Add(m_GameListCtrl, 1, wxEXPAND | wxALL);
	m_Panel->SetSizer(sizerPanel);
	// ---------------

	// Manager
	m_Mgr = new wxAuiManager(this, wxAUI_MGR_DEFAULT | wxAUI_MGR_LIVE_RESIZE);

	m_Mgr->AddPane(m_Panel, wxAuiPaneInfo()
			.Name(_T("Pane 0")).Caption(_T("Pane 0")).PaneBorder(false)
			.CaptionVisible(false).Layer(0).Center().Show());
	if (!g_pCodeWindow)
		m_Mgr->AddPane(CreateEmptyNotebook(), wxAuiPaneInfo()
				.Name(_T("Pane 1")).Caption(_("Logging")).CaptionVisible(true)
				.Layer(0).FloatingSize(wxSize(600, 350)).CloseButton(true).Hide());
	AuiFullscreen = m_Mgr->SavePerspective();

	// Create toolbar
	RecreateToolbar();
	if (!SConfig::GetInstance().m_InterfaceToolbar) DoToggleToolbar(false);

	m_LogWindow = new CLogWindow(this, IDM_LOGWINDOW);
	m_LogWindow->Hide();
	m_LogWindow->Disable();

	g_TASInputDlg = new TASInputDlg(this);
	Movie::SetInputManip(TASManipFunction);

	State::SetOnAfterLoadCallback(OnAfterLoadCallback);

	// Setup perspectives
	if (g_pCodeWindow)
	{
		// Load perspective
		DoLoadPerspective();
	}
	else
	{
		if (SConfig::GetInstance().m_InterfaceLogWindow)
			ToggleLogWindow(true);
		if (SConfig::GetInstance().m_InterfaceLogConfigWindow)
			ToggleLogConfigWindow(true);
		if (SConfig::GetInstance().m_InterfaceConsole)
			ToggleConsole(true);
	}

	// Show window
	Show();

	// Commit
	m_Mgr->Update();

	#ifdef _WIN32
		SetToolTip(wxT(""));
		GetToolTip()->SetAutoPop(25000);
	#endif

	#if defined(HAVE_XRANDR) && HAVE_XRANDR
		m_XRRConfig = new X11Utils::XRRConfiguration(X11Utils::XDisplayFromHandle(GetHandle()),
				X11Utils::XWindowFromHandle(GetHandle()));
	#endif

	// -------------------------
	// Connect event handlers

	m_Mgr->Bind(wxEVT_AUI_RENDER, &CFrame::OnManagerResize, this);
	// ----------

	// Update controls
	UpdateGUI();
}
// Destructor
CFrame::~CFrame()
{
	drives.clear();

	#if defined(HAVE_XRANDR) && HAVE_XRANDR
		delete m_XRRConfig;
	#endif

	ClosePages();

	delete m_Mgr;
}

bool CFrame::RendererIsFullscreen()
{
	bool fullscreen = false;

	if (Core::GetState() == Core::CORE_RUN || Core::GetState() == Core::CORE_PAUSE)
	{
		fullscreen = m_RenderFrame->IsFullScreen();
	}

#if defined(__APPLE__)
	if (m_RenderFrame != NULL)
	{
		NSView *view = (NSView *) m_RenderFrame->GetHandle();
		NSWindow *window = [view window];

		fullscreen = (([window styleMask] & NSFullScreenWindowMask) == NSFullScreenWindowMask);
	}
#endif

	return fullscreen;
}

void CFrame::OnQuit(wxCommandEvent& WXUNUSED (event))
{
	Close(true);
}

// --------
// Events
void CFrame::OnActive(wxActivateEvent& event)
{
	if (Core::GetState() == Core::CORE_RUN || Core::GetState() == Core::CORE_PAUSE)
	{
		if (event.GetActive() && event.GetEventObject() == m_RenderFrame)
		{
#ifdef __WXMSW__
			::SetFocus((HWND)m_RenderParent->GetHandle());
#else
			m_RenderParent->SetFocus();
#endif
			
			if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor &&
					Core::GetState() == Core::CORE_RUN)
				m_RenderParent->SetCursor(wxCURSOR_BLANK);
		}
		else
		{
			if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor)
				m_RenderParent->SetCursor(wxNullCursor);
		}
	}
	event.Skip();
}

void CFrame::OnClose(wxCloseEvent& event)
{
	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		DoStop();
		if (Core::GetState() != Core::CORE_UNINITIALIZED)
			return;
		UpdateGUI();
	}

	//Stop Dolphin from saving the minimized Xpos and Ypos
	if(main_frame->IsIconized())
		main_frame->Iconize(false);

	// Don't forget the skip or the window won't be destroyed
	event.Skip();

	// Save GUI settings
	if (g_pCodeWindow)
	{
		SaveIniPerspectives();
	}
	else
	{
		// Close the log window now so that its settings are saved
		m_LogWindow->Close();
		m_LogWindow = NULL;
	}


	// Uninit
	m_Mgr->UnInit();
}

// Post events

// Warning: This may cause an endless loop if the event is propagated back to its parent
void CFrame::PostEvent(wxCommandEvent& event)
{
	if (g_pCodeWindow &&
		event.GetId() >= IDM_INTERPRETER &&
		event.GetId() <= IDM_ADDRBOX)
	{
		event.StopPropagation();
		g_pCodeWindow->GetEventHandler()->AddPendingEvent(event);
	}
	else
	{
		event.Skip();
	}
}

void CFrame::OnMove(wxMoveEvent& event)
{
	event.Skip();

	if (!IsMaximized() &&
		!(SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderToMain && RendererIsFullscreen()))
	{
		SConfig::GetInstance().m_LocalCoreStartupParameter.iPosX = GetPosition().x;
		SConfig::GetInstance().m_LocalCoreStartupParameter.iPosY = GetPosition().y;
	}
}

void CFrame::OnResize(wxSizeEvent& event)
{
	event.Skip();

	if (!IsMaximized() &&
		!(SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderToMain && RendererIsFullscreen()) &&
		!(Core::GetState() != Core::CORE_UNINITIALIZED &&
			SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderToMain &&
			SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderWindowAutoSize))
	{
		SConfig::GetInstance().m_LocalCoreStartupParameter.iWidth = GetSize().GetWidth();
		SConfig::GetInstance().m_LocalCoreStartupParameter.iHeight = GetSize().GetHeight();
	}

	// Make sure the logger pane is a sane size
	if (!g_pCodeWindow && m_LogWindow && m_Mgr->GetPane(_T("Pane 1")).IsShown() &&
			!m_Mgr->GetPane(_T("Pane 1")).IsFloating() &&
			(m_LogWindow->x > GetClientRect().GetWidth() ||
			 m_LogWindow->y > GetClientRect().GetHeight()))
		ShowResizePane();
}

// Host messages

#ifdef _WIN32
WXLRESULT CFrame::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
	if (WM_SYSCOMMAND == nMsg && (SC_SCREENSAVE == wParam || SC_MONITORPOWER == wParam))
		return 0;
	else
		return wxFrame::MSWWindowProc(nMsg, wParam, lParam);
}
#endif

void CFrame::OnHostMessage(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case IDM_UPDATEGUI:
		UpdateGUI();
		break;

	case IDM_UPDATESTATUSBAR:
		if (GetStatusBar() != NULL)
			GetStatusBar()->SetStatusText(event.GetString(), event.GetInt());
		break;

	case IDM_UPDATETITLE:
		if (m_RenderFrame != NULL)
			m_RenderFrame->SetTitle(event.GetString());
		break;

	case IDM_WINDOWSIZEREQUEST:
		{
			std::pair<int, int> *win_size = (std::pair<int, int> *)(event.GetClientData());
			OnRenderWindowSizeRequest(win_size->first, win_size->second);
			delete win_size;
		}
		break;

	case WM_USER_CREATE:
		if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor)
			m_RenderParent->SetCursor(wxCURSOR_BLANK);
		break;

#ifdef __WXGTK__
	case IDM_PANIC:
		{
			wxString caption = event.GetString().BeforeFirst(':');
			wxString text = event.GetString().AfterFirst(':');
			bPanicResult = (wxYES == wxMessageBox(text, 
						caption, event.GetInt() ? wxYES_NO : wxOK, wxGetActiveWindow()));
			panic_event.Set();
		}
		break;
#endif

	case WM_USER_STOP:
		DoStop();
		break;
	}
}

void CFrame::GetRenderWindowSize(int& x, int& y, int& width, int& height)
{
#ifdef __WXGTK__
	if (!wxIsMainThread())
		wxMutexGuiEnter();
#endif
	wxRect client_rect = m_RenderParent->GetClientRect();
	width = client_rect.width;
	height = client_rect.height;
	x = client_rect.x;
	y = client_rect.y;
#ifdef __WXGTK__
	if (!wxIsMainThread())
		wxMutexGuiLeave();
#endif
}

void CFrame::OnRenderWindowSizeRequest(int width, int height)
{
	if (Core::GetState() == Core::CORE_UNINITIALIZED ||
			!SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderWindowAutoSize || 
			RendererIsFullscreen() || m_RenderFrame->IsMaximized())
		return;

	int old_width, old_height, log_width = 0, log_height = 0;
	m_RenderFrame->GetClientSize(&old_width, &old_height);

	// Add space for the log/console/debugger window
	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderToMain &&
			(SConfig::GetInstance().m_InterfaceLogWindow ||
			 SConfig::GetInstance().m_InterfaceConsole ||
			 SConfig::GetInstance().m_InterfaceLogConfigWindow) &&
			!m_Mgr->GetPane(wxT("Pane 1")).IsFloating())
	{
		switch (m_Mgr->GetPane(wxT("Pane 1")).dock_direction)
		{
			case wxAUI_DOCK_LEFT:
			case wxAUI_DOCK_RIGHT:
				log_width = m_Mgr->GetPane(wxT("Pane 1")).rect.GetWidth();
				break;
			case wxAUI_DOCK_TOP:
			case wxAUI_DOCK_BOTTOM:
				log_height = m_Mgr->GetPane(wxT("Pane 1")).rect.GetHeight();
				break;
		}
	}

	if (old_width != width + log_width || old_height != height + log_height)
		m_RenderFrame->SetClientSize(width + log_width, height + log_height);
}

bool CFrame::RendererHasFocus()
{
	if (m_RenderParent == NULL)
		return false;
#ifdef _WIN32
	if (m_RenderParent->GetParent()->GetHWND() == GetForegroundWindow())
		return true;
#else
	if (wxWindow::FindFocus() == NULL)
		return false;
	// Why these different cases?
	if (m_RenderParent == wxWindow::FindFocus() ||
			m_RenderParent == wxWindow::FindFocus()->GetParent() ||
			m_RenderParent->GetParent() == wxWindow::FindFocus()->GetParent())
		return true;
#endif
	return false;
}

void CFrame::OnGameListCtrl_ItemActivated(wxListEvent& WXUNUSED (event))
{
	// Show all platforms and regions if...
	// 1. All platforms are set to hide
	// 2. All Regions are set to hide
	// Otherwise call BootGame to either...
	// 1. Boot the selected iso
	// 2. Boot the default or last loaded iso.
	// 3. Call BrowseForDirectory if the gamelist is empty
	if (!m_GameListCtrl->GetISO(0) &&
		!((SConfig::GetInstance().m_ListGC &&
		SConfig::GetInstance().m_ListWii &&
		SConfig::GetInstance().m_ListWad) &&
		(SConfig::GetInstance().m_ListJap &&
		SConfig::GetInstance().m_ListUsa  &&
		SConfig::GetInstance().m_ListPal  &&
		SConfig::GetInstance().m_ListFrance &&
		SConfig::GetInstance().m_ListItaly &&
		SConfig::GetInstance().m_ListKorea &&
		SConfig::GetInstance().m_ListTaiwan &&
		SConfig::GetInstance().m_ListUnknown)))
	{
		SConfig::GetInstance().m_ListGC		= SConfig::GetInstance().m_ListWii =
		SConfig::GetInstance().m_ListWad	= SConfig::GetInstance().m_ListJap =
		SConfig::GetInstance().m_ListUsa	= SConfig::GetInstance().m_ListPal =
		SConfig::GetInstance().m_ListFrance	= SConfig::GetInstance().m_ListItaly =
		SConfig::GetInstance().m_ListKorea	= SConfig::GetInstance().m_ListTaiwan =
		SConfig::GetInstance().m_ListUnknown= true;

		GetMenuBar()->FindItem(IDM_LISTGC)->Check(true);
		GetMenuBar()->FindItem(IDM_LISTWII)->Check(true);
		GetMenuBar()->FindItem(IDM_LISTWAD)->Check(true);
		GetMenuBar()->FindItem(IDM_LISTJAP)->Check(true);
		GetMenuBar()->FindItem(IDM_LISTUSA)->Check(true);
		GetMenuBar()->FindItem(IDM_LISTPAL)->Check(true);
		GetMenuBar()->FindItem(IDM_LISTFRANCE)->Check(true);
		GetMenuBar()->FindItem(IDM_LISTITALY)->Check(true);
		GetMenuBar()->FindItem(IDM_LISTKOREA)->Check(true);
		GetMenuBar()->FindItem(IDM_LISTTAIWAN)->Check(true);
		GetMenuBar()->FindItem(IDM_LIST_UNK)->Check(true);

		m_GameListCtrl->Update();
	}
	else if (!m_GameListCtrl->GetISO(0))
	{
		m_GameListCtrl->BrowseForDirectory();
	}
	else
	{
		// Game started by double click
		BootGame(std::string(""));
	}
}

bool IsHotkey(wxKeyEvent &event, int Id)
{
	return (event.GetKeyCode() != WXK_NONE &&
			event.GetKeyCode() == SConfig::GetInstance().m_LocalCoreStartupParameter.iHotkey[Id] &&
			event.GetModifiers() == SConfig::GetInstance().m_LocalCoreStartupParameter.iHotkeyModifier[Id]);
}

int GetCmdForHotkey(unsigned int key)
{
	switch (key)
	{
	case HK_OPEN: return wxID_OPEN;
	case HK_CHANGE_DISC: return IDM_CHANGEDISC;
	case HK_REFRESH_LIST: return wxID_REFRESH;
	case HK_PLAY_PAUSE: return IDM_PLAY;
	case HK_STOP: return IDM_STOP;
	case HK_RESET: return IDM_RESET;
	case HK_FRAME_ADVANCE: return IDM_FRAMESTEP;
	case HK_START_RECORDING: return IDM_RECORD;
	case HK_PLAY_RECORDING: return IDM_PLAYRECORD;
	case HK_EXPORT_RECORDING: return IDM_RECORDEXPORT;
	case HK_READ_ONLY_MODE: return IDM_RECORDREADONLY;
	case HK_FULLSCREEN: return IDM_TOGGLE_FULLSCREEN;
	case HK_SCREENSHOT: return IDM_SCREENSHOT;
	case HK_EXIT: return wxID_EXIT;

	case HK_WIIMOTE1_CONNECT: return IDM_CONNECT_WIIMOTE1;
	case HK_WIIMOTE2_CONNECT: return IDM_CONNECT_WIIMOTE2;
	case HK_WIIMOTE3_CONNECT: return IDM_CONNECT_WIIMOTE3;
	case HK_WIIMOTE4_CONNECT: return IDM_CONNECT_WIIMOTE4;

	case HK_LOAD_STATE_SLOT_1: return IDM_LOADSLOT1;
	case HK_LOAD_STATE_SLOT_2: return IDM_LOADSLOT2;
	case HK_LOAD_STATE_SLOT_3: return IDM_LOADSLOT3;
	case HK_LOAD_STATE_SLOT_4: return IDM_LOADSLOT4;
	case HK_LOAD_STATE_SLOT_5: return IDM_LOADSLOT5;
	case HK_LOAD_STATE_SLOT_6: return IDM_LOADSLOT6;
	case HK_LOAD_STATE_SLOT_7: return IDM_LOADSLOT7;
	case HK_LOAD_STATE_SLOT_8: return IDM_LOADSLOT8;

	case HK_SAVE_STATE_SLOT_1: return IDM_SAVESLOT1;
	case HK_SAVE_STATE_SLOT_2: return IDM_SAVESLOT2;
	case HK_SAVE_STATE_SLOT_3: return IDM_SAVESLOT3;
	case HK_SAVE_STATE_SLOT_4: return IDM_SAVESLOT4;
	case HK_SAVE_STATE_SLOT_5: return IDM_SAVESLOT5;
	case HK_SAVE_STATE_SLOT_6: return IDM_SAVESLOT6;
	case HK_SAVE_STATE_SLOT_7: return IDM_SAVESLOT7;
	case HK_SAVE_STATE_SLOT_8: return IDM_SAVESLOT8;

	case HK_LOAD_LAST_STATE_1: return IDM_LOADLAST1;
	case HK_LOAD_LAST_STATE_2: return IDM_LOADLAST2;
	case HK_LOAD_LAST_STATE_3: return IDM_LOADLAST3;
	case HK_LOAD_LAST_STATE_4: return IDM_LOADLAST4;
	case HK_LOAD_LAST_STATE_5: return IDM_LOADLAST5;
	case HK_LOAD_LAST_STATE_6: return IDM_LOADLAST6;
	case HK_LOAD_LAST_STATE_7: return IDM_LOADLAST7;
	case HK_LOAD_LAST_STATE_8: return IDM_LOADLAST8;

	case HK_SAVE_FIRST_STATE: return IDM_SAVEFIRSTSTATE;
	case HK_UNDO_LOAD_STATE: return IDM_UNDOLOADSTATE;
	case HK_UNDO_SAVE_STATE: return IDM_UNDOSAVESTATE;
	}

	return -1;
}

void OnAfterLoadCallback()
{
	// warning: this gets called from the CPU thread, so we should only queue things to do on the proper thread
	if(main_frame)
	{
		wxCommandEvent event(wxEVT_HOST_COMMAND, IDM_UPDATEGUI);
		main_frame->GetEventHandler()->AddPendingEvent(event);
	}
}

void TASManipFunction(SPADStatus *PadStatus, int controllerID)
{
	if (main_frame)
		main_frame->g_TASInputDlg->GetValues(PadStatus, controllerID);
}

void CFrame::OnKeyDown(wxKeyEvent& event)
{
	if(Core::GetState() != Core::CORE_UNINITIALIZED &&
			(RendererHasFocus() || g_TASInputDlg->HasFocus()))
	{
		int WiimoteId = -1;
		// Toggle fullscreen
		if (IsHotkey(event, HK_FULLSCREEN))
			DoFullscreen(!RendererIsFullscreen());
		// Send Debugger keys to CodeWindow
		else if (g_pCodeWindow && (event.GetKeyCode() >= WXK_F9 && event.GetKeyCode() <= WXK_F11))
			event.Skip();
		// Pause and Unpause
		else if (IsHotkey(event, HK_PLAY_PAUSE))
			DoPause();
		// Stop
		else if (IsHotkey(event, HK_STOP))
			DoStop();
		// Screenshot hotkey
		else if (IsHotkey(event, HK_SCREENSHOT))
			Core::SaveScreenShot();
		else if (IsHotkey(event, HK_EXIT))
			wxPostEvent(this, wxCommandEvent(wxID_EXIT));
		// Wiimote connect and disconnect hotkeys
		else if (IsHotkey(event, HK_WIIMOTE1_CONNECT))
			WiimoteId = 0;
		else if (IsHotkey(event, HK_WIIMOTE2_CONNECT))
			WiimoteId = 1;
		else if (IsHotkey(event, HK_WIIMOTE3_CONNECT))
			WiimoteId = 2;
		else if (IsHotkey(event, HK_WIIMOTE4_CONNECT))
			WiimoteId = 3;
		else
		{
			unsigned int i = NUM_HOTKEYS;
			if (!SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderToMain || g_TASInputDlg->HasFocus())
			{
				for (i = 0; i < NUM_HOTKEYS; i++)
				{
					if (IsHotkey(event, i))
					{
						int cmd = GetCmdForHotkey(i);
						if (cmd >= 0)
						{ 
							wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, cmd);
							wxMenuItem *item = GetMenuBar()->FindItem(cmd);
							if (item && item->IsCheckable())
							{
								item->wxMenuItemBase::Toggle();
								evt.SetInt(item->IsChecked());
							}
							GetEventHandler()->AddPendingEvent(evt);
							break;
						}
					}
				}
			}
			// On OS X, we claim all keyboard events while
			// emulation is running to avoid wxWidgets sounding
			// the system beep for unhandled key events when
			// receiving pad/wiimote keypresses which take an
			// entirely different path through the HID subsystem.
#ifndef __APPLE__
			// On other platforms, we leave the key event alone
			// so it can be passed on to the windowing system.
			if (i == NUM_HOTKEYS)
				event.Skip();
#endif
		}

		// Actually perform the wiimote connection or disconnection
		if (WiimoteId >= 0)
		{
			bool connect = !GetMenuBar()->IsChecked(IDM_CONNECT_WIIMOTE1 + WiimoteId);
			ConnectWiimote(WiimoteId, connect);
		}

		// Send the OSD hotkeys to the video backend
		if (event.GetKeyCode() >= '3' && event.GetKeyCode() <= '7' && event.GetModifiers() == wxMOD_NONE)
		{
#ifdef _WIN32
			PostMessage((HWND)Core::GetWindowHandle(), WM_USER, WM_USER_KEYDOWN, event.GetKeyCode());
#elif defined(HAVE_X11) && HAVE_X11
			X11Utils::SendKeyEvent(X11Utils::XDisplayFromHandle(GetHandle()), event.GetKeyCode());
#endif
		}
		// Send the freelook hotkeys to the video backend
		if ((event.GetKeyCode() == ')' || event.GetKeyCode() == '(' ||
					event.GetKeyCode() == '0' || event.GetKeyCode() == '9' ||
					event.GetKeyCode() == 'W' || event.GetKeyCode() == 'S' ||
					event.GetKeyCode() == 'A' || event.GetKeyCode() == 'D' ||
					event.GetKeyCode() == 'R')
				&& event.GetModifiers() == wxMOD_SHIFT)
		{
#ifdef _WIN32
			PostMessage((HWND)Core::GetWindowHandle(), WM_USER, WM_USER_KEYDOWN, event.GetKeyCode());
#elif defined(HAVE_X11) && HAVE_X11
			X11Utils::SendKeyEvent(X11Utils::XDisplayFromHandle(GetHandle()), event.GetKeyCode());
#endif
		}
	}
	else
	{
		event.Skip();
	}
}

void CFrame::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void CFrame::OnMouse(wxMouseEvent& event)
{
#if defined(HAVE_X11) && HAVE_X11
	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		if(event.Dragging())
			X11Utils::SendMotionEvent(X11Utils::XDisplayFromHandle(GetHandle()),
					event.GetPosition().x, event.GetPosition().y);
		else
			X11Utils::SendButtonEvent(X11Utils::XDisplayFromHandle(GetHandle()), event.GetButton(),
					event.GetPosition().x, event.GetPosition().y, event.ButtonDown());
	}
#endif
	event.Skip();
}

void CFrame::DoFullscreen(bool bF)
{
	ToggleDisplayMode(bF);

#if defined(__APPLE__)
	NSView *view = (NSView *) m_RenderFrame->GetHandle();
	NSWindow *window = [view window];

	if (bF != RendererIsFullscreen())
	{
		[window toggleFullScreen:nil];
	}
#else
	m_RenderFrame->ShowFullScreen(bF, wxFULLSCREEN_ALL);
#endif

	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderToMain)
	{
		if (bF)
		{
			// Save the current mode before going to fullscreen
			AuiCurrent = m_Mgr->SavePerspective();
			m_Mgr->LoadPerspective(AuiFullscreen, true);
		}
		else
		{
			// Restore saved perspective
			m_Mgr->LoadPerspective(AuiCurrent, true);
		}
	}
	else
	{
		m_RenderFrame->Raise();
	}
}

const CGameListCtrl *CFrame::GetGameListCtrl() const
{
	return m_GameListCtrl;
}
