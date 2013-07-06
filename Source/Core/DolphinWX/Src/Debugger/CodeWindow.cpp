// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// Include
#include "Common.h"
#include "CommonPaths.h"

#include <wx/wx.h>
#include <wx/mimetype.h>

#include "Host.h"

#include "RegisterWindow.h"
#include "BreakpointWindow.h"
#include "MemoryWindow.h"
#include "JitWindow.h"

#include "CodeWindow.h"
#include "CodeView.h"

#include "../WxUtils.h"
#include "FileUtil.h"
#include "Core.h"
#include "HW/Memmap.h"
#include "HLE/HLE.h"
#include "Boot/Boot.h"
#include "LogManager.h"
#include "HW/CPU.h"
#include "PowerPC/PowerPC.h"
#include "PowerPC/JitInterface.h"
#include "PowerPC/JitCommon/JitBase.h"
#include "Debugger/PPCDebugInterface.h"
#include "Debugger/Debugger_SymbolMap.h"
#include "PowerPC/PPCAnalyst.h"
#include "PowerPC/Profiler.h"
#include "PowerPC/PPCSymbolDB.h"
#include "PowerPC/SignatureDB.h"
#include "PowerPC/PPCTables.h"

#include "ConfigManager.h"

extern "C"  // Bitmaps
{
	#include "../../resources/toolbar_add_memorycheck.c"
	#include "../../resources/toolbar_add_breakpoint.c"
}

// -------
// Main

BEGIN_EVENT_TABLE(CCodeWindow, wxPanel)
	// cpu
	EVT_MENU_RANGE(IDM_CPU_BEGIN, IDM_CPU_END, CCodeWindow::OnMenuCPU)
	EVT_MENU_RANGE(IDM_CPU_REFRESH_BEGIN, IDM_CPU_REFRESH_END, CCodeWindow::OnMenuCPURefresh)
	EVT_MENU_RANGE(IDM_CPU_CHANGE_BEGIN, IDM_CPU_CHANGE_END, CCodeWindow::OnMenuCPUChange)

	// symbols
	EVT_MENU_RANGE(IDM_CLEARSYMBOLS, IDM_PATCHHLEFUNCTIONS, CCodeWindow::OnSymbolsMenu)

	// profiler
	EVT_MENU_RANGE(IDM_PROFILE_BEGIN, IDM_PROFILE_END, CCodeWindow::OnProfilerMenu)

	// toolbar
	EVT_MENU_RANGE(IDT_DEBUG_BEGIN, IDT_DEBUG_END, CCodeWindow::OnCodeStep)
	EVT_TEXT(IDM_ADDRBOX, CCodeWindow::OnAddrBoxChange)

	// other
	EVT_MENU(IDM_FONTPICKER, CCodeWindow::OnChangeFont)
	EVT_LISTBOX(ID_SYMBOLLIST,			CCodeWindow::OnSymbolListChange)
	EVT_LISTBOX(ID_CALLSTACKLIST,		CCodeWindow::OnCallstackListChange)
	EVT_LISTBOX(ID_CALLERSLIST,			CCodeWindow::OnCallersListChange)
	EVT_LISTBOX(ID_CALLSLIST,			CCodeWindow::OnCallsListChange)

	EVT_HOST_COMMAND(wxID_ANY,			CCodeWindow::OnHostMessage)

END_EVENT_TABLE()

