// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef WXUTILS_H
#define WXUTILS_H

#include <string>
#include <wx/string.h>
#include <wx/listbox.h>

class wxListBoxEx : public wxListBox
{
public:
	wxListBoxEx(wxWindow *parent
		, wxWindowID id
		, const wxPoint& pos
		, wxSize size
		, const wxArrayString& choices
		, long style = 0
		, const wxValidator& validator = wxDefaultValidator
		, const wxString& name = wxListBoxNameStr);

private:
	void OnSelectAll(wxCommandEvent& event);
	void OnCopy(wxCommandEvent& event);

	DECLARE_EVENT_TABLE();
};

namespace WxUtils
{

// Launch a file according to its mime type
void Launch(const char *filename);

// Launch an file explorer window on a certain path
void Explore(const char *path);

}  // namespace

std::string WxStrToStr(const wxString& str);
wxString StrToWxStr(const std::string& str);

#endif // WXUTILS
