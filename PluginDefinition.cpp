//This file is part of DoxyIt.
//
//Copyright (C)2013 Justin Dailey <dail8859@yahoo.com>
//
//DoxyIt is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <fstream>
#include "PluginDefinition.h"
#include "menuCmdID.h"
#include "trex.h"
#include "Utils.h"
#include "Parsers.h"

#include "SettingsDialog.h"

// Global variables
FuncItem funcItem[nbFunc];
NppData nppData;
HANDLE _hModule;			// For dialog initialization
bool do_active_commenting;	// active commenting - when pressing enter in a document block, create a new doc line
//bool do_active_wrapping;	// active wrapping - wrap text inside of document blocks...todo
bool use_fingertext;		// use fingertext if it is available
bool fingertext_found;		// if we found the fingertext plugin installed
bool fingertext_enabled;	// if fingertext is enabled

// Local variables
SciFnDirect pSciMsg;		// For direct scintilla call
sptr_t pSciWndData;			// For direct scintilla call
SettingsDialog sd;


void commandMenuInit();
void commandMenuCleanUp();

// --- Menu callbacks ---
void doxyItFunction();
void doxyItFile();
void activeCommenting();
void useFingerText();
//void activeWrapping();
void showSettings();


LRESULT SendScintilla(UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return pSciMsg(pSciWndData, Msg, wParam, lParam);
}

bool updateScintilla()
{
	HWND curScintilla;

	// Get the current scintilla
	int which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if(which == -1) return false;
	curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

	// Get the function and pointer to it for more effecient calls
	pSciMsg = (SciFnDirect) SendMessage(curScintilla,SCI_GETDIRECTFUNCTION, 0, 0);
	pSciWndData = (sptr_t) SendMessage(curScintilla,SCI_GETDIRECTPOINTER, 0, 0);

	return true;
}


// --- Configuration ---

void configSave()
{
	TCHAR iniPath[MAX_PATH];
	int len = sizeof(parsers) / sizeof(parsers[0]);

	::SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM) iniPath);
	::_tcscat_s(iniPath, TEXT("\\"));
	::_tcscat_s(iniPath, NPP_PLUGIN_NAME);
	::_tcscat_s(iniPath, TEXT(".ini"));

	// [DoxyIt]
	::WritePrivateProfileString(NPP_PLUGIN_NAME, TEXT("active_commenting"), do_active_commenting ? TEXT("true") : TEXT("false"), iniPath);
	::WritePrivateProfileString(NPP_PLUGIN_NAME, TEXT("use_fingertext"), use_fingertext ? TEXT("true") : TEXT("false"), iniPath);

	for(int i = 0; i < len; ++i)
	{
		const Parser *p = &parsers[i];
		const ParserDefinition *pd = &p->pd;
		std::wstring ws;

		// Wrap everything in quotes to perserve whitespace
		ws.assign(pd->doc_start.begin(), pd->doc_start.end());
		ws = TEXT("\"") + ws + TEXT("\"");
		::WritePrivateProfileString(p->lang.c_str(), TEXT("doc_start"), ws.c_str(), iniPath);

		ws.assign(pd->doc_line.begin(), pd->doc_line.end());
		ws = TEXT("\"") + ws + TEXT("\"");
		::WritePrivateProfileString(p->lang.c_str(), TEXT("doc_line_"), ws.c_str(), iniPath);

		ws.assign(pd->doc_end.begin(), pd->doc_end.end());
		ws = TEXT("\"") + ws + TEXT("\"");
		::WritePrivateProfileString(p->lang.c_str(), TEXT("doc_end__"), ws.c_str(), iniPath);

		ws.assign(pd->command_prefix.begin(), pd->command_prefix.end());
		ws = TEXT("\"") + ws + TEXT("\"");
		::WritePrivateProfileString(p->lang.c_str(), TEXT("command_prefix"), ws.c_str(), iniPath);
	}
}