// Class
CCodeWindow::CCodeWindow(const SCoreStartupParameter& _LocalCoreStartupParameter, CFrame *parent,
	wxWindowID id, const wxPoint& position, const wxSize& size, long style, const wxString& name)
	: wxPanel((wxWindow*)parent, id, position, size, style, name)
	, Parent(parent)
	, m_RegisterWindow(NULL)
	, m_BreakpointWindow(NULL)
	, m_MemoryWindow(NULL)
	, m_JitWindow(NULL)
	, m_SoundWindow(NULL)
	, m_VideoWindow(NULL)
	, codeview(NULL)
{
	InitBitmaps();

	wxBoxSizer* sizerBig   = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizerLeft  = new wxBoxSizer(wxVERTICAL);

	DebugInterface* di = &PowerPC::debug_interface;

	codeview = new CCodeView(di, &g_symbolDB, this, ID_CODEVIEW);
	sizerBig->Add(sizerLeft, 2, wxEXPAND);
	sizerBig->Add(codeview, 5, wxEXPAND);

	sizerLeft->Add(callstack = new wxListBoxEx(this, ID_CALLSTACKLIST,
				wxDefaultPosition, wxSize(90, 100), wxArrayString(), wxLB_EXTENDED), 0, wxEXPAND);
	sizerLeft->Add(symbols = new wxListBox(this, ID_SYMBOLLIST,
				wxDefaultPosition, wxSize(90, 100), 0, NULL, wxLB_SORT), 1, wxEXPAND);
	sizerLeft->Add(calls = new wxListBox(this, ID_CALLSLIST, wxDefaultPosition,
				wxSize(90, 100), 0, NULL, wxLB_SORT), 0, wxEXPAND);
	sizerLeft->Add(callers = new wxListBox(this, ID_CALLERSLIST, wxDefaultPosition,
				wxSize(90, 100), 0, NULL, wxLB_SORT), 0, wxEXPAND);

	SetSizer(sizerBig);

	sizerLeft->Fit(this);
	sizerBig->Fit(this);
}

wxMenuBar *CCodeWindow::GetMenuBar()
{
	return Parent->GetMenuBar();
}

wxAuiToolBar *CCodeWindow::GetToolBar()
{
	return Parent->m_ToolBarDebug;
}

// ----------
// Events

void CCodeWindow::OnHostMessage(wxCommandEvent& event)
{
	switch (event.GetId())
	{
		case IDM_NOTIFYMAPLOADED:
			NotifyMapLoaded();
			if (m_BreakpointWindow) m_BreakpointWindow->NotifyUpdate();
			break;

		case IDM_UPDATEDISASMDIALOG:
			Update();
			//if (codeview) codeview->Center(PC);
			if (m_RegisterWindow) m_RegisterWindow->NotifyUpdate();
			break;

		case IDM_UPDATEBREAKPOINTS:
			Update();
			if (m_BreakpointWindow) m_BreakpointWindow->NotifyUpdate();
			break;
	}
}

// The Play, Stop, Step, Skip, Go to PC and Show PC buttons go here
void CCodeWindow::OnCodeStep(wxCommandEvent& event)
{
	switch (event.GetId())
	{
		case IDM_STEP:
			SingleStep();
			break;

		case IDM_STEPOVER:
			StepOver();
			break;

		case IDM_TOGGLE_BREAKPOINT:
			ToggleBreakpoint();
			break;

		case IDM_SKIP:
			PC += 4;
			Update();
			break;

		case IDM_SETPC:
			PC = codeview->GetSelection();
			Update();
			break;

		case IDM_GOTOPC:
			JumpToAddress(PC);
			break;

		case IDM_GOTO:
		{
			u32 address;
			wxString s_address;
			wxTextEntryDialog input_address(this
				, "Address:"
				, wxGetTextFromUserPromptStr
				, wxString::Format("%08x", codeview->GetSelection()));
			if (input_address.ShowModal() == wxID_OK)
			{
				s_address = input_address.GetValue().Trim().Trim(false);
				if (AsciiToHex(WxStrToStr(s_address).c_str(), address))
				{
					address = address - address % 4;
					JumpToAddress(address);
					m_MemoryWindow->Center(address);
				}
			}
			break;
		}
	}

	UpdateGUI();
}

void CCodeWindow::JumpToAddress(u32 _Address)
{
	codeview->Center(_Address);
	UpdateLists();
}

void CCodeWindow::OnCodeViewChange(wxCommandEvent &event)
{
	UpdateLists();
}

