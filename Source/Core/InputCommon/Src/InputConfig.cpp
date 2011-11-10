// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "CommonPaths.h"
#include "InputConfig.h"
#include "../../Core/Src/ConfigManager.h"
#include "../../Core/Src/HW/Wiimote.h"

InputPlugin::~InputPlugin()
{
	// delete pads
	std::vector<ControllerEmu*>::const_iterator i = controllers.begin(),
		e = controllers.end();
	for ( ; i != e; ++i )
		delete *i;
}

bool InputPlugin::LoadConfig(bool def)
{
	IniFile inifile
		, isoinifile
		, profileinifile;
	std::string ini
		, isoini = ""
		, profileini
		, profile
		, path;
	bool useProfile = false;

	ini = File::GetUserPath(D_CONFIG_IDX) + ini_name + ".ini";
	isoini = SConfig::GetInstance().m_LocalCoreStartupParameter.m_strGameIni;

	std::vector< ControllerEmu* >::const_iterator
		i = controllers.begin(),
		e = controllers.end();
	for (int n = 0; i!=e; ++i, ++n)
	{
		if (!def
			&& isoinifile.Load(isoini)
			&& isoinifile.Exists("Controls", ((*i)->GetName() + "Profile").c_str()))
		{
			isoinifile.Get("Controls", ((*i)->GetName() + "Profile").c_str(), &profile);
			profileini = File::GetUserPath(D_PROFILE_IDX) + profile_name + DIR_SEP + profile + ".ini";
			if (File::Exists(profileini))
				useProfile = true;
			else
				NOTICE_LOG(CONSOLE, "Selected controller profile \"%s\" doesn't exist", profileini.c_str());
		}

		if (useProfile)
		{
			profileinifile.Load(profileini);
			(*i)->LoadConfig(profileinifile.GetOrCreateSection("Profile"));
			useProfile = false;
		}

		// load from ISO ini
		else if (!def
			&& isoinifile.Load(isoini)
			&& SConfig::GetInstance().m_LocalCoreStartupParameter.bInputSettingsISO)
		{
			// copy from default on first use
			if (!isoinifile.GetSection((*i)->GetName().c_str()))
			{
				isoinifile.GetOrCreateSection((*i)->GetName().c_str());
				if (inifile.Load(ini))
					isoinifile.GetSection((*i)->GetName().c_str())->Copy(inifile.GetOrCreateSection((*i)->GetName().c_str()));
				isoinifile.Save(isoini);
			}
			(*i)->LoadConfig(isoinifile.GetOrCreateSection((*i)->GetName().c_str()));
		}

		// load user default
		else if (inifile.Load(ini))
		{
			(*i)->LoadConfig(inifile.GetOrCreateSection((*i)->GetName().c_str()));
		}

		// load default
		else
		{
			(*i)->LoadDefaults(g_controller_interface);
			(*i)->UpdateReferences(g_controller_interface);
		}

		// update refs
		(*i)->UpdateReferences(g_controller_interface);
	}

	return true;
}

void InputPlugin::SaveConfig(bool def)
{
	std::string ini_filename;

	if (!def
		&& SConfig::GetInstance().m_LocalCoreStartupParameter.bInputSettingsISO
		&& !SConfig::GetInstance().m_LocalCoreStartupParameter.m_strGameIni.empty())
		ini_filename = SConfig::GetInstance().m_LocalCoreStartupParameter.m_strGameIni;
	else
		ini_filename = File::GetUserPath(D_CONFIG_IDX) + ini_name + ".ini";

	IniFile inifile;
	inifile.Load(ini_filename);

	std::vector< ControllerEmu* >::const_iterator i = controllers.begin(),
		e = controllers.end();
	for ( ; i!=e; ++i )
		(*i)->SaveConfig(inifile.GetOrCreateSection((*i)->GetName().c_str()));
	
	inifile.Save(ini_filename);
}
