// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


#ifndef _INIFILE_H_
#define _INIFILE_H_

#include <map>
#include <string>
#include <set>
#include <vector>
#include <map>

#include "StringUtil.h"

struct CaseInsensitiveStringCompare
{
	bool operator() (const std::string& a, const std::string& b) const
	{
		return strcasecmp(a.c_str(), b.c_str()) < 0;
	}
};

class IniFile
{
public:
	enum Mode
	{
		MODE_READ,
		MODE_WRITE,
		MODE_PATCH // read, but don't apply defaults
	};
	class Section
	{
		friend class IniFile;

	public:
		Section() : parsed(false) {}
		Section(const std::string& _name) : name(_name), parsed(false) {}

		bool Exists(const char *key) const;
		bool Delete(const char *key);

		void Set(const char* key, const char* newValue);

		void Set(const std::string &key, const std::string &value) {
			Set(key.c_str(), value.c_str());
		}
		void Set(const char* key, u32 newValue) {
			Set(key, StringFromFormat("0x%08x", newValue).c_str());
		}
		void Set(const char* key, float newValue) {
			Set(key, StringFromFormat("%f", newValue).c_str());
		}
		void Set(const char* key, double newValue) {
			Set(key, StringFromFormat("%f", newValue).c_str());
		}
		void Set(const char* key, int newValue) {
			Set(key, StringFromInt(newValue).c_str());
		}
		void Set(const char* key, bool newValue) {
			Set(key, StringFromBool(newValue).c_str());
		}
		void Set(const char* key, const std::vector<std::string>& newValues);

		template <typename T, typename U>
		void Set(const char* key, T newValue, U defaultValue)
		{
			if (newValue == defaultValue)
				Delete(key);
			else
				Set(key, newValue);
		}

		template <typename T, typename U>
		bool Get(const char* key, T* value, U defaultValue) const
		{
			std::string temp;
			if (const_cast<Section*>(this)->GetLine(key, &temp) &&
			    TryParse(temp, value))
			{
				return true;
			}
			*value = defaultValue;
			return false;
		}
		bool Get(const char* key, std::vector<std::string>& values) const;

		std::vector<std::string> GetLines(const bool remove_comments = true) const;
		void SetLines(std::vector<std::string> lines);

		template <typename T>
		void Do(Mode mode, const char* key, T* value, T defaultValue = 0)
		{
			switch (mode)
			{
			case MODE_WRITE:
				Set(key, *value, defaultValue);
				break;
			case MODE_PATCH:
				T temp;
				if (Get(key, &temp))
					*value = temp;
				break;
			case MODE_READ:
				Get(key, value, defaultValue);
				break;
			}
		}

		bool operator < (const Section& other) const {
			return name < other.name;
		}

		std::string name;
		std::vector<std::string> lines;
		std::map<std::string /* key */, std::pair<
			size_t, // offset
			std::string // value
		>, CaseInsensitiveStringCompare> keys;
	private:
		std::string* GetLine(const char* key, std::string* valueOut);
		bool parsed;
	};

	/**
	 * Loads sections and keys.
	 * @param filename filename of the ini file which should be loaded
	 * @param keep_current_data If true, "extends" the currently loaded list of sections and keys with the loaded data (and replaces existing entries). If false, existing data will be erased.
	 * @warning Using any other operations than "Get*" and "Exists" is untested and will behave unexpectedly
	 * @todo This really is just a hack to support having two levels of gameinis (defaults and user-specified) and should eventually be replaced with a less stupid system.
	 */
	bool Load(const char* filename, bool keep_current_data = false);
	bool Load(const std::string &filename, bool keep_current_data = false) { return Load(filename.c_str(), keep_current_data); }

	bool Save(const char* filename);
	bool Save(const std::string &filename) { return Save(filename.c_str()); }

	// Returns true if key exists in section
	bool Exists(const char* sectionName, const char* key) const;

	template <typename T>
	bool Get(const char* section, const char* key, T* value) const
	{
		return Get(section, key, value, T());
	}

	template <typename T, typename U>
	bool Get(const char* section, const char* key, T* value, U defaultValue) const
	{
		const Section* sect = GetSection(section);
		if (sect)
		{
			return sect->Get(key, value, defaultValue);
		}
		else
		{
			*value = defaultValue;
			return false;
		}
	}

	// temporary
	template <typename T>
	void Set(const char* section, const char* key, T value)
	{
		return GetOrCreateSection(section)->Set(key, value);
	}

	bool DeleteKey(const char* sectionName, const char* key);
	bool DeleteSection(const char* sectionName);

	void SortSections();

	Section* GetOrCreateSection(const char* section);
	const Section* GetSection(const char* section) const;
	Section* GetSection(const char* section);

private:
	std::vector<Section> sections;

	void CreateSection(const char* section);
};

#endif // _INIFILE_H_
