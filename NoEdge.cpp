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

#include <Windows.h>
#include <Psapi.h>

inline void errorExit(const char*, int exitcode) {
	exit(exitcode);
}

/*
This function converts two strings to lower case and returns true if the resulting strings are equal, otherwise false
*/
static bool equal(const char*s1, const char*s2) {
	while (tolower(*s1) == tolower(*s2)) {
		if (*s1 == 0)
			return true;
		s1++, s2++;
	}
	return false;
}

static void messageLoop(void(*setTimerTick)(DWORD)) {
	while (1) {
		MSG msg;
		int ret = GetMessage(&msg, NULL, 0, 0);
		if (ret > 0) {
			if (msg.message == WM_TIMER)
				setTimerTick(msg.time);
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else if (ret == 0)
			break;
	}
}
/*
This installs the "NoEdgeShortcuts" keyboard hook and enters the Windows message loop. This allows
the hook to run as expected.
*/
static int myMain() {
	char nohookprio[20];
	int  len;
	if ((len = GetEnvironmentVariableA("NoEdgePriority", nohookprio, sizeof nohookprio)) > 0 && len < sizeof nohookprio) {
		DWORD pprio = NORMAL_PRIORITY_CLASS;
		DWORD tprio = THREAD_PRIORITY_NORMAL;
		if (equal(nohookprio, "high")) {
			pprio = HIGH_PRIORITY_CLASS;
			tprio = THREAD_PRIORITY_HIGHEST;
		}
		else if (equal(nohookprio, "abovenormal")) {
			pprio = ABOVE_NORMAL_PRIORITY_CLASS;
			tprio = THREAD_PRIORITY_ABOVE_NORMAL;
		}
		else if (equal(nohookprio, "belownormal")) {
			pprio = BELOW_NORMAL_PRIORITY_CLASS;
			tprio = THREAD_PRIORITY_BELOW_NORMAL;
		} else if (equal(nohookprio, "idle")) {
			pprio = IDLE_PRIORITY_CLASS;
			tprio = THREAD_PRIORITY_IDLE;
		}
		if (GetPriorityClass(GetCurrentProcess()) != pprio || GetThreadPriority(GetCurrentThread()) != tprio) {
			// Put process into given priority class and select matching thread priority to ensure
			// expected hook processing.
			len = SetPriorityClass(GetCurrentProcess(), pprio);
			len = SetThreadPriority(GetCurrentThread(), tprio);
		}
	}
	// Retrieve the keyboard hook dll
	HINSTANCE hi = LoadLibrary("NoEdgeShortcuts.dll");
	if (hi == NULL)
		errorExit("Cannot load NoEdgeShortcuts.dll", 1);
	// Retrieve the address resolving function
	void*(*getFunctionAddress)(int) = (void*(*)(int))GetProcAddress(hi, "GetFunctionAddress");
	if (getFunctionAddress == NULL)
		errorExit("Cannot retrieve address retrieval function GetFunctionAddress", 2);
	// Retrieve the keyboard hook procedure address
	HOOKPROC hook = (HOOKPROC)getFunctionAddress(0);
	// A function that sets the timer component of the WM_TIMER message into the stored
	// last key event of the hook.
	void(*SetTimerTick)(DWORD) = (void(*)(DWORD))getFunctionAddress(1);
	// A function that returns the lowest relevant function address and the minimum length to be locked
	void(*GetDllInfo)(void**, SIZE_T*) = (void(*)(void**, SIZE_T*))getFunctionAddress(2);
	if (hook == NULL)
		errorExit("Cannot retrieve address of NoEdgeKeyboardHook", 2);
	if (SetTimerTick == NULL)
		errorExit("Cannot retrieve address of SetTimerTick", 2);
	if (GetDllInfo == NULL)
		errorExit("Cannot retrieve address of GetDllInfo", 2);
	SIZE_T minlength[2];
	void *addresses[2];
	GetDllInfo(addresses, minlength);
	SIZE_T progpos, loopsize = (SIZE_T)messageLoop;
	if ((progpos = (SIZE_T)equal) < loopsize && (progpos = (SIZE_T)myMain) < loopsize)
		loopsize = 1000;	// messageLoop has highest function address: Assume its size is less than 1000 bytes-
	else
		loopsize = progpos - loopsize;
	if (!VirtualLock((void*)messageLoop, loopsize) || !VirtualLock(addresses[0], minlength[0]) || !VirtualLock(addresses[1], minlength[1]))
		len = GetLastError();
	// Install the (global) keyboard hook
	HHOOK hookhd = SetWindowsHookExA(WH_KEYBOARD_LL, hook, hi, 0);
	if (hookhd == NULL)
		errorExit("Cannot set keyboard hook", 3);
	// Enter the message loop
	messageLoop(SetTimerTick);
	exit(0);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE dummy, char* command, int minmaxnormal) {
	return myMain();
}