void CCodeWindow::OnAddrBoxChange(wxCommandEvent& event)
{
	if (!GetToolBar()) return;

	wxTextCtrl* pAddrCtrl = (wxTextCtrl*)GetToolBar()->FindControl(IDM_ADDRBOX);
	wxString txt = pAddrCtrl->GetValue();

	std::string text(WxStrToStr(txt));
	text = StripSpaces(text);
	if (text.size() == 8)
	{
		u32 addr;
		sscanf(text.c_str(), "%08x", &addr);
		JumpToAddress(addr);
	}

	event.Skip(1);
}

void CCodeWindow::OnCallstackListChange(wxCommandEvent& event)
{
	wxArrayInt selection;
	callstack->GetSelections(selection);
	int index = -1;
	if (!selection.IsEmpty())
		index = selection[0];

	if (index >= 0)
	{
		u32 address = (u32)(u64)(callstack->GetClientData(index));
		if (address)
			JumpToAddress(address);
	}
}

void CCodeWindow::OnCallersListChange(wxCommandEvent& event)
{
	int index = callers->GetSelection();
	if (index >= 0)
	{
		u32 address = (u32)(u64)(callers->GetClientData(index));
		if (address)
			JumpToAddress(address);
	}
}

void CCodeWindow::OnCallsListChange(wxCommandEvent& event)
{
	int index = calls->GetSelection();
	if (index >= 0)
	{
		u32 address = (u32)(u64)(calls->GetClientData(index));
		if (address)
			JumpToAddress(address);
	}
}

void CCodeWindow::SingleStep()
{
	if (CCPU::IsStepping())
	{
		JitInterface::InvalidateICache(PC, 4);
		CCPU::StepOpcode(&sync_event);
		wxThread::Sleep(20);
		// need a short wait here
		JumpToAddress(PC);
		Update();
		Host_UpdateLogDisplay();
	}
}

void CCodeWindow::StepOver()
{
	if (CCPU::IsStepping())
	{
		UGeckoInstruction inst = Memory::Read_Instruction(PC);
		if (inst.LK)
		{
			PowerPC::breakpoints.Add(PC + 4, true);
			CCPU::EnableStepping(false);
			JumpToAddress(PC);
			Update();
		}
		else
		{
			SingleStep();
		}

		UpdateGUI();
	}
}

void CCodeWindow::ToggleBreakpoint()
{
	if (CCPU::IsStepping())
	{
		if (codeview) codeview->ToggleBreakpoint(codeview->GetSelection());
		Update();
	}
}

void CCodeWindow::UpdateLists()
{
	callers->Clear();
	u32 addr = codeview->GetSelection();
	Symbol *symbol = g_symbolDB.GetSymbolFromAddr(addr);
	if (!symbol)
		return;

	for (int i = 0; i < (int)symbol->callers.size(); i++)
	{
		u32 caller_addr = symbol->callers[i].callAddress;
		Symbol *caller_symbol = g_symbolDB.GetSymbolFromAddr(caller_addr);
		if (caller_symbol)
		{
			int idx = callers->Append(StrToWxStr(StringFromFormat
						("< %s (%08x)", caller_symbol->name.c_str(), caller_addr).c_str()));
			callers->SetClientData(idx, (void*)(u64)caller_addr);
		}
	}

	calls->Clear();
	for (int i = 0; i < (int)symbol->calls.size(); i++)
	{
		u32 call_addr = symbol->calls[i].function;
		Symbol *call_symbol = g_symbolDB.GetSymbolFromAddr(call_addr);
		if (call_symbol)
		{
			int idx = calls->Append(StrToWxStr(StringFromFormat
						("> %s (%08x)", call_symbol->name.c_str(), call_addr).c_str()));
			calls->SetClientData(idx, (void*)(u64)call_addr);
		}
	}
}

void CCodeWindow::UpdateCallstack()
{
	if (Core::GetState() == Core::CORE_STOPPING) return;

	callstack->Clear();

	std::vector<Dolphin_Debugger::CallstackEntry> stack;

	bool ret = Dolphin_Debugger::GetCallstack(stack);

	for (size_t i = 0; i < stack.size(); i++)
	{
		int idx = callstack->Append(StrToWxStr(stack[i].Name));
		callstack->SetClientData(idx, (void*)(u64)stack[i].vAddress);
	}

	if (!ret)
		callstack->Append(StrToWxStr("invalid callstack"));
}

