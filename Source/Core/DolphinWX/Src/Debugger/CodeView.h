// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef CODEVIEW_H_
#define CODEVIEW_H_

#define wxUSE_XPM_IN_MSW 1
#define USE_XPM_BITMAPS 1

#include <wx/wx.h>

#include "Common.h"

#include <vector>

DECLARE_EVENT_TYPE(wxEVT_CODEVIEW_CHANGE, -1);

class DebugInterface;
class SymbolDB;

class CCodeView : public wxControl
{
public:
	CCodeView(DebugInterface* debuginterface, SymbolDB *symbol_db,
			wxWindow* parent, wxWindowID Id = wxID_ANY);
	void OnPaint(wxPaintEvent& event);
	void OnErase(wxEraseEvent& event);
	void OnMouseDown(wxMouseEvent& event);
	void OnMouseMove(wxMouseEvent& event);
	void OnMouseUpL(wxMouseEvent& event);
	void OnMouseUpR(wxMouseEvent& event);
	void OnPopupMenu(wxCommandEvent& event);
	void InsertBlrNop(int);

	u32 GetSelection() {return(selection);}
	void ToggleBreakpoint(u32 address);	

	struct BlrStruct // for IDM_INSERTBLR
	{
		u32 Address;
		u32 OldValue;
	};
	std::vector<BlrStruct> BlrList;

	void Center(u32 addr)
	{
		curAddress = addr;
		selection = addr;
		if (debugger->isAlive())
			debugger->showJitResults(selection);
		Refresh();
	}

	void SetPlain()
	{
		plain = true;
	}

private:
	void RaiseEvent();
	int YToAddress(int y);

	u32 AddrToBranch(u32 addr);
	void OnResize(wxSizeEvent& event);

	DebugInterface* debugger;
	SymbolDB* symbol_db;

	bool plain;

	int curAddress;
	int align;
	int rowHeight;

	u32 selection;
	u32 oldSelection;
	bool selectionChanged;
	bool selecting;
	bool hasFocus;
	bool showHex;

	int lx, ly;
	void _MoveTo(int x, int y) {lx = x; ly = y;}
	void _LineTo(wxPaintDC &dc, int x, int y);

	DECLARE_EVENT_TABLE()
};

#endif /*CODEVIEW_H_*/
