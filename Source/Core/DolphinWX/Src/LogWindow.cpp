// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "LogWindow.h"
#include "ConsoleListener.h"
#include "Console.h"
#include "IniFile.h"
#include "FileUtil.h"
#include "Debugger/DebuggerUIUtil.h"
#include "WxUtils.h"

#include <wx/fontmap.h>

// Milliseconds between msgQueue flushes to wxTextCtrl
#define UPDATETIME 200

BEGIN_EVENT_TABLE(CLogWindow, wxPanel)
	EVT_CLOSE(CLogWindow::OnClose)
	EVT_TEXT_ENTER(IDM_SUBMITCMD, CLogWindow::OnSubmit)
	EVT_BUTTON(IDM_CLEARLOG, CLogWindow::OnClear)
	EVT_CHOICE(IDM_FONT, CLogWindow::OnFontChange)
	EVT_CHECKBOX(IDM_WRAPLINE, CLogWindow::OnWrapLineCheck)
	EVT_TIMER(IDTM_UPDATELOG, CLogWindow::OnLogTimer)
END_EVENT_TABLE()

CLogWindow::CLogWindow(CFrame *parent, wxWindowID id, const wxPoint& pos,
		const wxSize& size, long style, const wxString& name)
	: wxPanel(parent, id, pos, size, style, name)
	, x(0), y(0), winpos(0)
	, Parent(parent), m_ignoreLogTimer(false), m_LogAccess(true)
	, m_Log(NULL), m_cmdline(NULL), m_FontChoice(NULL)
{
	m_LogManager = LogManager::GetInstance();

	CreateGUIControls();

	m_LogTimer = new wxTimer(this, IDTM_UPDATELOG);
	m_LogTimer->Start(UPDATETIME);
}

void CLogWindow::CreateGUIControls()
{
	IniFile ini;
	ini.Load(File::GetUserPath(F_LOGGERCONFIG_IDX));

	ini.Get("LogWindow", "x", &x, Parent->GetSize().GetX() / 2);
	ini.Get("LogWindow", "y", &y, Parent->GetSize().GetY());
	ini.Get("LogWindow", "pos", &winpos, wxAUI_DOCK_RIGHT);

	// Set up log listeners
	int verbosity;
	ini.Get("Options", "Verbosity", &verbosity, 0);
	
	// Ensure the verbosity level is valid
	if (verbosity < 0)
		verbosity = 0;
	if (verbosity > MAX_LOGLEVEL)
		verbosity = MAX_LOGLEVEL;

	// Get the logger output settings from the config ini file.
	ini.Get("Options", "WriteToFile", &m_writeFile, false);
	ini.Get("Options", "WriteToConsole", &m_writeConsole, true);
	ini.Get("Options", "WriteToWindow", &m_writeWindow, true);
#ifdef _MSC_VER
	if (IsDebuggerPresent())
	{
		ini.Get("Options", "WriteToDebugger", &m_writeDebugger, true);
	}
	else
#endif
	{
		m_writeDebugger = false;
	}

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
	{
		bool enable;
		ini.Get("Logs", m_LogManager->GetShortName((LogTypes::LOG_TYPE)i), &enable, true);

		if (m_writeWindow && enable)
			m_LogManager->AddListener((LogTypes::LOG_TYPE)i, this);
		else
			m_LogManager->RemoveListener((LogTypes::LOG_TYPE)i, this);

		if (m_writeFile && enable)
			m_LogManager->AddListener((LogTypes::LOG_TYPE)i, m_LogManager->GetFileListener());
		else
			m_LogManager->RemoveListener((LogTypes::LOG_TYPE)i, m_LogManager->GetFileListener());

		if (m_writeConsole && enable)
			m_LogManager->AddListener((LogTypes::LOG_TYPE)i, m_LogManager->GetConsoleListener());
		else
			m_LogManager->RemoveListener((LogTypes::LOG_TYPE)i, m_LogManager->GetConsoleListener());

		if (m_writeDebugger && enable)
			m_LogManager->AddListener((LogTypes::LOG_TYPE)i, m_LogManager->GetDebuggerListener());
		else
			m_LogManager->RemoveListener((LogTypes::LOG_TYPE)i, m_LogManager->GetDebuggerListener());

		m_LogManager->SetLogLevel((LogTypes::LOG_TYPE)i, (LogTypes::LOG_LEVELS)(verbosity));
	}

	// Font
	m_FontChoice = new wxChoice(this, IDM_FONT,
			wxDefaultPosition, wxDefaultSize, 0, NULL, 0, wxDefaultValidator);
	m_FontChoice->Append(_("Default font"));
	m_FontChoice->Append(_("Monospaced font"));
	m_FontChoice->Append(_("Selected font"));

	DefaultFont = GetFont();
	MonoSpaceFont.SetNativeFontInfoUserDesc(_T("lucida console windows-1252"));
	LogFont.push_back(DefaultFont);
	LogFont.push_back(MonoSpaceFont);
	LogFont.push_back(DebuggerFont);

	int font;
	ini.Get("Options", "Font", &font, 0);
	m_FontChoice->SetSelection(font);

	// Word wrap
	bool wrap_lines;
	ini.Get("Options", "WrapLines", &wrap_lines, false);
	m_WrapLine = new wxCheckBox(this, IDM_WRAPLINE, _("Word Wrap"));
	m_WrapLine->SetValue(wrap_lines);

	// Log viewer
	m_Log = CreateTextCtrl(this, IDM_LOG, wxTE_RICH | wxTE_MULTILINE | wxTE_READONLY |
			(wrap_lines ? wxTE_WORDWRAP : wxTE_DONTWRAP));

	// submit row
	m_cmdline = new wxTextCtrl(this, IDM_SUBMITCMD, wxEmptyString, wxDefaultPosition, wxDefaultSize,
			wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB);

	// Sizers
	wxBoxSizer *sTop = new wxBoxSizer(wxHORIZONTAL);
	sTop->Add(new wxButton(this, IDM_CLEARLOG, _("Clear"),
				wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT));
	sTop->Add(m_FontChoice, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 3);
	sTop->Add(m_WrapLine, 0, wxALIGN_CENTER_VERTICAL);

	sBottom = new wxBoxSizer(wxVERTICAL);
	PopulateBottom();

	wxBoxSizer *sMain = new wxBoxSizer(wxVERTICAL);
	sMain->Add(sTop, 0, wxEXPAND);
	sMain->Add(sBottom, 1, wxEXPAND);
	SetSizer(sMain);

	m_cmdline->SetFocus();
}

