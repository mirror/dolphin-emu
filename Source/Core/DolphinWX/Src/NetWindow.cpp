// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <FileUtil.h>
#include <IniFile.h>

#include "WxUtils.h"
#include "NetPlayClient.h"
#include "NetPlayServer.h"
#include "NetWindow.h"
#include "Frame.h"
#include "Core.h"
#include "ConfigManager.h"
#include "Host.h"

#include <sstream>
#include <string>

#include <wx/clipbrd.h>

#define NETPLAY_TITLEBAR	"Dolphin NetPlay"
#define INITIAL_PAD_BUFFER_SIZE 20

static wxString FailureReasonStringForHostLabel(int reason)
{
	switch (reason)
	{
	case TraversalClient::BadHost:
		return _("(Error: Bad host)");
	case TraversalClient::VersionTooOld:
		return _("(Error: Dolphin too old)");
	case TraversalClient::ServerForgotAboutUs:
		return _("(Error: Disconnected)");
	case TraversalClient::SocketSendError:
		return _("(Error: Socket)");
	case TraversalClient::ResendTimeout:
		return _("(Error: Timeout)");
	default:
		return _("(Error: Unknown)");
	}
}

static wxString FailureReasonStringForDialog(int reason)
{
	switch (reason)
	{
	case TraversalClient::BadHost:
	{
		auto server = StrToWxStr(SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayCentralServer);
		return wxString::Format(_("Couldn't look up central server %s"), server);
	}
	case TraversalClient::VersionTooOld:
		return _("Dolphin too old for traversal server");
	case TraversalClient::ServerForgotAboutUs:
		return _("Disconnected from traversal server");
	case TraversalClient::SocketSendError:
		return _("Socket error sending to traversal server");
	case TraversalClient::ResendTimeout:
		return _("Timeout connecting to traversal server");
	case TraversalClient::ConnectFailedError + TraversalConnectFailedClientDidntRespond:
		return _("Traversal server timed out connecting to the host");
	case TraversalClient::ConnectFailedError + TraversalConnectFailedClientFailure:
		return _("Server rejected traversal attempt");
	case TraversalClient::ConnectFailedError + TraversalConnectFailedNoSuchClient:
		return _("Invalid host");
	case NetPlayClient::ServerError + CON_ERR_SERVER_FULL:
		return _("Server full");
	case NetPlayClient::ServerError + CON_ERR_GAME_RUNNING:
		return _("Game already running");
	case NetPlayClient::ServerError + CON_ERR_VERSION_MISMATCH:
		return _("Dolphin version mismatch");
	case NetPlayClient::InvalidPacket:
		return _("Bad packet from server");
	case NetPlayClient::ReceivedENetDisconnect:
		return _("Disconnected");
	default:
		return wxString::Format(_("Unknown error %x"), reason);
	}
}

static wxString ClassNameString(IOSync::Class::ClassID cls)
{
	switch (cls)
	{
	case IOSync::Class::ClassSI:
		return _("Controllers");
	case IOSync::Class::ClassEXI:
		return _("Memory Cards");
	default:
		abort();
	}
}

BEGIN_EVENT_TABLE(NetPlayDiag, wxFrame)
	EVT_COMMAND(wxID_ANY, wxEVT_THREAD, NetPlayDiag::OnThread)
	EVT_MENU(IDM_DESYNC_DETECTION, NetPlayDiag::OnDesyncDetection)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(ConnectDiag, wxDialog)
	EVT_COMMAND(wxID_ANY, wxEVT_THREAD, ConnectDiag::OnThread)
END_EVENT_TABLE()

static std::unique_ptr<NetPlayServer> netplay_server;
static std::unique_ptr<NetPlayClient> netplay_client;
extern CFrame* main_frame;
NetPlayDiag *NetPlayDiag::npd = NULL;

