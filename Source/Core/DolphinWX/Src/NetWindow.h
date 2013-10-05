// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _NETWINDOW_H_
#define _NETWINDOW_H_

#include "CommonTypes.h"

#include <queue>
#include <string>

#include <wx/wx.h>
#include <wx/event.h>
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/gbsizer.h>
#include <wx/listbox.h>
#include <wx/spinctrl.h>

#include "GameListCtrl.h"

#include "FifoQueue.h"

#include "NetPlayClient.h"

enum
{
	NP_GUI_EVT_CHANGE_GAME = 45,
	NP_GUI_EVT_START_GAME,
	NP_GUI_EVT_STOP_GAME,
};

class NetPlayDiag : public wxFrame, public NetPlayUI
{
public:
	NetPlayDiag(wxWindow* const parent, const std::string& game, const bool is_hosting = false);
	~NetPlayDiag();

	Common::FifoQueue<std::string>	chat_msgs;

	void OnStart(wxCommandEvent& event);

	// implementation of NetPlayUI methods
	void BootGame(const std::string& filename);
	void StopGame();

	void Update();
	void AppendChat(const std::string& msg);

	void OnMsgChangeGame(const std::string& filename);
	void OnMsgStartGame();
	void OnMsgStopGame();

	static NetPlayDiag *&GetInstance() { return npd; };

	bool IsRecording();

	static const GameListItem* FindISO(const std::string& id);
	void UpdateGameName();

private:
    DECLARE_EVENT_TABLE()

	void OnChat(wxCommandEvent& event);
	void OnQuit(wxCommandEvent& event);
	void OnThread(wxCommandEvent& event);
	void OnAdjustBuffer(wxCommandEvent& event);
	void OnConfigPads(wxCommandEvent& event);
    void OnDefocusName(wxFocusEvent& event);
    void OnCopyIP(wxCommandEvent& event);
	void GetNetSettings(NetSettings &settings);

	wxTextCtrl*		m_name_text;
	wxListBox*		m_player_lbox;
	wxTextCtrl*		m_chat_text;
	wxTextCtrl*		m_chat_msg_text;
	wxCheckBox*		m_memcard_write;
	wxCheckBox*		m_record_chkbox;
    wxStaticText*   m_host_label;
    wxButton*       m_host_copy_btn;
    bool            m_host_copy_btn_is_retry;

	std::string		m_selected_game;
	wxStaticText*	m_game_label;
	wxButton*		m_start_btn;
    bool            m_is_hosting;

	std::vector<int>	m_playerids;

	static NetPlayDiag* npd;
};

class ConnectDiag : public wxDialog
{
public:
	ConnectDiag(wxWindow* parent);
	~ConnectDiag();
	std::string GetHost();
	bool Validate();
	void OnThread(wxCommandEvent& event);

private:
	void OnChange(wxCommandEvent& event);
	bool IsHostOk();
	wxTextCtrl* m_HostCtrl;
	wxButton* m_ConnectBtn;
};

class PadMapDiag : public wxDialog
{
public:
	PadMapDiag(wxWindow* const parent, PadMapping map[], PadMapping wiimotemap[], std::vector<const Player *>& player_list);

private:
	void OnAdjust(wxCommandEvent& event);

	wxChoice*	m_map_cbox[8];
	PadMapping* const m_mapping;
	PadMapping* const m_wiimapping;
	std::vector<const Player *>& m_player_list;
};

namespace NetPlay
{
	void StopGame();
	void ShowConnectDialog(wxWindow* parent);
	void StartHosting(std::string id, wxWindow* parent);
}

#endif // _NETWINDOW_H_