void CCodeWindow::CreateMenu(wxMenuBar *pMenuBar)
{
	// CPU
	wxMenu* pCoreMenu = new wxMenu;

	wxMenuItem* interpreter = pCoreMenu->AppendCheckItem(IDM_INTERPRETER, _("&Interpreter"));

	pCoreMenu->AppendSeparator();
	pCoreMenu->AppendRadioItem(IDM_COMPILER_DEFAULT, _("&Default"));
	pCoreMenu->AppendRadioItem(IDM_COMPILER_IL, _("I&ntermediate language"));
	pCoreMenu->AppendCheckItem(IDM_SKIP_IDLE, _("&Skip idle"));
	pCoreMenu->AppendCheckItem(IDM_JIT_BLOCK_LINK, _("&Block link"));
	pCoreMenu->Append(IDM_JIT_CLEAR_CACHE, _("&Reload code"));
	pCoreMenu->Append(IDM_JIT_LARGE_CACHE, _("&Large cache")
		, _("Store all recompiled code simultaneously"), wxITEM_CHECK);

	wxMenu *disable = new wxMenu;

	disable->AppendCheckItem(IDM_JIT, _("&All"), _("Disable recompilation"));

	disable->AppendSeparator();
	disable->AppendCheckItem(IDM_JIT_BRANCH, _("&Branch"), wxEmptyString);

	disable->AppendSeparator();
	disable->AppendCheckItem(IDM_JIT_LS, _("&LoadStore"));
	disable->AppendCheckItem(IDM_JIT_LS_LBZX, _("   &lbzx"));
	disable->AppendCheckItem(IDM_JIT_LS_LXZ, _("   l&Xz"));
	disable->AppendCheckItem(IDM_JIT_LS_LWZ, _("   l&wz"));
	disable->AppendCheckItem(IDM_JIT_LS_F, _("   &Floating"));
	disable->AppendCheckItem(IDM_JIT_LS_P, _("   &Paired"));

	disable->AppendSeparator();
	disable->AppendCheckItem(IDM_JIT_I, _("&Integer"));
	disable->AppendCheckItem(IDM_JIT_FP, _("&FloatingPoint"));
	disable->AppendCheckItem(IDM_JIT_P, _("&Paired"));
	disable->AppendCheckItem(IDM_JIT_SR, _("&SystemRegisters"));

	pCoreMenu->AppendSubMenu(disable, _("Disable"));

	pCoreMenu->AppendSeparator();
	pCoreMenu->Append(IDM_LOGINSTRUCTIONS, _("&Log instruction coverage"));
	pCoreMenu->Append(IDM_SEARCHINSTRUCTION, _("&Search for an op"));

	pMenuBar->Append(pCoreMenu, _("&CPU"));

	// Debug
	wxMenu* pDebugMenu = new wxMenu;

	pDebugMenu->AppendCheckItem(IDM_DEBUGGING, _("&Enable"));

	pDebugMenu->AppendSeparator();
	pDebugMenu->AppendCheckItem(IDM_LOG_MEMORY, _("&Log memory"));
	pDebugMenu->AppendCheckItem(IDM_LOG_GECKO, _("&Log Gecko"));

	pDebugMenu->AppendSeparator();
	pDebugMenu->Append(IDM_STEP, _("Step &Into\tF11"));
	pDebugMenu->Append(IDM_STEPOVER, _("Step &Over\tF10"));
	pDebugMenu->Append(IDM_TOGGLE_BREAKPOINT, _("Toggle &Breakpoint\tF9"));
	pDebugMenu->Append(IDM_GOTO, _("&Goto\tCtrl+G"));

	pMenuBar->Append(pDebugMenu, _("&Debug"));

	CreateMenuSymbols(pMenuBar);
}