NetPlayDiag::NetPlayDiag(wxWindow* const parent, const std::string& game, const bool is_hosting, bool print_host_id_to_stdout)
	: wxFrame(parent, wxID_ANY, wxT(NETPLAY_TITLEBAR), wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE | wxTAB_TRAVERSAL)
	, m_selected_game(game)
	, m_start_btn(NULL)
	, m_is_hosting(is_hosting)
	, m_print_host_id_to_stdout(print_host_id_to_stdout)
{
	npd = this;
	wxPanel* const panel = new wxPanel(this);
	panel->Bind(wxEVT_RIGHT_DOWN, &NetPlayDiag::OnRightClick, this);
	m_device_map_diag = NULL;
	m_is_running = false;
	m_lag_timer.Bind(wxEVT_TIMER, &NetPlayDiag::LagWarningTimerHit, this);

	// top crap
	m_top_szr = new wxBoxSizer(wxHORIZONTAL);
	m_game_label = new wxStaticText(panel, wxID_ANY, "");
	m_top_szr->Add(m_game_label, 0, wxEXPAND);
	m_warn_label = new wxStaticText(panel, wxID_ANY, "");
	m_warn_label->SetForegroundColour(*wxRED);
	m_top_szr->AddStretchSpacer();
	m_top_szr->Add(m_warn_label, 0, wxEXPAND | wxRIGHT, 3);
	UpdateGameName();

	// middle crap

	// chat
	wxBoxSizer* const nickname_szr = new wxBoxSizer(wxHORIZONTAL);
	nickname_szr->Add(new wxStaticText(panel, wxID_ANY, _("Nickname: ")), 0, wxCENTER);
	m_name_text = new wxTextCtrl(panel, wxID_ANY, StrToWxStr(SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayNickname));
	m_name_text->Bind(wxEVT_KILL_FOCUS, &NetPlayDiag::OnDefocusName, this);
	nickname_szr->Add(m_name_text, 1, wxCENTER);

	m_chat_text = new wxTextCtrl(panel, wxID_ANY, wxEmptyString
		, wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_MULTILINE);

	m_chat_msg_text = new wxTextCtrl(panel, wxID_ANY, wxEmptyString
		, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_chat_msg_text->Bind(wxEVT_COMMAND_TEXT_ENTER, &NetPlayDiag::OnChat, this);

	wxButton* const chat_msg_btn = new wxButton(panel, wxID_ANY, _("Send"));
	chat_msg_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnChat, this);

	wxBoxSizer* const chat_msg_szr = new wxBoxSizer(wxHORIZONTAL);
	chat_msg_szr->Add(m_chat_msg_text, 1);
	chat_msg_szr->Add(chat_msg_btn, 0);

	wxStaticBoxSizer* const chat_szr = new wxStaticBoxSizer(wxVERTICAL, panel, _("Chat"));
	chat_szr->Add(nickname_szr, 0, wxEXPAND | wxBOTTOM, 5);
	chat_szr->Add(m_chat_text, 1, wxEXPAND);
	chat_szr->Add(chat_msg_szr, 0, wxEXPAND | wxTOP, 5);

	m_player_lbox = new wxListBox(panel, wxID_ANY, wxDefaultPosition, wxSize(256, -1));

	wxStaticBoxSizer* const player_szr = new wxStaticBoxSizer(wxVERTICAL, panel, _("Players"));

	if (is_hosting)
	{
		wxBoxSizer* const host_szr = new wxBoxSizer(wxHORIZONTAL);
		m_host_type_choice = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxSize(60, -1));
		m_host_type_choice->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &NetPlayDiag::OnChoice, this);
		m_host_type_choice->Append(_("ID:"));
		host_szr->Add(m_host_type_choice);
		// The initial label is for sizing...
		m_host_label = new wxStaticText(panel, wxID_ANY, "555.555.555.555:55555", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxALIGN_LEFT);
		// Update() should fix this immediately.
		m_host_label->SetLabel(_(""));
		host_szr->Add(m_host_label, 1, wxLEFT | wxCENTER, 5);
		m_host_copy_btn = new wxButton(panel, wxID_ANY, _("Copy"));
		m_host_copy_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnCopyIP, this);
		m_host_copy_btn->Disable();
		host_szr->Add(m_host_copy_btn, 0, wxLEFT | wxCENTER, 5);
		player_szr->Add(host_szr, 0, wxEXPAND | wxBOTTOM, 5);

		m_host_type_choice->Select(0);
		UpdateHostLabel();
	}

	player_szr->Add(m_player_lbox, 1, wxEXPAND);
	// player list
	if (is_hosting)
	{
		wxButton* const player_config_btn = new wxButton(panel, wxID_ANY, _("Configure Pads"));
		player_config_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnConfigPads, this);
		player_szr->Add(player_config_btn, 0, wxEXPAND | wxTOP, 5);
	}

	wxBoxSizer* const mid_szr = new wxBoxSizer(wxHORIZONTAL);
	mid_szr->Add(chat_szr, 1, wxEXPAND | wxRIGHT, 5);
	mid_szr->Add(player_szr, 0, wxEXPAND);

	// bottom crap
	wxButton* const quit_btn = new wxButton(panel, wxID_ANY, _("Quit"));
	quit_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnQuit, this);

	wxBoxSizer* const bottom_szr = new wxBoxSizer(wxHORIZONTAL);
	if (is_hosting)
	{
		m_start_btn = new wxButton(panel, wxID_ANY, _("Start"));
		m_start_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnStart, this);
		bottom_szr->Add(m_start_btn);

		bottom_szr->Add(new wxStaticText(panel, wxID_ANY, _("Buffer:")), 0, wxLEFT | wxCENTER, 5 );
		wxSpinCtrl* const padbuf_spin = new wxSpinCtrl(panel, wxID_ANY, wxT("20")
			, wxDefaultPosition, wxSize(64, -1), wxSP_ARROW_KEYS, 0, 200, INITIAL_PAD_BUFFER_SIZE);
		padbuf_spin->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED, &NetPlayDiag::OnAdjustBuffer, this);
		bottom_szr->Add(padbuf_spin, 0, wxCENTER);

		m_memcard_write = new wxCheckBox(panel, wxID_ANY, _("Write memcards (GC)"));
		bottom_szr->Add(m_memcard_write, 0, wxCENTER);
	}

	m_record_chkbox = new wxCheckBox(panel, wxID_ANY, _("Record input"));
	bottom_szr->Add(m_record_chkbox, 0, wxCENTER);

	bottom_szr->AddStretchSpacer(1);
	bottom_szr->Add(quit_btn);

	// main sizer
	wxBoxSizer* const main_szr = new wxBoxSizer(wxVERTICAL);
	main_szr->Add(m_top_szr, 0, wxEXPAND | wxALL, 5);
	main_szr->Add(mid_szr, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);
	main_szr->Add(bottom_szr, 0, wxEXPAND | wxALL, 5);

	panel->SetSizerAndFit(main_szr);

	main_szr->SetSizeHints(this);
	SetSize(650, 512-128);

	Center();
	Show();

	if (netplay_client)
	{
		netplay_client->m_state_callback = [=](NetPlayClient* npc) { OnStateChanged(); };
		netplay_client->SetDialog(this);
	}
	if (netplay_server)
		netplay_server->SetDialog(this);
	Update();
}

