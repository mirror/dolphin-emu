// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <string>
#include <vector>
#include <QDir>
#include "Util/Resources.h"

#include "Common.h"
#include "CommonPaths.h"

#include "FileUtil.h"
#include "ISOFile.h"
#include "StringUtil.h"
#include "Hash.h"
#include "IniFile.h"

#include "Filesystem.h"
#include "BannerLoader.h"
#include "FileSearch.h"
#include "CompressedBlob.h"
#include "ChunkFile.h"
#include "ConfigManager.h"

static const u32 CACHE_REVISION = 0x115;

#define DVD_BANNER_WIDTH 96
#define DVD_BANNER_HEIGHT 32

GameListItem::GameListItem(std::string rFileName)
    : m_FileName(rFileName)
	, m_emu_state(0)
	, m_FileSize(0)
	, m_Revision(0)
	, m_Valid(false)
	, m_BlobCompressed(false)
	, m_ImageWidth(0)
	, m_ImageHeight(0)
{	
	bool hasBanner = false;

	/* DON'T RUN THIS CODE. see note at SaveToCache below.
	if (LoadFromCache())
	{
		m_Valid = true;
	}
	else */
	{
		DiscIO::IVolume* pVolume = DiscIO::CreateVolumeFromFilename(rFileName);

		if (pVolume != NULL)
		{
			if (!DiscIO::IsVolumeWadFile(pVolume))
				m_Platform = DiscIO::IsVolumeWiiDisc(pVolume) ? WII_DISC : GAMECUBE_DISC;
			else
			{
				m_Platform = WII_WAD;
			}

			m_volume_names = pVolume->GetNames();

			m_Country  = pVolume->GetCountry();
			m_FileSize = pVolume->GetRawSize();
			m_VolumeSize = pVolume->GetSize();

			m_UniqueID = pVolume->GetUniqueID();
			m_BlobCompressed = DiscIO::IsCompressedBlob(rFileName.c_str());
			m_IsDiscTwo = pVolume->IsDiscTwo();
			m_Revision = pVolume->GetRevision();

			// check if we can get some info from the banner file too
			DiscIO::IFileSystem* pFileSystem = DiscIO::CreateFileSystem(pVolume);

			if (pFileSystem != NULL || m_Platform == WII_WAD)
			{
				DiscIO::IBannerLoader* pBannerLoader = DiscIO::CreateBannerLoader(*pFileSystem, pVolume);

				if (pBannerLoader != NULL)
				{
					if (pBannerLoader->IsValid())
					{
						if (m_Platform != WII_WAD)
							m_names = pBannerLoader->GetNames();
						m_company = pBannerLoader->GetCompany();
						m_descriptions = pBannerLoader->GetDescriptions();

						std::vector<u32> Buffer = pBannerLoader->GetBanner(&m_ImageWidth, &m_ImageHeight);
						m_Banner = QImage(m_ImageWidth, m_ImageHeight, QImage::Format_RGB888);
						for (int i = 0; i < m_ImageWidth * m_ImageHeight; i++)
						{
							int x = i % m_ImageWidth, y = i / m_ImageWidth;
							m_Banner.setPixel(x, y, qRgb((Buffer[i] & 0xFF0000) >> 16,
												(Buffer[i] & 0x00FF00) >>  8,
												(Buffer[i] & 0x0000FF) >>  0));
						}
						m_Banner = m_Banner.scaled(DVD_BANNER_WIDTHok, DVD_BANNER_HEIGHT);
						hasBanner = !m_Banner.isNull();
					}
					delete pBannerLoader;
				}

				delete pFileSystem;
			}

			delete pVolume;

			m_Valid = true;

			// Create a cache file only if we have an image.
			// Wii isos create their images after you have generated the first savegame
			/*if (hasBanner)
				SaveToCache(); // TODO: QImage files aren't save to the cache file, so this breaks banners if run. */
		}
	}

	if (IsValid())
	{
		IniFile ini;
		ini.Load(File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP + m_UniqueID + ".ini");
		ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + m_UniqueID + ".ini", true);
		ini.Get("EmuState", "EmulationStateId", &m_emu_state);
		ini.Get("EmuState", "EmulationIssues", &m_issues);
	}

	if(!hasBanner)
	{
		// default banner
		m_Banner = Resources::GetPixmap(Resources::BANNER_MISSING).toImage();
	}
}