void CCodeWindow::CreateMenuOptions(wxMenu* pMenu)
{
	wxMenuItem* boottopause = pMenu->Append(IDM_BOOTTOPAUSE, _("Boot to pause"),
		_("Start the game directly instead of booting to pause"),
		wxITEM_CHECK);

	wxMenuItem* automaticstart = pMenu->Append(IDM_AUTOMATICSTART, _("&Automatic start"),
		StrToWxStr(
		"Automatically load the Default ISO when Dolphin starts, or the last game you loaded,"
		" if you have not given it an elf file with the --elf command line. [This can be"
		" convenient if you are bug-testing with a certain game and want to rebuild"
		" and retry it several times, either with changes to Dolphin or if you are"
		" developing a homebrew game.]"),
		wxITEM_CHECK);

	pMenu->Append(IDM_FONTPICKER, _("&Font..."), wxEmptyString, wxITEM_NORMAL);
}

void CCodeWindow::OnMenuCPU(wxCommandEvent& event)
{
	SCoreStartupParameter &boot = SConfig::GetInstance().m_LocalCoreStartupParameter;

	switch (event.GetId())
	{
	case IDM_BOOTTOPAUSE:
		boot.bBootToPause = event.IsChecked();
		return;

	case IDM_AUTOMATICSTART:
		boot.bAutomaticStart = event.IsChecked();
		return;

	case IDM_LOG_MEMORY:
		boot.bLogMemory = event.IsChecked();

	case IDM_LOG_GECKO:
		boot.bLogGecko = event.IsChecked();
		return;

	case IDM_JIT_CLEAR_CACHE:
		JitInterface::ClearSafe();
		break;

	case IDM_LOGINSTRUCTIONS:
		PPCTables::LogCompiledInstructions();
		break;

	case IDM_SEARCHINSTRUCTION:
	{
		wxString str;
		str = wxGetTextFromUser(_T(""), wxT("Op?"), wxEmptyString, this);
		for (u32 addr = 0x80000000; addr < 0x80100000; addr += 4)
		{
			const char *name = PPCTables::GetInstructionName(Memory::ReadUnchecked_U32(addr));
			auto const wx_name = WxStrToStr(str);
			if (name && (wx_name == name))
			{
				NOTICE_LOG(POWERPC, "Found %s at %08x", wx_name.c_str(), addr);
			}
		}
		break;
	}
	}

	UpdateGUI();
}

void CCodeWindow::OnMenuCPURefresh(wxCommandEvent& event)
{
	SCoreStartupParameter &boot = SConfig::GetInstance().m_LocalCoreStartupParameter;

	switch (event.GetId())
	{
	case IDM_JIT:
		boot.bJIT = !event.IsChecked();
		break;

	case IDM_JIT_BRANCH:
		boot.bJITBranch = !event.IsChecked();
		break;

	case IDM_JIT_LS:
		boot.bJITLoadStore = !event.IsChecked();
		break;
	case IDM_JIT_LS_LXZ:
		boot.bJITLoadStorelXz = !event.IsChecked();
		break;
	case IDM_JIT_LS_LWZ:
		boot.bJITLoadStorelwz = !event.IsChecked();
		break;
	case IDM_JIT_LS_LBZX:
		boot.bJITLoadStorelbzx = !event.IsChecked();
		break;
	case IDM_JIT_LS_F:
		boot.bJITLoadStoreFloating = !event.IsChecked();
		break;
	case IDM_JIT_LS_P:
		boot.bJITLoadStorePaired = !event.IsChecked();
		break;

	case IDM_JIT_I:
		boot.bJITInteger = !event.IsChecked();
		break;
	case IDM_JIT_FP:
		boot.bJITFloatingPoint = !event.IsChecked();
		break;
	case IDM_JIT_P:
		boot.bJITPaired = !event.IsChecked();
		break;
	case IDM_JIT_SR:
		boot.bJITSystemRegisters = !event.IsChecked();
		break;
	}

	JitInterface::ClearSafe();

	UpdateGUI();
}

