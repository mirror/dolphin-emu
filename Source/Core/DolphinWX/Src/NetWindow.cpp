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

#include <sstream>
#include <string>

#include <wx/clipbrd.h>

#define NETPLAY_TITLEBAR	"Dolphin NetPlay"
#define INITIAL_PAD_BUFFER_SIZE 20

static wxString FailureReasonStringForHost(int reason)
{
	switch (reason)
	{
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

static wxString FailureReasonStringForConnect(int reason)
{
	switch (reason)
	{
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
		return _("Unknown error");
	}
}

BEGIN_EVENT_TABLE(NetPlayDiag, wxFrame)
	EVT_COMMAND(wxID_ANY, wxEVT_THREAD, NetPlayDiag::OnThread)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(ConnectDiag, wxDialog)
	EVT_COMMAND(wxID_ANY, wxEVT_THREAD, ConnectDiag::OnThread)
END_EVENT_TABLE()

static std::unique_ptr<NetPlayServer> netplay_server;
static std::unique_ptr<NetPlayClient> netplay_client;
extern CFrame* main_frame;
NetPlayDiag *NetPlayDiag::npd = NULL;

NetPlayDiag::NetPlayDiag(wxWindow* const parent, const std::string& game, const bool is_hosting)
	: wxFrame(parent, wxID_ANY, wxT(NETPLAY_TITLEBAR), wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE | wxTAB_TRAVERSAL)
	, m_selected_game(game)
	, m_start_btn(NULL)
	, m_is_hosting(is_hosting)
{
	npd = this;
	wxPanel* const panel = new wxPanel(this);

	// top crap
	m_game_label = new wxStaticText(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
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
	main_szr->Add(m_game_label, 0, wxEXPAND | wxALL, 5);
	main_szr->Add(mid_szr, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);
	main_szr->Add(bottom_szr, 0, wxEXPAND | wxALL, 5);

	panel->SetSizerAndFit(main_szr);

	main_szr->SetSizeHints(this);
	SetSize(650, 512-128);

	Center();
	Show();
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

NetPlayDiag::~NetPlayDiag()
{
	netplay_client.reset();
	netplay_server.reset();
	npd = NULL;
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
	settings.m_EXIDevice[0] = instance.m_EXIDevice[0];
	settings.m_EXIDevice[1] = instance.m_EXIDevice[1];
}

void NetPlayDiag::OnStart(wxCommandEvent&)
{
	NetSettings settings;
	GetNetSettings(settings);
	netplay_server->SetNetSettings(settings);
	netplay_server->StartGame(FindISO(m_selected_game)->GetFileName());
}

void NetPlayDiag::BootGame(const std::string& filename)
{
	main_frame->BootGame(filename);
}

void NetPlayDiag::StopGame()
{
	main_frame->DoStop();
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
	if (m_start_btn)
		m_start_btn->Disable();
}

void NetPlayDiag::OnMsgStopGame()
{
	wxCommandEvent evt(wxEVT_THREAD, NP_GUI_EVT_STOP_GAME);
	GetEventHandler()->AddPendingEvent(evt);
	if (m_start_btn)
		m_start_btn->Enable();
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
			m_host_label->SetLabel(FailureReasonStringForHost(g_TraversalClient->m_FailureReason));
			m_host_copy_btn->SetLabel(_("Retry"));
			m_host_copy_btn->Enable();
			m_host_copy_btn_is_retry = true;
			break;
		case TraversalClient::InitFailure:
			// can't happen
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
	for (auto it = set.begin(); it != set.end(); ++it)
	{
		wxString kind = EnLabel(*it);
		if (m_host_type_choice->FindString(kind) == wxNOT_FOUND)
			m_host_type_choice->Append(kind);
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
		UpdateGameName();
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
			netplay_client.reset();
		}
		}
		break;
	case NP_GUI_EVT_STOP_GAME :
		// client stop game
		{
		netplay_client->StopGame();
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

void NetPlayDiag::OnConfigPads(wxCommandEvent&)
{
	PadMapping mapping[4];
	PadMapping wiimotemapping[4];
	std::vector<const Player *> player_list;
	netplay_server->GetPadMapping(mapping);
	netplay_server->GetWiimoteMapping(wiimotemapping);
	netplay_client->GetPlayers(player_list);
	PadMapDiag pmd(this, mapping, wiimotemapping, player_list);
	pmd.ShowModal();
	netplay_server->SetPadMapping(mapping);
	netplay_server->SetWiimoteMapping(wiimotemapping);
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
		printf("already a NPC???\n");
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
		netplay_client->SetDialog(new NetPlayDiag(GetParent(), "", false));
		EndModal(0);
	}
	else
	{
		wxString err = FailureReasonStringForConnect(netplay_client->m_failure_reason);
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
		netplay_client->m_state_callback = nullptr;
		if (GetReturnCode() != 0)
			netplay_client.reset();
	}
	SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayHost = StripSpaces(WxStrToStr(m_HostCtrl->GetValue()));
	SConfig::GetInstance().SaveSettings();
}

PadMapDiag::PadMapDiag(wxWindow* const parent, PadMapping map[], PadMapping wiimotemap[], std::vector<const Player *>& player_list)
	: wxDialog(parent, wxID_ANY, _("Configure Pads"), wxDefaultPosition, wxDefaultSize)
	, m_mapping(map)
	, m_wiimapping (wiimotemap)
	, m_player_list(player_list)
{
	wxBoxSizer* const h_szr = new wxBoxSizer(wxHORIZONTAL);
	h_szr->AddSpacer(10);

	wxArrayString player_names;
	player_names.Add(_("None"));
	for (unsigned int i = 0; i < m_player_list.size(); i++)
		player_names.Add(m_player_list[i]->name);

	wxString wiimote_names[5];
	wiimote_names[0] = _("None");
	for (unsigned int i=1; i < 5; ++i)
		wiimote_names[i] = wxString(_("Wiimote ")) + (wxChar)(wxT('0')+i);

	for (unsigned int i=0; i<4; ++i)
	{
		wxBoxSizer* const v_szr = new wxBoxSizer(wxVERTICAL);
		v_szr->Add(new wxStaticText(this, wxID_ANY, (wxString(_("Pad ")) + (wxChar)(wxT('0')+i))),
					    1, wxALIGN_CENTER_HORIZONTAL);

		m_map_cbox[i] = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, player_names);
		m_map_cbox[i]->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &PadMapDiag::OnAdjust, this);
		if (m_mapping[i] == -1)
			m_map_cbox[i]->Select(0);
		else
			for (unsigned int j = 0; j < m_player_list.size(); j++)
				if (m_mapping[i] == m_player_list[j]->pid)
					m_map_cbox[i]->Select(j + 1);

		v_szr->Add(m_map_cbox[i], 1);

		h_szr->Add(v_szr, 1, wxTOP | wxEXPAND, 20);
		h_szr->AddSpacer(10);
	}

	for (unsigned int i=0; i<4; ++i)
	{
		wxBoxSizer* const v_szr = new wxBoxSizer(wxVERTICAL);
		v_szr->Add(new wxStaticText(this, wxID_ANY, (wxString(_("Wiimote ")) + (wxChar)(wxT('0')+i))),
					    1, wxALIGN_CENTER_HORIZONTAL);

		m_map_cbox[i+4] = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, player_names);
		m_map_cbox[i+4]->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &PadMapDiag::OnAdjust, this);
		if (m_wiimapping[i] == -1)
			m_map_cbox[i+4]->Select(0);
		else
			for (unsigned int j = 0; j < m_player_list.size(); j++)
				if (m_wiimapping[i] == m_player_list[j]->pid)
					m_map_cbox[i+4]->Select(j + 1);

		v_szr->Add(m_map_cbox[i+4], 1);

		h_szr->Add(v_szr, 1, wxTOP | wxEXPAND, 20);
		h_szr->AddSpacer(10);
	}

	wxBoxSizer* const main_szr = new wxBoxSizer(wxVERTICAL);
	main_szr->Add(h_szr);
	main_szr->AddSpacer(5);
	main_szr->Add(CreateButtonSizer(wxOK), 0, wxEXPAND | wxLEFT | wxRIGHT, 20);
	main_szr->AddSpacer(5);
	SetSizerAndFit(main_szr);
	SetFocus();
}