CLogWindow::~CLogWindow()
{
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
	{
		m_LogManager->RemoveListener((LogTypes::LOG_TYPE)i, this);
	}
	m_LogTimer->Stop();
	delete m_LogTimer;
}

void CLogWindow::OnClose(wxCloseEvent& event)
{
	SaveSettings();
	event.Skip();
}

void CLogWindow::SaveSettings()
{
	IniFile ini;
	ini.Load(File::GetUserPath(F_LOGGERCONFIG_IDX));

	if (!Parent->g_pCodeWindow)
	{
		ini.Set("LogWindow", "x", x);
		ini.Set("LogWindow", "y", y);
		ini.Set("LogWindow", "pos", winpos);
	}
	ini.Set("Options", "Font", m_FontChoice->GetSelection());
	ini.Set("Options", "WrapLines", m_WrapLine->IsChecked());
	ini.Save(File::GetUserPath(F_LOGGERCONFIG_IDX));
}

void CLogWindow::OnSubmit(wxCommandEvent& WXUNUSED (event))
{
	if (!m_cmdline) return;
	Console_Submit(WxStrToStr(m_cmdline->GetValue()).c_str());
	m_cmdline->SetValue(wxEmptyString);
}

void CLogWindow::OnClear(wxCommandEvent& WXUNUSED (event))
{
	m_Log->Clear();

	{
	std::lock_guard<std::mutex> lk(m_LogSection);
	int msgQueueSize = (int)msgQueue.size();
	for (int i = 0; i < msgQueueSize; i++)
		msgQueue.pop();
	}

	m_LogManager->GetConsoleListener()->ClearScreen();
}

void CLogWindow::UnPopulateBottom()
{
	sBottom->Detach(m_Log);
	sBottom->Detach(m_cmdline);
}

void CLogWindow::PopulateBottom()
{
	sBottom->Add(m_Log, 1, wxEXPAND | wxSHRINK);
	sBottom->Add(m_cmdline, 0, wxEXPAND);
	Layout();
}

wxTextCtrl* CLogWindow::CreateTextCtrl(wxPanel* parent, wxWindowID id, long Style)
{
	wxTextCtrl* TC = new wxTextCtrl(parent, id, wxEmptyString, wxDefaultPosition, wxDefaultSize, Style);
#ifdef __APPLE__
	TC->SetBackgroundColour(*wxLIGHT_GREY);
#else
	TC->SetBackgroundColour(*wxBLACK);
#endif
	if (m_FontChoice && m_FontChoice->GetSelection() < (int)LogFont.size() && m_FontChoice->GetSelection() >= 0)
		TC->SetDefaultStyle(wxTextAttr(wxNullColour, wxNullColour, LogFont[m_FontChoice->GetSelection()]));

	return TC;
}

