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

__declspec(dllexport)
struct {
	FILE *Fp;
	char KeyDown[10];
	char KeyUp[10];
	char SysKeyDown[15];
	char SysKeyUp[15];
	char InvalidParam[20];
	char hexdigits[20];
	char Idle[10];
	char GotHotKey[15];
	char InHotKey[15];
	char WaitHotKeyRelease[20];
	char format[100];
	INPUT LastKey;
	UINT_PTR TimerID;
	DWORD Timeout;
} data = {
	NULL,
	"KEYDOWN",
	"KEYUP",
	"SYSKEYDOWN",
	"SYSKEYUP",
	"Invalid WPARAM",
	"0123456789abcdef",
	"Idle",
	"GotHotKey",
	"InHotKey",
	"WaitHotKeyRelease",
	"WParam: %s, LPARAM: vkCode: %x, scCode: %x, flags: %x, time: %u, extra: %s, KeyState: %s\n"
};


/*
	Function (const char*)getWPString((WPARAM) wp):
	Returns a C-string representation of the WPARAM value.
*/

static const char* getWPString(WPARAM wp) {
	if (wp == WM_KEYDOWN)
		return data.KeyDown;
	if (wp == WM_KEYUP)
		return data.KeyUp;
	if (wp == WM_SYSKEYDOWN)
		return data.SysKeyDown;
	if (wp == WM_SYSKEYUP)
		return data.SysKeyUp;
	return data.InvalidParam;
}

/*
	Function (const char*)getExtra((ULONG_PTR) extra, (char*) buffer):
	Fills buffer with the hexadecimal representation of the extra value
	and returns this representation.
*/
static const char*getExtra(ULONG_PTR extra, char *buffer) {
	int i, j;
	for (i = 0; extra != 0; i++, extra >>= 4)
		buffer[i] = data.hexdigits[extra & 0xf];
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
		return data.Idle;
	case NoEdgeWinPressed:
		return data.GotHotKey;
	case NoEdgeWaitWinRelease:
		return data.InHotKey;
	case NoEdgeIgnoreKeyEvents:
		return data.WaitHotKeyRelease;
	}
}

/*
	Timeout handler, will be invoked Timeout milliseconds after reception
	(and discarding) of a LEFT-WINDOWS key press event. If another key input
	event occurred in the meantime, the timer will be discarded.
*/
void __stdcall NoEdgeWindowsKeyTimeout(HWND, UINT, UINT_PTR, DWORD) {
	if (KeyState == NoEdgeWinPressed)
		SendInput(1, &data.LastKey, sizeof data.LastKey);
}

/*
	A small procedure that sets the time component of the prepared INPUT
	structure, can be used to set the current time from WM_TIMER event
	before calling TranslateMessage and DispatchMessage to create a better
	key press event during invokation of NoEdgeWindowsKeyTimeout.
*/
extern "C" __declspec(dllexport)
void SetTimerTick(DWORD tick) {
	data.LastKey.ki.time = tick;
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
		if (data.Fp != NULL) {
			char buffer[50];
			fprintf(data.Fp, data.format,
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
					data.LastKey.type = INPUT_KEYBOARD;
					data.LastKey.ki.dwExtraInfo = hs->dwExtraInfo;
					data.LastKey.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
					data.LastKey.ki.time = hs->time;
					data.LastKey.ki.wScan = (WORD)hs->scanCode;
					data.LastKey.ki.wVk = (WORD)hs->vkCode;
					data.TimerID = SetTimer(NULL, 0, data.Timeout, NoEdgeWindowsKeyTimeout);
					return -1;
				}
				break;
			case NoEdgeWinPressed:
				if (data.TimerID != 0)
					KillTimer(NULL, data.TimerID);
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
	Returns lowest function and data address of keyboard hook dll in parameter addresses and the corresponding
	estimated code and data length in minlength.
*/
extern "C" __declspec(dllexport)
void GetDllInfo(void *addresses[2], SIZE_T minlength[2]) {
	SIZE_T minaddr = -1, maxaddr = 0, current;
	minaddr = maxaddr = (unsigned long long)getWPString;
	if ((current = (unsigned long long)getExtra) < minaddr)
		minaddr = current;
	else if (current > maxaddr)
		maxaddr = current;
	if ((current = (unsigned long long)getState) < minaddr)
		minaddr = current;
	else if (current > maxaddr)
		maxaddr = current;
	if ((current = (unsigned long long)NoEdgeWindowsKeyTimeout) < minaddr)
		minaddr = current;
	else if (current > maxaddr)
		maxaddr = current;
	if ((current = (unsigned long long)SetTimerTick) < minaddr)
		minaddr = current;
	else if (current > maxaddr)
		maxaddr = current;
	if ((current = (unsigned long long)NoEdgeKeyboardHook) < minaddr)
		minaddr = current;
	else if (current > maxaddr)
		maxaddr = current;
	if (addresses != NULL) {
		*addresses = (void*)minaddr;
		addresses[1] = &data;
	}
	if (minlength != NULL) {
		// We assume the functions are smaller than 1000 bytes
		*minlength = maxaddr - minaddr + 1000;
		minlength[1] = sizeof data;
	}
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
			fopen_s(&data.Fp, buffer, "a");
			if (data.Fp == NULL)
				return FALSE;
			delete[] buffer;
			buffer = new char[100];
			if ((len = GetEnvironmentVariableA("NoEdgeTimeout", buffer, 100)) <= 0 || len >= 5)
				data.Timeout = 100;
			else if ((len = atoi(buffer)) < 32)
				data.Timeout = 32;
			else if (len > 1024)
				data.Timeout = 1024;
			else
				data.Timeout = len;
			fprintf(data.Fp, "NoEdge Timeout: %d\n", data.Timeout);
			KeyState = NoEdgeIdle;
		}
		break;
	case DLL_PROCESS_DETACH:
		if (data.Fp != NULL)
			fclose(data.Fp);
	}
	return TRUE;
}