// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <wx/notebook.h>

#include "HotkeyDlg.h"
#include "ConfigManager.h"

BEGIN_EVENT_TABLE(HotkeyConfigDialog,wxDialog)
	EVT_COMMAND_RANGE(0, NUM_HOTKEYS - 1,
			wxEVT_COMMAND_BUTTON_CLICKED, HotkeyConfigDialog::OnButtonClick)
	EVT_TIMER(wxID_ANY, HotkeyConfigDialog::OnButtonTimer)
END_EVENT_TABLE()

HotkeyConfigDialog::HotkeyConfigDialog(wxWindow *parent, wxWindowID id, const wxString &title,
		const wxPoint &position, const wxSize& size, long style)
: wxDialog(parent, id, title, position, size, style)
{
	CreateHotkeyGUIControls();

#if wxUSE_TIMER
	m_ButtonMappingTimer = new wxTimer(this, wxID_ANY);
	g_Pressed = 0;
	g_Modkey = 0;
	ClickedButton = NULL;
	GetButtonWaitingID = 0;
	GetButtonWaitingTimer = 0;
#endif
}

HotkeyConfigDialog::~HotkeyConfigDialog()
{
	delete m_ButtonMappingTimer;
}

// Save keyboard key mapping
void HotkeyConfigDialog::SaveButtonMapping(int Id, int Key, int Modkey)
{
	SConfig::GetInstance().m_LocalCoreStartupParameter.iHotkey[Id] = Key;
	SConfig::GetInstance().m_LocalCoreStartupParameter.iHotkeyModifier[Id] = Modkey;
}

void HotkeyConfigDialog::EndGetButtons(void)
{
	wxTheApp->Unbind(wxEVT_KEY_DOWN, &HotkeyConfigDialog::OnKeyDown, this);
	m_ButtonMappingTimer->Stop();
	GetButtonWaitingTimer = 0;
	GetButtonWaitingID = 0;
	ClickedButton = NULL;
	SetEscapeId(wxID_ANY);
}

void HotkeyConfigDialog::OnKeyDown(wxKeyEvent& event)
{
	if(ClickedButton != NULL)
	{
		// Save the key
		g_Pressed = event.GetKeyCode();
		g_Modkey = event.GetModifiers();

		// Don't allow modifier keys
		if (g_Pressed == WXK_CONTROL || g_Pressed == WXK_ALT ||
			g_Pressed == WXK_SHIFT || g_Pressed == WXK_COMMAND)
			return;

		// Use the space key to set a blank key
		if (g_Pressed == WXK_SPACE)
		{
			SaveButtonMapping(ClickedButton->GetId(), -1, 0);
			SetButtonText(ClickedButton->GetId(), wxString());
		}
		else
		{
			SetButtonText(ClickedButton->GetId(),
					InputCommon::WXKeyToString(g_Pressed),
					InputCommon::WXKeymodToString(g_Modkey));
			SaveButtonMapping(ClickedButton->GetId(), g_Pressed, g_Modkey);
		}
		EndGetButtons();
	}
}

// Update the textbox for the buttons
void HotkeyConfigDialog::SetButtonText(int id, const wxString &keystr, const wxString &modkeystr)
{
	m_Button_Hotkeys[id]->SetLabel(modkeystr + keystr);
}

void HotkeyConfigDialog::DoGetButtons(int _GetId)
{
	// Values used in this function
	const int Seconds = 4; // Seconds to wait for
	const int TimesPerSecond = 40; // How often to run the check

	// If the Id has changed or the timer is not running we should start one
	if( GetButtonWaitingID != _GetId || !m_ButtonMappingTimer->IsRunning() )
	{
		if(m_ButtonMappingTimer->IsRunning())
			m_ButtonMappingTimer->Stop();

		// Save the button Id
		GetButtonWaitingID = _GetId;
		GetButtonWaitingTimer = 0;

		// Start the timer
		#if wxUSE_TIMER
		m_ButtonMappingTimer->Start(1000 / TimesPerSecond);
		#endif
	}

	// Process results
	// Count each time
	GetButtonWaitingTimer++;

	// This is run every second
	if (GetButtonWaitingTimer % TimesPerSecond == 0)
	{
		// Current time
		int TmpTime = Seconds - (GetButtonWaitingTimer / TimesPerSecond);
		// Update text
		SetButtonText(_GetId, wxString::Format(wxT("[ %d ]"), TmpTime));
	}

	// Time's up
	if (GetButtonWaitingTimer / TimesPerSecond >= Seconds)
	{
		// Revert back to old label
		SetButtonText(_GetId, OldLabel);
		EndGetButtons();
	}
}

// Input button clicked
void HotkeyConfigDialog::OnButtonClick(wxCommandEvent& event)
{
	event.Skip();

	if (m_ButtonMappingTimer->IsRunning())
		return;

	wxTheApp->Bind(wxEVT_KEY_DOWN, &HotkeyConfigDialog::OnKeyDown, this);

	// Get the button
	ClickedButton = (wxButton *)event.GetEventObject();
	SetEscapeId(wxID_CANCEL);
	// Save old label so we can revert back
	OldLabel = ClickedButton->GetLabel();
	ClickedButton->SetWindowStyle(wxWANTS_CHARS);
	ClickedButton->SetLabel(_("<Press Key>"));
	DoGetButtons(ClickedButton->GetId());
}

