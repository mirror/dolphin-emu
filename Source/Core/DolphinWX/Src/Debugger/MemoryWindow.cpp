// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/listctrl.h>
#include <wx/thread.h>
#include <wx/listctrl.h>

#include "../WxUtils.h"
#include "MemoryWindow.h"
#include "HW/CPU.h"
#include "PowerPC/PowerPC.h"
#include "Host.h"
#include "FileUtil.h"

#include "Debugger/PPCDebugInterface.h"
#include "PowerPC/PPCSymbolDB.h"

#include "Core.h"
#include "ConfigManager.h"
#include "LogManager.h"

#include "HW/Memmap.h"
#include "HW/DSP.h"

#include "../../DolphinWX/Src/Globals.h"

enum
{
	IDM_SYMBOLLIST = 350,
	IDM_SETVALBUTTON,
	IDM_REFRESH,
	IDM_DUMP_MEMORY,
	IDM_DUMP_MEM2,
	IDM_DUMP_FAKEVMEM,
	IDM_VALBOX,
	IDM_U8,
	IDM_U16,
	IDM_U32,
	IDM_SEARCH,
	IDM_ASCII,
	IDM_HEX
};

BEGIN_EVENT_TABLE(CMemoryWindow, wxPanel)
	EVT_LISTBOX(IDM_SYMBOLLIST,		CMemoryWindow::OnSymbolListChange)
	EVT_HOST_COMMAND(wxID_ANY,		CMemoryWindow::OnHostMessage)
	EVT_BUTTON(IDM_SETVALBUTTON,	CMemoryWindow::SetMemoryValue)
	EVT_BUTTON(IDM_REFRESH,			CMemoryWindow::Refresh)
	EVT_BUTTON(IDM_DUMP_MEMORY,		CMemoryWindow::OnDumpMemory)
	EVT_BUTTON(IDM_DUMP_MEM2,		CMemoryWindow::OnDumpMem2)
	EVT_BUTTON(IDM_DUMP_FAKEVMEM,	CMemoryWindow::OnDumpFakeVMEM)
	EVT_CHECKBOX(IDM_U8,			CMemoryWindow::U8)
	EVT_CHECKBOX(IDM_U16,			CMemoryWindow::U16)
	EVT_CHECKBOX(IDM_U32,			CMemoryWindow::U32)
	EVT_BUTTON(IDM_SEARCH,			CMemoryWindow::onSearch)
	EVT_CHECKBOX(IDM_ASCII,			CMemoryWindow::onAscii)
	EVT_CHECKBOX(IDM_HEX,			CMemoryWindow::onHex)
END_EVENT_TABLE()

