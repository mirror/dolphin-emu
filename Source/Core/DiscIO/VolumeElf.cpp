// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <algorithm>
#include <math.h>

#include "VolumeElf.h"
#include "StringUtil.h"
#include "MathUtil.h"

#define ALIGN_40(x) ROUND_UP(Common::swap32(x), 0x40)

namespace DiscIO
{
	CVolumeELF::CVolumeELF(IBlobReader* _pReader)
		: m_pReader(_pReader)
	{}

	CVolumeELF::~CVolumeELF()
	{
		delete m_pReader;
		m_pReader = NULL; // I don't think this makes any difference, but anyway
	}

	bool CVolumeELF::Read(u64 _Offset, u64 _Length, u8* _pBuffer) const
	{
		if (m_pReader == NULL)
			return false;

		return m_pReader->Read(_Offset, _Length, _pBuffer);
	}

	IVolume::ECountry CVolumeELF::GetCountry() const
	{
		return COUNTRY_UNKNOWN;
	}

	bool CVolumeELF::GetTitleID(u8* _pBuffer) const
	{
		u32 Offset = ALIGN_40(hdr_size) + ALIGN_40(cert_size);

		if (!Read(Offset + 0x01DC, 8, _pBuffer))
			return false;

		return true;
	}

	std::vector<std::string> CVolumeELF::GetNames() const
	{
		std::vector<std::string> names;

		u32 footer_size;
		if (!Read(0x1C, 4, (u8*)&footer_size))
		{
			return names;
		}

		footer_size = Common::swap32(footer_size);

		//Japanese, English, German, French, Spanish, Italian, Dutch, unknown, unknown, Korean
		for (int i = 0; i != 10; ++i)
		{
			static const u32 string_length = 42;
			static const u32 bytes_length = string_length * sizeof(u16);

			u16 temp[string_length];

			if (footer_size < 0xF1 || !Read(0x9C + (i * bytes_length) + OpeningBnrOffset, bytes_length, (u8*)&temp))
			{
				names.push_back("");
			}
			else
			{
				std::wstring out_temp;
				out_temp.resize(string_length);
				std::transform(temp, temp + out_temp.size(), out_temp.begin(), (u16(&)(u16))Common::swap16);
				out_temp.erase(std::find(out_temp.begin(), out_temp.end(), 0x00), out_temp.end());

				names.push_back(UTF16ToUTF8(out_temp));
			}
		}

		return names;
	}

	u64 CVolumeELF::GetSize() const
	{
		if (m_pReader)
			return m_pReader->GetDataSize();
		else
			return 0;
	}

	u64 CVolumeELF::GetRawSize() const
	{
		if (m_pReader)
			return m_pReader->GetRawSize();
		else
			return 0;
	}

} // namespace
