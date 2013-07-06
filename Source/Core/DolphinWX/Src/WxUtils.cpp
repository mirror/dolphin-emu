// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"

#include <wx/wx.h>
#include <wx/string.h>
#include <wx/clipbrd.h>

#include "WxUtils.h"


BEGIN_EVENT_TABLE(wxListBoxEx, wxListBox)
	EVT_MENU(wxID_COPY, wxListBoxEx::OnCopy)
	EVT_MENU(wxID_SELECTALL, wxListBoxEx::OnSelectAll)
END_EVENT_TABLE()

wxListBoxEx::wxListBoxEx(wxWindow *parent
	, wxWindowID id
	, const wxPoint& pos
	, wxSize size
	, const wxArrayString& choices
	, long style
	, const wxValidator& validator
	, const wxString& name) :
	wxListBox(parent, id, pos, size, choices, style, validator, name)
{
	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, 'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, 'A', wxID_SELECTALL);
	wxAcceleratorTable accel(WXSIZEOF(entries), entries);
	SetAcceleratorTable(accel);
}

void wxListBoxEx::OnSelectAll(wxCommandEvent& event)
{
	event.Skip();

	for (int i = 0; i < GetCount(); i++)
		SetSelection(i, true);
}

void wxListBoxEx::OnCopy(wxCommandEvent& event)
{
	event.Skip();

	if (!wxTheClipboard->Open())
		return;

	wxString s;

	for (int i = 0; i < GetCount(); i++)
	{
		s.Append(GetString(i));
		s.Append("\n");
	}

	wxTheClipboard->SetData(new wxTextDataObject(s));
	wxTheClipboard->Close();
}

namespace WxUtils {

// Launch a file according to its mime type
void Launch(const char *filename)
{
	if (! ::wxLaunchDefaultBrowser(StrToWxStr(filename)))
	{
		// WARN_LOG
	}
}

// Launch an file explorer window on a certain path
void Explore(const char *path)
{
	wxString wxPath = StrToWxStr(path);
	// Default to file
	if (! wxPath.Contains(wxT("://")))
	{
		wxPath = wxT("file://") + wxPath;
	}

#ifdef __WXGTK__
	wxPath.Replace(wxT(" "), wxT("\\ "));
#endif

	if (! ::wxLaunchDefaultBrowser(wxPath))
	{
		// WARN_LOG
	}
}

}  // namespace

std::string WxStrToStr(const wxString& str)
{
	return str.ToUTF8().data();
}

wxString StrToWxStr(const std::string& str)
{
	//return wxString::FromUTF8Unchecked(str.c_str());
	return wxString::FromUTF8(str.c_str());
}