void configLoad()
{
	TCHAR iniPath[MAX_PATH];
	int len = sizeof(parsers) / sizeof(parsers[0]);
	TCHAR tbuffer[MAX_PATH];
	char buffer[MAX_PATH];

	::SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM) iniPath);
	::_tcscat_s(iniPath, TEXT("\\"));
	::_tcscat_s(iniPath, NPP_PLUGIN_NAME);
	::_tcscat_s(iniPath, TEXT(".ini"));

	// [DoxyIt]
	GetPrivateProfileString(NPP_PLUGIN_NAME, TEXT("active_commenting"), TEXT("true"), tbuffer, MAX_PATH, iniPath);
	wcstombs(buffer, tbuffer, MAX_PATH);
	do_active_commenting = strcmp(buffer, "true") == 0;
	GetPrivateProfileString(NPP_PLUGIN_NAME, TEXT("use_fingertext"), TEXT("true"), tbuffer, MAX_PATH, iniPath);
	wcstombs(buffer, tbuffer, MAX_PATH);
	use_fingertext = strcmp(buffer, "true") == 0;

	for(int i = 0; i < len; ++i)
	{
		Parser *p = &parsers[i];

		// NOTE: We cant use the default value because GetPrivateProfileString strips the whitespace,
		// also, wrapping it in quotes doesn't seem to work either. So...use "!!!" as the default text
		// and if we find that the value wasnt found and we have "!!!" then use the default value in the
		// parser, else, use what we pulled from the file.
		GetPrivateProfileString(p->lang.c_str(), TEXT("doc_start"), TEXT("!!!"), tbuffer, MAX_PATH, iniPath);
		wcstombs(buffer, tbuffer, MAX_PATH);
		if(strncmp(buffer, "!!!", 3) == 0) p->pd.doc_start.assign(p->default_doc_start.begin(), p->default_doc_start.end());
		else p->pd.doc_start.assign(buffer);

		GetPrivateProfileString(p->lang.c_str(), TEXT("doc_line_"), TEXT("!!!"), tbuffer, MAX_PATH, iniPath);
		wcstombs(buffer, tbuffer, MAX_PATH);
		if(strncmp(buffer, "!!!", 3) == 0) p->pd.doc_line.assign(p->default_doc_line.begin(), p->default_doc_line.end());
		else p->pd.doc_line.assign(buffer);

		GetPrivateProfileString(p->lang.c_str(), TEXT("doc_end__"), TEXT("!!!"), tbuffer, MAX_PATH, iniPath);
		wcstombs(buffer, tbuffer, MAX_PATH);
		if(strncmp(buffer, "!!!", 3) == 0) p->pd.doc_end.assign(p->default_doc_end.begin(), p->default_doc_end.end());
		else p->pd.doc_end.assign(buffer);

		GetPrivateProfileString(p->lang.c_str(), TEXT("command_prefix"), TEXT("!!!"), tbuffer, MAX_PATH, iniPath);
		wcstombs(buffer, tbuffer, MAX_PATH);
		if(strncmp(buffer, "!!!", 3) == 0) p->pd.command_prefix.assign(p->default_command_prefix.begin(), p->default_command_prefix.end());
		else p->pd.command_prefix.assign(buffer);
	}

	// Write out the file if it doesnt exist yet
	if(!PathFileExists(iniPath)) configSave();
}



void pluginInit(HANDLE hModule)
{
	InitializeParsers();

	_hModule = hModule;
}

void pluginCleanUp()
{
	commandMenuCleanUp();
	CleanUpParsers();
}



bool setCommand(size_t index, TCHAR *cmdName, PFUNCPLUGINCMD pFunc, ShortcutKey *sk = NULL, bool check0nInit = false)
{
	if (index >= nbFunc || !pFunc) return false;

	lstrcpy(funcItem[index]._itemName, cmdName);
	funcItem[index]._pFunc = pFunc;
	funcItem[index]._init2Check = check0nInit;
	funcItem[index]._pShKey = sk;

	return true;
}

