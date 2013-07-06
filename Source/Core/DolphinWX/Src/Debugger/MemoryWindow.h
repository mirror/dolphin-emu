// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef MEMORYWINDOW_H_
#define MEMORYWINDOW_H_

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/listbox.h>
#include "MemoryView.h"
#include "Thread.h"
#include "StringUtil.h"

#include "CoreParameter.h"

class CRegisterWindow;
class CBreakPointWindow;

class CMemoryWindow
	: public wxPanel
{
	public:

		CMemoryWindow(wxWindow* parent,
					  wxWindowID id = wxID_ANY,
					  const wxPoint& pos = wxDefaultPosition,
					  const wxSize& size = wxDefaultSize,
					  long style = wxTAB_TRAVERSAL | wxBORDER_NONE,
					  const wxString& name = _("Memory"));

		wxCheckBox* chk8;
		wxCheckBox* chk16;
		wxCheckBox* chk32;
		wxButton*   btnSearch;
		wxCheckBox* chkAscii;
		wxCheckBox* chkHex;
		void Save(IniFile& _IniFile) const;
		void Load(IniFile& _IniFile);

		void Center(u32 addr);
		void Update();
		void NotifyMapLoaded();

		void JumpToAddress(u32 _Address);

	private:
		DECLARE_EVENT_TABLE()

		CMemoryView* memview;
		wxListBox* symbols;

		wxButton* buttonGo;
		wxTextCtrl* valbox;

		void U8(wxCommandEvent& event);
		void U16(wxCommandEvent& event);
		void U32(wxCommandEvent& event);
		void onSearch(wxCommandEvent& event);
		void onAscii(wxCommandEvent& event);
		void onHex(wxCommandEvent& event);
		void OnSymbolListChange(wxCommandEvent& event);
		void OnCallstackListChange(wxCommandEvent& event);
		void OnHostMessage(wxCommandEvent& event);
		void SetMemoryValue(wxCommandEvent& event);
		void Refresh(wxCommandEvent& event);
		void OnDumpMemory(wxCommandEvent& event);
		void OnDumpMem2(wxCommandEvent& event);
		void OnDumpFakeVMEM(wxCommandEvent& event);
};

#endif /*MEMORYWINDOW_*/
