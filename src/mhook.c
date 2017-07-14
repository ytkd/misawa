#define STRICT
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "cmd.h"


static HWND vhwndApp;
static HHOOK vKeyHook;
static HANDLE vMtx;

LRESULT CALLBACK KeyProc(int nCode, WPARAM wParam, LPARAM lParam);
int __stdcall InstallHook(HINSTANCE h, HWND hwnd);
int __stdcall UninstallHook(void);


BOOL __stdcall DllEntryPoint(HINSTANCE hInst, DWORD dwReason, void *p) {
	switch (dwReason) {
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_PROCESS_DETACH:
		UninstallHook();
		break;
	}
	return TRUE;
}


int __stdcall InstallHook(HINSTANCE h, HWND hwnd) {
	vhwndApp = hwnd;
	if (vKeyHook == NULL) {
		vKeyHook = SetWindowsHookEx(WH_KEYBOARD, KeyProc, h, 0);
	}
	vMtx = CreateMutex(NULL, FALSE, NULL);
	return (int)vKeyHook;
}

int __stdcall UninstallHook(void) {
	if (vKeyHook) {
		UnhookWindowsHookEx(vKeyHook);
		vKeyHook = 0;
	}
	CloseHandle(vMtx);
	return 0;
}



LRESULT CALLBACK KeyProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	int ev;
	int stat;

	if (nCode < 0) {
		return CallNextHookEx(vKeyHook, nCode, wParam, lParam);
	}

	/* bit 31: transit state. 0: pressed  1: released
           bit 30: prev state.    0: released 1: pressed
	 */
	stat = (lParam >> 30) & 3;
	if (stat & 2) {
		return CallNextHookEx(vKeyHook, nCode, wParam, lParam);
	}

	ev = IDM_KEYDOWN;
	switch (wParam) {
	case VK_RETURN:
		ev = IDM_ENTERKEYDOWN;
		stat &= 2;
		break;
	case VK_SHIFT:
	case VK_CONTROL:
	case VK_MENU:
		break;
	default:
		stat &= 2;
		break;
	}
	switch (stat) {
	case 0:/*  release to pressed  */
		PostMessage(vhwndApp, WM_COMMAND, ev, 0);
	}

	return CallNextHookEx(vKeyHook, nCode, wParam, lParam);
}


