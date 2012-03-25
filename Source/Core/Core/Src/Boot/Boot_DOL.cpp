// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <algorithm>

#include "Boot_DOL.h"
#include "FileUtil.h"
#include "../HW/Memmap.h"
#include "CommonFuncs.h"

CDolLoader::CDolLoader(u8* _pBuffer, u32 _Size)
	: m_isWii(false)
{
	Initialize(_pBuffer, _Size);
}

CDolLoader::CDolLoader(const char* _szFilename)
	: m_isWii(false)
{
	const u64 size = File::GetSize(_szFilename);
	std::vector<u8> tmpBuffer((size_t)size);

	{
	File::IOFile pStream(_szFilename, "rb");	
	pStream.ReadBytes(tmpBuffer.data(), (size_t)size);
	}

	Initialize(tmpBuffer.data(), (u32)size);
}

void CDolLoader::Initialize(u8* _pBuffer, u32 _Size)
{	
	memcpy(&m_dolheader, _pBuffer, sizeof(SDolHeader));

	// swap memory
	u32* p = (u32*)&m_dolheader;
	for (size_t i = 0; i < (sizeof(SDolHeader)/sizeof(u32)); i++)	
		p[i] = Common::swap32(p[i]);
	
	u32 HID4_pattern = 0x7c13fba6;
	u32 HID4_mask = 0xfc1fffff;

	for (int i = 0; i < DOL_NUM_TEXT; i++)
	{
		if (m_dolheader.textOffset[i] != 0)
		{
			auto const data = _pBuffer + m_dolheader.textOffset[i];
			text_section[i].assign(data, data + m_dolheader.textSize[i]);
			
			for (unsigned int j = 0; j < (m_dolheader.textSize[i]/sizeof(u32)); j++)
			{
				u32 word = Common::swap32(((u32*)text_section[i].data())[j]);
				if ((word & HID4_mask) == HID4_pattern)
				{
					m_isWii = true;
					break;
				}
			}
		}
	}

	for (int i = 0; i < DOL_NUM_DATA; i++)
	{
		if (m_dolheader.dataOffset[i] != 0)
		{
			auto const data = _pBuffer + m_dolheader.dataOffset[i];
			data_section[i].assign(data, data + m_dolheader.dataSize[i]);
		}
	}
}

void CDolLoader::Load()
{
	// load all text (code) sections
	for (int i = 0; i < DOL_NUM_TEXT; i++)
	{
		if (m_dolheader.textOffset[i] != 0)
		{
			for (u32 num = 0; num < m_dolheader.textSize[i]; num++)
				Memory::Write_U8(text_section[i][num], m_dolheader.textAddress[i] + num);
		}
	}

	// load all data sections
	for (int i = 0; i < DOL_NUM_DATA; i++)
	{
		if (m_dolheader.dataOffset[i] != 0)
		{
			for (u32 num = 0; num < m_dolheader.dataSize[i]; num++)
				Memory::Write_U8(data_section[i][num], m_dolheader.dataAddress[i] + num);
		}
	}
}
