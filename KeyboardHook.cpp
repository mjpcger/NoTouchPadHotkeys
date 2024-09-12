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
*/

enum KeyboardState {
	NoEdgeIdle = 0,					// Not in hot-key sequence
	NoEdgeWinPressed = 1,			// hot-key has been pressed
	NoEdgeWaitWinRelease = 2,		// In hot-key sequence
	NoEdgeIgnoreKeyEvents = 3		// Ignore key events of hot-key sequence
};

__declspec(dllexport)
struct {
	INPUT LastKey;
	UINT_PTR TimerID;
	DWORD Timeout;
	enum KeyboardState KeyState;
} data;

/*
Timeout handler, will be invoked Timeout milliseconds after reception
(and discarding) of a LEFT-WINDOWS key press event. If another key input
event occurred in the meantime, the timer will be discarded.
*/
static void __stdcall NoEdgeWindowsKeyTimeout(HWND, UINT, UINT_PTR, DWORD) {
	if (data.KeyState == NoEdgeWinPressed)
		SendInput(1, &data.LastKey, sizeof data.LastKey);
}

/*
A small procedure that sets the time component of the prepared INPUT
structure, can be used to set the current time from WM_TIMER event
before calling TranslateMessage and DispatchMessage to create a better
key press event during invokation of NoEdgeWindowsKeyTimeout.
*/
static void SetTimerTick(DWORD tick) {
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
static LRESULT CALLBACK NoEdgeKeyboardHook(int code, WPARAM wp, LPARAM lp) {
	if (code == HC_ACTION) {
		KBDLLHOOKSTRUCT *hs = (KBDLLHOOKSTRUCT*)lp;
		switch (data.KeyState) {
		case NoEdgeIdle:
			if (wp == WM_KEYDOWN && hs->scanCode == 0x5b && hs->vkCode == 0x5b) {
				data.KeyState = NoEdgeWinPressed;
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
				data.KeyState = NoEdgeIgnoreKeyEvents;
		case NoEdgeIgnoreKeyEvents:
				if (wp == WM_KEYUP && hs->scanCode == 0x5b && hs->vkCode == 0x5b)
					data.KeyState = NoEdgeIdle;
				return -1;
			}
			else
				data.KeyState = NoEdgeWaitWinRelease;
			break;
		case NoEdgeWaitWinRelease:
			if (wp == WM_KEYUP && hs->scanCode == 0x5b && hs->vkCode == 0x5b)
				data.KeyState = NoEdgeIdle;
			break;
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
			char *buffer = new char[100];
			int len;
			if ((len = GetEnvironmentVariableA("NoEdgeTimeout", buffer, 100)) <= 0 || len >= 5)
				data.Timeout = 100;
			else if ((len = atoi(buffer)) < 32)
				data.Timeout = 32;
			else if (len > 1024)
				data.Timeout = 1024;
			else
				data.Timeout = len;
			data.KeyState = NoEdgeIdle;
		}
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

/*
Returns lowest function and data address of keyboard hook dll in parameter addresses and the corresponding
estimated code and data length in minlength.
*/
static void GetDllInfo(void *addresses[2], SIZE_T minlength[2]) {
	SIZE_T minaddr = -1, maxaddr = 0, current, add = 0;
	minaddr = maxaddr = (SIZE_T)NoEdgeWindowsKeyTimeout;
	if ((current = (SIZE_T)SetTimerTick) < minaddr)
		minaddr = current;
	else if (current > maxaddr)
		maxaddr = current;
	if ((current = (SIZE_T)NoEdgeKeyboardHook) < minaddr)
		minaddr = current;
	else if (current > maxaddr)
		maxaddr = current;
	if ((current = (SIZE_T)DllMain) > maxaddr && (add == 0 || current < maxaddr + add))
		add = current - maxaddr;
	if ((current = (SIZE_T)GetDllInfo) > maxaddr && (add == 0 || current < maxaddr + add))
		add = current - maxaddr;
	if (addresses != NULL) {
		*addresses = (void*)minaddr;
		addresses[1] = &data;
	}
	if (minlength != NULL) {
		// In case we did not find an upper limit, we assume the maximum function size is less than 1000 bytes.
		*minlength = maxaddr - minaddr + (add > 0 ? add : 1000);
		minlength[1] = sizeof data;
	}
}

extern "C" __declspec(dllexport)
void* GetFunctionAddress(int index) {
	switch (index) {
	case 0:	return NoEdgeKeyboardHook;
	case 1:	return SetTimerTick;
	case 2: return GetDllInfo;
	}
	return NULL;
}
