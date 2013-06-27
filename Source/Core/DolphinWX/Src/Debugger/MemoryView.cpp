// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "DebuggerUIUtil.h"
#include "Common.h"
#include "Host.h"
#include "PowerPC/PowerPC.h"
#include "HW/Memmap.h"

#include "MemoryView.h"
#include "../WxUtils.h"

#include <wx/event.h>
#include <wx/clipbrd.h>

CMemoryView::CMemoryView(DebugInterface* debuginterface, wxWindow* parent)
	: CDebugView(debuginterface, parent, wxID_ANY)
	, memory(0)
	, viewAsType(VIEWAS_FP)
{
	curAddress = debuginterface->getPC();
}

void CMemoryView::ToggleBreakpoint(u32 address)
{
	debugger->toggleMemCheck(address);

	Refresh();
	Host_UpdateBreakPointView();
}

void CMemoryView::OnPopupMenu(int id)
{
	switch (id)
	{
		case IDM_TOGGLEMEMORY:
			memory ^= 1;
			Refresh();
			break;

		case IDM_VIEWASFP:
			viewAsType = VIEWAS_FP;
			Refresh();
			break;

		case IDM_VIEWASASCII:
			viewAsType = VIEWAS_ASCII;
			Refresh();
			break;
		case IDM_VIEWASHEX:
			viewAsType = VIEWAS_HEX;
			Refresh();
			break;
	}
}

void CMemoryView::OnMouseUpR()
{
	//menu.Append(IDM_GOTOINMEMVIEW, "&Goto in mem view");
	menu->Append(IDM_TOGGLEMEMORY, StrToWxStr("Toggle &memory"));

	wxMenu* viewAsSubMenu = new wxMenu;
	viewAsSubMenu->Append(IDM_VIEWASFP, StrToWxStr("FP value"));
	viewAsSubMenu->Append(IDM_VIEWASASCII, StrToWxStr("ASCII"));
	viewAsSubMenu->Append(IDM_VIEWASHEX, StrToWxStr("Hex"));
	menu->AppendSubMenu(viewAsSubMenu, StrToWxStr("View As:"));
}

void CMemoryView::PaintRow()
{
	int addressL = marginL + 1;

	int valueL = addressL + 9 * charWidth;
	if (viewAsType == VIEWAS_HEX)
		valueL = addressL;

	int viewL = valueL + 11 * charWidth;
	if (viewAsType == VIEWAS_HEX)
		viewL = valueL + 9 * charWidth;

	int symL = viewL + 45 * charWidth;
	if (viewAsType == VIEWAS_ASCII)
		symL = viewL + 9 * charWidth;
	if (viewAsType == VIEWAS_HEX)
	{
		switch (dataType)
		{
		case 0:	symL = viewL + (8 + 4) * 8 * charWidth; break;
		case 1:	symL = viewL + (8 + 2) * 8 * charWidth; break;
		case 2:	symL = viewL + (8 + 1) * 8 * charWidth; break;
		}
	}

	int colWidth = 32;

	// TODO: Add any drawing code here...
	int width   = rc.width;
	int numRows = (rc.height / rowHeight) / 2 + 2;
	dc->SetBackgroundMode(wxTRANSPARENT);
	const wxChar* bgColor = _T("#ffffff");
	wxPen currentPen(_T("#000000"));
	wxPen selPen(_T("#808080")); // gray

	wxBrush currentBrush(_T("#FFEfE8")); // light gray
	wxBrush bgBrush(bgColor);

	dc->SetBrush(currentBrush);
	dc->SetTextForeground(_T("#600000"));
	dc->DrawText(wxString::Format(_T("%08x"), address), addressL, rowY1);

	if (viewAsType != VIEWAS_HEX)
	{
		char mem[256];
		debugger->getRawMemoryString(memory, address, mem, 256);
		dc->SetTextForeground(_T("#000080"));
		dc->DrawText(StrToWxStr(mem), valueL, rowY1);
		dc->SetTextForeground(_T("#000000"));
	}

	if (debugger->isAlive())
	{
		char dis[256] = {0};
		u32 mem_data = debugger->readExtraMemory(memory, address);

		if (viewAsType == VIEWAS_FP)
		{
			float flt = *(float *)(&mem_data);
			sprintf(dis, "%f", flt);
		}
		else if (viewAsType == VIEWAS_ASCII)
		{
			u32 a[4] = {(mem_data&0xff000000)>>24,
				(mem_data&0xff0000)>>16,
				(mem_data&0xff00)>>8,
				mem_data&0xff};
			for (size_t i = 0; i < 4; i++)
				if (a[i] == '\0')
					a[i] = ' ';
			sprintf(dis, "%c%c%c%c", a[0], a[1], a[2], a[3]);
		}
		else if (viewAsType == VIEWAS_HEX)
		{
			dis[0] = 0;
			dis[1] = 0;

			for (int i = 0; i < colWidth; i += 4)
			{
				u32 mema = debugger->readExtraMemory(memory, i);
				char buf[32] = "";
				switch (dataType)
				{
				case 0:
					sprintf(buf, "%02X %02X %02X %02X ",
						((mema&0xff000000)>>24)&0xFF,
						((mema&0xff0000)>>16)&0xFF,
						((mema&0xff00)>>8)&0xFF,
						mema&0xff);
					break;
				case 1:
					sprintf(buf, "%02X%02X %02X%02X ",
						((mema&0xff000000)>>24)&0xFF,
						((mema&0xff0000)>>16)&0xFF,
						((mema&0xff00)>>8)&0xFF,
						mema&0xff);
					break;
				case 2:
					sprintf(buf, "%02X%02X%02X%02X ",
						((mema&0xff000000)>>24)&0xFF,
						((mema&0xff0000)>>16)&0xFF,
						((mema&0xff00)>>8)&0xFF,
						mema&0xff);
					break;
				}
				strcat(dis, buf);
			}
			curAddress += 32;
		}
		else
		{
			sprintf(dis, "INVALID VIEWAS TYPE");
		}

		char desc[256] = "";
		dc->DrawText(StrToWxStr(dis), viewL, rowY1);

		if (desc[0] == 0)
			strcpy(desc, debugger->getDescription(address).c_str());

		dc->SetTextForeground(_T("#0000FF"));

		if (strlen(desc))
			dc->DrawText(StrToWxStr(desc), symL, rowY1);
	}
}