const GameListItem* NetPlayDiag::FindISO(const std::string& id)
{
	for (size_t i = 0; const GameListItem* item = CGameListCtrl::GetISO(i); i++)
	{
		if (item->GetRevisionSpecificUniqueID() == id)
			return item;
	}

	return NULL;
}

void NetPlayDiag::UpdateGameName()
{
	auto item = FindISO(m_selected_game);
	wxString name;
	if (!item)
	{
		name = wxString::Format(_("Unknown (%s)"), m_selected_game);
	}
	else
	{
		std::string gameName = item->GetName(item->GetLang());
		std::string uniqueId = item->GetUniqueID();
		int rev = item->GetRevision();
		if (rev)
			name = wxString::Format(_("%s (%s, Revision %d)"), gameName, uniqueId, rev);
		else
			name = wxString::Format(_("%s (%s)"), gameName, uniqueId);
	}
	m_game_label->SetLabel(_(" Game : ") + name);

}

void NetPlayDiag::UpdateLagWarning()
{
	wxCommandEvent evt(wxEVT_THREAD, NP_GUI_EVT_WARN_LAGGING);
	GetEventHandler()->AddPendingEvent(evt);
}

void NetPlayDiag::DoUpdateLagWarning()
{
	auto p = netplay_client->GetLaggardNamesAndTimer();
	wxString label = "";
	if (!p.first.empty())
	{
		label = _("Waiting for: ") + WxStrToStr(p.first);
		m_lag_timer.StartOnce(p.second);
	}
	m_warn_label->SetLabel(label);
	m_top_szr->Layout();
}

void NetPlayDiag::LagWarningTimerHit(wxTimerEvent&)
{
	DoUpdateLagWarning();
}

NetPlayDiag::~NetPlayDiag()
{
	if (m_device_map_diag)
		m_device_map_diag->Destroy();
	// We must be truly stopped before killing netplay_client.
	main_frame->DoStop();
	netplay_client.reset();
	netplay_server.reset();
	npd = NULL;
	main_frame->UpdateGUI();
}

