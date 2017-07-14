#define STRICT
#include <windows.h>

#include "res.h"
#include "cmd.h"
#include "misc.h"
#include "wave.h"

#define TRAYMESSAGE (WM_APP+100)

#define W_CLASS "MisawaType"

static struct pcm_data *se1;
static struct pcm_data *se2;

static NOTIFYICONDATA ni = {
	sizeof(NOTIFYICONDATA),
	NULL,
	1,
	NIF_MESSAGE | NIF_ICON|NIF_TIP,
	TRAYMESSAGE,
	NULL,
	"ミサワタイプ"
};
static HMENU hMenu;
static HANDLE dll;

HWND init_application(HINSTANCE hInst);
void Uninstall(void);

#ifdef _DEBUG
void Entry(void);
/*
*  Entry point (debug)
*/
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR pszCmdLine, int nCmdShow) {
	Entry();
	return 0;
}
#endif

HINSTANCE  vhInst;

void Entry(void) {
	MSG msg;

	vhInst = GetModuleHandle(NULL);

	if (!init_application(vhInst)) {
		ExitProcess(0);
	}
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	m_free(se1);
	m_free(se2);
	sound_close();
	ExitProcess(msg.wParam);
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	POINT pt;

	switch (msg) {
	case WM_CREATE:
		ni.hWnd = hwnd;
		ni.hIcon = (HICON)LoadIcon(vhInst, MAKEINTRESOURCE(IDI_ICON));
		Shell_NotifyIcon(NIM_ADD, &ni);
		hMenu = CreatePopupMenu();
		AppendMenu(hMenu, MF_STRING, IDM_QUIT, "ミサワタイプ終了");
		break;

	case WM_COMMAND:
		switch (wParam) {
		case IDM_QUIT:
			DestroyWindow(hwnd);
			break;
		case IDM_KEYDOWN:
			pcm_set(se1);
			sound_run();
			break;
		case IDM_ENTERKEYDOWN:
			pcm_set(se2);
			sound_run();
			break;
		}
		break;

	case TRAYMESSAGE:
		if (wParam == 1 && lParam == WM_LBUTTONDOWN) {
			SetForegroundWindow(hwnd);
			GetCursorPos(&pt);
			TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
		}
		break;

	case WM_DESTROY:
		Shell_NotifyIcon(NIM_DELETE, &ni);
		if (dll) {
			Uninstall();
			FreeLibrary(dll);
		}
		DestroyMenu(hMenu);
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

HWND init_application(HINSTANCE hInst) {
	HWND hwnd;
	int (WINAPI *install)(HANDLE, HWND);
	static WNDCLASS wc = {
		0,
		WndProc,
		0, 0,
		NULL,/*  Instance  */
		NULL,/*  ICON  */
		NULL,/*  Cursor  */
		(HBRUSH)(COLOR_WINDOW + 1),
		NULL,
		W_CLASS
	};
	int ret;

	if (FindWindow(W_CLASS, NULL)) {
		return NULL;
	}

	wc.hInstance = GetModuleHandle(NULL);
	wc.hIcon = (HICON)LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = (HCURSOR)LoadCursor(NULL, IDC_ARROW);
	RegisterClass(&wc);
	hwnd = CreateWindowEx(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		W_CLASS
		,""
		,WS_POPUP
		,10
		,10
		,10
		,10
		,NULL
		,NULL
		,hInst
		,NULL);

	if (!hwnd) {
		return NULL;
	}

	dll = LoadLibrary("MHOOK.DLL");
	if (!dll) {
		MessageBox(NULL, "can't load DLL.", "Error", 0);
		goto CANTLOAD;
	}

	install = GetProcAddress(dll, "InstallHook");
	if (!install) {
		MessageBox(NULL, "can't bind procedure.", "Error", 0);
		goto CANTLOAD;
	}
	ret = install(dll, hwnd);
	if (!ret) {
		MessageBox(NULL, "can't bind procedure.", "Error", 0);
		goto CANTLOAD;
	}

	if (sound_open()) {
		goto CANTLOAD;
	}
	se1 = load_waveform("se01.wav");
	if (se1 == NULL) {
		sound_close();
		goto CANTLOAD;
	}

	se2 = load_waveform("se02.wav");
	if (se2 == NULL) {
		sound_close();
		goto CANTLOAD;
	}

	return hwnd;

CANTLOAD:
	DestroyWindow(hwnd);
	return NULL;
}

void Uninstall(void) {
	void (CALLBACK *uninst)(void);

	uninst = (void (CALLBACK *)(void ))GetProcAddress(dll, "UninstallHook");
	if (uninst) {
		uninst();
	}
}
