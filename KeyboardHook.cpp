/*
* Copyright 2021 Martin Conrad
* 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* 
* http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/

#include <Windows.h>
#include <stdio.h>

/*
  This DLL provides a keyboard hook procedure that filters specific keyboard
  input as generated from some keyboards whenever the finger swipes from the
  left, upper or right edge of the integrated touch pad onto the touchpad.
 
  The idea is to filter out all LEFT-WINDOWS key combinations. However, to
  allow hotkey combinations with LEFT-WINDOWS key, a key press will be generated
  after 100 milliseconds if no further keyboard events arrive in the meantime
  (100 milliseconds is the default, it can be changed via environment variable
  NoEdgeTimeout. Minimum 32 milliseconds, maximum 1024 milliseconds).
 
  The keyboard hook can generate a log file that contains the contents of the
  keyboard events that occur, inclusive a time stamp. To activate the log,
  set the name of the log file in environment variable NoEdgeLog:
 	set NoEdgeLog=<name>
  To deactivate the log, set the log file name to nul (the default):
 	set NoEdgeLog=nul
 */

#define PRINT 0

FILE *Fp;

/*
	Function (const char*)getWPString((WPARAM) wp):
	Returns a C-string representation of the WPARAM value.
*/
static const char* getWPString(WPARAM wp) {
	if (wp == WM_KEYDOWN)
		return "KEYDOWN";
	if (wp == WM_KEYUP)
		return "KEYUP";
	if (wp == WM_SYSKEYDOWN)
		return "SYSKEYDOWN";
	if (wp == WM_SYSKEYUP)
		return "SYSKEYUP";
	return "Invalid WPARAM";
}

/*
	Function (const char*)getExtra((ULONG_PTR) extra, (char*) buffer):
	Fills buffer with the hexadecimal representation of the extra value
	and returns this representation.
*/
static const char*getExtra(ULONG_PTR extra, char *buffer) {
	int i, j;
	for (i = 0; extra != 0; i++, extra >>= 4)
		buffer[i] = "0123456789abcdef"[extra & 0xf];
	if (i == 0)
		buffer[i++] = '0';
	buffer[i] = 0;
	for (j = 0, i--; j < i; j++, i--) {
		char c = buffer[i];
		buffer[i] = buffer[j];
		buffer[j] = c;
	}
	return buffer;
}

static enum {
	NoEdgeIdle = 0,					// Not in hot-key sequence
	NoEdgeWinPressed = 1,			// hot-key has been pressed
	NoEdgeWaitWinRelease = 2,		// In hot-key sequence
	NoEdgeIgnoreKeyEvents = 3		// Ignore key events of hot-key sequence
} KeyState;

/*
	Function (const char*)getState():
	Returns the string representation of the internal key state.
*/
static const char* getState() {
	switch (KeyState) {
	case NoEdgeIdle:
		return "Idle";
	case NoEdgeWinPressed:
		return "GotHotKey";
	case NoEdgeWaitWinRelease:
		return "InHotKey";
	case NoEdgeIgnoreKeyEvents:
		return "WaitHotKeyRelease";
	}
}

static INPUT LastKey;
static UINT_PTR TimerID;
static DWORD Timeout;

/*
	Timeout handler, will be invoked Timeout milliseconds after reception
	(and discarding) of a LEFT-WINDOWS key press event. If another key input
	event occurred in the meantime, the timer will be discarded.
*/
void __stdcall NoEdgeWindowsKeyTimeout(HWND, UINT, UINT_PTR, DWORD) {
	if (KeyState == NoEdgeWinPressed)
		SendInput(1, &LastKey, sizeof LastKey);
}

/*
	A small procedure that sets the time component of the prepared INPUT
	structure, can be used to set the current time from WM_TIMER event
	before calling TranslateMessage and DispatchMessage to create a better
	key press event during invokation of NoEdgeWindowsKeyTimeout.
*/
extern "C" __declspec(dllexport)
void SetTimerTick(DWORD tick) {
	LastKey.ki.time = tick;
}

