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

/**
 * This program installs the "NoEdgeShortcuts" keyboard hook and enters the Windows
 * message loop. This allows the hook to run as expected.
 */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE dummy, char* command, int minmaxnormal) {
	// Retrieve the keyboard hook dll
	HINSTANCE hi = LoadLibrary("NoEdgeShortcuts.dll");
	if (hi == NULL)
		exit(1);
	// Retrieve the keyboard hook procedure address
	HOOKPROC hook = (HOOKPROC)GetProcAddress(hi, "NoEdgeKeyboardHook");
	if (hook == NULL)
		exit(2);
	// Install the (global) keyboard hook
	HHOOK hookhd = SetWindowsHookExA(WH_KEYBOARD_LL, hook, hi, 0);
	if (hookhd == NULL)
		exit(3);
	// Put process into high priority class and select highest priority to ensure
	// fast hook processing. Ignore possible failure; if an error occurs, the only
	// effect might be minimal slower hook processing...
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	// Enter the message loop
	while (1) {
		MSG msg;
		int ret = GetMessage(&msg, NULL, 0, 0);
		if (ret > 0) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else if (ret == 0)
			break;
	}
	return 0;
}