// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef MEMORYVIEW_H_
#define MEMORYVIEW_H_

#include "DebuggerUIUtil.h"
#include "Common.h"
#include "DebugInterface.h"
#include "CodeView.h"

class CMemoryView : public CDebugView
{
public:
	CMemoryView(DebugInterface* debuginterface, wxWindow* parent);

	void ToggleBreakpoint(u32 address);

	int GetMemoryType() { return memory; }

	int dataType;	// u8,u16,u32

private:
	enum
	{
		IDM_COPYCODE = CDebugView::IDM_SIZE,
		IDM_RUNTOHERE,
		IDM_DYNARECRESULTS,
		IDM_TOGGLEMEMORY,
		IDM_VIEWASFP,
		IDM_VIEWASASCII,
		IDM_VIEWASHEX,
	};
	void OnPopupMenu(int id);

	void OnMouseUpR();

	void PaintRow();

	void Center_(u32 addr);

	int memory;

	enum EViewAsType
	{
		VIEWAS_ASCII = 0,
		VIEWAS_FP,
		VIEWAS_HEX,
	};
	EViewAsType viewAsType;
};

#endif // MEMORYVIEW_H_
