// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


// see IniFile.h

#include <stdlib.h>
#include <stdio.h>

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>

#include "FileUtil.h"
#include "StringUtil.h"
#include "IniFile.h"

namespace {

void ParseLine(const std::string& line, std::string* keyOut, std::string* valueOut)
{
	if (line[0] == '#')
		return;

	int FirstEquals = (int)line.find("=", 0);

	if (FirstEquals >= 0)
	{
		// Yes, a valid line!
		*keyOut = StripSpaces(line.substr(0, FirstEquals));
		if (valueOut) *valueOut = StripQuotes(StripSpaces(line.substr(FirstEquals + 1, std::string::npos)));
	}
}

}

std::string* IniFile::Section::GetLine(const char* key, std::string* valueOut)
{
	if (!parsed)
	{
		parsed = true;
		for (auto iter = lines.begin(); iter != lines.end(); ++iter)
		{
			const std::string& line = *iter;
			std::string _key, value;
			bool raw = line.size() >= 1 && (line[0] == '$' || line[0] == '+' || line[0] == '*');
			if (!raw)
			{
				ParseLine(line, &_key, &value);
				if (_key.size())
				{
					keys[_key] = std::make_pair(iter - lines.begin(), value);
				}
			}
		}
	}
	auto it = keys.find(key);
	if (it != keys.end())
	{
		if (valueOut)
			*valueOut = it->second.second;
		return &lines[it->second.first];
	}

	return 0;
}

void IniFile::Section::Set(const char* key, const char* newValue)
{
	std::string value;
	std::string* line = GetLine(key, &value);
	if (line)
	{
		// Change the value - keep the key and comment
		*line = StripSpaces(key) + " = " + newValue;
		keys[key].second = newValue;
	}
	else
	{
		// The key did not already exist in this section - let's add it.
		keys[key] = std::make_pair(lines.size(), newValue);
		lines.push_back(std::string(key) + " = " + newValue);
	}
}

void IniFile::Section::Set(const char* key, const std::vector<std::string>& newValues)
{
	std::string temp;
	// Join the strings with ,
	std::vector<std::string>::const_iterator it;
	for (it = newValues.begin(); it != newValues.end(); ++it)
	{
		temp = (*it) + ",";
	}
	// remove last ,
	temp.resize(temp.length() - 1);
	Set(key, temp.c_str());
}

bool IniFile::Section::Get(const char* key, std::vector<std::string>& out) const
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (!retval || temp.empty())
	{
		return false;
	}
	// ignore starting , if any
	size_t subStart = temp.find_first_not_of(",");
	size_t subEnd;

	// split by ,
	while (subStart != std::string::npos)
	{
		// Find next ,
		subEnd = temp.find_first_of(",", subStart);
		if (subStart != subEnd)
			// take from first char until next ,
			out.push_back(StripSpaces(temp.substr(subStart, subEnd - subStart)));
		// Find the next non , char
		subStart = temp.find_first_not_of(",", subEnd);
	}
	return true;
}

bool IniFile::Section::Exists(const char *key) const
{
	return keys.find(key) != keys.end();
}

bool IniFile::Section::Delete(const char *key)
{
	std::string* line = GetLine(key, 0);
	if (line)
	{
		*line = "[";
		keys.erase(key);
	}
	return false;
}

// Return a list of all lines in a section
std::vector<std::string> IniFile::Section::GetLines(const bool remove_comments) const
{
	std::vector<std::string> stripped;

	for (std::vector<std::string>::const_iterator iter = lines.begin(); iter != lines.end(); ++iter)
	{
		std::string line = StripSpaces(*iter);

		if (remove_comments)
		{
			int commentPos = (int)line.find('#');
			if (commentPos == 0)
			{
				continue;
			}

			if (commentPos != (int)std::string::npos)
			{
				line = StripSpaces(line.substr(0, commentPos));
			}
		}

		stripped.push_back(line);
	}

	return std::move(stripped);
}