void PadMapDiag::OnAdjust(wxCommandEvent& event)
{
	(void)event;
	for (unsigned int i = 0; i < 4; i++)
	{
		int player_idx = m_map_cbox[i]->GetSelection();
		if (player_idx > 0)
			m_mapping[i] = m_player_list[player_idx - 1]->pid;
		else
			m_mapping[i] = -1;

		player_idx = m_map_cbox[i+4]->GetSelection();
		if (player_idx > 0)
			m_wiimapping[i] = m_player_list[player_idx - 1]->pid;
		else
			m_wiimapping[i] = -1;
	}
}

void NetPlay::StopGame()
{
	if (netplay_client != NULL)
		netplay_client->Stop();
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

void NetPlay::StartHosting(std::string id, wxWindow* parent)
{
	if (NetPlayDiag::GetInstance() != NULL)
	{
		NetPlayDiag::GetInstance()->Raise();
		netplay_server->ChangeGame(id);
		return;
	}

	netplay_server.reset(new NetPlayServer());

	if (!g_TraversalClient)
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
	sprintf(buf, "127.0.0.1:%d", g_TraversalClient->GetPort());
	netplay_client.reset(new NetPlayClient(buf, nickname, [&](NetPlayClient* npc) {
		if (npc->m_state == NetPlayClient::Connected ||
		    npc->m_state == NetPlayClient::Failure)
			ev.Set();
	}));
	if (netplay_client->m_state == NetPlayClient::Failure)
	{
		PanicAlert("Failed to init netplay client.  This shouldn't happen...");
		netplay_client.reset();
		netplay_server.reset();
		return;
	}
	ev.Wait();

	if (netplay_client->m_state != NetPlayClient::Connected)
	{
		printf("state=%d\n", netplay_client->m_state);
		wxMessageBox(_("Failed to connect to localhost.  This shouldn't happen..."), _("Error"), wxOK, parent);
		netplay_client.reset();
		netplay_server.reset();
		return;
	}
	auto diag = new NetPlayDiag(parent, id, true);
	netplay_client->SetDialog(diag);
	netplay_server->SetDialog(diag);
	diag->Update();
}
