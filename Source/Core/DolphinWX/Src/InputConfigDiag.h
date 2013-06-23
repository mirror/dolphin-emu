// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _CONFIGBOX_H_
#define _CONFIGBOX_H_

#define SLIDER_TICK_COUNT			100
#define DETECT_WAIT_TIME			1500
#define PREVIEW_UPDATE_TIME			25

// might have to change this setup for wiimote
#define PROFILES_PATH				"Profiles/"

#include <wx/wx.h>
#include <wx/listbox.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/checkbox.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/spinctrl.h>

#include <sstream>
#include <vector>

#include "ControllerInterface/ControllerInterface.h"
#include "ControllerEmu.h"
#include "InputConfig.h"
#include "FileSearch.h"
#include "UDPWrapper.h"

class PadSetting
{
protected:
	PadSetting(wxControl* const _control) : wxcontrol(_control) { wxcontrol->SetClientData(this); }

public:
	virtual void UpdateGUI() = 0;
	virtual void UpdateValue() = 0;

	virtual ~PadSetting() {}

	wxControl* const	wxcontrol;
};

class PadSettingExtension : public PadSetting
{
public:
	PadSettingExtension(wxWindow* const parent, ControllerEmu::Extension* const ext);
	void UpdateGUI();
	void UpdateValue();

	ControllerEmu::Extension* const	extension;
};

class PadSettingSpin : public PadSetting
{
public:
	PadSettingSpin(wxWindow* const parent, ControllerEmu::ControlGroup::Setting* const setting)
		: PadSetting(new wxSpinCtrl(parent, -1, wxEmptyString, wxDefaultPosition
			, wxSize(54, -1), 0, setting->low, setting->high, (int)(setting->value * 100)))
		, value(setting->value) {}

	void UpdateGUI();
	void UpdateValue();

	ControlState& value;
};

class PadSettingCheckBox : public PadSetting
{
public:
	PadSettingCheckBox(wxWindow* const parent, ControlState& _value, const char* const label);
	void UpdateGUI();
	void UpdateValue();

	ControlState&		value;
};

class GamepadPage;

class ControlDialog : public wxDialog
{
public:
	ControlDialog(GamepadPage* const parent, InputPlugin& plugin, ControllerInterface::ControlReference* const ref);
	
	wxStaticBoxSizer* CreateControlChooser(GamepadPage* const parent);

	void DetectControl(wxCommandEvent& event);
	void ClearControl(wxCommandEvent& event);
	void SetControl(wxCommandEvent& event);
	void SetDevice(wxCommandEvent& event);

	void UpdateGUI();
	void UpdateListContents();
	void SelectControl(const std::string& name);

	void SetSelectedControl(wxCommandEvent& event);
	void AppendControl(wxCommandEvent& event);

	ControllerInterface::ControlReference* const		control_reference;
	InputPlugin&				m_plugin;
	wxComboBox*				device_cbox;

	wxTextCtrl*		textctrl;
	wxListBox*		control_lbox;
	wxSlider*		range_slider;

private:
	GamepadPage* const		m_parent;
	wxStaticText*		m_bound_label;
	ControllerInterface::DeviceQualifier	m_devq;
};

class ExtensionButton : public wxButton
{
public:
	ExtensionButton(wxWindow* const parent, ControllerEmu::Extension* const ext)
		: wxButton(parent, -1, _("Configure"), wxDefaultPosition)
		, extension(ext) {}

	ControllerEmu::Extension* const	extension;
};

class ControlButton : public wxButton
{
public:
	ControlButton(wxWindow* const parent, ControllerInterface::ControlReference* const _ref, const unsigned int width, const std::string& label = "");

	ControllerInterface::ControlReference* const		control_reference;
};

class UDPConfigButton : public wxButton
{
public:
	UDPWrapper* const wrapper;
	UDPConfigButton(wxWindow* const parent, UDPWrapper * udp)
		: wxButton(parent, -1, _("Configure"), wxDefaultPosition)
		, wrapper(udp)
	{}
};

class ControlGroupBox : public wxBoxSizer
{
public:
	ControlGroupBox(ControllerEmu::ControlGroup* const group, wxWindow* const parent, GamepadPage* const eventsink);
	~ControlGroupBox();

	std::vector<PadSetting*>		options;

	ControllerEmu::ControlGroup* const	control_group;
	wxStaticBitmap*					static_bitmap;
	std::vector<ControlButton*>		control_buttons;
};

class ControlGroupsSizer : public wxBoxSizer
{
public:
	ControlGroupsSizer(ControllerEmu* const controller, wxWindow* const parent, GamepadPage* const eventsink, std::vector<ControlGroupBox*>* const groups = NULL);
};

class InputConfigDialog;

class GamepadPage : public wxPanel
{
	friend class InputConfigDialog;
	friend class ControlDialog;

public:
	GamepadPage(wxWindow* parent, InputPlugin& plugin, const unsigned int pad_num, InputConfigDialog* const config_dialog);

	void UpdateGUI();

	void RefreshDevices(wxCommandEvent& event);

	void LoadProfile(wxCommandEvent& event);
	void SaveProfile(wxCommandEvent& event);
	void DeleteProfile(wxCommandEvent& event);

	void ConfigControl(wxEvent& event);
	void ClearControl(wxEvent& event);
	void DetectControl(wxCommandEvent& event);

	void ConfigExtension(wxCommandEvent& event);

	void ConfigUDPWii(wxCommandEvent& event);

	void SetDevice(wxCommandEvent& event);

	void ClearAll(wxCommandEvent& event);
	void LoadDefaults(wxCommandEvent& event);
	void LoadUserDefault(wxCommandEvent& event);
	void SaveUserDefault(wxCommandEvent& event);

	void AdjustControlOption(wxCommandEvent& event);
	void AdjustSetting(wxCommandEvent& event);

	void GetProfilePath(std::string& path);

	wxComboBox*					profile_cbox;
	wxComboBox*					device_cbox;
	wxButton					*ud_load_button, *ud_save_button;

	std::vector<ControlGroupBox*>		control_groups;

protected:
	
	ControllerEmu* const				controller;

private:

	ControlDialog*				m_control_dialog;
	InputConfigDialog* const	m_config_dialog;
	InputPlugin &m_plugin;
};

class InputConfigDialog : public wxDialog
{
	struct ext
	{
		ControllerEmu::Extension*	object;
		int							type;
		wxDialog*					dlg;
		std::size_t					size;
	};

public:
	InputConfigDialog(wxWindow* const parent, InputPlugin& plugin, const std::string& name, const int tab_num = 0);
	//~InputConfigDialog();

	void OnClose(wxCloseEvent& event);
	void OnCloseExt(wxCloseEvent& event);

	void Save(wxCommandEvent& event);
	void Apply(wxCommandEvent& event);
	void Cancel(wxCommandEvent& event);

	void UpdateDeviceComboBox();
	void UpdateProfileComboBox(std::string fname = "");

	void UpdateControlReferences();
	void UpdateBitmaps(wxTimerEvent&);
	void UpdateGUI();

	wxNotebook*					m_pad_notebook;
	ext							m_ext;

private:

	wxWindow*					m_parent;
	std::vector<GamepadPage*>	m_padpages;
	InputPlugin&				m_plugin;
	wxTimer*					m_update_timer;
	wxString					m_title;
};

#endif