void commandMenuInit()
{
	ShortcutKey *sk = new ShortcutKey();
	sk->_isAlt = TRUE;
	sk->_isCtrl = TRUE;
	sk->_isShift = TRUE;
	sk->_key = 'D';

	setCommand(0, TEXT("DoxyIt - Function"), doxyItFunction, sk, false);
	//setCommand(1, TEXT("DoxyIt - File"), doxyItFile, NULL, false);
	setCommand(1, TEXT("Active commenting"), activeCommenting, NULL, do_active_commenting);
	setCommand(2, TEXT("Use FingerText (if available)"), useFingerText, NULL, use_fingertext);
	setCommand(3, TEXT(""), NULL);
	setCommand(4, TEXT("Settings..."), showSettings);
	//setCommand(3, TEXT("Active word wrapping"), activeWrapping, NULL, do_active_wrapping);
}

void commandMenuCleanUp()
{
	// Don't forget to deallocate your shortcut here
	delete funcItem[0]._pShKey;
}


void setNppInfo(NppData notepadPlusData)
{
	nppData = notepadPlusData;
	configLoad();
	commandMenuInit();

	// Dialog Init
	sd.init((HINSTANCE) _hModule, nppData);
}

bool checkFingerText()
{
	if(fingertext_found && use_fingertext)
	{
		CommunicationInfo ci;
		ci.internalMsg = FINGERTEXT_ISENABLED;
		ci.srcModuleName = NPP_PLUGIN_NAME;
		ci.info = NULL;
		::SendMessage(nppData._nppHandle, NPPM_MSGTOPLUGIN, (WPARAM) TEXT("FingerText.dll"), (LPARAM) &ci);
		return ci.info != NULL;
	}
	else
		return false;
}


// --- Menu call backs ---

void doxyItFunction()
{
	std::string doc_block;
	int lang_type;
	int startPos;
	int startLine, endLine;
	char *indent = NULL;

	if(!updateScintilla()) return;

	// Check if it is enabled
	fingertext_enabled = checkFingerText();

	// Get the current language type and parse it
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTLANGTYPE, 0, (LPARAM) &lang_type);
	doc_block = Parse(lang_type);
	if(doc_block.length() == 0) return;

	// Keep track of where we started
	startPos = SendScintilla(SCI_GETCURRENTPOS);
	startLine = SendScintilla(SCI_LINEFROMPOSITION, startPos);

	// Get the whitespace of the next line so we can insert it infront of 
	// all the lines of the document block that is going to be inserted
	indent = getLineIndentStr(startLine + 1);

	SendScintilla(SCI_BEGINUNDOACTION);
	SendScintilla(SCI_REPLACESEL, SCI_UNUSED, (LPARAM) doc_block.c_str());
	endLine = SendScintilla(SCI_LINEFROMPOSITION, SendScintilla(SCI_GETCURRENTPOS)); // get the end of the document block
	if(indent) insertBeforeLines(indent, startLine, endLine + 1);
	SendScintilla(SCI_ENDUNDOACTION);

	// Activate FingerText
	if(fingertext_enabled)
	{
		CommunicationInfo ci;
		ci.internalMsg = FINGERTEXT_ACTIVATE;
		ci.srcModuleName = NPP_PLUGIN_NAME;
		ci.info = NULL;

		// Reset to where we started
		SendScintilla(SCI_SETCURRENTPOS, startPos);

		::SendMessage(nppData._nppHandle, NPPM_MSGTOPLUGIN, (WPARAM) TEXT("FingerText.dll"), (LPARAM) &ci);
	}

	if(indent) delete[] indent;
	// return (return_val, function_name, (parameters))
}