void CLogWindow::OnFontChange(wxCommandEvent& event)
{
	// Update selected font
	LogFont[LogFont.size()-1] = DebuggerFont;
	m_Log->SetStyle(0, m_Log->GetLastPosition(),
			wxTextAttr(wxNullColour, wxNullColour, LogFont[event.GetSelection()]));
	m_Log->SetDefaultStyle(wxTextAttr(wxNullColour, wxNullColour, LogFont[event.GetSelection()]));

	SaveSettings();
}

void CLogWindow::OnWrapLineCheck(wxCommandEvent& event)
{
#ifdef __WXGTK__
	// Clear the old word wrap state and set the new
	m_Log->SetWindowStyleFlag(m_Log->GetWindowStyleFlag() ^ (wxTE_WORDWRAP | wxTE_DONTWRAP));
#else
	wxString Text;
	// Unfortunately wrapping styles can only be changed dynamically with wxGTK
	// Notice:	To retain the colors when changing word wrapping we need to
	//			loop through every letter with GetStyle and then reapply them letter by letter
	// Prevent m_Log access while it's being destroyed
	m_LogAccess = false;
	UnPopulateBottom();
	Text = m_Log->GetValue();
	m_Log->Destroy();
	if (event.IsChecked())
		m_Log = CreateTextCtrl(this, IDM_LOG,
				wxTE_RICH | wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
	else
		m_Log = CreateTextCtrl(this, IDM_LOG,
				wxTE_RICH | wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	m_Log->SetDefaultStyle(wxTextAttr(*wxWHITE));
	m_Log->AppendText(Text);
	PopulateBottom();
	m_LogAccess = true;
#endif
	SaveSettings();
}

void CLogWindow::OnLogTimer(wxTimerEvent& WXUNUSED(event))
{
	if (!m_LogAccess) return;
	if (m_ignoreLogTimer) return;

	UpdateLog();
	// Scroll to the last line
	if (msgQueue.size() > 0)
	{
		m_Log->ScrollLines(1);
		m_Log->ShowPosition( m_Log->GetLastPosition() );
	}
}

void CLogWindow::UpdateLog()
{
	if (!m_LogAccess) return;
	if (!m_Log) return;

	// m_LogTimer->Stop();
	// instead of stopping the timer, let's simply ignore its calls during UpdateLog,
	// because repeatedly stopping and starting a timer churns memory (and potentially leaks it).
	m_ignoreLogTimer = true;

	if (!msgQueue.empty())
	{
		std::lock_guard<std::mutex> lk(m_LogSection);
		int msgQueueSize = (int)msgQueue.size();
		for (int i = 0; i < msgQueueSize; i++)
		{
			switch (msgQueue.front().first)
			{
				case LogTypes::ERROR_:
					m_Log->SetDefaultStyle(wxTextAttr(*wxRED));
					break;

				case LogTypes::WARNING:
					m_Log->SetDefaultStyle(wxTextAttr(*wxYELLOW));
					break;

				case LogTypes::NOTICE:
					m_Log->SetDefaultStyle(wxTextAttr(*wxGREEN));
					break;

				case LogTypes::INFO:
					m_Log->SetDefaultStyle(wxTextAttr(*wxCYAN));
					break;

				case LogTypes::DEBUG:
					m_Log->SetDefaultStyle(wxTextAttr(*wxLIGHT_GREY));
					break;

				case LogTypes::BLUE:
					m_Log->SetDefaultStyle(wxTextAttr(wxColour(128, 128, 255)));
					break;

				case LogTypes::CYAN:
					m_Log->SetDefaultStyle(wxTextAttr(*wxCYAN));
					break;

				case LogTypes::MAGENTA:
					m_Log->SetDefaultStyle(wxTextAttr(wxColour(255, 0, 255)));
					break;

				case LogTypes::GREY:
					m_Log->SetDefaultStyle(wxTextAttr(*wxLIGHT_GREY));
					break;

				case LogTypes::WHITE:
				default:
					m_Log->SetDefaultStyle(wxTextAttr(*wxWHITE));
					break;
			}
			if (msgQueue.front().second.size())
			{
				int j = m_Log->GetLastPosition();
				m_Log->AppendText(msgQueue.front().second);
				// White timestamp
				m_Log->SetStyle(j, j + 9, wxTextAttr(*wxWHITE));
			}
			msgQueue.pop();
		}
	}	// unlock log

	// m_LogTimer->Start(UPDATETIME);
	m_ignoreLogTimer = false;
}

void CLogWindow::Log(LogTypes::LOG_LEVELS level, const char *text)
{
	std::lock_guard<std::mutex> lk(m_LogSection);

	if (msgQueue.size() >= 100)
		msgQueue.pop();

	msgQueue.push(std::make_pair(u8(level), StrToWxStr(text)));
}