CMemoryWindow::CMemoryWindow(wxWindow* parent, wxWindowID id,
		const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxPanel(parent, id, pos, size, style, name)
{
	wxBoxSizer* sizerBig   = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizerRight = new wxBoxSizer(wxVERTICAL);
	// Didn't see anything useful in the left part
	//wxBoxSizer* sizerLeft  = new wxBoxSizer(wxVERTICAL);

	DebugInterface* di = &PowerPC::debug_interface;

	//symbols = new wxListBox(this, IDM_SYMBOLLIST, wxDefaultPosition,
	//	   	wxSize(20, 100), 0, NULL, wxLB_SORT);
	//sizerLeft->Add(symbols, 1, wxEXPAND);
	memview = new CMemoryView(di, this);
	memview->dataType = 0;
	//sizerBig->Add(sizerLeft, 1, wxEXPAND);
	sizerBig->Add(memview, 20, wxEXPAND);
	sizerBig->Add(sizerRight, 0, wxEXPAND | wxALL, 3);

	sizerRight->Add(valbox = new wxTextCtrl(this, IDM_VALBOX, _T("")));
	sizerRight->Add(new wxButton(this, IDM_SETVALBUTTON, _("Set &Value")), 0, wxEXPAND);

	sizerRight->AddSpacer(5);
	sizerRight->Add(new wxButton(this, IDM_REFRESH, _("&Refresh")), 0, wxEXPAND);

	sizerRight->AddSpacer(5);
	sizerRight->Add(new wxButton(this, IDM_DUMP_MEMORY, _("&Dump MRAM")), 0, wxEXPAND);
	sizerRight->Add(new wxButton(this, IDM_DUMP_MEM2, _("&Dump EXRAM")), 0, wxEXPAND);

	if (SConfig::GetInstance().m_LocalCoreStartupParameter.iTLBHack == 1)
		sizerRight->Add(new wxButton(this, IDM_DUMP_FAKEVMEM, _("&Dump FakeVMEM")));

	wxStaticBoxSizer* sizerSearchType = new wxStaticBoxSizer(wxVERTICAL, this, _("Search"));

	sizerSearchType->Add(btnSearch = new wxButton(this, IDM_SEARCH, _("Search")));
	sizerSearchType->Add(chkAscii = new wxCheckBox(this, IDM_ASCII, _T("&Ascii ")));
	sizerSearchType->Add(chkHex = new wxCheckBox(this, IDM_HEX, _("&Hex")));
	sizerRight->Add(sizerSearchType);
	wxStaticBoxSizer* sizerDataTypes = new wxStaticBoxSizer(wxVERTICAL, this, _("Data Type"));

	sizerDataTypes->Add(chk8 = new wxCheckBox(this, IDM_U8, _T("&U8")));
	sizerDataTypes->Add(chk16 = new wxCheckBox(this, IDM_U16, _T("&U16")));
	sizerDataTypes->Add(chk32 = new wxCheckBox(this, IDM_U32, _T("&U32")));
	sizerRight->Add(sizerDataTypes, 0, wxEXPAND);
	SetSizer(sizerBig);
	chkHex->SetValue(1); //Set defaults
	chk8->SetValue(1);

	//sizerLeft->Fit(this);
	sizerRight->Fit(this);
	sizerBig->Fit(this);
}

void CMemoryWindow::Save(IniFile& _IniFile) const
{
	// Prevent these bad values that can happen after a crash or hanging
	if(GetPosition().x != -32000 && GetPosition().y != -32000)
	{
		_IniFile.Set("MemoryWindow", "x", GetPosition().x);
		_IniFile.Set("MemoryWindow", "y", GetPosition().y);
		_IniFile.Set("MemoryWindow", "w", GetSize().GetWidth());
		_IniFile.Set("MemoryWindow", "h", GetSize().GetHeight());
	}
}

void CMemoryWindow::Load(IniFile& _IniFile)
{
	int x, y, w, h;
	_IniFile.Get("MemoryWindow", "x", &x, GetPosition().x);
	_IniFile.Get("MemoryWindow", "y", &y, GetPosition().y);
	_IniFile.Get("MemoryWindow", "w", &w, GetSize().GetWidth());
	_IniFile.Get("MemoryWindow", "h", &h, GetSize().GetHeight());
	SetSize(x, y, w, h);
}

void CMemoryWindow::JumpToAddress(u32 _Address)
{
	memview->Center(_Address);
}

void CMemoryWindow::Refresh(wxCommandEvent& event)
{
	memview->Refresh();
}

void CMemoryWindow::SetMemoryValue(wxCommandEvent& event)
{
	std::string str_val = WxStrToStr(valbox->GetValue());
	u32 addr;
	u32 val;

	if (!TryParse(std::string("0x") + str_val, &val))
	{
		PanicAlert("Invalid Value: %s", str_val.c_str());
		return;
	}

	Memory::Write_U32(val, memview->GetSelection());
	memview->Refresh();
}

void CMemoryWindow::Center(u32 addr)
{
	memview->Center(addr);
}

void CMemoryWindow::Update()
{
	memview->Refresh();
}

void CMemoryWindow::NotifyMapLoaded()
{
	symbols->Show(false); // hide it for faster filling
	symbols->Clear();
#if 0
	#ifdef _WIN32
	const FunctionDB::XFuncMap &syms = g_symbolDB.Symbols();
	for (FuntionDB::XFuncMap::iterator iter = syms.begin(); iter != syms.end(); ++iter)
	{
	int idx = symbols->Append(iter->second.name.c_str());
	symbols->SetClientData(idx, (void*)&iter->second);
	}
	#endif
#endif
	symbols->Show(true);
	Update();
}

void CMemoryWindow::OnSymbolListChange(wxCommandEvent& event)
{
	int index = symbols->GetSelection();
	if (index >= 0)
	{
		Symbol* pSymbol = static_cast<Symbol *>(symbols->GetClientData(index));
		if (pSymbol != NULL)
		{
			memview->Center(pSymbol->address);
		}
	}
}

void CMemoryWindow::OnHostMessage(wxCommandEvent& event)
{
	switch (event.GetId())
	{
		case IDM_NOTIFYMAPLOADED:
			NotifyMapLoaded();
			break;
	}
}

void DumpArray(const std::string& filename, const u8* data, size_t length)
{
	if (data)
	{
		File::IOFile f(filename, "wb");
		f.WriteBytes(data, length);
	}
}

// Write mram to file
void CMemoryWindow::OnDumpMemory( wxCommandEvent& event )
{
	DumpArray(File::GetUserPath(F_RAMDUMP_IDX), Memory::m_pRAM, Memory::REALRAM_SIZE);
}

// Write exram (aram or mem2) to file
void CMemoryWindow::OnDumpMem2( wxCommandEvent& event )
{	
	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bWii)
	{
		DumpArray(File::GetUserPath(F_ARAMDUMP_IDX), Memory::m_pEXRAM, Memory::EXRAM_SIZE);
	}
	else
	{
		DumpArray(File::GetUserPath(F_ARAMDUMP_IDX), DSP::GetARAMPtr(), DSP::ARAM_SIZE);
	}
}