void NetPlayDiag::OnChat(wxCommandEvent&)
{
	wxString s = m_chat_msg_text->GetValue();

	if (s.Length())
	{
		netplay_client->SendChatMessage(WxStrToStr(s));
		m_chat_text->AppendText(s.Prepend(wxT(" >> ")).Append(wxT('\n')));
		m_chat_msg_text->Clear();
	}
}

void NetPlayDiag::GetNetSettings(NetSettings &settings)
{
	SConfig &instance = SConfig::GetInstance();
	settings.m_CPUthread = instance.m_LocalCoreStartupParameter.bCPUThread;
	settings.m_DSPHLE = instance.m_LocalCoreStartupParameter.bDSPHLE;
	settings.m_DSPEnableJIT = instance.m_EnableJIT;
	settings.m_WriteToMemcard = m_memcard_write->GetValue();
}

void NetPlayDiag::UpdateDevicesOnGUI()
{
	if (m_device_map_diag && netplay_server)
	{
		g_MainNetHost->RunOnThisThreadSync([&]() {
			m_device_map_diag->UpdateDeviceMap(m_is_wii, m_is_running);
		});
	}
}

void NetPlayDiag::OnStart(wxCommandEvent&)
{
	NetSettings settings;
	GetNetSettings(settings);
	netplay_server->SetNetSettings(settings);
	netplay_server->StartGame(FindISO(m_selected_game)->GetFileName());
	if (m_start_btn)
		m_start_btn->Disable();
	m_record_chkbox->Disable();
	m_is_running = true;
	UpdateDevices();
}

void NetPlayDiag::BootGame(const std::string& filename)
{
	main_frame->BootGame(filename, /*is_netplay=*/true);
}

void NetPlayDiag::GameStopped()
{
	if (m_start_btn)
		m_start_btn->Enable();
	m_record_chkbox->Enable();
	m_is_running = false;
}

// NetPlayUI methods called from ---NETPLAY--- thread
void NetPlayDiag::Update()
{
	wxCommandEvent evt(wxEVT_THREAD, 1);
	GetEventHandler()->AddPendingEvent(evt);
}

void NetPlayDiag::AppendChat(const std::string& msg)
{
	chat_msgs.Push(msg);
	// silly
	Update();
}

void NetPlayDiag::OnMsgChangeGame(const std::string& filename)
{
	wxCommandEvent evt(wxEVT_THREAD, NP_GUI_EVT_CHANGE_GAME);
	// TODO: using a wxString in AddPendingEvent from another thread is unsafe i guess?
	evt.SetString(StrToWxStr(filename));
	GetEventHandler()->AddPendingEvent(evt);
}

void NetPlayDiag::OnMsgStartGame()
{
	wxCommandEvent evt(wxEVT_THREAD, NP_GUI_EVT_START_GAME);
	GetEventHandler()->AddPendingEvent(evt);
}

void NetPlayDiag::OnMsgStopGame()
{
	Host_Message(WM_USER_STOP);
}

void NetPlayDiag::UpdateDevices()
{
	wxCommandEvent evt(wxEVT_THREAD, NP_GUI_EVT_UPDATE_DEVICES);
	GetEventHandler()->AddPendingEvent(evt);
}

void NetPlayDiag::OnStateChanged()
{
	if (netplay_client->m_state == NetPlayClient::Failure)
	{
		wxCommandEvent evt(wxEVT_THREAD, NP_GUI_EVT_FAILURE);
		GetEventHandler()->AddPendingEvent(evt);
	}
}

void NetPlayDiag::OnAdjustBuffer(wxCommandEvent& event)
{
	const int val = ((wxSpinCtrl*)event.GetEventObject())->GetValue();
	netplay_server->AdjustPadBufferSize(val);

	std::ostringstream ss;
	ss << "< Pad Buffer: " << val << " >";
	netplay_client->SendChatMessage(ss.str());
	m_chat_text->AppendText(StrToWxStr(ss.str()).Append(wxT('\n')));
}

void NetPlayDiag::OnQuit(wxCommandEvent&)
{
	Destroy();
}

