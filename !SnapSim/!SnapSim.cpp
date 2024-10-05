// SnapSim 1.0.0
// 
// inspired by https://github.com/cafali/SnapKey
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <thread>
#include <sstream>
#include <string>
#include <unordered_map>
#include <regex>
using namespace std;

#define ID_TRAY_APP_ICON                1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  3000
#define ID_TRAY_LOCK_FUNCTION           3001
#define ID_TRAY_RESTART_SNAPSIM         3002
#define WM_TRAYICON                     (WM_USER + 1)

struct KeyState
{
    bool registered = false;
    bool keyDown = false;
    int group;
    bool simulated = false;
};

struct GroupState
{
    int previousKey;
    int activeKey;
};

unordered_map<int, GroupState> GroupInfo;
unordered_map<int, KeyState> KeyInfo;
int delay;
HHOOK hHook = NULL;
HANDLE hMutex = NULL;
NOTIFYICONDATA nid;
bool isLocked = false;
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitNotifyIconData(HWND hwnd);
void check();
void CreateDefaultConfig(const std::string& filename);
void RestoreConfigFromBackup(const std::string& backupFilename, const std::string& destinationFilename);
std::string GetVersionInfo();
void SendKey(int target, bool keyDown);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    KeyInfo[65].registered = true; // 'A'
    KeyInfo[65].group = 1;

    KeyInfo[68].registered = true; // 'D'
    KeyInfo[68].group = 1;

    KeyInfo[83].registered = true; // 'S'
    KeyInfo[83].group = 2;

    KeyInfo[87].registered = true; // 'W'
    KeyInfo[87].group = 2;


    hMutex = CreateMutex(NULL, TRUE, TEXT("SnapSim"));
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBox(NULL, TEXT("SnapSim is already running!"), TEXT("SnapSim"), MB_ICONINFORMATION | MB_OK);
        return 1;
    }
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("SnapSim");

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window Registration Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }
    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        TEXT("SnapSim"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 240, 120,
        NULL, NULL, wc.hInstance, NULL);

    if (hwnd == NULL) {
        MessageBox(NULL, TEXT("Window Creation Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }

    InitNotifyIconData(hwnd);

    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
    if (hHook == NULL)
    {
        MessageBox(NULL, TEXT("Failed to install hook!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }


    UnhookWindowsHookEx(hHook);

    Shell_NotifyIcon(NIM_DELETE, &nid);

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return 0;
}

void handleKeyDown(int keyCode)
{
    KeyState& currentKeyInfo = KeyInfo[keyCode];
    GroupState& currentGroupInfo = GroupInfo[currentKeyInfo.group];
    if (!currentKeyInfo.keyDown)
    {
        currentKeyInfo.keyDown = true;
        SendKey(keyCode, true);
        if (currentGroupInfo.activeKey == 0 || currentGroupInfo.activeKey == keyCode)
        {
            currentGroupInfo.activeKey = keyCode;
        }
        else
        {
            currentGroupInfo.previousKey = currentGroupInfo.activeKey;
            currentGroupInfo.activeKey = keyCode;

            SendKey(currentGroupInfo.previousKey, false);
        }
    }
}

void handleKeyUp(int keyCode)
{
    KeyState& currentKeyInfo = KeyInfo[keyCode];
    GroupState& currentGroupInfo = GroupInfo[currentKeyInfo.group];
    if (currentGroupInfo.previousKey == keyCode && !currentKeyInfo.keyDown)
    {
        currentGroupInfo.previousKey = 0;
    }
    if (currentKeyInfo.keyDown)
    {
        currentKeyInfo.keyDown = false;
        if (currentGroupInfo.activeKey == keyCode && currentGroupInfo.previousKey != 0)
        {
            SendKey(keyCode, false);

            currentGroupInfo.activeKey = currentGroupInfo.previousKey;
            currentGroupInfo.previousKey = 0;

            SendKey(currentGroupInfo.activeKey, true);
        }
        else
        {
            currentGroupInfo.previousKey = 0;
            if (currentGroupInfo.activeKey == keyCode) currentGroupInfo.activeKey = 0;
            SendKey(keyCode, false);
        }
    }
}

bool isSimulatedKeyEvent(DWORD flags) {
    return flags & 0x10;
}
void check() {
    HWND hwnd = FindWindow(nullptr, L"Counter-Strike 2");
    HWND activeWindow = GetForegroundWindow();
    if (hwnd == activeWindow)
    {
        isLocked = false;

    }
    else {
        isLocked = true;
    }
}
void SendKey(int targetKey, bool keyDown)
{
    HWND hwnd = FindWindow(nullptr, L"Counter-Strike 2");
    UINT msg = keyDown ? WM_KEYDOWN : WM_KEYUP;
    PostMessage(hwnd, msg, (WPARAM)targetKey, 0);
}




LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    check();
    if (!isLocked && nCode >= 0)
    {
        KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;
        if (!isSimulatedKeyEvent(pKeyBoard->flags)) {
            if (KeyInfo[pKeyBoard->vkCode].registered)
            {
                delay = std::rand() % 15 + 15;
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) handleKeyDown(pKeyBoard->vkCode);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) handleKeyUp(pKeyBoard->vkCode);
                return 1;
            }
        }
    }
    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

void InitNotifyIconData(HWND hwnd)
{
    memset(&nid, 0, sizeof(NOTIFYICONDATA));

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;


    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    lstrcpy(nid.szTip, TEXT("SnapSim"));

    Shell_NotifyIcon(NIM_ADD, &nid);
}



LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONDOWN)
        {
            POINT curPoint;
            GetCursorPos(&curPoint);
            SetForegroundWindow(hwnd);

            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_RESTART_SNAPSIM, TEXT("Restart SnapSim"));
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, TEXT("Exit SnapSim"));

            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, curPoint.x, curPoint.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_EXIT_CONTEXT_MENU_ITEM:
            PostQuitMessage(0);
            break;
        case ID_TRAY_LOCK_FUNCTION:
        {
            isLocked = !isLocked;

            if (isLocked)
            {
                HICON hIconOff = (HICON)LoadImage(NULL, TEXT("icon_off.ico"), IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
                if (hIconOff)
                {
                    nid.hIcon = hIconOff;
                    Shell_NotifyIcon(NIM_MODIFY, &nid);
                    DestroyIcon(hIconOff);
                }
            }
            else
            {
                HICON hIconOn = (HICON)LoadImage(NULL, TEXT("icon.ico"), IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
                if (hIconOn)
                {
                    nid.hIcon = hIconOn;
                    Shell_NotifyIcon(NIM_MODIFY, &nid);
                    DestroyIcon(hIconOn);
                }
            }

            HMENU hMenu = GetSubMenu(GetMenu(hwnd), 0);
            CheckMenuItem(hMenu, ID_TRAY_LOCK_FUNCTION, MF_BYCOMMAND | (isLocked ? MF_CHECKED : MF_UNCHECKED));
        }
        break;
        case ID_TRAY_RESTART_SNAPSIM:
        {
            TCHAR szExeFileName[MAX_PATH];
            GetModuleFileName(NULL, szExeFileName, MAX_PATH);
            ShellExecute(NULL, NULL, szExeFileName, NULL, NULL, SW_SHOWNORMAL);

            PostQuitMessage(0);
        }
        break;

        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}
