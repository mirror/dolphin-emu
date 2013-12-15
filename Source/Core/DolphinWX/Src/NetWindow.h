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
#include "NetPlayServer.h"
#include "IOSync.h"
#include <unordered_map>

enum
{
	NP_GUI_EVT_CHANGE_GAME = 45,
	NP_GUI_EVT_START_GAME,
	NP_GUI_EVT_STOP_GAME,
	NP_GUI_EVT_FAILURE,
	NP_GUI_EVT_UPDATE_DEVICES,
	NP_GUI_EVT_WARN_LAGGING
};

class DeviceMapDiag;

class NetPlayDiag : public wxFrame, public NetPlayUI
{
public:
	NetPlayDiag(wxWindow* const parent, const std::string& game, const bool is_hosting = false, bool print_host_id_to_stdout = false);
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
	virtual void UpdateDevices() override;
	void OnStateChanged();

	static NetPlayDiag *&GetInstance() { return npd; };

	virtual bool IsRecording() override;

	static const GameListItem* FindISO(const std::string& id);
	void UpdateGameName();
	virtual void UpdateLagWarning() override;

private:
	DECLARE_EVENT_TABLE()

	void OnChat(wxCommandEvent& event);
	void OnQuit(wxCommandEvent& event);
	void UpdateHostLabel();
	void OnChoice(wxCommandEvent& event);
	void OnThread(wxCommandEvent& event);
	void OnAdjustBuffer(wxCommandEvent& event);
	void OnConfigPads(wxCommandEvent& event);
	void OnShowDeviceMapDiag(wxShowEvent& event);
	void OnDefocusName(wxFocusEvent& event);
	void OnCopyIP(wxCommandEvent& event);
	void GetNetSettings(NetSettings &settings);
	void OnErrorClosed(wxCommandEvent& event);
	void OnRightClick(wxMouseEvent& event);
	void OnDesyncDetection(wxCommandEvent& event);

	void DoUpdateLagWarning();
	void LagWarningTimerHit(wxTimerEvent& event);
	void UpdateDevicesOnGUI();

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
	bool			m_is_wii;
	wxStaticText*	m_game_label;
	wxBoxSizer*		m_top_szr;
	wxStaticText*	m_warn_label;
	wxButton*		m_start_btn;
	bool			m_is_hosting;
	DeviceMapDiag*	m_device_map_diag;
	wxTimer			m_lag_timer;
	bool			m_is_running;
	bool			m_print_host_id_to_stdout;

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
	bool IsHostOk();

	wxTextCtrl* m_HostCtrl;
private:
	DECLARE_EVENT_TABLE()

	void OnChange(wxCommandEvent& event);
	void OnThread(wxCommandEvent& event);
	wxButton* m_ConnectBtn;
};

class DeviceMapDiag : public wxDialog
{
public:
	DeviceMapDiag(wxWindow* parent, NetPlayServer* server);

	void UpdateDeviceMap(bool is_wii, bool is_running);
private:
	void OnAdjust(wxCommandEvent& event);

	std::unordered_map<wxChoice*, std::pair<int, int>> m_choice_to_cls_idx;
	std::vector<std::pair<PlayerId, int>> m_pos_to_pid_local_idx[IOSync::Class::NumClasses];
	NetPlayServer* m_server;
};

namespace NetPlay
{
	void GameStopped();
	void ShowConnectDialog(wxWindow* parent);
	void ConnectFromCommandLine(wxWindow* parent, std::string host);
	void StartHosting(wxWindow* parent, std::string id, bool print_host_id_to_stdout);
}

#endif // _NETWINDOW_H_