void NetPlayDiag::UpdateHostLabel()
{
	if (m_print_host_id_to_stdout)
	{
		switch (g_TraversalClient->m_State)
		{
		case TraversalClient::Connecting:
			printf("Traversal state: Connecting\n");
			break;
		case TraversalClient::Failure:
			printf("Traversal state: Connecting\n");
			break;
		case TraversalClient::Connected:
			printf("Traversal state: Connected %.*s\n", (int) g_TraversalClient->m_HostId.size(), g_TraversalClient->m_HostId.data());
			break;
		}
		fflush(stdout);
	}
	wxString label = _(" (internal IP)");
	auto DeLabel = [=](wxString str) {
		return WxStrToStr(str.Left(str.Len() - label.Len()));
	};
	auto EnLabel = [=](std::string str) {
		return StrToWxStr(str) + label;
	};
	int sel = m_host_type_choice->GetSelection();
	if (sel == 0)
	{
		// the traversal ID
		switch (g_TraversalClient->m_State)
		{
		case TraversalClient::Connecting:
			m_host_label->SetForegroundColour(*wxLIGHT_GREY);
			m_host_label->SetLabel("...");
			m_host_copy_btn->SetLabel(_("Copy"));
			m_host_copy_btn->Disable();
			break;
		case TraversalClient::Connected:
			m_host_label->SetForegroundColour(*wxBLACK);
			m_host_label->SetLabel(wxString(g_TraversalClient->m_HostId.data(), g_TraversalClient->m_HostId.size()));
			m_host_copy_btn->SetLabel(_("Copy"));
			m_host_copy_btn->Enable();
			m_host_copy_btn_is_retry = false;
			break;
		case TraversalClient::Failure:
			m_host_label->SetForegroundColour(*wxBLACK);
			m_host_label->SetLabel(FailureReasonStringForHostLabel(g_TraversalClient->m_FailureReason));
			m_host_copy_btn->SetLabel(_("Retry"));
			m_host_copy_btn->Enable();
			m_host_copy_btn_is_retry = true;
			break;
		}
	}
	else if (sel != wxNOT_FOUND) // wxNOT_FOUND shouldn't generally happen
	{
		m_host_label->SetForegroundColour(*wxBLACK);
		m_host_label->SetLabel(netplay_server->GetInterfaceHost(DeLabel(m_host_type_choice->GetString(sel))));
		m_host_copy_btn->SetLabel(_("Copy"));
		m_host_copy_btn->Enable();
		m_host_copy_btn_is_retry = false;
	}

	auto set = netplay_server->GetInterfaceSet();
	for (const std::string& iface : set)
	{
		wxString wxIface = EnLabel(iface);
		if (m_host_type_choice->FindString(wxIface) == wxNOT_FOUND)
			m_host_type_choice->Append(wxIface);
	}
	for (unsigned i = 1, count = m_host_type_choice->GetCount(); i != count; i++)
	{
		if (set.find(DeLabel(m_host_type_choice->GetString(i))) == set.end())
		{
			m_host_type_choice->Delete(i);
			i--;
			count--;
		}
	}
}

void NetPlayDiag::OnChoice(wxCommandEvent& event)
{
	UpdateHostLabel();
}

// update gui
void NetPlayDiag::OnThread(wxCommandEvent& event)
{
	if (m_is_hosting)
	{
		UpdateHostLabel();
	}

	// player list
	m_playerids.clear();
	std::string tmps;
	netplay_client->GetPlayerList(tmps, m_playerids);

	const int selection = m_player_lbox->GetSelection();

	m_player_lbox->Clear();
	std::istringstream ss(tmps);
	while (std::getline(ss, tmps))
		m_player_lbox->Append(StrToWxStr(tmps));

	m_player_lbox->SetSelection(selection);

	switch (event.GetId())
	{
	case NP_GUI_EVT_CHANGE_GAME :
		// update selected game :/
		{
		m_selected_game.assign(WxStrToStr(event.GetString()));
		// Would sure be nice if this enum weren't inside DolphinWX.
		auto iso = FindISO(m_selected_game);
		m_is_wii = iso ? iso->GetPlatform() >= GameListItem::WII_DISC : false;
		UpdateGameName();
		UpdateDevicesOnGUI(); // for selectively enabled classes
		}
		break;
	case NP_GUI_EVT_START_GAME :
		// client start game :/
		{
		auto iso = FindISO(m_selected_game);
		if (iso)
		{
			netplay_client->StartGame(iso->GetFileName());
		}
		else
		{
			PanicAlertT("The host chose a game that was not found locally.");
			netplay_client->GameStopped();
		}
		}
		break;
	case NP_GUI_EVT_FAILURE:
		{
		main_frame->DoStop();
		wxString err = FailureReasonStringForDialog(netplay_client->m_failure_reason);
		// see other comment about wx bug
		auto complain = new wxMessageDialog(this, err);
		complain->Bind(wxEVT_WINDOW_MODAL_DIALOG_CLOSED, &NetPlayDiag::OnErrorClosed, this);
		complain->ShowWindowModal();
		return;
		}
	case NP_GUI_EVT_UPDATE_DEVICES:
		{
		UpdateDevicesOnGUI();
		}
		break;
	case NP_GUI_EVT_WARN_LAGGING:
		{
		DoUpdateLagWarning();
		}
		break;
	}

	// chat messages
	while (chat_msgs.Size())
	{
		std::string s;
		chat_msgs.Pop(s);
		//PanicAlert("message: %s", s.c_str());
		m_chat_text->AppendText(StrToWxStr(s).Append(wxT('\n')));
	}
}