#define HOTKEY_NUM_COLUMNS 2

void HotkeyConfigDialog::CreateHotkeyGUIControls(void)
{
	const wxString pageNames[] =
	{
		_("General"),
		_("State Saves")
	};

	const wxString hkText[] =
	{
		_("Open"),
		_("Change Disc"),
		_("Refresh List"),

		_("Play/Pause"),
		_("Stop"),
		_("Reset"),
		_("Frame Advance"),

		_("Start Recording"),
		_("Play Recording"),
		_("Export Recording"),
		_("Read-only mode"),

		_("Toggle Fullscreen"),
		_("Take Screenshot"),
		_("Exit"),

		_("Connect Wiimote 1"),
		_("Connect Wiimote 2"),
		_("Connect Wiimote 3"),
		_("Connect Wiimote 4"),

		_("Load State Slot 1"),
		_("Load State Slot 2"),
		_("Load State Slot 3"),
		_("Load State Slot 4"),
		_("Load State Slot 5"),
		_("Load State Slot 6"),
		_("Load State Slot 7"),
		_("Load State Slot 8"),

		_("Save State Slot 1"),
		_("Save State Slot 2"),
		_("Save State Slot 3"),
		_("Save State Slot 4"),
		_("Save State Slot 5"),
		_("Save State Slot 6"),
		_("Save State Slot 7"),
		_("Save State Slot 8"),

		_("Load State Last 1"),
		_("Load State Last 2"),
		_("Load State Last 3"),
		_("Load State Last 4"),
		_("Load State Last 5"),
		_("Load State Last 6"),
		_("Load State Last 7"),
		_("Load State Last 8"),

		_("Save Oldest State"),
		_("Undo Load State"),
		_("Undo Save State")
	};

	const int page_breaks[3] = {HK_OPEN, HK_LOAD_STATE_SLOT_1, NUM_HOTKEYS};

	// Configuration controls sizes
	wxSize size(100,20);
	// A small type font
	wxFont m_SmallFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

	wxNotebook *Notebook = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

	for (int j = 0; j < 2; j++)
	{
		wxPanel *Page = new wxPanel(Notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize);
		Notebook->AddPage(Page, pageNames[j]);

		wxGridBagSizer *sHotkeys = new wxGridBagSizer();

		// Header line
		for (int i = 0; i < HOTKEY_NUM_COLUMNS; i++)
		{
			wxBoxSizer *HeaderSizer = new wxBoxSizer(wxHORIZONTAL);
			wxStaticText *StaticTextHeader = new wxStaticText(Page, wxID_ANY, _("Action"));
			HeaderSizer->Add(StaticTextHeader, 1, wxALL, 2);
			StaticTextHeader = new wxStaticText(Page, wxID_ANY, _("Key"), wxDefaultPosition, size);
			HeaderSizer->Add(StaticTextHeader, 0, wxALL, 2);	
			sHotkeys->Add(HeaderSizer, wxGBPosition(0, i), wxDefaultSpan, wxEXPAND | wxLEFT, (i > 0) ? 30 : 1);
		}

		int column_break = (page_breaks[j+1] + page_breaks[j] + 1) / 2;
		
		for (int i = page_breaks[j]; i < page_breaks[j+1]; i++)
		{
			// Text for the action
			wxStaticText *stHotkeys = new wxStaticText(Page, wxID_ANY, hkText[i]);

			// Key selection button
			m_Button_Hotkeys[i] = new wxButton(Page, i, wxEmptyString,
					wxDefaultPosition, size);
			m_Button_Hotkeys[i]->SetFont(m_SmallFont);
			m_Button_Hotkeys[i]->SetToolTip(_("Left click to detect hotkeys.\nEnter space to clear."));
			SetButtonText(i,
					InputCommon::WXKeyToString(SConfig::GetInstance().m_LocalCoreStartupParameter.iHotkey[i]),
					InputCommon::WXKeymodToString(
						SConfig::GetInstance().m_LocalCoreStartupParameter.iHotkeyModifier[i]));

			wxBoxSizer *sHotkey = new wxBoxSizer(wxHORIZONTAL);
			sHotkey->Add(stHotkeys, 1, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxALL, 2);
			sHotkey->Add(m_Button_Hotkeys[i], 0, wxALL, 2);
			sHotkeys->Add(sHotkey,
					wxGBPosition((i < column_break) ? i - page_breaks[j] + 1 : i - column_break + 1,
						(i < column_break) ? 0 : 1),
					wxDefaultSpan, wxEXPAND | wxLEFT, (i < column_break) ? 1 : 30);
		}

		wxStaticBoxSizer *sHotkeyBox = new wxStaticBoxSizer(wxVERTICAL, Page, _("Hotkeys"));
		sHotkeyBox->Add(sHotkeys);

		wxBoxSizer* const sPage = new wxBoxSizer(wxVERTICAL);
		sPage->Add(sHotkeyBox, 0, wxEXPAND | wxALL, 5);
		Page->SetSizer(sPage);
	}

	wxBoxSizer *sMainSizer = new wxBoxSizer(wxVERTICAL);
	sMainSizer->Add(Notebook, 0, wxEXPAND | wxALL, 5);
	sMainSizer->Add(CreateButtonSizer(wxOK), 0, wxEXPAND | wxLEFT | wxRIGHT | wxDOWN, 5);
	SetSizerAndFit(sMainSizer);
	SetFocus();
}

