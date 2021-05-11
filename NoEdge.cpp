/*
 Copyright 2021 Martin Conrad

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

//#define CONSOLEPROGRAM

#include <Windows.h>
#ifdef CONSOLEPROGRAM
#	include <stdio.h>
	const char *Arg0;
	inline void errorExit(const char *message, int exitcode) {
		exit((printf("%s: %s: %u\n", Arg0, message, GetLastError()), exitcode));
	}
	void dumpMessage(MSG *msg, const char*arg) {
#ifdef _WIN64
		printf("%s: %s: message: %u, wParam: %llu, lParam: %lld, time: %lu\n", Arg0, arg, msg->message, msg->wParam, msg->lParam, msg->time);
#else
		printf("%s: %s: message: %u, wParam: %u, lParam: %d, time: %lu\n", Arg0, arg, msg->message, msg->wParam, msg->lParam, msg->time);
#endif
	}
#else
	inline void errorExit(const char*, int exitcode) {
		exit(exitcode);
	}

	inline void dumpMessage(MSG*, const char*) {}
#endif

/*
  This program installs the "NoEdgeShortcuts" keyboard hook and enters the Windows
  message loop. This allows the hook to run as expected.
 */
#ifdef CONSOLEPROGRAM
int main(int n, char **s) {
	Arg0 = *s;
#else
int WINAPI WinMain(HINSTANCE instance, HINSTANCE dummy, char* command, int minmaxnormal) {
#endif
	// Retrieve the keyboard hook dll
	HINSTANCE hi = LoadLibrary("NoEdgeShortcuts.dll");
	if (hi == NULL)
		errorExit("Cannot load NoEdgeShortcuts.dll", 1);
	// Retrieve the keyboard hook procedure address
	HOOKPROC hook = (HOOKPROC)GetProcAddress(hi, "NoEdgeKeyboardHook");
	// A function that sets the timer component of the WM_TIMER message into the stored
	// last key event of the hook.
	void(*SetTimerTick)(DWORD) = (void(*)(DWORD))GetProcAddress(hi, "SetTimerTick");
	if (hook == NULL)
		errorExit("Cannot retrieve hook address NoEdgeKeyboardHook", 2);
	// Install the (global) keyboard hook
	HHOOK hookhd = SetWindowsHookExA(WH_KEYBOARD_LL, hook, hi, 0);
	if (hookhd == NULL)
		errorExit("Cannot set keyboard hook", 3);
#ifdef INCREMENTPRIORITY
	// Put process into high priority class and select highest priority to ensure
	// fast hook processing. Ignore possible failure; if an error occurs, the only
	// effect might be minimal slower hook processing...
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
	// Enter the message loop
	while (1) {
		MSG msg;
		int ret = GetMessage(&msg, NULL, 0, 0);
		dumpMessage(&msg, "Got MSG");
		if (ret > 0) {
			if (msg.message == WM_TIMER && SetTimerTick != NULL)
				SetTimerTick(msg.time);
			TranslateMessage(&msg);
			dumpMessage(&msg, "Translated MSG");
			DispatchMessage(&msg);
			dumpMessage(&msg, "Dispatched MSG");
		}
		else if (ret == 0)
			break;
	}
	return 0;
}