void doxyItFile()
{
	/*
	TCHAR fileName[MAX_PATH];
	char fname[MAX_PATH];
	std::ostringstream doc_block;
	char *eol;

	::SendMessage(nppData._nppHandle, NPPM_GETFILENAME, MAX_PATH, (LPARAM) fileName);
	wcstombs(fname, fileName, sizeof(fname));

	if(!updateScintilla()) return;

	eol = getEolStr();

	// Check if it is enabled
	fingertext_enabled = checkFingerText();

	doc_block << doc_start << eol;
	doc_block << doc_line << command_prefix << "file " << fname << eol;
	doc_block << doc_line << command_prefix << "brief " << eol;
	doc_block << doc_line << eol;
	doc_block << doc_line << command_prefix << "author " << eol;
	doc_block << doc_line << command_prefix << "version 1.0" << eol;
	doc_block << doc_end;

	SendScintilla(SCI_REPLACESEL, SCI_UNUSED, (LPARAM) doc_block.str().c_str());
	*/
}

void activeCommenting()
{
	do_active_commenting = !do_active_commenting;
	::SendMessage(nppData._nppHandle, NPPM_SETMENUITEMCHECK, funcItem[1]._cmdID, (LPARAM) do_active_commenting);
}

void useFingerText()
{
	use_fingertext = !use_fingertext;
	::SendMessage(nppData._nppHandle, NPPM_SETMENUITEMCHECK, funcItem[2]._cmdID, (LPARAM) use_fingertext);
}

/*
void activeWrapping()
{
	do_active_wrapping = !do_active_wrapping;
	::SendMessage(nppData._nppHandle, NPPM_SETMENUITEMCHECK, funcItem[3]._cmdID, (LPARAM) do_active_wrapping);
}
*/

void showSettings()
{
	updateScintilla();
	sd.doDialog();
}


// --- Notification callbacks ---

void doxyItNewLine()
{
	std::ostringstream doc_block;
	std::string indentation;
	int curPos, curLine;
	const ParserDefinition *pd;
	const char *eol;
	char *previousLine, *found = NULL;

	if(!updateScintilla()) return;

	if(!(pd = &getCurrentParser()->pd)) return;
	eol = getEolStr();

	curPos = (int) SendScintilla(SCI_GETCURRENTPOS);
	curLine = (int) SendScintilla(SCI_LINEFROMPOSITION, curPos);

	previousLine = getLine(curLine - 1);

	// NOTE: we cannot use getLineIndentStr() because doc_start or doc_line may start with whitespace
	// which we don't want counted towards the indentation string.

	if(found = strstr(previousLine, pd->doc_line.c_str()))
	{
		indentation.append(previousLine, found - previousLine);

		doc_block << indentation.c_str() <<  pd->doc_line.c_str();

		SendScintilla(SCI_BEGINUNDOACTION);
		clearLine(curLine); // Clear any automatic indentation
		SendScintilla(SCI_REPLACESEL, SCI_UNUSED, (LPARAM) doc_block.str().c_str());
		SendScintilla(SCI_ENDUNDOACTION);
	}
	// If doc_start is relatively long we do not want the user typing the entire line, just the first 3 should suffice.
	// Also, if doc_end is found, this means a doc block was closed. This allows e.g. /** inline comment */
	else if((found = strstr(previousLine, pd->doc_start.substr(0, 3).c_str())) &&
			strstr(previousLine, pd->doc_end.c_str()) == 0)
	{
		//::MessageBox(NULL, TEXT("Insert doc_line and doc_end"), NPP_PLUGIN_NAME, MB_OK);
		indentation.append(previousLine, found - previousLine);

		// Count the characters in common so we can add the rest
		unsigned int i = 0;
		while(i < pd->doc_start.length() && found[i] == pd->doc_start.at(i)) ++i;

		doc_block << &pd->doc_start.c_str()[i] << eol;
		doc_block << indentation.c_str() << pd->doc_line.c_str() << eol;
		doc_block << indentation.c_str() << pd->doc_end.c_str();

		SendScintilla(SCI_BEGINUNDOACTION);
		clearLine(curLine); // Clear any automatic indentation
		SendScintilla(SCI_DELETEBACK); // Clear the newline
		SendScintilla(SCI_REPLACESEL, SCI_UNUSED, (LPARAM) doc_block.str().c_str());
		SendScintilla(SCI_ENDUNDOACTION);

		// Go up and to the end of the previous line
		SendScintilla(SCI_LINEUP);
		SendScintilla(SCI_LINEEND);
	}

	delete[] previousLine;
}