void NetPlayDiag::OnErrorClosed(wxCommandEvent&)
{
	Destroy();
}

void NetPlayDiag::OnConfigPads(wxCommandEvent&)
{
	if (m_device_map_diag)
	{
		m_device_map_diag->SetFocus();
	}
	else
	{
		m_device_map_diag = new DeviceMapDiag(this, netplay_server.get());
		m_device_map_diag->Bind(wxEVT_SHOW, &NetPlayDiag::OnShowDeviceMapDiag, this);
		UpdateDevices();
	}
}

void NetPlayDiag::OnShowDeviceMapDiag(wxShowEvent& event)
{
	if (!event.IsShown())
	{
		m_device_map_diag->Destroy();
		m_device_map_diag = NULL;
	}
}

void NetPlayDiag::OnDefocusName(wxFocusEvent&)
{
	std::string name = StripSpaces(WxStrToStr(m_name_text->GetValue()));
	std::string* cur = &SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayNickname;
	if (*cur != name)
	{
		*cur = name;
		SConfig::GetInstance().SaveSettings();
		netplay_client->ChangeName(name);
		Update();
	}
}

void NetPlayDiag::OnCopyIP(wxCommandEvent&)
{
	if (m_host_copy_btn_is_retry)
	{
		g_TraversalClient->ReconnectToServer();
		Update();
	}
	else
	{
		if (wxTheClipboard->Open())
		{
			wxTheClipboard->SetData(new wxTextDataObject(m_host_label->GetLabel()));
			wxTheClipboard->Close();
		}
	}
}

void NetPlayDiag::OnRightClick(wxMouseEvent& event)
{
	if (!netplay_server)
		return;
	wxMenu* menu = new wxMenu;
	menu->AppendCheckItem(IDM_DESYNC_DETECTION, _("Desync Detection (slow)"));
	menu->Check(IDM_DESYNC_DETECTION, netplay_server->m_enable_memory_hash);
	menu->Enable(IDM_DESYNC_DETECTION, !m_is_running);
	PopupMenu(menu);
}

void NetPlayDiag::OnDesyncDetection(wxCommandEvent& event)
{
	netplay_server->m_enable_memory_hash = !netplay_server->m_enable_memory_hash;
}

bool NetPlayDiag::IsRecording()
{
	return m_record_chkbox->GetValue();
}