void CCodeWindow::OnMenuCPUChange(wxCommandEvent& event)
{
	SCoreStartupParameter &boot = SConfig::GetInstance().m_LocalCoreStartupParameter;

	switch (event.GetId())
	{
	case IDM_DEBUGGING:
		boot.bEnableDebugging = event.IsChecked();
		break;

	case IDM_INTERPRETER:
		boot.bInterpreter = event.IsChecked();
		break;

	case IDM_COMPILER_DEFAULT:
		boot.iCompiler = 0;
		break;

	case IDM_COMPILER_IL:
		boot.iCompiler = 1;
		break;

	case IDM_SKIP_IDLE:
		boot.bSkipIdle = event.IsChecked();
		break;

	case IDM_JIT_BLOCK_LINK:
		boot.bJITBlockLink = event.IsChecked();
		break;

	case IDM_JIT_LARGE_CACHE:
		boot.bJITLargeCache = event.IsChecked();
		break;
	}

	PowerPC::Change();

	UpdateGUI();
}

// Toolbar
void CCodeWindow::InitBitmaps()
{
	// load original size 48x48
	m_Bitmaps[Toolbar_Step] = wxGetBitmapFromMemory(toolbar_add_breakpoint_png);
	m_Bitmaps[Toolbar_StepOver] = wxGetBitmapFromMemory(toolbar_add_memcheck_png);
	m_Bitmaps[Toolbar_Skip] = wxGetBitmapFromMemory(toolbar_add_memcheck_png);
	m_Bitmaps[Toolbar_GotoPC] = wxGetBitmapFromMemory(toolbar_add_memcheck_png);
	m_Bitmaps[Toolbar_SetPC] = wxGetBitmapFromMemory(toolbar_add_memcheck_png);

	// scale to 24x24 for toolbar
	for (size_t n = 0; n < ToolbarDebugBitmapMax; n++)
		m_Bitmaps[n] = wxBitmap(m_Bitmaps[n].ConvertToImage().Scale(24, 24));
}

void CCodeWindow::PopulateToolbar(wxAuiToolBar* toolBar)
{
	int w = m_Bitmaps[0].GetWidth(),
		h = m_Bitmaps[0].GetHeight();

	toolBar->SetToolBitmapSize(wxSize(w, h));
	toolBar->AddTool(IDM_STEP,		_("Step"),			m_Bitmaps[Toolbar_Step]);
	toolBar->AddTool(IDM_STEPOVER,	_("Step Over"),		m_Bitmaps[Toolbar_StepOver]);
	toolBar->AddTool(IDM_SKIP,		_("Skip"),			m_Bitmaps[Toolbar_Skip]);
	toolBar->AddSeparator();
	toolBar->AddTool(IDM_GOTOPC,		_("Show PC"),		m_Bitmaps[Toolbar_GotoPC]);
	toolBar->AddTool(IDM_SETPC,		_("Set PC"),		m_Bitmaps[Toolbar_SetPC]);
	toolBar->AddSeparator();
	toolBar->AddControl(new wxTextCtrl(toolBar, IDM_ADDRBOX, _T("")));

	toolBar->Realize();
}

// Update GUI
void CCodeWindow::Update()
{
	if (!codeview) return;

	codeview->Refresh();
	UpdateCallstack();
	Parent->UpdateGUI();

	// Do not automatically show the current PC position when a breakpoint is hit or
	// when we pause since this can be called at other times too.
	//codeview->Center(PC);
}

