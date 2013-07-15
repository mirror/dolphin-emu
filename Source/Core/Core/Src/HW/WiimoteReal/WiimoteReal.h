// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


#ifndef WIIMOTE_REAL_H
#define WIIMOTE_REAL_H

#include <functional>
#include <vector>

#include "WiimoteRealBase.h"
#include "ChunkFile.h"
#include "Thread.h"
#include "FifoQueue.h"
#include "Timer.h"

#include "../Wiimote.h"
#include "../WiimoteEmu/WiimoteEmu.h"

#include "../../InputCommon/Src/InputConfig.h"

typedef std::vector<u8> Report;

namespace WiimoteReal
{

class Wiimote : NonCopyable
{
friend class WiimoteEmu::Wiimote;
public:
	Wiimote();
	~Wiimote();

	void ControlChannel(const u16 channel, const void* const data, const u32 size);
	void InterruptChannel(const u16 channel, const void* const data, const u32 size);
	void Update();

	const Report& ProcessReadQueue();

	bool Read();
	bool Write();

	void StartThread();
	void StopThread();

	// "handshake" / stop packets
	void EmuStart();
	void EmuStop();
	void EmuResume();
	void EmuPause();

	// connecting and disconnecting from physical devices
	// (using address inserted by FindWiimotes)
	bool Connect();
	void Disconnect();

	// TODO: change to something like IsRelevant
	bool IsConnected() const;

	bool Prepare(int index);

	void DisableDataReporting();
	void EnableDataReporting(u8 mode);
	void SetChannel(u16 channel);
	
	void QueueReport(u8 rpt_id, const void* data, unsigned int size);

	int index;

#if defined(__APPLE__)
	IOBluetoothDevice *btd;
	IOBluetoothL2CAPChannel *ichan;
	IOBluetoothL2CAPChannel *cchan;
	char input[MAX_PAYLOAD];
	int inputlen;
	bool m_connected;
#elif defined(__linux__) && HAVE_BLUEZ
	bdaddr_t bdaddr;					// Bluetooth address
	int cmd_sock;						// Command socket
	int int_sock;						// Interrupt socket

#elif defined(_WIN32)
	std::basic_string<TCHAR> devicepath;	// Unique wiimote reference
	//ULONGLONG btaddr;					// Bluetooth address
	HANDLE dev_handle;					// HID handle
	OVERLAPPED hid_overlap_read, hid_overlap_write;	// Overlap handle
	enum win_bt_stack_t stack;			// Type of bluetooth stack to use
#endif

protected:
	Report m_last_input_report;
	u16	m_channel;

private:
	void ClearReadQueue();
	void WriteReport(Report rpt);
	
	int IORead(u8* buf);
	int IOWrite(u8 const* buf, int len);

	void ThreadFunc();

	bool m_rumble_state;
	
	bool				m_run_thread;
	Common::Thread			m_wiimote_thread;
	
	Common::FifoQueue<Report>	m_read_reports;
	Common::FifoQueue<Report>	m_write_reports;
	
	Common::Timer m_last_audio_report;
};

class WiimoteScanner
{
public:
	WiimoteScanner();
	~WiimoteScanner();

	bool IsReady() const;
	
	void WantWiimotes(bool do_want);
	void WantBB(bool do_want);

	void StartScanning();
	void StopScanning();

	void FindWiimotes(std::vector<Wiimote*>&, Wiimote*&);

	// function called when not looking for more wiimotes
	void Update();

private:
	void ThreadFunc();

	Common::Thread m_scan_thread;

	volatile bool m_run_thread;
	volatile bool m_want_wiimotes;
	volatile bool m_want_bb;

#if defined(_WIN32)
	void CheckDeviceType(std::basic_string<TCHAR> &devicepath, bool &real_wiimote, bool &is_bb);
#elif defined(__linux__) && HAVE_BLUEZ
	int device_id;
	int device_sock;
#endif
};

extern std::recursive_mutex g_refresh_lock;
extern WiimoteScanner g_wiimote_scanner;
extern Wiimote *g_wiimotes[MAX_BBMOTES];

void InterruptChannel(int _WiimoteNumber, u16 _channelID, const void* _pData, u32 _Size);
void ControlChannel(int _WiimoteNumber, u16 _channelID, const void* _pData, u32 _Size);
void Update(int _WiimoteNumber);

void DoState(PointerWrap &p);
void StateChange(EMUSTATE_CHANGE newState);

int FindWiimotes(Wiimote** wm, int max_wiimotes);
void ChangeWiimoteSource(unsigned int index, int source);

bool IsValidBluetoothName(const std::string& name);
bool IsBalanceBoardName(const std::string& name);

}; // WiimoteReal

#endif