ConnectDiag::ConnectDiag(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, _("Connect to NetPlay"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer* sizerTop = new wxBoxSizer(wxVERTICAL);
	std::string host = SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayHost;
	wxStaticText* hostLabel = new wxStaticText(this, wxID_ANY, _("Host or ID:"));
	m_HostCtrl = new wxTextCtrl(this, wxID_ANY, StrToWxStr(host));
	// focus and select all
	m_HostCtrl->SetFocus();
	m_HostCtrl->SetSelection(-1, -1);
	m_HostCtrl->Bind(wxEVT_TEXT, &ConnectDiag::OnChange, this);
	wxBoxSizer* sizerHost = new wxBoxSizer(wxHORIZONTAL);
	sizerHost->Add(hostLabel, 0, wxLEFT | wxCENTER, 5);
	sizerHost->Add(m_HostCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);
	wxStdDialogButtonSizer* sizerButtons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	m_ConnectBtn = sizerButtons->GetAffirmativeButton();
	m_ConnectBtn->SetLabel(_("Connect"));
	m_ConnectBtn->Enable(IsHostOk());
	sizerTop->Add(sizerHost, 0, wxTOP | wxBOTTOM | wxEXPAND, 5);
	sizerTop->Add(sizerButtons, 0, wxEXPAND);
	SetSizerAndFit(sizerTop);
	SetMaxSize(wxSize(10000, GetBestSize().GetHeight()));
}

bool ConnectDiag::Validate()
{
	if (netplay_client)
	{
		// shouldn't be possible, just in case
		return false;
	}
	std::string hostSpec = GetHost();
	std::string nickname = SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayNickname;
	netplay_client.reset(new NetPlayClient(GetHost(), nickname, [=](NetPlayClient* npc) {
		auto state = npc->m_state;
		if (state == NetPlayClient::Connected || state == NetPlayClient::Failure)
		{
			wxCommandEvent evt(wxEVT_THREAD, 1);
			GetEventHandler()->AddPendingEvent(evt);
		}
	}));
	// disable the GUI
	m_HostCtrl->Disable();
	m_ConnectBtn->Disable();
	return false;
}

void ConnectDiag::OnThread(wxCommandEvent& event)
{
	if (netplay_client->m_state == NetPlayClient::Connected)
	{
		// changes m_state_callback
		new NetPlayDiag(GetParent(), "", false);
		EndModal(0);
	}
	else
	{
		wxString err = FailureReasonStringForDialog(netplay_client->m_failure_reason);
		netplay_client.reset();
		// connection failure
		auto complain = new wxMessageDialog(this, err);
		complain->ShowWindowModal();
		// We leak the message dialog because of a wx bug.
		// bring the UI back
		m_HostCtrl->Enable();
		m_ConnectBtn->Enable();
	}
}

void ConnectDiag::OnChange(wxCommandEvent& event)
{
	m_ConnectBtn->Enable(IsHostOk());
}

std::string ConnectDiag::GetHost()
{
	return WxStrToStr(m_HostCtrl->GetValue());
}

bool ConnectDiag::IsHostOk()
{
	std::string host = GetHost();
	size_t pos = host.find(':');
	if (pos != std::string::npos)
	{
		// ip:port
		return pos + 1 < host.size() &&
		       host.find_first_not_of("0123456789", pos + 1) == std::string::npos;
	}
	else
	{
		// traversal host id
		return host.size() == sizeof(TraversalHostId);
	}
}

ConnectDiag::~ConnectDiag()
{
	if (netplay_client)
	{
		if (GetReturnCode() != 0)
			netplay_client.reset();
	}
	SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayHost = StripSpaces(WxStrToStr(m_HostCtrl->GetValue()));
	SConfig::GetInstance().SaveSettings();
}

DeviceMapDiag::DeviceMapDiag(wxWindow* parent, NetPlayServer* server)
	: wxDialog(parent, wxID_ANY, _("Configure Pads"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxTAB_TRAVERSAL)
{
	m_server = server;
	SetFocus();
}

void DeviceMapDiag::UpdateDeviceMap(bool is_wii, bool is_running)
{
	// It's unsafe to use DestroyChildren here!
	for (auto& child : GetChildren())
		child->Destroy();
	m_choice_to_cls_idx.clear();

	auto main_szr = new wxBoxSizer(wxVERTICAL);

	for (int classId = 0; classId < IOSync::Class::NumClasses; classId++)
	{
		if (IOSync::g_Classes[classId]->m_WiiOnly && !is_wii)
			continue;
		auto class_szr = new wxStaticBoxSizer(wxHORIZONTAL, this, ClassNameString((IOSync::Class::ClassID) classId));
		main_szr->AddSpacer(10);
		main_szr->Add(class_szr, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
		int max = IOSync::g_Classes[classId]->GetMaxDeviceIndex();

		std::unordered_map<u32, int> local_idx_to_pos;

		wxArrayString options;
		PlayerId pid = 0;
		m_pos_to_pid_local_idx[classId].clear();
		options.Add(_("None"));
		m_pos_to_pid_local_idx[classId].push_back(std::make_pair(255, 255));
		for (auto& player : m_server->m_players)
		{
			if (!player.connected || player.sitting_out_this_game)
				continue;
			for (const auto& p : player.devices_present)
			{
				int itsClass = p.first & 0xff, local_idx = p.first >> 8;
				if (itsClass != classId) continue;
				local_idx_to_pos[(pid << 8) | local_idx] = (int) options.Count();
				m_pos_to_pid_local_idx[classId].push_back(std::make_pair(pid, local_idx));
				options.Add(wxString::Format("%s:%u", StrToWxStr(player.name), local_idx));
			}
			pid++;
		}

		for (int idx = 0; idx < max; idx++)
		{
			auto v_szr = new wxBoxSizer(wxVERTICAL);
			wxChoice* choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, options);
			if (!IOSync::g_Classes[classId]->m_AllowInGameSwap && is_running)
				choice->Disable();
			choice->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &DeviceMapDiag::OnAdjust, this);
			m_choice_to_cls_idx[choice] = std::make_pair(classId, idx);
			auto cur = m_server->m_device_info[classId][idx].desired_mapping;
			if (cur.first == 255)
				choice->Select(0);
			else
				choice->Select(local_idx_to_pos[(cur.first << 8) | cur.second]);
			v_szr->Add(new wxStaticText(this, wxID_ANY, wxString::Format(_("Slot %d"), idx)));
			v_szr->Add(choice, 0);
			class_szr->Add(v_szr, 0, wxEXPAND);
			class_szr->AddSpacer(10);
		}

	}

	main_szr->AddSpacer(5);
	main_szr->Add(CreateButtonSizer(wxOK), 0, wxEXPAND | wxLEFT | wxRIGHT, 20);
	main_szr->AddSpacer(5);
	SetSizerAndFit(main_szr);
	Show();
	// Why is this required?
	Layout();
}

void DeviceMapDiag::OnAdjust(wxCommandEvent& event)
{
	auto p = m_choice_to_cls_idx[(wxChoice*) event.GetEventObject()];
	int classId = p.first, index = p.second;
	int pos = event.GetSelection();
	auto q = m_pos_to_pid_local_idx[classId][pos];
	PlayerId pid = q.first;
	int local_index = q.second;
	g_MainNetHost->RunOnThread([=]() {
		ASSUME_ON(NET);
		m_server->SetDesiredDeviceMapping(classId, index, pid, local_index);
	});
}

void NetPlay::GameStopped()
{
	if (netplay_client != NULL)
		netplay_client->GameStopped();
}

void NetPlay::ShowConnectDialog(wxWindow* parent)
{
	if (NetPlayDiag::GetInstance() != NULL)
	{
		NetPlayDiag::GetInstance()->Raise();
		return;
	}
	ConnectDiag diag(parent);
	// it'll open the window itself
	diag.ShowModal();
}

void NetPlay::ConnectFromCommandLine(wxWindow* parent, std::string host)
{
	ConnectDiag diag(parent);
	diag.m_HostCtrl->SetValue(StrToWxStr(host));
	if (!diag.IsHostOk())
	{
		ERROR_LOG(NETPLAY, "Invalid host specified on command line");
		exit(1);
	}
	diag.Validate();
	diag.ShowModal();
}

void NetPlay::StartHosting(wxWindow* parent, std::string id, bool print_host_id_to_stdout)
{
	if (NetPlayDiag::GetInstance() != NULL)
	{
		NetPlayDiag::GetInstance()->Raise();
		netplay_server->ChangeGame(id);
		return;
	}

	netplay_server.reset(new NetPlayServer());

	if (!g_TraversalClient || !g_MainNetHost)
	{
		netplay_server.reset();
		wxString error;
		if (SConfig::GetInstance().m_LocalCoreStartupParameter.iNetPlayListenPort != 0)
		{
			error = _("Failed to init traversal client.  Force Netplay Listen Port is enabled; someone is probably already listening on that port.");
		}
		else
		{
			error = _("Failed to init traversal client.  This shouldn't happen...");
		}
		wxMessageBox(error, _("Error"), wxOK, parent);
		return;
	}

	netplay_server->ChangeGame(id);
	netplay_server->AdjustPadBufferSize(INITIAL_PAD_BUFFER_SIZE);
	std::string nickname = SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayNickname;
	Common::Event ev;
	char buf[64];
	sprintf(buf, "127.0.0.1:%d", g_MainNetHost->GetPort());
	netplay_client.reset(new NetPlayClient(buf, nickname, [&](NetPlayClient* npc) {
		if (npc->m_state == NetPlayClient::Connected ||
		    npc->m_state == NetPlayClient::Failure)
			ev.Set();
	}));
	ev.Wait();
	if (netplay_client->m_state == NetPlayClient::Failure)
	{
		PanicAlert("Failed to init netplay client.  This shouldn't happen...");
		netplay_client.reset();
		netplay_server.reset();
		return;
	}

	if (netplay_client->m_state != NetPlayClient::Connected)
	{
		printf("state=%d\n", netplay_client->m_state);
		wxMessageBox(_("Failed to connect to localhost.  This shouldn't happen..."), _("Error"), wxOK, parent);
		netplay_client.reset();
		netplay_server.reset();
		return;
	}
	new NetPlayDiag(parent, id, true, print_host_id_to_stdout);
}