void CCodeWindow::UpdateGUI()
{
	if (!codeview) return;

	bool Initialized = (Core::GetState() != Core::CORE_UNINITIALIZED);
	bool Pause = (Core::GetState() == Core::CORE_PAUSE);
	bool Stepping = CCPU::IsStepping();
	wxAuiToolBar* ToolBar = GetToolBar();

	// Toolbar
	if (!ToolBar) return;

	if (!Initialized)
	{
		ToolBar->EnableTool(IDM_STEPOVER, false);
		ToolBar->EnableTool(IDM_SKIP, false);
	}
	else
	{
		if (!Stepping)
		{
			ToolBar->EnableTool(IDM_STEPOVER, false);
			ToolBar->EnableTool(IDM_SKIP, false);
		}
		else
		{
			ToolBar->EnableTool(IDM_STEPOVER, true);
			ToolBar->EnableTool(IDM_SKIP, true);
		}
	}

	ToolBar->EnableTool(IDM_STEP, Initialized && Stepping);

	if (ToolBar) ToolBar->Realize();



	// Menu bar
	// ------------------
	// JIT
	GetMenuBar()->Enable(IDM_SEARCHINSTRUCTION, Initialized);
	GetMenuBar()->Enable(IDM_JIT_CLEAR_CACHE, Initialized);
	GetMenuBar()->Enable(IDM_COMPILER_DEFAULT, !Initialized);
	GetMenuBar()->Enable(IDM_COMPILER_IL, !Initialized);

	// Symbols
	GetMenuBar()->Enable(IDM_CLEARSYMBOLS, Initialized);
	GetMenuBar()->Enable(IDM_SCANFUNCTIONS, Initialized);
	GetMenuBar()->Enable(IDM_LOADMAPFILE, Initialized);
	GetMenuBar()->Enable(IDM_SAVEMAPFILE, Initialized);
	GetMenuBar()->Enable(IDM_SAVEMAPFILEWITHCODES, Initialized);
	GetMenuBar()->Enable(IDM_CREATESIGNATUREFILE, Initialized);
	GetMenuBar()->Enable(IDM_RENAME_SYMBOLS, Initialized);
	GetMenuBar()->Enable(IDM_USESIGNATUREFILE, Initialized);
	GetMenuBar()->Enable(IDM_PATCHHLEFUNCTIONS, Initialized);

	SCoreStartupParameter boot = SConfig::GetInstance().m_LocalCoreStartupParameter;

	GetMenuBar()->Check(IDM_AUTOMATICSTART, boot.bAutomaticStart);
	GetMenuBar()->Check(IDM_BOOTTOPAUSE, boot.bBootToPause);

	GetMenuBar()->Check(IDM_DEBUGGING, boot.bEnableDebugging);
	GetMenuBar()->Check(IDM_LOG_MEMORY, boot.bLogMemory);
	GetMenuBar()->Check(IDM_LOG_GECKO, boot.bLogGecko);

	GetMenuBar()->Check(IDM_INTERPRETER, boot.bInterpreter);
	GetMenuBar()->Check(IDM_COMPILER_DEFAULT, boot.iCompiler == 0);
	GetMenuBar()->Check(IDM_COMPILER_IL, boot.iCompiler == 1);
	GetMenuBar()->Check(IDM_SKIP_IDLE, boot.bSkipIdle);
	GetMenuBar()->Check(IDM_JIT_BLOCK_LINK, boot.bJITBlockLink);

	GetMenuBar()->Check(IDM_JIT_BRANCH, !boot.bJITBranch);
	GetMenuBar()->Check(IDM_JIT, !boot.bJIT);
	GetMenuBar()->Check(IDM_JIT_LS, !boot.bJITLoadStore);
	GetMenuBar()->Check(IDM_JIT_LS_LXZ, !boot.bJITLoadStorelXz);
	GetMenuBar()->Check(IDM_JIT_LS_LWZ, !boot.bJITLoadStorelwz);
	GetMenuBar()->Check(IDM_JIT_LS_LBZX, !boot.bJITLoadStorelbzx);
	GetMenuBar()->Check(IDM_JIT_LS_F, !boot.bJITLoadStoreFloating);
	GetMenuBar()->Check(IDM_JIT_LS_P, !boot.bJITLoadStorePaired);
	GetMenuBar()->Check(IDM_JIT_I, !boot.bJITInteger);
	GetMenuBar()->Check(IDM_JIT_FP, !boot.bJITFloatingPoint);
	GetMenuBar()->Check(IDM_JIT_P, !boot.bJITPaired);
	GetMenuBar()->Check(IDM_JIT_SR, !boot.bJITSystemRegisters);

	// Update Fonts
	callstack->SetFont(DebuggerFont);
	symbols->SetFont(DebuggerFont);
	callers->SetFont(DebuggerFont);
	calls->SetFont(DebuggerFont);
}