void handleNotification(SCNotification *notifyCode)
{
	static bool do_newline = false;
	NotifyHeader nh = notifyCode->nmhdr;
	int ch = notifyCode->ch;

	switch(nh.code)
	{
		case SCN_UPDATEUI: // Now is when we can check to see if we do the commenting
			if(do_newline)
			{
				do_newline = false;
				if(!updateScintilla()) return;
				doxyItNewLine();
			}
			break;
		case SCN_CHARADDED:
			// Set a flag so that all line endings can trigger the commenting
			if((ch == '\r' || ch == '\n') && do_active_commenting) do_newline = true;
			break;
		case NPPN_READY:
			CommunicationInfo ci;

			// Check if FingerText is installed
			ci.internalMsg = FINGERTEXT_GETVERSION;
			ci.srcModuleName = NPP_PLUGIN_NAME;
			ci.info = NULL;

			// NPPM_MSGTOPLUGIN returns true if the dll is found
			if(::SendMessage(nppData._nppHandle, NPPM_MSGTOPLUGIN, (WPARAM) TEXT("FingerText.dll"), (LPARAM) &ci))
			{
				if((int) ci.info >= 561) fingertext_found = true;
				else fingertext_found = false;
			}
			else
			{
				::MessageBox(NULL, TEXT("FingerText not found."), NPP_PLUGIN_NAME, MB_OK);
				fingertext_found = false;
				return;
			}
			break;
		case NPPN_SHUTDOWN:
			configSave();
			break;
	}
	/*
	else if(do_active_wrapping) // && line starts with doc_line
	{
		int lineMax = 40;
		// Get the line length without counting line endings
		int lineStart = ::SendMessage(curScintilla, SCI_POSITIONFROMLINE, curLine, 0);
		int lineEnd = ::SendMessage(curScintilla, SCI_GETLINEENDPOSITION, curLine, 0);
		int lineLen = lineEnd - lineStart;

		if(lineLen > lineMax)
		{
			int char_width = ::SendMessage(curScintilla, SCI_TEXTWIDTH, STYLE_DEFAULT, (LPARAM) " ");
			::SendMessage(curScintilla, SCI_SETTARGETSTART, lineStart, 0);
			::SendMessage(curScintilla, SCI_SETTARGETEND, lineStart, 0);
			::SendMessage(curScintilla, SCI_LINESSPLIT, lineMax * char_width, 0);

			// Check the next few lines to insert the doc_line in front of them
			for(int i = 1; i < 5; ++i)
			{
				// Get the length and allocate a buffer
				int lineLen = ::SendMessage(curScintilla, SCI_LINELENGTH, curLine + i, 0);
				char *text = new char[lineLen + 1];

				// Get the text
				::SendMessage(curScintilla, SCI_GETLINE, curLine + i, (LPARAM) text);
				text[lineLen] = '\0';

				// if it doesn't start with doc_line or doc_start, insert the doc_line
				// else we are done
				if(strncmp(text, doc_line.c_str(), doc_line.length()) != 0 && strncmp(text, doc_end.c_str(), doc_end.length()) != 0)
				{
					int lineStart = ::SendMessage(curScintilla, SCI_POSITIONFROMLINE, curLine + i, 0);
					::SendMessage(curScintilla, SCI_INSERTTEXT, lineStart, (LPARAM) doc_line.c_str());
				}
				else
				{
					delete[] text;
					break;
				}

				delete[] text;
			}
		}
	}
	*/
}