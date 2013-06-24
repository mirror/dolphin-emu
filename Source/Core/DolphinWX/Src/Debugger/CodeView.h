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

class CDebugView : public wxControl
{
public:

	CDebugView(DebugInterface* debuginterface
		, wxWindow* parent
		, wxWindowID Id = wxID_ANY);

	u32 GetSelection() { return(selection); }
	u32 GetAddress() { return(curAddress); }

	void Center(u32 addr);

protected:
	enum
	{
		IDM_GOTOINMEMVIEW = 12000,
		IDM_COPYADDRESS,
		IDM_COPYHEX,
		IDM_SIZE,
	};
	void OnPopupMenu(wxCommandEvent& event);

	void OnPaint(wxPaintEvent& event);
	void OnErase(wxEraseEvent& event);
	void OnMouseDown(wxMouseEvent& event);
	void OnMouseMove(wxMouseEvent& event);
	void OnMouseUpL(wxMouseEvent& event);
	void OnMouseUpR(wxMouseEvent& event);

	void RaiseEvent();
	int YToAddress(int y);

	wxMenu* menu;
	void OnResize(wxSizeEvent& event);

	DebugInterface* debugger;

	u32 curAddress;
	int align;
	int rowHeight;

	u32 selection;
	u32 oldSelection;
	bool selectionChanged;
	bool selecting;
	bool hasFocus;
	bool showHex;

	wxPaintDC *dc;
	bool plain;
	wxRect rc;
	u32 address;
	int numRows;
	int charWidth;
	int marginL;
	int rowY1;
	int rowY2;

	int lx, ly;
	void _MoveTo(int x, int y) {lx = x; ly = y;}
	void _LineTo(wxPaintDC *dc, int x, int y);

private:
	virtual void OnMouseUpR() = 0;
	virtual void OnPopupMenu(int id) = 0;

	virtual void PaintBefore() {};
	virtual void PaintAfter() {};
	virtual void PaintRow() {};

	virtual void ToggleBreakpoint(u32 address) = 0;

	DECLARE_EVENT_TABLE()
};

class CCodeView : public CDebugView
{
public:
	CCodeView(DebugInterface* debuginterface
		, SymbolDB *symbol_db
		, wxWindow* parent
		, wxWindowID Id = wxID_ANY);

	void SetPlain()
	{
		plain = true;
	}

	void ToggleBreakpoint(u32 address);

private:
	enum
	{
		IDM_COPYCODE = CDebugView::IDM_SIZE,
		IDM_INSERTBLR, IDM_INSERTNOP,
		IDM_RUNTOHERE,
		IDM_JITRESULTS,
		IDM_FOLLOWBRANCH,
		IDM_RENAMESYMBOL,
		IDM_PATCHALERT,
		IDM_COPYFUNCTION,
		IDM_ADDFUNCTION,
	};
	void OnPopupMenu(int id);

	SymbolDB* symbol_db;

	struct branch
	{
		int src, dst, srcAddr;
	};
	branch branches[256];
	int numBranches;

	void OnMouseUpR();

	void PaintBefore();
	void PaintAfter();
	void PaintRow();

	int symL;

	void InsertBlrNop(int);
	u32 AddrToBranch(u32 addr);

	struct BlrStruct // for IDM_INSERTBLR
	{
		u32 Address;
		u32 OldValue;
	};
	std::vector<BlrStruct> BlrList;
};

#endif /*CODEVIEW_H_*/
