// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "ARCodeAddEdit.h"
#include "ARDecrypt.h"
#include "WxUtils.h"

using namespace ActionReplay;

BEGIN_EVENT_TABLE(CARCodeAddEdit, wxDialog)
	EVT_BUTTON(wxID_OK, CARCodeAddEdit::SaveCheatData)
	EVT_SPIN(ID_ENTRY_SELECT, CARCodeAddEdit::ChangeEntry)
END_EVENT_TABLE()

CARCodeAddEdit::CARCodeAddEdit(int _selection, wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& position, const wxSize& size, long style)
	: wxDialog(parent, id, title, position, size, style)
	, selection(_selection)
{
	ActionReplay::ARCode tempEntries;
	wxString currentName = _("Insert name here..");

	if (selection == wxNOT_FOUND)
	{
		tempEntries.name = "";
	}
	else
	{
		currentName = StrToWxStr(arCodes.at(selection).name);
		tempEntries = arCodes.at(selection);
	}

	wxBoxSizer* sEditCheat = new wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* sbEntry = new wxStaticBoxSizer(wxVERTICAL, this, _("Cheat Code"));
	wxGridBagSizer* sgEntry = new wxGridBagSizer(0, 0);

	wxStaticText* EditCheatNameText = new wxStaticText(this, ID_EDITCHEAT_NAME_TEXT, _("Name:"), wxDefaultPosition, wxDefaultSize);
	EditCheatName = new wxTextCtrl(this, ID_EDITCHEAT_NAME, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	EditCheatName->SetValue(currentName);
	EntrySelection = new wxSpinButton(this, ID_ENTRY_SELECT, wxDefaultPosition, wxDefaultSize, wxVERTICAL);
	EntrySelection->SetRange(1, ((int)arCodes.size()) > 0 ? (int)arCodes.size() : 1); 
	EntrySelection->SetValue((int)(arCodes.size() - selection));
	EditCheatCode = new wxTextCtrl(this, ID_EDITCHEAT_CODE, wxEmptyString, wxDefaultPosition, wxSize(300, 100), wxTE_MULTILINE);
	UpdateTextCtrl(tempEntries);

	sgEntry->Add(EditCheatNameText, wxGBPosition(0, 0), wxGBSpan(1, 1), wxALIGN_CENTER|wxALL, 5);
	sgEntry->Add(EditCheatName,		wxGBPosition(0, 1), wxGBSpan(1, 1), wxEXPAND|wxALL, 5);
	sgEntry->Add(EntrySelection,	wxGBPosition(0, 2), wxGBSpan(2, 1), wxEXPAND|wxALL, 5);
	sgEntry->Add(EditCheatCode,		wxGBPosition(1, 0), wxGBSpan(1, 2), wxEXPAND|wxALL, 5);
	sgEntry->AddGrowableCol(1);
	sgEntry->AddGrowableRow(1);
	sbEntry->Add(sgEntry, 1, wxEXPAND|wxALL);

	sEditCheat->Add(sbEntry, 1, wxEXPAND|wxALL, 5);
	sEditCheat->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 5);

	SetSizerAndFit(sEditCheat);
	SetFocus();
}

void CARCodeAddEdit::ChangeEntry(wxSpinEvent& event)
{
	ActionReplay::ARCode currentCode = arCodes.at((int)arCodes.size() - event.GetPosition());
	EditCheatName->SetValue(StrToWxStr(currentCode.name));
	UpdateTextCtrl(currentCode);
}

void CARCodeAddEdit::SaveCheatData(wxCommandEvent& WXUNUSED (event))
{
	std::vector<ActionReplay::AREntry> decryptedLines;
	std::vector<std::string> encryptedLines;

	// Split the entered cheat into lines.
	std::vector<std::string> userInputLines;
	SplitString(WxStrToStr(EditCheatCode->GetValue()), '\n', userInputLines);

	for (size_t i = 0;  i < userInputLines.size();  i++)
	{
		// Make sure to ignore unneeded whitespace characters.
		std::string line_str = StripSpaces(userInputLines[i]);

		if (line_str == "")
			continue;

		// Let's parse the current line.  Is it in encrypted or decrypted form?
		std::vector<std::string> pieces;
		SplitString(line_str, ' ', pieces);

		if (pieces.size() == 2 && pieces[0].size() == 8 && pieces[1].size() == 8)
		{
			// Decrypted code line.
			u32 addr = strtoul(pieces[0].c_str(), NULL, 16);
			u32 value = strtoul(pieces[1].c_str(), NULL, 16);

			decryptedLines.push_back(ActionReplay::AREntry(addr, value));
			continue;
		}
		else if (pieces.size() == 1)
		{
			SplitString(line_str, '-', pieces);

			if (pieces.size() == 3 && pieces[0].size() == 4 && pieces[1].size() == 4 && pieces[2].size() == 5)
			{
				// Encrypted code line.  We'll have to decode it later.
				encryptedLines.push_back(pieces[0] + pieces[1] + pieces[2]);
				continue;
			}
		}

		// If the above-mentioned conditions weren't met, then something went wrong.
		if (!PanicYesNoT("Unable to parse line %lu of the entered AR code as a valid "
						"encrypted or decrypted code.  Make sure you typed it correctly.\n"
						"Would you like to ignore this line and continue parsing?",  i + 1))
		{
			return;
		}
	}

	// If the entered code was in encrypted form, we decode it here.
	if (encryptedLines.size())
	{
		// TODO: what if both decrypted AND encrypted lines are entered into a single AR code?
		ActionReplay::DecryptARCode(encryptedLines, decryptedLines);
	}

	// Codes with no lines appear to be deleted/hidden from the list.  Let's prevent that.
	if (!decryptedLines.size())
	{
		PanicAlertT("The resulting decrypted AR code doesn't contain any lines.");
		return;
	}


	if (selection == wxNOT_FOUND)
	{
		// Add a new AR cheat code.
		ActionReplay::ARCode newCheat;

		newCheat.name = WxStrToStr(EditCheatName->GetValue());
		newCheat.ops = decryptedLines;
		newCheat.active = true;

		arCodes.push_back(newCheat);
	}
	else
	{
		// Update the currently-selected AR cheat code.
		arCodes.at(selection).name = WxStrToStr(EditCheatName->GetValue());
		arCodes.at(selection).ops = decryptedLines;
	}

	AcceptAndClose();
}

void CARCodeAddEdit::UpdateTextCtrl(ActionReplay::ARCode arCode)
{
	EditCheatCode->Clear();

	if (arCode.name != "")
	{
		for (u32 i = 0; i < arCode.ops.size(); i++)
			EditCheatCode->AppendText(wxString::Format(wxT("%08X %08X\n"), arCode.ops.at(i).cmd_addr, arCode.ops.at(i).value));
	}
	else
	{
		EditCheatCode->SetValue(_("Insert Encrypted or Decrypted code here..."));
	}
}