void IniFile::Section::SetLines(std::vector<std::string> _lines)
{
	lines = _lines;
	keys.clear();
	parsed = false;
}

// IniFile

const IniFile::Section* IniFile::GetSection(const char* sectionName) const
{
	for (const auto& sect : sections)
		if (!strcasecmp(sect.name.c_str(), sectionName))
			return (&(sect));
	return 0;
}

IniFile::Section* IniFile::GetSection(const char* sectionName)
{
	for (auto& sect : sections)
		if (!strcasecmp(sect.name.c_str(), sectionName))
			return (&(sect));
	return 0;
}

IniFile::Section* IniFile::GetOrCreateSection(const char* sectionName)
{
	Section* section = GetSection(sectionName);
	if (!section)
	{
		sections.push_back(Section(sectionName));
		section = &sections[sections.size() - 1];
	}
	return section;
}

bool IniFile::DeleteSection(const char* sectionName)
{
	Section* s = GetSection(sectionName);
	if (!s)
		return false;
	for (std::vector<Section>::iterator iter = sections.begin(); iter != sections.end(); ++iter)
	{
		if (&(*iter) == s)
		{
			sections.erase(iter);
			return true;
		}
	}
	return false;
}

bool IniFile::Exists(const char* sectionName, const char* key) const
{
	const Section* section = GetSection(sectionName);
	if (!section)
		return false;
	return section->Exists(key);
}

bool IniFile::DeleteKey(const char* sectionName, const char* key)
{
	Section* section = GetSection(sectionName);
	if (!section)
		return false;
	return section->Delete(key);
}

void IniFile::SortSections()
{
	std::sort(sections.begin(), sections.end());
}

bool IniFile::Load(const char* filename, bool keep_current_data)
{
	// Maximum number of letters in a line
	static const int MAX_BYTES = 1024*32;

	if (!keep_current_data)
		sections.clear();
	// first section consists of the comments before the first real section

	// Open file
	std::ifstream in;
	OpenFStream(in, filename, std::ios::in);

	if (in.fail()) return false;

	Section* current_section = NULL;
	while (!in.eof())
	{
		char templine[MAX_BYTES];
		in.getline(templine, MAX_BYTES);
		std::string line = templine;

#ifndef _WIN32
		// Check for CRLF eol and convert it to LF
		if (!line.empty() && line.at(line.size()-1) == '\r')
		{
			line.erase(line.size()-1);
		}
#endif

		if (line.size() > 0)
		{
			if (line[0] == '[')
			{
				size_t endpos = line.find("]");

				if (endpos != std::string::npos)
				{
					// New section!
					std::string sub = line.substr(1, endpos - 1);
					current_section = GetOrCreateSection(sub.c_str());
				}
			}
			else if (current_section)
			{
				current_section->lines.push_back(line);
			}
		}
	}

	in.close();
	return true;
}

bool IniFile::Save(const char* filename)
{
	std::ofstream out;
	std::string temp = File::GetTempFilenameForAtomicWrite(filename);
	OpenFStream(out, temp, std::ios::out);

	if (out.fail())
	{
		return false;
	}

	for (auto& section : sections)
	{
		if (section.keys.size() != 0 || section.lines.size() != 0)
			out << "[" << section.name << "]" << std::endl;

		for (auto s : section.lines)
		{
			if (s != "[") // deleted
				out << s << std::endl;
		}
	}

	out.close();

	return File::RenameSync(temp, filename);
}

// Unit test. TODO: Move to the real unit test framework.
/*
   int main()
   {
    IniFile ini;
    ini.Load("my.ini");
    ini.Set("Hej", "A", "amaskdfl");
    ini.Set("Mossa", "A", "amaskdfl");
    ini.Set("Aissa", "A", "amaskdfl");
    //ini.Read("my.ini");
    std::string x;
    ini.Get("Hej", "B", &x, "boo");
    ini.DeleteKey("Mossa", "A");
    ini.DeleteSection("Mossa");
    ini.SortSections();
    ini.Save("my.ini");
    //UpdateVars(ini);
    return 0;
   }
 */
