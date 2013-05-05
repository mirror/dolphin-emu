// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef __GECKOCODEDIAG_h__
#define __GECKOCODEDIAG_h__

#include "GeckoCode.h"
#include "GeckoCodeConfig.h"

#include "wx/wx.h"

namespace Gecko
{


class CodeConfigPanel : public wxPanel
{
public:
	CodeConfigPanel(wxWindow* const parent);


	void LoadCodes(const IniFile& inifile, const std::string& gameid = "", bool checkRunning = false);
	const std::vector<GeckoCode>& GetCodes() const { return m_gcodes; }

protected:
	void UpdateInfoBox(wxCommandEvent&);
	void ToggleCode(wxCommandEvent& evt);
	void DownloadCodes(wxCommandEvent&);
	//void ApplyChanges(wxCommandEvent&);

	void UpdateCodeList(bool checkRunning = false);

private:
	std::vector<GeckoCode> m_gcodes;

	std::string m_gameid;

	// wxwidgets stuff
	wxCheckListBox	*m_listbox_gcodes;
	struct
	{
		wxStaticText	*label_name, *label_notes, *label_creator;
		wxTextCtrl		*textctrl_notes;
		wxListBox	*listbox_codes;
	} m_infobox;
	wxButton* btn_download;
};



}

#endif