/*
	The keyboard hook procedure. Handles LEFT-WINDOWS key press events as follows:
	- Buffers the event and sets a timer of NoEdgeTimeout milliseconds
	- If another key event arrives before the timer elapses, the timer will be killed
	  and all key events until (and inclusive) LEFT-WINDOWS release will be discarded.
	- If the timer elapses without any key input, the timer function generates a 
	  LEFT-WINDOW key press event with all parameters set as in the original key event.
	- The controlling process can use the SetTimerTick function to replace the time
	  component of the original key event with the time component of the WM_TIMER
	  message.
*/
extern "C" __declspec(dllexport)
LRESULT CALLBACK NoEdgeKeyboardHook(int code, WPARAM wp, LPARAM lp) {
	if (code == HC_ACTION) {
		KBDLLHOOKSTRUCT *hs = (KBDLLHOOKSTRUCT*)lp;
		if (Fp != NULL) {
			char buffer[50];
			fprintf(Fp, "WParam: %s, LPARAM: vkCode: %x, scCode: %x, flags: %x, time: %u, extra: %s, KeyState: %s\n",
				// time,
				getWPString(wp),
				hs->vkCode,
				hs->scanCode,
				hs->flags,
				hs->time,
				getExtra(hs->dwExtraInfo, buffer),
				getState()
			);
			switch (KeyState) {
			case NoEdgeIdle:
				if (wp == WM_KEYDOWN && hs->scanCode == 0x5b && hs->vkCode == 0x5b) {
					KeyState = NoEdgeWinPressed;
					LastKey.type = INPUT_KEYBOARD;
					LastKey.ki.dwExtraInfo = hs->dwExtraInfo;
					LastKey.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
					LastKey.ki.time = hs->time;
					LastKey.ki.wScan = (WORD)hs->scanCode;
					LastKey.ki.wVk = (WORD)hs->vkCode;
					TimerID = SetTimer(NULL, 0, Timeout, NoEdgeWindowsKeyTimeout);
					return -1;
				}
				break;
			case NoEdgeWinPressed:
				if (TimerID != 0)
					KillTimer(NULL, TimerID);
				if (wp != WM_KEYDOWN || hs->scanCode != 0x5b || hs->vkCode != 0x5b) {
					KeyState = NoEdgeIgnoreKeyEvents;
			case NoEdgeIgnoreKeyEvents:
					if (wp == WM_KEYUP && hs->scanCode == 0x5b && hs->vkCode == 0x5b)
						KeyState = NoEdgeIdle;
					return -1;
				}
				else
					KeyState = NoEdgeWaitWinRelease;
				break;
			case NoEdgeWaitWinRelease:
				if (wp == WM_KEYUP && hs->scanCode == 0x5b && hs->vkCode == 0x5b)
					KeyState = NoEdgeIdle;
				break;
			}
		}
	}
	return CallNextHookEx(0, code, wp, lp);
}

/*
	DLL initialization / finalization:
	- Open / close the log file (environment variable NoEdgeLog),
	- Initialize timeout (from environment variable NoEdgeTimeout).
*/
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID res) {
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		{
			char *buffer = NULL;
			int len = GetEnvironmentVariableA("NoEdgeLog", buffer, 0);
			if (len == 0) {
				buffer = new char[4];
				strcpy_s(buffer, 4, "nul");
			}
			else {
				buffer = new char[len];
				buffer[len = GetEnvironmentVariableA("NoEdgeLog", buffer, len)] = 0;
			}
			fopen_s(&Fp, buffer, "a");
			if (Fp == NULL)
				return FALSE;
			delete[] buffer;
			buffer = new char[100];
			if ((len = GetEnvironmentVariableA("NoEdgeTimeout", buffer, 100)) <= 0 || len >= 5)
				Timeout = 100;
			else if ((len = atoi(buffer)) < 32)
				Timeout = 32;
			else if (len > 1024)
				Timeout = 1024;
			else
				Timeout = len;
			fprintf(Fp, "NoEdge Timeout: %d\n", Timeout);
			KeyState = NoEdgeIdle;
		}
		break;
	case DLL_PROCESS_DETACH:
		if (Fp != NULL)
			fclose(Fp);
	}
	return TRUE;
}