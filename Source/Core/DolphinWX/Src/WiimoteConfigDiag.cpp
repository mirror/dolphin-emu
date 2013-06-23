
#include "WiimoteConfigDiag.h"
#include "HW/Wiimote.h"
#include "HW/WiimoteReal/WiimoteReal.h"
#include "Frame.h"
#include "WxUtils.h"

WiimoteConfigDiag::WiimoteConfigDiag(wxWindow* parent, InputPlugin& plugin, const wxString& title)
	: wxDialog(NULL, -1, _("Dolphin Wiimote Configuration"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxMINIMIZE_BOX|wxDIALOG_NO_PARENT)
	, m_parent(parent)
	, m_emu_config_diag(NULL)
	, m_plugin(plugin)
{
	WiimoteReal::LoadSettings();

	wxBoxSizer* const main_sizer = new wxBoxSizer(wxVERTICAL);


	// "Wiimotes" controls
	wxStaticText* wiimote_label[4];
	wxChoice* wiimote_source_ch[4];

	for (unsigned int i = 0; i < MAX_WIIMOTES; ++i)
	{
		wxString str;
		str.Printf(_("Wiimote %i"), i + 1);

		const wxString src_choices[] = { _("None"),
		_("Emulated Wiimote"), _("Real Wiimote"), _("Hybrid Wiimote") };

		// reserve four ids, so that we can calculate the index from the ids later on
		// Stupid wx 2.8 doesn't support reserving sequential IDs, so we need to do that more complicated..
		int source_ctrl_id =  wxWindow::NewControlId();
		m_wiimote_index_from_ctrl_id.insert(std::pair<wxWindowID, unsigned int>(source_ctrl_id, i));

		int config_bt_id = wxWindow::NewControlId();
		m_wiimote_index_from_conf_bt_id.insert(std::pair<wxWindowID, unsigned int>(config_bt_id, i));

		wiimote_label[i] = new wxStaticText(this, wxID_ANY, str);
		wiimote_source_ch[i] = new wxChoice(this, source_ctrl_id, wxDefaultPosition, wxDefaultSize, sizeof(src_choices)/sizeof(*src_choices), src_choices);
		wiimote_source_ch[i]->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &WiimoteConfigDiag::SelectSource, this);
		wiimote_configure_bt[i] = new wxButton(this, config_bt_id, _("Configure"));
		wiimote_configure_bt[i]->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &WiimoteConfigDiag::ConfigEmulatedWiimote, this);

		m_orig_wiimote_sources[i] = g_wiimote_sources[i];
		wiimote_source_ch[i]->Select(m_orig_wiimote_sources[i]);
		if (m_orig_wiimote_sources[i] != WIIMOTE_SRC_EMU && m_orig_wiimote_sources[i] != WIIMOTE_SRC_HYBRID)
			wiimote_configure_bt[i]->Disable();
	}

	// "Wiimotes" layout
	wxStaticBoxSizer* const wiimote_group = new wxStaticBoxSizer(wxHORIZONTAL, this, _("Wiimotes"));
	wxFlexGridSizer* const wiimote_sizer = new wxFlexGridSizer(3, 5, 5);
	for (unsigned int i = 0; i < 4; ++i)
	{
		wiimote_sizer->Add(wiimote_label[i], 0, wxALIGN_CENTER_VERTICAL);
		wiimote_sizer->Add(wiimote_source_ch[i], 0, wxALIGN_CENTER_VERTICAL);
		wiimote_sizer->Add(wiimote_configure_bt[i]);
	}
	wiimote_group->Add(wiimote_sizer, 1, wxEXPAND, 5 );
	
	
	// "BalanceBoard" layout
	wxStaticBoxSizer* const bb_group = new wxStaticBoxSizer(wxHORIZONTAL, this, _("Balance Board"));
	wxFlexGridSizer* const bb_sizer = new wxFlexGridSizer(1, 5, 5);
	int source_ctrl_id =  wxWindow::NewControlId();
	m_wiimote_index_from_ctrl_id.insert(std::pair<wxWindowID, unsigned int>(source_ctrl_id, WIIMOTE_BALANCE_BOARD));
	const wxString src_choices[] = { _("None"), _("Real Balance Board") };
	wxChoice* bb_source = new wxChoice(this, source_ctrl_id, wxDefaultPosition, wxDefaultSize, sizeof(src_choices)/sizeof(*src_choices), src_choices);
	bb_source->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &WiimoteConfigDiag::SelectSource, this);
	
	m_orig_wiimote_sources[WIIMOTE_BALANCE_BOARD] = g_wiimote_sources[WIIMOTE_BALANCE_BOARD];
	bb_source->Select(m_orig_wiimote_sources[WIIMOTE_BALANCE_BOARD] ? 1 : 0);
	
	bb_sizer->Add(bb_source, 0, wxALIGN_CENTER_VERTICAL);
	
	bb_group->Add(bb_sizer, 1, wxEXPAND, 5 );

	
	// "Real wiimotes" controls
	wxButton* const refresh_btn = new wxButton(this, -1, _("Refresh"), wxDefaultPosition);
	refresh_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &WiimoteConfigDiag::RefreshRealWiimotes, this);

	wxStaticBoxSizer* const real_wiimotes_group = new wxStaticBoxSizer(wxVERTICAL, this, _("Real Wiimotes"));
	
	wxBoxSizer* const real_wiimotes_sizer = new wxBoxSizer(wxHORIZONTAL);
	
	if (!WiimoteReal::g_wiimote_scanner.IsReady())
		real_wiimotes_group->Add(new wxStaticText(this, -1, _("A supported bluetooth device could not be found.\n"
			"You must manually connect your wiimotes.")), 0, wxALIGN_CENTER | wxALL, 5);
		
	wxCheckBox* const continuous_scanning = new wxCheckBox(this, wxID_ANY, _("Continuous Scanning"));
	continuous_scanning->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &WiimoteConfigDiag::OnContinuousScanning, this);
	continuous_scanning->SetValue(SConfig::GetInstance().m_WiimoteContinuousScanning);

	auto wiimote_speaker = new wxCheckBox(this, wxID_ANY, _("Enable Speaker Data"));
	wiimote_speaker->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &WiimoteConfigDiag::OnEnableSpeaker, this);
	wiimote_speaker->SetValue(SConfig::GetInstance().m_WiimoteEnableSpeaker);
	
	real_wiimotes_sizer->Add(continuous_scanning, 0, wxALIGN_CENTER_VERTICAL);
	real_wiimotes_sizer->AddStretchSpacer(1);
	real_wiimotes_sizer->Add(refresh_btn, 0, wxALL | wxALIGN_CENTER, 5);

	real_wiimotes_group->Add(wiimote_speaker, 0);
	real_wiimotes_group->Add(real_wiimotes_sizer, 0, wxEXPAND);

	// "General Settings" controls
	const wxString str[] = { _("Bottom"), _("Top") };
	wxChoice* const WiiSensBarPos = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, str);
	wxSlider* const WiiSensBarSens = new wxSlider(this, wxID_ANY, 0, 0, 4, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	wxSlider* const WiimoteSpkVolume = new wxSlider(this, wxID_ANY, 0, 0, 127);
	wxCheckBox* const WiimoteMotor = new wxCheckBox(this, wxID_ANY, _("Wiimote Motor"));
	wxCheckBox* const WiimoteReconnectOnLoad = new wxCheckBox(this, wxID_ANY, _("Reconnect Wiimote on State Loading"));

	wxStaticText* const WiiSensBarPosText = new wxStaticText(this, wxID_ANY, _("Sensor Bar Position:"));
	wxStaticText* const WiiSensBarSensText = new wxStaticText(this, wxID_ANY, _("IR Sensitivity:"));
	wxStaticText* const WiiSensBarSensMinText = new wxStaticText(this, wxID_ANY, _("Min"));
	wxStaticText* const WiiSensBarSensMaxText = new wxStaticText(this, wxID_ANY, _("Max"));
	wxStaticText* const WiimoteSpkVolumeText = new wxStaticText(this, wxID_ANY, _("Speaker Volume:"));
	wxStaticText* const WiimoteSpkVolumeMinText = new wxStaticText(this, wxID_ANY, _("Min"));
	wxStaticText* const WiimoteSpkVolumeMaxText = new wxStaticText(this, wxID_ANY, _("Max"));

	// With some GTK themes, no minimum size will be applied - so do this manually here
	WiiSensBarSens->SetMinSize(wxSize(100,-1));
	WiimoteSpkVolume->SetMinSize(wxSize(100,-1));


	// Disable some controls when emulation is running
	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		WiiSensBarPos->Disable();
		WiiSensBarSens->Disable();
		WiimoteSpkVolume->Disable();
		WiimoteMotor->Disable();
		WiiSensBarPosText->Disable();
		WiiSensBarSensText->Disable();
		WiiSensBarSensMinText->Disable();
		WiiSensBarSensMaxText->Disable();
		WiimoteSpkVolumeText->Disable();
		WiimoteSpkVolumeMinText->Disable();
		WiimoteSpkVolumeMaxText->Disable();
	}


	// "General Settings" initialization
	WiiSensBarPos->SetSelection(SConfig::GetInstance().m_SYSCONF->GetData<u8>("BT.BAR"));
	WiiSensBarSens->SetValue(SConfig::GetInstance().m_SYSCONF->GetData<u32>("BT.SENS"));
	WiimoteSpkVolume->SetValue(SConfig::GetInstance().m_SYSCONF->GetData<u8>("BT.SPKV"));
	WiimoteMotor->SetValue(SConfig::GetInstance().m_SYSCONF->GetData<bool>("BT.MOT"));
	WiimoteReconnectOnLoad->SetValue(SConfig::GetInstance().m_WiimoteReconnectOnLoad);

	WiiSensBarPos->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &WiimoteConfigDiag::OnSensorBarPos, this);
	WiiSensBarSens->Bind(wxEVT_COMMAND_SLIDER_UPDATED, &WiimoteConfigDiag::OnSensorBarSensitivity, this);
	WiimoteSpkVolume->Bind(wxEVT_COMMAND_SLIDER_UPDATED, &WiimoteConfigDiag::OnSpeakerVolume, this);
	WiimoteMotor->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &WiimoteConfigDiag::OnMotor, this);
	WiimoteReconnectOnLoad->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &WiimoteConfigDiag::OnReconnectOnLoad, this);


	// "General Settings" layout
	wxStaticBoxSizer* const general_sizer = new wxStaticBoxSizer(wxVERTICAL, this, _("General Settings"));
	wxFlexGridSizer* const choice_sizer = new wxFlexGridSizer(2, 5, 5);

	wxBoxSizer* const sensbarsens_sizer = new wxBoxSizer(wxHORIZONTAL);
	sensbarsens_sizer->Add(WiiSensBarSensMinText, 0, wxALIGN_CENTER_VERTICAL);
	sensbarsens_sizer->Add(WiiSensBarSens);
	sensbarsens_sizer->Add(WiiSensBarSensMaxText, 0, wxALIGN_CENTER_VERTICAL);

	wxBoxSizer* const spkvol_sizer = new wxBoxSizer(wxHORIZONTAL);
	spkvol_sizer->Add(WiimoteSpkVolumeMinText, 0, wxALIGN_CENTER_VERTICAL);
	spkvol_sizer->Add(WiimoteSpkVolume);
	spkvol_sizer->Add(WiimoteSpkVolumeMaxText, 0, wxALIGN_CENTER_VERTICAL);

	choice_sizer->Add(WiiSensBarPosText, 0, wxALIGN_CENTER_VERTICAL);
	choice_sizer->Add(WiiSensBarPos);
	choice_sizer->Add(WiiSensBarSensText, 0, wxALIGN_CENTER_VERTICAL);
	choice_sizer->Add(sensbarsens_sizer);
	choice_sizer->Add(WiimoteSpkVolumeText, 0, wxALIGN_CENTER_VERTICAL);
	choice_sizer->Add(spkvol_sizer);

	wxGridSizer* const general_wiimote_sizer = new wxGridSizer(1, 5, 5);
	general_wiimote_sizer->Add(WiimoteMotor);
	general_wiimote_sizer->Add(WiimoteReconnectOnLoad);

	general_sizer->Add(choice_sizer);
	general_sizer->Add(general_wiimote_sizer);


	// Dialog layout
	main_sizer->Add(wiimote_group, 0, wxEXPAND | wxALL, 5);
	main_sizer->Add(bb_group, 0, wxEXPAND | wxALL, 5);
	main_sizer->Add(real_wiimotes_group, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	main_sizer->Add(general_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	Bind(wxEVT_COMMAND_BUTTON_CLICKED, &WiimoteConfigDiag::Save, this, wxID_OK);
	Bind(wxEVT_COMMAND_BUTTON_CLICKED, &WiimoteConfigDiag::Cancel, this, wxID_CANCEL);
	Bind(wxEVT_CLOSE_WINDOW, &WiimoteConfigDiag::OnClose, this, wxID_ANY);

	SetSizerAndFit(main_sizer);
	Center();
	UpdateGUI();
}
void WiimoteConfigDiag::OnClose(wxCloseEvent& event)
{
	// close open dialogs
	if (m_emu_config_diag)
		m_emu_config_diag->Close();
	Hide();
	m_parent->Update();
	event.Skip();
}

void WiimoteConfigDiag::ConfigEmulatedWiimote(wxCommandEvent& ev)
{
	if (m_emu_config_diag)
	{
		m_emu_config_diag->m_pad_notebook->SetSelection(m_wiimote_index_from_conf_bt_id[ev.GetId()]);
		m_emu_config_diag->SetFocus();
		return;
	}

	m_emu_config_diag = new InputConfigDialog(this, m_plugin, _trans("Dolphin Emulated Wiimote Configuration"), m_wiimote_index_from_conf_bt_id[ev.GetId()]);
	m_emu_config_diag->Show();
}

void WiimoteConfigDiag::UpdateGUI()
{
	if (m_emu_config_diag)
		m_emu_config_diag->UpdateGUI();
}

void WiimoteConfigDiag::Update()
{
	if (m_emu_config_diag && !m_emu_config_diag->IsShown())
	{
		m_emu_config_diag->Destroy();
		m_emu_config_diag = NULL;
	}
}

void WiimoteConfigDiag::RefreshRealWiimotes(wxCommandEvent&)
{
	WiimoteReal::Refresh();
}

void WiimoteConfigDiag::SelectSource(wxCommandEvent& event)
{
	// This needs to be changed now in order for refresh to work right.
	// Revert if the dialog is canceled.
	int index = m_wiimote_index_from_ctrl_id[event.GetId()];
	
	if(index != WIIMOTE_BALANCE_BOARD)
	{
		WiimoteReal::ChangeWiimoteSource(index, event.GetInt());
		if (g_wiimote_sources[index] != WIIMOTE_SRC_EMU && g_wiimote_sources[index] != WIIMOTE_SRC_HYBRID)
			wiimote_configure_bt[index]->Disable();
		else
			wiimote_configure_bt[index]->Enable();
	}
	else
	{
		WiimoteReal::ChangeWiimoteSource(index, event.GetInt() ? WIIMOTE_SRC_REAL : WIIMOTE_SRC_NONE);	
	}
}

void WiimoteConfigDiag::RevertSource()
{
	for (int i = 0; i < MAX_BBMOTES; ++i)
		g_wiimote_sources[i] = m_orig_wiimote_sources[i];
}

void WiimoteConfigDiag::Save(wxCommandEvent& event)
{
	std::string ini_filename = File::GetUserPath(D_CONFIG_IDX) + WIIMOTE_INI_NAME ".ini";

	IniFile inifile;
	inifile.Load(ini_filename);

	for (unsigned int i=0; i<MAX_WIIMOTES; ++i)
	{
		std::string secname("Wiimote");
		secname += (char)('1' + i);
		IniFile::Section& sec = *inifile.GetOrCreateSection(secname.c_str());

		sec.Set("Source", (int)g_wiimote_sources[i]);
	}
	
	std::string secname("BalanceBoard");
	IniFile::Section& sec = *inifile.GetOrCreateSection(secname.c_str());
	sec.Set("Source", (int)g_wiimote_sources[WIIMOTE_BALANCE_BOARD]);

	inifile.Save(ini_filename);

	Close();

	event.Skip();
}

void WiimoteConfigDiag::Cancel(wxCommandEvent& event)
{
	RevertSource();
	Close();
	event.Skip();
}