// Write fake vmem to file
void CMemoryWindow::OnDumpFakeVMEM( wxCommandEvent& event )
{
	DumpArray(File::GetUserPath(F_FAKEVMEMDUMP_IDX), Memory::m_pVirtualFakeVMEM, Memory::FAKEVMEM_SIZE);
}

void CMemoryWindow::U8(wxCommandEvent& event)
{
	chk16->SetValue(0);
	chk32->SetValue(0);
	memview->dataType = 0;
	memview->Refresh();
}

void CMemoryWindow::U16(wxCommandEvent& event)
{
	chk8->SetValue(0);
	chk32->SetValue(0);
	memview->dataType = 1;
	memview->Refresh();
}

void CMemoryWindow::U32(wxCommandEvent& event)
{
	chk16->SetValue(0);
	chk8->SetValue(0);
	memview->dataType = 2;
	memview->Refresh();
}

void CMemoryWindow::onSearch(wxCommandEvent& event)
{
	u8* TheRAM = 0;
	u32 szRAM = 0;
	switch (memview->GetMemoryType())
	{
	case 0:
	default:
		if (Memory::m_pRAM)
		{
			TheRAM = Memory::m_pRAM;
			szRAM = Memory::REALRAM_SIZE;
		}
		break;
	case 1:
		{
			u8* aram = DSP::GetARAMPtr();
			if (aram)
			{
				TheRAM = aram;
				szRAM = DSP::ARAM_SIZE;
			}
		}
		break;
	}
	//Now we have memory to look in
	//Are we looking for ASCII string, or hex?
	//memview->cu
	wxString rawData = valbox->GetValue();
	std::vector<u8> Dest; //May need a better name
	u32 size = 0;
	int pad = rawData.size()%2; //If it's uneven
	unsigned int i = 0;
	long count = 0;
	char copy[3] = {0};
	long newsize = 0;
	unsigned char *tmp2 = 0;
	char* tmpstr = 0;

	if (chkHex->GetValue())
	{
		//We are looking for hex
		//If it's uneven
		size = (rawData.size()/2) + pad;
		Dest.resize(size+32);
		newsize = rawData.size();

		if (pad)
		{
			tmpstr = new char[newsize + 2];
			memset(tmpstr, 0, newsize + 2);
			tmpstr[0] = '0';
		}
		else
		{
			tmpstr = new char[newsize + 1];
			memset(tmpstr, 0, newsize + 1);
		}
		sprintf(tmpstr, "%s%s", tmpstr, WxStrToStr(rawData).c_str());
		tmp2 = &Dest.front();
		count = 0;
		for(i = 0; i < strlen(tmpstr); i++)
		{
			copy[0] = tmpstr[i];
			copy[1] = tmpstr[i+1];
			copy[2] = 0;
			int tmpint;
			sscanf(copy, "%02x", &tmpint);
			tmp2[count++] = tmpint;
			// Dest[count] should now be the hex of what the two chars were!
			// Also should add a check to make sure it's A-F only
			//sscanf(copy, "%02x", &tmp2[count++]);
			i += 1;
		}
		delete[] tmpstr;
	}
	else
	{
		//Looking for an ascii string
		size = rawData.size();
		Dest.resize(size+1);
		tmpstr = new char[size+1];

		tmp2 = &Dest.front();
		sprintf(tmpstr, "%s", WxStrToStr(rawData).c_str());

		for(i = 0; i < size; i++)
			tmp2[i] = tmpstr[i];

		delete[] tmpstr;
	}

	if(size)
	{
		unsigned char* pnt = &Dest.front();
		unsigned int k = 0;
		//grab
		i = memview->GetSelection() + 4;
		for( ; i < szRAM; i++)
		{
			for(k = 0; k < size; k++)
			{
				if(i + k > szRAM) break;
				if(k > size) break;
				if(pnt[k] != TheRAM[i+k])
				{
					k = 0;
					break;
				}
			}
			if(k == size)
			{
				//Match was found
				wxMessageBox(_("A match was found. Placing viewer at the offset."));
				wxChar tmpwxstr[128] = {0};
				wxSprintf(tmpwxstr, _T("%08x"), i);
				wxString tmpwx(tmpwxstr);
				Center(i);
				return;
			}
		}
		wxMessageBox(_("No match was found."));
	}
}

void CMemoryWindow::onAscii(wxCommandEvent& event)
{
	chkHex->SetValue(0);
}

void CMemoryWindow::onHex(wxCommandEvent& event)
{
	chkAscii->SetValue(0);
}