GameListItem::~GameListItem()
{
}

bool GameListItem::LoadFromCache()
{
	return CChunkFileReader::Load<GameListItem>(CreateCacheFilename(), CACHE_REVISION, *this);
}

void GameListItem::SaveToCache()
{
	if (!File::IsDirectory(File::GetUserPath(D_CACHE_IDX)))
	{
		File::CreateDir(File::GetUserPath(D_CACHE_IDX));
	}

	CChunkFileReader::Save<GameListItem>(CreateCacheFilename(), CACHE_REVISION, *this);
}

void GameListItem::DoState(PointerWrap &p)
{
	p.Do(m_volume_names);
	p.Do(m_company);
	p.Do(m_names);
	p.Do(m_descriptions);
	p.Do(m_UniqueID);
	p.Do(m_FileSize);
	p.Do(m_VolumeSize);
	p.Do(m_Country);
	p.Do(m_BlobCompressed);
	p.Do(m_ImageWidth);
	p.Do(m_ImageHeight);
	p.Do(m_Platform);
	p.Do(m_IsDiscTwo);
	p.Do(m_Revision);
}

std::string GameListItem::CreateCacheFilename()
{
	std::string Filename, LegalPathname, extension;
	SplitPath(m_FileName, &LegalPathname, &Filename, &extension);

	if (Filename.empty()) return Filename; // Disc Drive

	// Filename.extension_HashOfFolderPath_Size.cache
	// Append hash to prevent ISO name-clashing in different folders.
	Filename.append(StringFromFormat("%s_%x_%llx.cache",
		extension.c_str(), HashFletcher((const u8 *)LegalPathname.c_str(), LegalPathname.size()),
		File::GetSize(m_FileName)));

	std::string fullname(File::GetUserPath(D_CACHE_IDX));
	fullname += Filename;
	return fullname;
}

std::string GameListItem::GetCompany() const
{
	if (m_company.empty())
		return "N/A";
	else
		return m_company;
}

// (-1 = Japanese, 0 = English, etc)?
std::string GameListItem::GetDescription(int _index) const
{
	const u32 index = _index;

	if (index < m_descriptions.size())
		return m_descriptions[index];
	
	if (!m_descriptions.empty())
		return m_descriptions[0];

	return "";
}

// (-1 = Japanese, 0 = English, etc)?
std::string GameListItem::GetVolumeName(int _index) const
{
	u32 const index = _index;

	if (index < m_volume_names.size() && !m_volume_names[index].empty())
		return m_volume_names[index];

	if (!m_volume_names.empty())
		return m_volume_names[0];
	
	return "";
}

// (-1 = Japanese, 0 = English, etc)?
std::string GameListItem::GetBannerName(int _index) const
{
	u32 const index = _index;

	if (index < m_names.size() && !m_names[index].empty())
		return m_names[index];
	
	if (!m_names.empty())
		return m_names[0];

	return "";
}

// (-1 = Japanese, 0 = English, etc)?
std::string GameListItem::GetName(int _index) const
{
	// Prefer name from banner, fallback to name from volume, fallback to filename
	
	std::string name = GetBannerName(_index);
	
	if (name.empty())
		name = GetVolumeName(_index);

	if (name.empty())
	{
		// No usable name, return filename (better than nothing)
		SplitPath(GetFileName(), NULL, &name, NULL);
	}

	return name;
}

const QString GameListItem::GetWiiFSPath() const
{
    DiscIO::IVolume *Iso = DiscIO::CreateVolumeFromFilename(m_FileName);
	QString ret;

	if (Iso == NULL)
		return ret;

	if (DiscIO::IsVolumeWiiDisc(Iso) || DiscIO::IsVolumeWadFile(Iso))
	{
		char Path[250];
		u64 Title;

		Iso->GetTitleID((u8*)&Title);
		Title = Common::swap64(Title);

		sprintf(Path, "%stitle/%08x/%08x/data/",
				File::GetUserPath(D_WIIUSER_IDX).c_str(), (u32)(Title>>32), (u32)Title);

		if (!File::Exists(Path))
			File::CreateFullPath(Path);

		if (Path[0] == '.')
			ret = QString(QDir::currentPath() + QString(Path).mid(strlen(ROOT_DIR)));
		else
			ret = QString::fromStdString(Path);
	}
	delete Iso;

	return ret;
}

