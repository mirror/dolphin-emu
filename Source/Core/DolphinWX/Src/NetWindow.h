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
	NP_GUI_EVT_FAILURE,
};

class NetPlayDiag : public wxFrame, public NetPlayUI
{
public:
	NetPlayDiag(wxWindow* const parent, const std::string& game, const bool is_hosting = false);
	~NetPlayDiag();

	Common::FifoQueue<std::string>	chat_msgs;

	void OnStart(wxCommandEvent& event);

	// implementation of NetPlayUI methods
	virtual void BootGame(const std::string& filename) override;
	virtual void GameStopped() override;

	virtual void Update() override;
	virtual void AppendChat(const std::string& msg) override;

	virtual void OnMsgChangeGame(const std::string& filename) override;
	virtual void OnMsgStartGame() override;
	virtual void OnMsgStopGame() override;
	void OnStateChanged();

	static NetPlayDiag *&GetInstance() { return npd; };

	virtual bool IsRecording() override;

	static const GameListItem* FindISO(const std::string& id);
	void UpdateGameName();

private:
	DECLARE_EVENT_TABLE()

	void OnChat(wxCommandEvent& event);
	void OnQuit(wxCommandEvent& event);
	void UpdateHostLabel();
	void OnChoice(wxCommandEvent& event);
	void OnThread(wxCommandEvent& event);
	void OnAdjustBuffer(wxCommandEvent& event);
	void OnConfigPads(wxCommandEvent& event);
	void OnDefocusName(wxFocusEvent& event);
	void OnCopyIP(wxCommandEvent& event);
	void GetNetSettings(NetSettings &settings);
	void OnErrorClosed(wxCommandEvent& event);

	wxTextCtrl*		m_name_text;
	wxListBox*		m_player_lbox;
	wxTextCtrl*		m_chat_text;
	wxTextCtrl*		m_chat_msg_text;
	wxCheckBox*		m_memcard_write;
	wxCheckBox*		m_record_chkbox;
	wxChoice*		m_host_type_choice;
	wxStaticText*   m_host_label;
	wxButton*		m_host_copy_btn;
	bool			m_host_copy_btn_is_retry;

	std::string		m_selected_game;
	wxStaticText*	m_game_label;
	wxButton*		m_start_btn;
	bool			m_is_hosting;
	Common::Event	m_game_started_evt;

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

private:
	DECLARE_EVENT_TABLE()

	void OnChange(wxCommandEvent& event);
	void OnThread(wxCommandEvent& event);
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
	void GameStopped();
	void ShowConnectDialog(wxWindow* parent);
	void StartHosting(std::string id, wxWindow* parent);
}

#endif // _NETWINDOW_H_

