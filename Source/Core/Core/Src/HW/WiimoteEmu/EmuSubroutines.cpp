// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


/* HID reports access guide. */

/* 0x10 - 0x1a   Output   EmuMain.cpp: HidOutputReport()
       0x10 - 0x14: General
	   0x15: Status report request from the Wii
	   0x16 and 0x17: Write and read memory or registers
       0x19 and 0x1a: General
   0x20 - 0x22   Input    EmuMain.cpp: HidOutputReport() to the destination
       0x15 leads to a 0x20 Input report
       0x17 leads to a 0x21 Input report
	   0x10 - 0x1a leads to a 0x22 Input report
   0x30 - 0x3f   Input    This file: Update() */

#include <vector>
#include <string>
#include <fstream>

#include "Common.h"
#include "FileUtil.h"

#include "WiimoteEmu.h"
#include "WiimoteHid.h"
#include "../WiimoteReal/WiimoteReal.h"

#include "Attachment/Attachment.h"

using namespace std;

namespace WiimoteEmu
{

Wiimote* spy_wm = 0;

struct last_report_
{
	u8 type;
	u8 data[MAX_PAYLOAD];
};

void Spy(Wiimote* wm_, const void* data_, int size_)
{
#if 0
	if (size_ <= 0 || size_ > MAX_PAYLOAD)
		return;

	// enable log
	bool log_com = true;
	bool log_data = true;
	bool log_mp = false;
	bool log_audio = false;

	// save data
	bool save = false;

	// use empty container or emulated Wiimote 0
	bool empty_container = false;

	static Wiimote* wm_emu;

	if (!spy_wm)
	{
		wm_emu = 0;

		if (empty_container)
			spy_wm = new Wiimote(0);
		else
			if (wm_)
				spy_wm = wm_;
	}

	if (!spy_wm)
		return;

	if (wm_)
		wm_emu = wm_;

	// short name
	Wiimote* wm = &*spy_wm;

	bool audio_data = false;
	bool data_report = false;

	bool emu = wm_;
	string com = "", formatted = "";
	static int c;
	u8 report_mode = 0;
	u16 reg_type = 0;
	static wm_read_data g_rd;

	// last report
	static last_report_ last_report_emu;
	static last_report_ last_report_real;
	last_report_ &last_data = emu ? last_report_emu : last_report_real;

	// use a non-pointer array because that makes read syntax shorter
	u8 data[MAX_PAYLOAD] = {};
	memcpy(data, data_, size_);

	const hid_packet* const hidp = (hid_packet*)data;
	const wm_report* const sr = (wm_report*)hidp->data;

	// ignore emulated Wiimote data
	//if (emu) return;

	if (g_wiimote_sources[0]
		&& WIIMOTE_SRC_HYBRID == g_wiimote_sources[0]
		&& !emu)
		return;

	switch(sr->wm)
	{
	case WM_RUMBLE:
		if (log_com) com.append("WM_RUMBLE");
		break;

	case WM_LEDS:
		if (log_com) com.append("WM_LEDS");
		break;

	case WM_REPORT_MODE:
		if (log_com)
		{
			com.append("WM_REPORT_MODE");
			formatted = StringFromFormat("mode: 0x%02x", data[3]);
		}
		wm->m_reporting_mode = data[3];
		break;

	case WM_IR_PIXEL_CLOCK:
		if (log_com) com.append("WM_IR_PIXEL_CLOCK");
		break;

	case WM_SPEAKER_ENABLE:
		if (log_com)
		{
			com.append("WM_SPEAKER_ENABLE");
			formatted = StringFromFormat("on: %d", sr->enable);
		}
		break;

	case WM_REQUEST_STATUS:
	{
		wm_request_status* status = (wm_request_status*)sr->data;
		if (log_com)
		{
			com.append("WM_REQUEST_STATUS");
			formatted.append(StringFromFormat("rumble %d", status->rumble));
		}
		break;
	}

	case WM_WRITE_DATA:
	{
		wm_write_data* wd = (wm_write_data*)sr->data;
		u32 address = Common::swap24(wd->address);
		address &= ~0x010000;
		reg_type;

		if (log_com)
		{
			com.append("WRITE");
			formatted.append(StringFromFormat("%02x %08x %02x: %s\n", wd->space, address, wd->size, ArrayToString(wd->data, wd->size, 0).c_str()));
		}

		switch(wd->space)
		{
		case WM_SPACE_EEPROM:
			if (log_com) com.append(" REG_EEPROM");
			if (save)
				memcpy(wm->m_eeprom + address, wd->data, wd->size);
			break;

		case WM_SPACE_REGS1:
		case WM_SPACE_REGS2:
		{
			// reg
			reg_type = address >> 16;
			const u8 region_offset = (u8)address;
			void *region_ptr = NULL;
			int region_size = 0;

			switch(reg_type)
			{
			case WIIMOTE_REG_SPEAKER :
				if (log_com) com.append(" REG_SPEAKER");
				region_ptr = &wm->m_reg_speaker;
				region_size = WIIMOTE_REG_SPEAKER_SIZE;
				break;

			case WIIMOTE_REG_EXT:
				if (log_com) com.append(" REG_EXT");
				region_ptr = wm->m_motion_plus_active ? (void*)&wm->m_reg_motion_plus : (void*)&wm->m_reg_ext;
				region_size = WIIMOTE_REG_EXT_SIZE;
				break;

			case WIIMOTE_REG_MP :
				if (log_com) com.append(" REG_M+");
				if (!wm->m_motion_plus_active)
				{
					region_ptr = &wm->m_reg_motion_plus;
					region_size = WIIMOTE_REG_EXT_SIZE;
				}
				break;

			case WIIMOTE_REG_IR:
				 if (log_com) com.append(" REG_IR");
				region_ptr = &wm->m_reg_ir;
				region_size = WIIMOTE_REG_IR_SIZE;
				 break;
			}

			// save register
			if (save && region_ptr && (region_offset + wd->size <= region_size))
				memcpy((u8*)region_ptr + region_offset, wd->data, wd->size);

			// save key
			if (save && region_ptr == &wm->m_reg_ext && address >= 0xa40040 && address <= 0xa4004c)
			{
				wiimote_gen_key(&wm->m_ext_key, wm->m_reg_ext.encryption_key);
				formatted.append(StringFromFormat("writing key: %s", ArrayToString((u8*)&wm->m_ext_key, sizeof(wm->m_ext_key), 0, 30).c_str()));
			}

			switch(wd->space)
			{
			case WM_SPACE_REGS1:
			case WM_SPACE_REGS2:
			{
			switch(reg_type)
			{
			case WIIMOTE_REG_SPEAKER:
				if (log_com)
				{
					com.append(" REG_SPEAKER");
					if(wd->data[0] == 7)
					{
						u16 sampleValue;

						formatted.append("Sound configuration\n");
						if (wd->data[2] == 0x00)
						{
							memcpy(&sampleValue, &data[9], 2);
							formatted.append(StringFromFormat(
								"    format: 4-bit ADPCM (%i Hz)"
								"    volume: %02i%%"

								, 6000000 / sampleValue
								, (data[11] / 0x40) * 100
								));
						}
						else if (wd->data[2] == 0x40)
						{
							memcpy(&sampleValue, &data[9], 2);
							formatted.append(StringFromFormat(
							"    format: 8-bit PCM (%i Hz)"
							"    volume: %02i%%"

							, 12000000 / sampleValue
							, (data[11] / 0xff) * 100
							));
						}
					}
				}
				break;

				case WIIMOTE_REG_EXT:
				case WIIMOTE_REG_MP :
					if (data[5] == 0xf0)
						formatted.append(StringFromFormat("extension encryption: %02x", wm->m_reg_ext.encryption));
					break;

				break;
			}
			}
			}
		}
		}

		break;
	}

	case WM_READ_DATA:
	{
		wm_read_data* rd = (wm_read_data*)sr->data;
		u32 address = Common::swap24(rd->address);
		address &= 0xFEFFFF;
		rd->size = Common::swap16(rd->size);
		u8 *const block = new u8[rd->size];

		void *region_ptr = NULL;
		int region_size = 0;

		g_rd = *rd;

		if (log_com)
		{
			com.append("READ");
			formatted.append(StringFromFormat("%02x %06x %04x", rd->space, address, rd->size));
		}

		switch(rd->space)
		{
		case WM_SPACE_EEPROM:
			if (log_com) com.append(" REG_EEPROM");
			if (save)
				memcpy(block, wm->m_eeprom + address, rd->size);
			break;

		case WM_SPACE_REGS1:
		case WM_SPACE_REGS2:

			if (address >> 16 == 0xA4)
				address &= 0xFF00FF;
			reg_type = address >> 16;

			const u8 region_offset = (u8)address;

			switch(reg_type)
			{
			case WIIMOTE_REG_SPEAKER:
				if (log_com) com.append(" REG_SPEAKER");
				region_ptr = &wm->m_reg_speaker;
				region_size = WIIMOTE_REG_SPEAKER_SIZE;
				break;

			case WIIMOTE_REG_EXT:
				if (log_com) com.append(" REG_EXT");
				region_ptr = wm->m_motion_plus_active ? (void*)&wm->m_reg_motion_plus : (void*)&wm->m_reg_ext;
				region_size = WIIMOTE_REG_EXT_SIZE;
				break;

			case WIIMOTE_REG_MP:
				if (log_com) com.append(" REG_M+");
				if (!wm->m_motion_plus_active)
				{
					region_ptr = &wm->m_reg_motion_plus;
					region_size = WIIMOTE_REG_EXT_SIZE;
				}
				break;

			case WIIMOTE_REG_IR:
				if (log_com) com.append(" REG_IR");
				region_ptr = &wm->m_reg_ir;
				region_size = WIIMOTE_REG_IR_SIZE;
				break;
			}

			// print data to read
			//if (region_ptr)
			//	memcpy(block, (u8*)region_ptr + region_offset, rd->size);
			//LOG3(CONSOLE, "READING[0x%02x 0x%02x %d]: %s", data[3], region_offset, rd->size, ArrayToString(block, size, 0, 30).c_str());

			break;
		}

		break;
	}

	case WM_WRITE_SPEAKER_DATA:
		if (log_audio)
			com.append("WM_SPEAKER_DATA");
		else
			return;
		break;

	case WM_SPEAKER_MUTE:
		if (log_com)
		{
			com.append("WM_SPEAKER");
			formatted.append(StringFromFormat("mute: %d", sr->enable));
		}
		break;

	case WM_IR_LOGIC:
		if (log_com) com.append("WM_IR");
		break;

	case WM_STATUS_REPORT:
	{
		if (log_com) {
			com.append("WM_STATUS_REPORT");
			wm_status_report* pStatus = (wm_status_report*)(data + 2);
			formatted = StringFromFormat(
				"extension: %i"
				//"speaker enabled: %i"
				//"IR enabled: %i"
				//"LED 1: %i"
				//"LED 2: %i"
				//"LED 3: %i"
				//"LED 4: %i"
				//"Battery low: %i"
				//"Battery level: %i"
				, pStatus->extension
				//, pStatus->speaker
				//, pStatus->ir
				//, (pStatus->leds >> 0)
				//, (pStatus->leds >> 1)
				//, (pStatus->leds >> 2)
				//, (pStatus->leds >> 3)
				//, pStatus->battery_low
				//, pStatus->battery
				);
		}

		break;
	}

	case WM_READ_DATA_REPLY:
	{
		wm_read_data_reply* const rdr = (wm_read_data_reply*)(data + 2);
		u16 address = Common::swap16(rdr->address);
		u8 size = rdr->size + 1;

		if (address != u16(Common::swap24(g_rd.address)))
			LOG3(COMMON, "WM_READ_DATA_REPLY address %04x is different from WM_READ_DATA address %06x", address, Common::swap24(g_rd.address));

		g_rd.address[2] += size;

		if (log_com)
		{
			com.append("READ_REPLY");
			formatted.append(StringFromFormat("%02x %04x %02x %02x: %s\n", g_rd.space, address, size, rdr->error, ArrayToString(rdr->data, size, 0).c_str()));
		}

		switch(g_rd.space)
		{
		case WM_SPACE_EEPROM:
			if (log_com)
				com.append(" REG_EEPROM");

			//LOG3(CONSOLE, "%s", ArrayToString(rdr->data, size).c_str());

			// save
			if (save)
				memcpy(wm->m_eeprom + address, rdr->data, size);

			// Wiimote calibration
			if (address == 0x10) {
				accel_cal* calib = (accel_cal*)&wm->m_eeprom[0x16];
				formatted = StringFromFormat(
					"Wiimote calibration\n"
					"zero x: %u\n"
					"zero y: %u\n"
					"zero z: %u\n"

					"g x: %u\n"
					"g y: %u\n"
					"g z: %u"

					, calib->zero_g.x
					, calib->zero_g.y
					, calib->zero_g.z

					, calib->one_g.x
					, calib->one_g.y
					, calib->one_g.z
					);
			}
			break;

		case WM_SPACE_REGS1:
		case WM_SPACE_REGS2:
		{
			reg_type = Common::swap24(g_rd.address) >> 16;
			const u8 region_offset = (u8)address;
			void *region_ptr = NULL;
			int region_size = 0;

			switch(reg_type)
			{
			case WIIMOTE_REG_SPEAKER:
				if (log_com) com.append(" REG_SPEAKER");
				region_ptr = &wm->m_reg_speaker;
				region_size = WIIMOTE_REG_SPEAKER_SIZE;
				break;

			case WIIMOTE_REG_EXT:
				if (log_com) com.append(" REG_EXT");
				region_ptr = wm->m_motion_plus_active ? (void*)&wm->m_reg_motion_plus : (void*)&wm->m_reg_ext;
				region_size = WIIMOTE_REG_EXT_SIZE;
				break;

			case WIIMOTE_REG_MP:
				if (log_com) com.append(" REG_M+");
				if (!wm->m_motion_plus_active)
				{
					region_ptr = &wm->m_reg_motion_plus;
					region_size = WIIMOTE_REG_EXT_SIZE;
				}
				break;

			case WIIMOTE_REG_IR:
				if (log_com) com.append(" REG_IR");
				region_ptr = &wm->m_reg_ir;
				region_size = WIIMOTE_REG_IR_SIZE;
				break;
			}

			if (wm->m_reg_ext.encryption == 0xaa && region_ptr == &wm->m_reg_ext) {

				//formatted.append(StringFromFormat("key %s\n", ArrayToString(((u8*)&wm->m_ext_key), sizeof(wm->m_ext_key), 0, 30).c_str()));
				formatted.append(StringFromFormat("encrypted %s\n", ArrayToString(rdr->data, size, 0, 30).c_str()));
				wiimote_decrypt(&wm->m_ext_key, rdr->data, address, size);
				formatted.append(StringFromFormat("decrypted %s\n", ArrayToString(rdr->data, size, 0, 30).c_str()));
			}

			// save data
			if (save && !rdr->error && region_ptr == &wm->m_reg_ext && (region_offset + size <= region_size))
				memcpy((u8*)region_ptr + region_offset, rdr->data, size);

			if (!rdr->error && address >= 0xf0 && address <= 0xff)
			{
				formatted.append(StringFromFormat("extension ID: %s\n", ArrayToString(rdr->data, size).c_str()));
				u16 id = Common::swap16(*(u16*)rdr->data);
				int i = 0;
				for (int i = 0; i < wm->m_extension->attachments.size(); i++)
				{
					WiimoteEmu::Attachment* a = (WiimoteEmu::Attachment*)wm->m_extension->attachments.at(i);
					u16 id_ = Common::swap16(*(u16*)&a->reg.constant_id[4]);
					if (id_ == id)
					{
						wm->m_extension->active_extension = i;
						break;
					}
				}
			}

			// save key
			if (save && region_ptr == &wm->m_reg_ext && address == 0x40)
			{
				memcpy(((u8*)&wm->m_reg_ext.encryption_key), rdr->data, size);
				wiimote_gen_key(&wm->m_ext_key, wm->m_reg_ext.encryption_key);
				formatted.append(StringFromFormat("reading key: %s", ArrayToString(((u8*)&wm->m_ext_key), sizeof(wm->m_ext_key), 0, 30).c_str()));
			}

			// Nunchuk calibration
			if (region_ptr == &wm->m_reg_ext && address == 0x20)
			{
				u16 type = *(u16*)&wm->m_reg_ext.constant_id[4];

				if (type == 0x0000)
				{
					nu_cal* cal = (nu_cal*)&wm->m_reg_ext.calibration;

					formatted.append(StringFromFormat(
					"Nunchuk calibration\n"

					"zero x: %u\n"
					"zero y: %u\n"
					"zero z: %u\n"
					"g x: %u\n"
					"g y: %u\n"
					"g z: %u\n"

					"j x center: %u\n"
					"j x max: %i\n"
					"j x min: %i\n"

					"j y center: %u\n"
					"j y max: %u\n"
					"j y min: %u"

					, cal->cal_zero.x
					, cal->cal_zero.y
					, cal->cal_zero.z

					, cal->cal_g.x
					, cal->cal_g.y
					, cal->cal_g.z

					, cal->jx.center
					, cal->jx.max
					, cal->jx.min

					, cal->jy.center
					, cal->jy.max
					, cal->jy.min
					));
				}

				else if (type == 0x0101)
				{
					cc_cal* cal = (cc_cal*)&rdr->data;

					formatted.append(StringFromFormat(
						"Classic Controller calibration\n"

						"l x center: %u\n"
						"l x max: %u\n"
						"l x min: %u\n"

						"l y center: %u\n"
						"l y max: %u\n"
						"l y min: %u\n"

						"r x center.x: %u\n"
						"r x max.x: %u\n"
						"r x min.x: %u\n"

						"r y center: %u\n"
						"r y max.y: %u\n"
						"r y min: %u\n"

						"lt neutral: %u\n"
						"rt neutral %u"

						, cal->Lx.center
						, cal->Lx.max
						, cal->Lx.min

						, cal->Ly.center
						, cal->Ly.max
						, cal->Ly.min

						, cal->Rx.center
						, cal->Rx.max
						, cal->Rx.min

						, cal->Ry.center
						, cal->Ry.max
						, cal->Ry.min

						, cal->Tl.neutral
						, cal->Tr.neutral
						));
				}
			}

			break;
		}
		}

		break;
	}

	case WM_ACK_DATA:
	{
		wm_acknowledge* const ack = (wm_acknowledge*)sr->data;
		if (log_com) com.append(StringFromFormat("WM_ACK_DATA %02x %02x", ack->reportID, ack->errorID));
		break;
	}

	case WM_REPORT_CORE:
	case WM_REPORT_CORE_ACCEL:
	case WM_REPORT_CORE_EXT8:
	case WM_REPORT_CORE_ACCEL_IR12:
	case WM_REPORT_CORE_EXT19:
	case WM_REPORT_CORE_ACCEL_EXT16:
	case WM_REPORT_CORE_IR10_EXT9:
	case WM_REPORT_CORE_ACCEL_IR10_EXT6:
		data_report = true;
		break;

	default:
		if (log_com)
			com = StringFromFormat("unknown report 0x%02x", sr->wm);
		break;
	}

	if (data_report)
		report_mode = data[1];

	//if (data_report && wm->GetMotionPlusActive())
	//{
	//	if (data[1] == WM_REPORT_CORE_ACCEL_IR10_EXT6)
	//		static bool extension = false;
	//	if (extension != (bool)(data[17+4]&1))
	//		LOG3(CONSOLE, "Datareport extension %d", data[17+4]&1);
	//		extension = data[17+4]&1;
	//}

	if (log_com && !com.empty())
	{
		switch(sr->wm)
		{
		case WM_WRITE_DATA:
		case WM_READ_DATA:
		case WM_READ_DATA_REPLY:
			switch(reg_type)
			{
			case WIIMOTE_REG_EXT:
				LOG6(CONSOLE, "com[%s] %s", emu ? "E" : "R", com.c_str());
				break;
			default:
				LOG8(CONSOLE, "com[%s] %s", emu ? "E" : "R", com.c_str());
				break;
			}
			break;

		default:
			LOG2(CONSOLE, "com[%s] %s", emu ? "E" : "R", com.c_str());
		}

		if (!formatted.empty())
			LOG1(CONSOLE, "%s", formatted.c_str());
	}

	if (log_audio && audio_data)
	{
		//DEBUG_LOG(CONSOLE, "%s: %s\n", com.c_str(), ArrayToString(data, min(10, data_size), 0, 30).c_str());
	}

	if (data_report && (log_data || log_mp))
	{
		u8 mode = data[1];
		const ReportFeatures& rptf = reporting_mode_features[mode - WM_REPORT_CORE];

		//if (wm->m_reporting_mode != mode)
			//LOG2(WIIMOTE, "state mode %x, data mode %x", wm->m_reporting_mode, mode);

		string s_core = "", s_acc = "", s_ir = "", s_ext = "", s_ext_id = "";

		wm_core* core = (wm_core*)sr->data;
		accel_cal* calib = (accel_cal*)&wm->m_eeprom[0x16];
		wm_accel* accel = (wm_accel*)&data[4];

		s_acc = StringFromFormat(
			//"%3d %3d %3d"
			//"%3d %3d %3d"
			//"%3d %3d %3d"
			"%5.2f %5.2f %5.2f"

			//, calib->zero_g.x, calib->zero_g.y, calib->zero_g.z

			//, (calib->zero_g.x<<2) + calib->zero_g.xL, (calib->zero_g.y<<2) + calib->zero_g.yL, (calib->zero_g.z << 2) + calib->zero_g.zL

			//, calib->one_g.x, calib->one_g.y, calib->one_g.z
			//, (calib->one_g.x << 2) + calib->one_g.xL, (calib->one_g.y << 2) + calib->one_g.yL, (calib->one_g.z << 2) + calib->one_g.zL

			//, accel->x, accel->y, accel->z
			//, (accel->x << 2) + core->xL, (accel->y << 2) + core->yL, (accel->z << 2) + core->zL

			, (accel->x - calib->zero_g.x) / float(calib->one_g.x - calib->zero_g.x)
			, (accel->y - calib->zero_g.y) / float(calib->one_g.y - calib->zero_g.y)
			, (accel->z - calib->zero_g.z) / float(calib->one_g.z - calib->zero_g.z));

		if (rptf.ir)
		{
			if (mode == WM_REPORT_CORE_ACCEL_IR12)
			{
				wm_ir_extended *ir = (wm_ir_extended*)&data[rptf.ir];

				s_ir = StringFromFormat(
					"%4u %4u | %u"
					, ir->x | ir->xhi << 8
					, ir->y | ir->yhi << 8
					, ir->size);
			}

			else if (mode == WM_REPORT_CORE_ACCEL_IR10_EXT6)
			{
				wm_ir_basic *ir = (wm_ir_basic*)&data[rptf.ir];

				s_ir = StringFromFormat(
					"%4u %4u %4u %4u"
					, ir->x1 | ir->x1hi << 8
					, ir->y1 | ir->y1hi << 8
					, ir->x2 | ir->x2hi << 8
					, ir->y2 | ir->y1hi << 8);
			}
		}

		if (rptf.ext)
		{
			// decrypt
			//if (wm->m_reg_ext.encryption == 0xaa && !wm->GetMotionPlusActive())
			if (wm->m_reg_ext.encryption == 0xaa)
				wiimote_decrypt(&wm->m_ext_key, &data[rptf.ext], 0x00, 0x06);

			if (wm->m_motion_plus_active)
			{
				/*
				wm_motionplus_data *mp = (wm_motionplus_data*)&data[17];
				wm_nc_mp *nc_mp = (wm_nc_mp*)&data[17];

				if (!log_mp && mp->is_mp_data)
					return;
				if (!log_data && !mp->is_mp_data)
					return;

				if (mp->is_mp_data)
				{
					s_ext = StringFromFormat(""
						//"%02x %02x %02x %02x %02x %02x"
						//"| %04x %04x %04x
						" %5.2f %5.2f %5.2f"
						" %s%s%s"
						//, mp->roll1, mp->roll2
						//, mp->pitch1, mp->pitch2
						//, mp->yaw1, mp->yaw2
						//, mp->pitch2<<8 | mp->pitch1
						//, mp->roll2<<8 | mp->roll1
						//, mp->yaw2<<8 | mp->yaw1
						//, mp->pitch2<<8 | mp->pitch1
						//, mp->roll2<<8 | mp->roll1
						//, mp->yaw2<<8 | mp->yaw1
						, float((mp->pitch2<<8 | mp->pitch1) - 0x1f7f) / float(0x1fff)
						, float((mp->roll2<<8 | mp->roll1) - 0x1f7f) / float(0x1fff)
						, float((mp->yaw2<<8 | mp->yaw1) - 0x1f7f) / float(0x1fff)
						, mp->pitch_slow?"*":" ", mp->roll_slow?"*":" ", mp->yaw_slow?"*":" ");
				}
				else
				{
					s_ext = StringFromFormat(
						"%02x %02x | %02x %02x | %02x %02x %02x | %02x %02x %02x",
						nc_mp->bt.z, nc_mp->bt.c,
						nc_mp->jx, nc_mp->jy,
						nc_mp->ax + nc_mp->axL, nc_mp->ay + nc_mp->ayL, (nc_mp->az << 1) + nc_mp->azL);
				}

				s_ext_id = StringFromFormat(
					"%s %d %d"
					, mp->is_mp_data ? "+" : "e"
					, mp->is_mp_data ? mp->extension_connected : wm_nc_mp->extension_connected
					, wm->m_extension->active_extension);
				*/
			}
			else
			{
				nu_cal *cal = (nu_cal*)&wm->m_reg_ext.calibration;
				wm_extension *nc = (wm_extension*)&data[rptf.ext];

				s_ext = StringFromFormat(
					"%d %d"

					", %3u %3u [%3u %3u %3u]"
					", %.2f [%.2f, %.2f] %.2f [%.2f, %.2f]"

					", %4d %4d %4d"
					, nc->bt.z, nc->bt.c

					, nc->jx, nc->jy
					, cal->jx.min, cal->jx.center, cal->jx.max

					, double(nc->jx - cal->jx.center) / (double(abs(cal->jx.min - cal->jx.center) + abs(cal->jx.max - cal->jx.center)) / 2.0)
					, double(cal->jx.min - cal->jx.center) / double(abs(cal->jx.min - cal->jx.center))
					, double(cal->jx.max - cal->jx.center) /double(abs(cal->jx.max - cal->jx.center))

					, double(nc->jy - cal->jy.center) / (double(abs(cal->jy.min - cal->jy.center) + abs(cal->jy.max - cal->jy.center)) / 2.0)
					, double(cal->jy.min - cal->jy.center) / double(abs(cal->jy.min - cal->jx.center))
					, double(cal->jy.max - cal->jy.center) / double(abs(cal->jy.max - cal->jx.center))

					, nc->ax
					, nc->ay
					, nc->az

					//, (nc->ax << 1) + nc->axL
					//, (nc->ay << 1) + nc->ayL
					//, (nc->az << 1) + nc->azL
					);
			}

			//LOG1(CONSOLE, "M+ %d Extension %d %d %s", mp->is_mp_data, mp->is_mp_data ?
			//		mp->extension_connected : ((wm_nc_mp*)&data[17])->extension_connected, wm->m_extension->active_extension,
			//		ArrayToString(((u8*)&wm->m_reg_motion_plus.ext_identifier), sizeof(wm->m_reg_motion_plus.ext_identifier), 0).c_str());
		}

		// log data
		string s_data = StringFromFormat("data[%s] %02x:", (emu ? "E" : "R"), report_mode);

		if (!s_ext_id.empty())
			s_data.append(StringFromFormat(" | e %s", s_ext_id.c_str()));

		if (!s_core.empty())
			s_data.append(StringFromFormat(" | c %s", s_core.c_str()));

		if (!s_acc.empty())
			s_data.append(StringFromFormat(" | a %s", s_acc.c_str()));

		if (!s_ir.empty())
			s_data.append(StringFromFormat(" | ir %s", s_ir.c_str()));

		if (!s_ext.empty())
			s_data.append(StringFromFormat(" | ext %s", s_ext.c_str()));

		// unformatted
		//s_data = StringFromFormat("data[%s]: %s", (emu ? "E" : "R"), ArrayToString(data, rptf.size).c_str());

		// data report changed
		u16 unset = 0x20 | 0x40 | 0x80 | 0x2000 | 0x4000;
		*(u16*)&data[rptf.core] &= ~unset;

		// ignore ir because it change often
		if (rptf.ir)
		{
			if (rptf.ext)
				memset(&data[rptf.ir], 0xff, rptf.ext - rptf.ir);
			else
				memset(&data[rptf.ir], 0xff, rptf.size - rptf.ir);
		}

		wm_accel &acc = *(wm_accel*)&data[rptf.accel];
		wm_accel &acc_last = *(wm_accel*)&last_data.data[rptf.accel];
		if (abs(acc.x - acc_last.x) < 10)
			acc_last.x = acc.x;
		if (abs(acc.y - acc_last.y) < 10)
			acc_last.y = acc.y;
		if (abs(acc.z - acc_last.z) < 10)
			acc_last.z = acc.z;

		if (rptf.ext)
		{
			wm_extension &ext = *(wm_extension*)&data[rptf.ext];
			wm_extension &ext_last = *(wm_extension*)&last_data.data[rptf.ext];
			if (abs(ext.ax - ext_last.ax) < 10)
				ext_last.ax = ext.ax;
			if (abs(ext.ay - ext_last.ay) < 10)
				ext_last.ay = ext.ay;
			if (abs(ext.az - ext_last.az) < 10)
				ext_last.az = ext.az;
			ext.bt.axL = 0;
			ext.bt.ayL = 0;
			ext.bt.azL = 0;
		}

		bool data_report_change = memcmp(last_data.data, data, rptf.size);

		if (data_report_change)
			LOG3(CONSOLE, "%s", s_data.c_str());
		else
			LOG4(CONSOLE, "%s", s_data.c_str());

		memcpy(last_data.data, data, rptf.size);
		last_data.type = sr->wm;
	}
#endif
}

void Wiimote::ReportMode(const wm_report_mode* const dr)
{
	//INFO_LOG(WIIMOTE, "Set data report mode");
	//DEBUG_LOG(WIIMOTE, "  Rumble: %x", dr->rumble);
	//DEBUG_LOG(WIIMOTE, "  Continuous: %x", dr->continuous);
	//DEBUG_LOG(WIIMOTE, "  All The Time: %x", dr->all_the_time);
	//DEBUG_LOG(WIIMOTE, "  Mode: 0x%02x", dr->mode);

	//m_reporting_auto = dr->all_the_time;
	m_reporting_auto = dr->continuous;	// this right?
	m_reporting_mode = dr->mode;
	//m_reporting_channel = _channelID;	// this is set in every Interrupt/Control Channel now

	// reset IR camera
	//memset(m_reg_ir, 0, sizeof(*m_reg_ir));  //ugly hack

	if (dr->mode > 0x37)
		PanicAlert("Wiimote: Unsupported Reporting mode.");
	else if (dr->mode < WM_REPORT_CORE)
		PanicAlert("Wiimote: Reporting mode < 0x30.");
}

/* Here we process the Output Reports that the Wii sends. Our response will be
   an Input Report back to the Wii. Input and Output is from the Wii's
   perspective, Output means data to the Wiimote (from the Wii), Input means
   data from the Wiimote.

   The call browser:

   1. Wiimote_InterruptChannel > InterruptChannel > HidOutputReport
   2. Wiimote_ControlChannel > ControlChannel > HidOutputReport

   The IR enable/disable and speaker enable/disable and mute/unmute values are
		bit2: 0 = Disable (0x02), 1 = Enable (0x06)
*/
void Wiimote::HidOutputReport(const wm_report* const sr, const bool send_ack)
{
	INFO_LOG(WIIMOTE, "HidOutputReport (page: %i, cid: 0x%02x, wm: 0x%02x)", m_index, m_reporting_channel, sr->wm);

	// wiibrew:
	// In every single Output Report, bit 0 (0x01) of the first byte controls the Rumble feature.
	m_rumble_on = sr->rumble;

	switch (sr->wm)
	{
	case WM_RUMBLE : // 0x10
		// this is handled above
		return;	// no ack
		break;

	case WM_LEDS : // 0x11
		//INFO_LOG(WIIMOTE, "Set LEDs: 0x%02x", sr->data[0]);
		m_status.leds = sr->data[0] >> 4;
		break;

	case WM_REPORT_MODE :  // 0x12
		ReportMode((wm_report_mode*)sr->data);
		break;

	case WM_IR_PIXEL_CLOCK : // 0x13
		//INFO_LOG(WIIMOTE, "WM IR Clock: 0x%02x", sr->data[0]);
		//m_ir_clock = sr->enable;
		if (false == sr->ack)
			return;
		break;

	case WM_SPEAKER_ENABLE : // 0x14
		//ERROR_LOG(WIIMOTE, "WM Speaker Enable: %02x", sr->enable);
		//PanicAlert( "WM Speaker Enable: %d", sr->data[0] );
		m_status.speaker = sr->enable;
		if (false == sr->ack)
			return;
		break;

	case WM_REQUEST_STATUS : // 0x15
		if (WIIMOTE_SRC_EMU & g_wiimote_sources[m_index])
			RequestStatus((wm_request_status*)sr->data);
		return;	// sends its own ack
		break;

	case WM_WRITE_DATA : // 0x16
		WriteData((wm_write_data*)sr->data);
		break;

	case WM_READ_DATA : // 0x17
		if (WIIMOTE_SRC_EMU & g_wiimote_sources[m_index])
			ReadData((wm_read_data*)sr->data);
		return;	// sends its own ack
		break;

	case WM_WRITE_SPEAKER_DATA : // 0x18
		{
		//wm_speaker_data *spkz = (wm_speaker_data*)sr->data;
		//ERROR_LOG(WIIMOTE, "WM_WRITE_SPEAKER_DATA len:%x %s", spkz->length,
		//	ArrayToString(spkz->data, spkz->length, 100, false).c_str());
		Wiimote::SpeakerData((wm_speaker_data*)sr->data);
		}
		return;	// no ack
		break;

	case WM_SPEAKER_MUTE : // 0x19
		//ERROR_LOG(WIIMOTE, "WM Speaker Mute: %02x", sr->enable);
		//PanicAlert( "WM Speaker Mute: %d", sr->data[0] & 0x04 );
		// testing
		//if (sr->data[0] & 0x04)
		//	memset(&m_channel_status, 0, sizeof(m_channel_status));
		m_speaker_mute = sr->enable;
		if (false == sr->ack)
			return;
		break;

	case WM_IR_LOGIC: // 0x1a
		// comment from old plugin:
		// This enables or disables the IR lights, we update the global variable g_IR
		// so that WmRequestStatus() knows about it
		//INFO_LOG(WIIMOTE, "WM IR Enable: 0x%02x", sr->data[0]);
		m_status.ir = sr->enable;
		if (false == sr->ack)
			return;
		break;

	default:
		PanicAlert("HidOutputReport: Unknown channel 0x%02x", sr->wm);
		return; // no ack
		break;
	}

	// send ack
	if (send_ack && WIIMOTE_SRC_EMU & g_wiimote_sources[m_index])
		SendAck(sr->wm);
}

/* This will generate the 0x22 acknowledgement for most Input reports.
   It has the form of "a1 22 00 00 _reportID 00".
   The first two bytes are the core buttons data,
   00 00 means nothing is pressed.
   The last byte is the success code 00. */
void Wiimote::SendAck(u8 _reportID)
{
	u8 data[6];

	data[0] = 0xA1;
	data[1] = WM_ACK_DATA;

	wm_acknowledge* const ack = (wm_acknowledge*)(data + 2);

	ack->buttons = m_status.buttons;
	ack->reportID = _reportID;
	ack->errorID = 0;

	WiimoteEmu::Spy(this, data, (int)sizeof(data));
	Core::Callback_WiimoteInterruptChannel( m_index, m_reporting_channel, data, sizeof(data));
}

void Wiimote::HandleExtensionSwap()
{
	// handle switch extension
	if (m_extension->active_extension != m_extension->switch_extension)
	{
		// if an extension is currently connected and we want to switch to a different extension
		if ((m_extension->active_extension > 0) && m_extension->switch_extension)
			// detach extension first, wait til next Update() or RequestStatus() call to change to the new extension
			m_extension->active_extension = 0;
		else
			// set the wanted extension
			m_extension->active_extension = m_extension->switch_extension;

		// reset register
		((WiimoteEmu::Attachment*)m_extension->attachments[m_extension->active_extension])->Reset();
	}
}

// old comment
/* Here we produce a 0x20 status report to send to the Wii. We currently ignore
   the status request rs and all its eventual instructions it may include (for
   example turn off rumble or something else) and just send the status
   report. */
void Wiimote::RequestStatus(const wm_request_status* const rs)
{
	HandleExtensionSwap();

	// update status struct
	m_status.extension = (m_extension->active_extension || m_motion_plus_active) ? 1 : 0;

	// set up report
	u8 data[8];
	data[0] = 0xA1;
	data[1] = WM_STATUS_REPORT;

	// status values
	*(wm_status_report*)(data + 2) = m_status;

	// hybrid wiimote stuff
	if (WIIMOTE_SRC_REAL & g_wiimote_sources[m_index] && (m_extension->switch_extension <= 0))
	{
		using namespace WiimoteReal;

		std::lock_guard<std::recursive_mutex> lk(g_refresh_lock);

		if (g_wiimotes[m_index])
		{
			wm_request_status rpt = {};
			g_wiimotes[m_index]->QueueReport(WM_REQUEST_STATUS, &rpt, sizeof(rpt));
		}

		return;
	}

	// send report
	WiimoteEmu::Spy(this, data, sizeof(data));
	Core::Callback_WiimoteInterruptChannel(m_index, m_reporting_channel, data, sizeof(data));
}

/* Write data to Wiimote and Extensions registers. */
void Wiimote::WriteData(const wm_write_data* const wd)
{
	u32 address = Common::swap24(wd->address);

	// ignore the 0x010000 bit
	address &= ~0x010000;

	if (wd->size > 16)
	{
		PanicAlert("WriteData: size is > 16 bytes");
		return;
	}

	switch (wd->space)
	{
	case WM_SPACE_EEPROM :
		{
			// Write to EEPROM

			if (address + wd->size > WIIMOTE_EEPROM_SIZE)
			{
				ERROR_LOG(WIIMOTE, "WriteData: address + size out of bounds!");
				PanicAlert("WriteData: address + size out of bounds!");
				return;
			}
			memcpy(m_eeprom + address, wd->data, wd->size);

			// write mii data to file
			// i need to improve this greatly
			if (address >= 0x0FCA && address < 0x12C0)
			{
				// writing the whole mii block each write :/
				std::ofstream file;
				OpenFStream(file, File::GetUserPath(D_WIIUSER_IDX) + "mii.bin", std::ios::binary | std::ios::out);
				file.write((char*)m_eeprom + 0x0FCA, 0x02f0);
				file.close();
			}
		}
		break;

	case WM_SPACE_REGS1 :
	case WM_SPACE_REGS2 :
		{
			// Write to Control Register

			// ignore second byte for extension area
			if (0xA4 == (address >> 16))
				address &= 0xFF00FF;

			const u8 region_offset = (u8)address;
			void *region_ptr = NULL;
			int region_size = 0;

			switch (address >> 16)
			{
			// speaker
			case WIIMOTE_REG_SPEAKER :
				region_ptr = &m_reg_speaker;
				region_size = WIIMOTE_REG_SPEAKER_SIZE;
				break;

			// extension register
			case WIIMOTE_REG_EXT :
				region_ptr = m_motion_plus_active ? (void*)&m_reg_motion_plus : (void*)&m_reg_ext;
				region_size = WIIMOTE_REG_EXT_SIZE;
				break;

			// motion plus
			case WIIMOTE_REG_MP :
				if (false == m_motion_plus_active)
				{
					region_ptr = &m_reg_motion_plus;
					region_size = WIIMOTE_REG_EXT_SIZE;
				}
				break;

			// ir
			case 0xB0 :
				region_ptr = &m_reg_ir;
				region_size = WIIMOTE_REG_IR_SIZE;

				//if (5 == m_reg_ir->mode)
				//	PanicAlert("IR Full Mode is Unsupported!");
				break;
			}

			if (region_ptr && (region_offset + wd->size <= region_size))
			{
				memcpy((u8*)region_ptr + region_offset, wd->data, wd->size);
			}
			else
				return;	// TODO: generate a writedata error reply

			/* TODO?
			if (region_ptr == &m_reg_speaker)
			{
				ERROR_LOG(WIIMOTE, "Write to speaker register %x %s", address,
					ArrayToString(wd->data, wd->size, 100, false).c_str());
			}
			*/

			if (&m_reg_ext == region_ptr)
			{
				// Run the key generation on all writes in the key area, it doesn't matter
				// that we send it parts of a key, only the last full key will have an effect
				if (address >= 0xa40040 && address <= 0xa4004c)
					wiimote_gen_key(&m_ext_key, m_reg_ext.encryption_key);
			}
			else if (&m_reg_motion_plus == region_ptr)
			{
				// activate/deactivate motion plus
				if (0x55 == m_reg_motion_plus.activated)
				{
					// maybe hacky
					m_reg_motion_plus.activated = 0;
					m_motion_plus_active ^= 1;

					RequestStatus();
				}
			}
		}
		break;

	default:
		PanicAlert("WriteData: unimplemented parameters!");
		break;
	}
}

/* Read data from Wiimote and Extensions registers. */
void Wiimote::ReadData(const wm_read_data* const rd)
{
	u32 address = Common::swap24(rd->address);
	u16 size = Common::swap16(rd->size);

	// ignore the 0x010000 bit
	address &= 0xFEFFFF;

	// hybrid wiimote stuff
	// relay the read data request to real-wiimote
	if (WIIMOTE_SRC_REAL & g_wiimote_sources[m_index] && ((0xA4 != (address >> 16)) || (m_extension->switch_extension <= 0)))
	{
		WiimoteReal::InterruptChannel(m_index, m_reporting_channel, ((u8*)rd) - 2, sizeof(wm_read_data) + 2); // hacky

		// don't want emu-wiimote to send reply
		return;
	}

	ReadRequest rr;
	u8 *const block = new u8[size];

	switch (rd->space)
	{
	case WM_SPACE_EEPROM :
		{
			//PanicAlert("ReadData: reading from EEPROM: address: 0x%x size: 0x%x", address, size);
			// Read from EEPROM
			if (address + size >= WIIMOTE_EEPROM_FREE_SIZE)
			{
				if (address + size > WIIMOTE_EEPROM_SIZE)
				{
					PanicAlert("ReadData: address + size out of bounds");
					delete [] block;
					return;
				}
				// generate a read error
				size = 0;
			}

			// read mii data from file
			// i need to improve this greatly
			if (address >= 0x0FCA && address < 0x12C0)
			{
				// reading the whole mii block :/
				std::ifstream file;
				file.open((File::GetUserPath(D_WIIUSER_IDX) + "mii.bin").c_str(), std::ios::binary | std::ios::in);
				file.read((char*)m_eeprom + 0x0FCA, 0x02f0);
				file.close();
			}

			// read mem to be sent to wii
			memcpy(block, m_eeprom + address, size);
		}
		break;

	case WM_SPACE_REGS1 :
	case WM_SPACE_REGS2 :
		{
			// Read from Control Register

			// ignore second byte for extension area
			if (0xA4 == (address >> 16))
				address &= 0xFF00FF;

			const u8 region_offset = (u8)address;
			void *region_ptr = NULL;
			int region_size = 0;

			switch (address >> 16)
			{
			// speaker
			case WIIMOTE_REG_SPEAKER:
				region_ptr = &m_reg_speaker;
				region_size = WIIMOTE_REG_SPEAKER_SIZE;
				break;

			// extension
			case WIIMOTE_REG_EXT:
				region_ptr = m_motion_plus_active ? (void*)&m_reg_motion_plus : (void*)&m_reg_ext;
				region_size = WIIMOTE_REG_EXT_SIZE;
				break;

			// motion plus
			case WIIMOTE_REG_MP:
				// reading from WIIMOTE_REG_MP returns error when mplus is activated
				if (false == m_motion_plus_active)
				{
					region_ptr = &m_reg_motion_plus;
					region_size = WIIMOTE_REG_EXT_SIZE;
				}
				break;

			// ir
			case WIIMOTE_REG_IR:
				region_ptr = &m_reg_ir;
				region_size = WIIMOTE_REG_IR_SIZE;
				break;
			}

			if (region_ptr && (region_offset + size <= region_size))
			{
				memcpy(block, (u8*)region_ptr + region_offset, size);
			}
			else
				size = 0;	// generate read error

			if (&m_reg_ext == region_ptr)
			{
				// Encrypt data read from extension register
				// Check if encrypted reads is on
				if (0xaa == m_reg_ext.encryption)
					wiimote_encrypt(&m_ext_key, block, address & 0xffff, (u8)size);
			}
		}
		break;

	default :
		PanicAlert("WmReadData: unimplemented parameters (size: %i, address: 0x%x)!", size, rd->space);
		break;
	}

	// want the requested address, not the above modified one
	rr.address = Common::swap24(rd->address);
	rr.size = size;
	//rr.channel = _channelID;
	rr.position = 0;
	rr.data = block;

	// send up to 16 bytes
	SendReadDataReply(rr);

	// if there is more data to be sent, add it to the queue
	if (rr.size)
		m_read_requests.push( rr );
	else
		delete[] rr.data;
}

// old comment
/* Here we produce the actual 0x21 Input report that we send to the Wii. The
   message is divided into 16 bytes pieces and sent piece by piece. There will
   be five formatting bytes at the begging of all reports. A common format is
   00 00 f0 00 20, the 00 00 means that no buttons are pressed, the f means 16
   bytes in the message, the 0 means no error, the 00 20 means that the message
   is at the 00 20 offest in the registry that was read.
*/
void Wiimote::SendReadDataReply(ReadRequest& _request)
{
	u8 data[23];
	data[0] = 0xA1;
	data[1] = WM_READ_DATA_REPLY;

	wm_read_data_reply* const reply = (wm_read_data_reply*)(data + 2);
	reply->buttons = m_status.buttons;
	reply->address = Common::swap16(_request.address);

	// generate a read error
	// Out of bounds. The real Wiimote generate an error for the first
	// request to 0x1770 if we dont't replicate that the game will never
	// read the calibration data at the beginning of Eeprom. I think this
	// error is supposed to occur when we try to read above the freely
	// usable space that ends at 0x16ff.
	if (0 == _request.size)
	{
		reply->size = 0x0f;
		reply->error = 0x08;

		memset(reply->data, 0, sizeof(reply->data));
	}
	else
	{
		// Limit the amt to 16 bytes
		// AyuanX: the MTU is 640B though... what a waste!
		const int amt = std::min( (unsigned int)16, _request.size );

		// no error
		reply->error = 0;

		// 0x1 means two bytes, 0xf means 16 bytes
		reply->size = amt - 1;

		// Clear the mem first
		memset(reply->data, 0, sizeof(reply->data));

		// copy piece of mem
		memcpy(reply->data, _request.data + _request.position, amt);

		// update request struct
		_request.size -= amt;
		_request.position += amt;
		_request.address += amt;
	}

	// Send a piece
	WiimoteEmu::Spy(this, data, sizeof(data));
	Core::Callback_WiimoteInterruptChannel(m_index, m_reporting_channel, data, sizeof(data));
}

void Wiimote::DoState(PointerWrap& p)
{
	p.Do(m_extension->active_extension);
	p.Do(m_extension->switch_extension);

	p.Do(m_accel);
	p.Do(m_index);
	p.Do(ir_sin);
	p.Do(ir_cos);
	p.Do(m_rumble_on);
	p.Do(m_speaker_mute);
	p.Do(m_motion_plus_present);
	p.Do(m_motion_plus_active);
	p.Do(m_reporting_auto);
	p.Do(m_reporting_mode);
	p.Do(m_reporting_channel);
	p.Do(m_shake_step);
	p.Do(m_sensor_bar_on_top);
	p.Do(m_status);
	p.Do(m_adpcm_state);
	p.Do(m_ext_key);
	p.DoArray(m_eeprom, sizeof(m_eeprom));
	p.Do(m_reg_motion_plus);
	p.Do(m_reg_ir);
	p.Do(m_reg_ext);
	p.Do(m_reg_speaker);

	//Do 'm_read_requests' queue
	{
		u32 size = 0;
		if (p.mode == PointerWrap::MODE_READ)
		{
			//clear
			while (m_read_requests.size())
				m_read_requests.pop();

			p.Do(size);
			while (size--)
			{
				ReadRequest tmp;
				p.Do(tmp.address);
				p.Do(tmp.position);
				p.Do(tmp.size);
				tmp.data = new u8[tmp.size];
				p.DoArray(tmp.data, tmp.size);
				m_read_requests.push(tmp);
			}
		}
		else
		{
			std::queue<ReadRequest> tmp_queue(m_read_requests);
			size = m_read_requests.size();
			p.Do(size);
			while (!tmp_queue.empty())
			{
				ReadRequest tmp = tmp_queue.front();
				p.Do(tmp.address);
				p.Do(tmp.position);
				p.Do(tmp.size);
				p.DoArray(tmp.data, tmp.size);
				tmp_queue.pop();
			}
		}
	}
	p.DoMarker("Wiimote");

	if (p.GetMode() == PointerWrap::MODE_READ)
		RealState();
}

// load real Wiimote state
void Wiimote::RealState()
{
	using namespace WiimoteReal;

	if (g_wiimotes[m_index])
	{
		g_wiimotes[m_index]->SetChannel(m_reporting_channel);
		g_wiimotes[m_index]->EnableDataReporting(m_reporting_mode);
	}
}

}
