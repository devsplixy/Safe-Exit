#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

#include <windows.h>
#include <wrl.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <set>
#include <thread>
#include <algorithm>
#include <regex>
#include <cmath>
#include <gdiplus.h>
#include <shlobj.h>
#include <dwmapi.h>

#include "WebView2.h"
#include "SystemUtils.h"
#include "GameScanner.h"
#include "Engine.h"
#include "Watchdog.h"
#if __has_include("UIPayload.h")
#include "UIPayload.h"
#else
#include "UIPayload.stub.h"
#endif

#pragma comment(lib, "dwmapi.lib")
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:wWinMainCRTStartup")

using namespace std;
using namespace Microsoft::WRL;

HWND g_hWnd = NULL;
HWND g_hOverlay = NULL;
std::string g_OverlayState = "ENABLED";
int g_OverlayCountdown = 0;
std::vector<DWORD> g_GamePids;
const int OVERLAY_W = 226;
const int OVERLAY_H = 58;
int g_OverlayX = 0;
int g_OverlayY = 0;
ComPtr<ICoreWebView2Controller> g_pWebViewController = nullptr;
ComPtr<ICoreWebView2> g_pWebView = nullptr;

int g_BlockKey = VK_F9;
int g_UnblockKey = VK_F10;
bool g_AlwaysOnTop = false;
bool g_AutoReleaseEnabled = true;
int g_AutoReleaseSeconds = 60;
bool g_OverlayEnabled = false;
int g_TransparencyLevel = 0;

bool g_IsArmed = false;
ULONGLONG g_ArmedAtTick = 0;
bool g_IsBindingKey = false;
string g_ManualExeName = "";
string g_ManualExePath = "";
set<string> g_SessionWhitelist;

#define TIMER_AUTO_RELEASE 999
#define TIMER_MIN_HOLD 998
#define TIMER_OVERLAY_FOCUS 995
#define MIN_HOLD_MS 2000
#define WM_APP_HOTKEY (WM_APP + 1)
#define WM_APP_UI_EVENT (WM_APP + 2)
HHOOK g_hKeyboardHook = NULL;

string GetConfigPath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, path))) {
        string dir = string(path) + "\\Safe Exit";
        CreateDirectoryA(dir.c_str(), NULL);
        return dir + "\\settings.ini";
    }
    return "settings.ini";
}

void SaveSettings() {
    string path = GetConfigPath();
    WritePrivateProfileStringA("Binds", "BlockKey", to_string(g_BlockKey).c_str(), path.c_str());
    WritePrivateProfileStringA("Binds", "UnblockKey", to_string(g_UnblockKey).c_str(), path.c_str());
    WritePrivateProfileStringA("Prefs", "AlwaysOnTop", g_AlwaysOnTop ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("Prefs", "Overlay", g_OverlayEnabled ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("Prefs", "AutoRelease", g_AutoReleaseEnabled ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("Prefs", "AutoReleaseSecs", to_string(g_AutoReleaseSeconds).c_str(), path.c_str());
    WritePrivateProfileStringA("Prefs", "Transparency", to_string(g_TransparencyLevel).c_str(), path.c_str());
}

void LoadSettings() {
    string path = GetConfigPath();
    g_BlockKey = GetPrivateProfileIntA("Binds", "BlockKey", VK_F9, path.c_str());
    g_UnblockKey = GetPrivateProfileIntA("Binds", "UnblockKey", VK_F10, path.c_str());
    g_AlwaysOnTop = GetPrivateProfileIntA("Prefs", "AlwaysOnTop", 0, path.c_str());
    g_OverlayEnabled = GetPrivateProfileIntA("Prefs", "Overlay", 0, path.c_str());
    g_AutoReleaseEnabled = GetPrivateProfileIntA("Prefs", "AutoRelease", 1, path.c_str());
    g_AutoReleaseSeconds = GetPrivateProfileIntA("Prefs", "AutoReleaseSecs", 60, path.c_str());
    g_TransparencyLevel = GetPrivateProfileIntA("Prefs", "Transparency", 0, path.c_str());
}

void ApplyTransparency() {
    static const BYTE alphas[4] = { 255, 240, 222, 200 };
    int lvl = g_TransparencyLevel; if (lvl < 0) lvl = 0; if (lvl > 3) lvl = 3;
    LONG_PTR ex = GetWindowLongPtrW(g_hWnd, GWL_EXSTYLE);
    SetWindowLongPtrW(g_hWnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(g_hWnd, 0, alphas[lvl], LWA_ALPHA);
}

string VKToEng(int vk) {
    char name[128] = { 0 };
    UINT scanCode = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    switch (vk) { case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK: scanCode |= KF_EXTENDED; break; }
                              GetKeyNameTextA(scanCode << 16, name, 128);
                              return string(name).empty() ? "KEY " + to_string(vk) : string(name);
}
LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void InitOverlay(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), 0, OverlayWndProc, 0, 0, hInstance, NULL, NULL, NULL, NULL, L"SE_Overlay", NULL };
    RegisterClassExW(&wc);

    RECT work; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    g_OverlayX = work.right - OVERLAY_W - 24;
    g_OverlayY = work.top + 24;

    g_hOverlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"SE_Overlay", L"", WS_POPUP,
        g_OverlayX, g_OverlayY, OVERLAY_W, OVERLAY_H,
        NULL, NULL, hInstance, NULL);
}

bool IsGameFocused() {
    if (g_GamePids.empty()) return false;
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return false;
    for (DWORD p : g_GamePids) if (p == pid) return true;
    return false;
}

void UpdateOverlayState() {
    g_OverlayState = "ENABLED";
    if (g_OverlayCountdown > 0) g_OverlayState += " (" + to_string(g_OverlayCountdown) + ")";
}

void RenderOverlay() {
    if (!g_hOverlay) return;

    Gdiplus::Bitmap bmp(OVERLAY_W, OVERLAY_H, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

        const float R = 16.0f;
        Gdiplus::RectF rc(0.5f, 0.5f, (float)OVERLAY_W - 1.0f, (float)OVERLAY_H - 1.0f);
        float d = R * 2;
        Gdiplus::GraphicsPath card;
        card.AddArc(rc.X, rc.Y, d, d, 180, 90);
        card.AddArc(rc.GetRight() - d, rc.Y, d, d, 270, 90);
        card.AddArc(rc.GetRight() - d, rc.GetBottom() - d, d, d, 0, 90);
        card.AddArc(rc.X, rc.GetBottom() - d, d, d, 90, 90);
        card.CloseFigure();

        Gdiplus::LinearGradientBrush fill(Gdiplus::RectF(0, 0, (float)OVERLAY_W, (float)OVERLAY_H),
            Gdiplus::Color(228, 23, 35, 30), Gdiplus::Color(234, 9, 15, 13), 90.0f);
        g.FillPath(&fill, &card);

        for (int i = 5; i >= 0; --i) {
            float inset = 1.0f + i * 1.3f;
            Gdiplus::RectF r2(rc.X + inset, rc.Y + inset, rc.Width - inset * 2, rc.Height - inset * 2);
            float dd = (R - inset) * 2; if (dd < 2.0f) dd = 2.0f;
            Gdiplus::GraphicsPath gp;
            gp.AddArc(r2.X, r2.Y, dd, dd, 180, 90);
            gp.AddArc(r2.GetRight() - dd, r2.Y, dd, dd, 270, 90);
            gp.AddArc(r2.GetRight() - dd, r2.GetBottom() - dd, dd, dd, 0, 90);
            gp.AddArc(r2.X, r2.GetBottom() - dd, dd, dd, 90, 90);
            gp.CloseFigure();
            Gdiplus::Pen glowPen(Gdiplus::Color((BYTE)(8 + (5 - i) * 5), 53, 201, 156), 2.0f);
            g.DrawPath(&glowPen, &gp);
        }

        Gdiplus::Pen border(Gdiplus::Color(120, 53, 201, 156), 1.2f);
        g.DrawPath(&border, &card);

        float phase = (float)((GetTickCount64() % 1500) / 1500.0);
        float s = 0.5f + 0.5f * (float)sin(phase * 6.2831853f);
        for (int i = 4; i >= 1; --i) {
            BYTE a = (BYTE)((38 - i * 7) * (0.45f + 0.55f * s));
            Gdiplus::SolidBrush br(Gdiplus::Color(a, 53, 201, 156));
            float rr = 5.5f + i * 2.6f;
            g.FillEllipse(&br, 26 - rr, 29 - rr, rr * 2, rr * 2);
        }
        Gdiplus::SolidBrush dot(Gdiplus::Color(255, 90, 224, 176));
        g.FillEllipse(&dot, 20, 23, 12, 12);

        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font labelFont(&ff, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font stateFont(&ff, 15.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush labelBrush(Gdiplus::Color(150, 150, 168, 160));
        Gdiplus::SolidBrush stateBrush(Gdiplus::Color(255, 110, 231, 183));
        Gdiplus::StringFormat sf;
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        g.DrawString(L"SAFE EXIT", -1, &labelFont, Gdiplus::PointF(42, 9), &sf, &labelBrush);
        std::wstring state(g_OverlayState.begin(), g_OverlayState.end());
        g.DrawString(state.c_str(), -1, &stateFont, Gdiplus::PointF(42, 23), &sf, &stateBrush);
    }

    HBITMAP hbm = NULL;
    bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbm);
    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    HGDIOBJ old = SelectObject(memDC, hbm);
    POINT ptSrc = { 0, 0 };
    SIZE size = { OVERLAY_W, OVERLAY_H };
    POINT ptDst = { g_OverlayX, g_OverlayY };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_hOverlay, screenDC, &ptDst, &size, memDC, &ptSrc, 0, &bf, ULW_ALPHA);
    SelectObject(memDC, old);
    DeleteObject(hbm);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
}

void RefreshOverlay() {
    if (!g_hOverlay) return;
    if (g_IsArmed && g_OverlayEnabled && IsGameFocused()) {
        if (!IsWindowVisible(g_hOverlay)) ShowWindow(g_hOverlay, SW_SHOWNA);
        RenderOverlay();
    }
    else if (IsWindowVisible(g_hOverlay)) {
        ShowWindow(g_hOverlay, SW_HIDE);
    }
}

void FocusMainWindow() {
    if (!g_hWnd) return;
    if (IsIconic(g_hWnd)) ShowWindow(g_hWnd, SW_RESTORE);
    SetWindowPos(g_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g_hWnd);
    SetFocus(g_hWnd);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && !g_IsBindingKey) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;
            DWORD vkCode = pKeyBoard->vkCode;
            if (g_BlockKey == g_UnblockKey) {
                if (vkCode == (DWORD)g_BlockKey) PostMessage(g_hWnd, WM_APP_HOTKEY, 1, 0);
            }
            else {
                if (vkCode == (DWORD)g_BlockKey) PostMessage(g_hWnd, WM_APP_HOTKEY, 1, 0);
                else if (vkCode == (DWORD)g_UnblockKey) PostMessage(g_hWnd, WM_APP_HOTKEY, 2, 0);
            }
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

void RegisterAppHotkeys() { if (g_hKeyboardHook == NULL) g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0); }
void UnregisterAppHotkeys() { if (g_hKeyboardHook) { UnhookWindowsHookEx(g_hKeyboardHook); g_hKeyboardHook = NULL; } }

void DispatchUIEvent(const string& eventName, const string& jsonPayload) {
    string* script = new string("window.onBackendEvent('" + eventName + "', " + jsonPayload + ");");
    if (!PostMessage(g_hWnd, WM_APP_UI_EVENT, 0, (LPARAM)script)) delete script;
}

static string WideToAcp(const wchar_t* w) {
    if (!w || !*w) return "";
    int len = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    string s(len - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, w, -1, &s[0], len, NULL, NULL);
    return s;
}

void StartTargetMonitor() {
    thread([]() {
        string lastKey = "";
        for (;;) {
            TargetInfo t = ScoutTarget(g_ManualExeName, g_ManualExePath);
            if (!t.execName.empty()) {
                string formattedName = FormatGameName(t.execName, t.revision);
                string key = formattedName + "|" + t.absolutePath;
                if (key != lastKey) {
                    lastKey = key;
                    string b64Icon = GetBase64Icon(t.absolutePath);
                    string escapedPath = t.absolutePath; replace(escapedPath.begin(), escapedPath.end(), '\\', '/');
                    DispatchUIEvent("onGameDetected", "{\"name\": \"" + formattedName + "\", \"path\": \"" + escapedPath + "\", \"icon\": \"" + b64Icon + "\"}");
                }
            }
            Sleep(2000);
        }
    }).detach();
}

void BrowseForGame() {
    OPENFILENAMEW ofn; wchar_t szFile[MAX_PATH] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd; ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Executables\0*.exe\0All Files\0*.*\0"; ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        string fullPath = WideToAcp(szFile);
        g_ManualExePath = fullPath;
        size_t pos = fullPath.find_last_of("\\/");
        g_ManualExeName = (pos != string::npos) ? fullPath.substr(pos + 1) : fullPath;

        TargetInfo target = ScoutTarget(g_ManualExeName, g_ManualExePath);
        string formattedName = FormatGameName(g_ManualExeName, target.revision);
        string b64Icon = GetBase64Icon(fullPath);
        string escapedPath = fullPath; replace(escapedPath.begin(), escapedPath.end(), '\\', '/');

        DispatchUIEvent("onGameDetected", "{\"name\": \"" + formattedName + "\", \"path\": \"" + escapedPath + "\", \"icon\": \"" + b64Icon + "\"}");
    }
}

void SequenceEngage() {
    TargetInfo target = ScoutTarget(g_ManualExeName, g_ManualExePath);
    if (target.pids.empty() || target.absolutePath.empty()) { DispatchUIEvent("onError", "{\"msg\": \"Game executable not found.\"}"); return; }
    vector<string> discovered = HarvestSockets(target.pids);
    for (const string& ip : discovered) g_SessionWhitelist.insert(ip);
    if (g_SessionWhitelist.empty()) { DispatchUIEvent("onError", "{\"msg\": \"No active connection found. Join a server or lobby first.\"}"); return; }

    RemoveLockdownRules();
    ApplyLockdown(target.absolutePath, ComputeInverseRanges(g_SessionWhitelist));
    g_IsArmed = true;
    g_ArmedAtTick = GetTickCount64();
    g_GamePids = target.pids;
    KillTimer(g_hWnd, TIMER_MIN_HOLD);

    if (g_AutoReleaseEnabled) SetTimer(g_hWnd, TIMER_AUTO_RELEASE, g_AutoReleaseSeconds * 1000, NULL);
    if (g_OverlayEnabled) {
        g_OverlayCountdown = 2;
        UpdateOverlayState();
        SetTimer(g_hWnd, TIMER_OVERLAY_FOCUS, 250, NULL);
        RefreshOverlay();
    }

    DispatchUIEvent("onArmState", "{\"armed\": true}");
}

void SequenceRelease(bool force = false) {
    if (g_IsArmed && !force) {
        ULONGLONG elapsed = GetTickCount64() - g_ArmedAtTick;
        if (elapsed < MIN_HOLD_MS) {
            SetTimer(g_hWnd, TIMER_MIN_HOLD, (UINT)(MIN_HOLD_MS - elapsed), NULL);
            return;
        }
    }

    KillTimer(g_hWnd, TIMER_MIN_HOLD);
    RemoveLockdownRules();
    g_SessionWhitelist.clear();
    g_IsArmed = false;

    KillTimer(g_hWnd, TIMER_AUTO_RELEASE);
    KillTimer(g_hWnd, TIMER_OVERLAY_FOCUS);
    g_OverlayCountdown = 0;
    g_GamePids.clear();
    if (g_hOverlay) ShowWindow(g_hOverlay, SW_HIDE);

    DispatchUIEvent("onArmState", "{\"armed\": false}");
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0; 
        break;

    case WM_TIMER:
        if (wParam == TIMER_AUTO_RELEASE) SequenceRelease();
        else if (wParam == TIMER_MIN_HOLD) SequenceRelease();
        else if (wParam == TIMER_OVERLAY_FOCUS) {
            if (g_IsArmed && g_OverlayEnabled) {
                if (g_OverlayCountdown > 0) {
                    ULONGLONG elapsed = GetTickCount64() - g_ArmedAtTick;
                    int remaining = 2 - (int)(elapsed / 1000);
                    if (remaining < 0) remaining = 0;
                    if (remaining != g_OverlayCountdown) { g_OverlayCountdown = remaining; UpdateOverlayState(); }
                }
                RefreshOverlay();
            }
        }
        break;

    case WM_APP_HOTKEY:
        if (wParam == 1) { if (g_BlockKey == g_UnblockKey) { if (g_IsArmed) SequenceRelease(); else SequenceEngage(); } else SequenceEngage(); }
        else if (wParam == 2) SequenceRelease();
        break;

    case WM_APP_UI_EVENT: {
        string* script = reinterpret_cast<string*>(lParam);
        if (script) {
            if (g_pWebView) g_pWebView->ExecuteScript(wstring(script->begin(), script->end()).c_str(), nullptr);
            delete script;
        }
        break;
    }

    case WM_SIZE:
        if (g_pWebViewController != nullptr) {
            RECT bounds; GetClientRect(hWnd, &bounds);
            g_pWebViewController->put_Bounds(bounds);
        }
        break;

    case WM_DESTROY:
        SequenceRelease(true);
        UnregisterAppHotkeys();
        PostQuitMessage(0);
        break;

    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 3 && wcscmp(argv[1], L"--shadow") == 0) {
        DWORD hostPid = _wtoi(argv[2]); LocalFree(argv);
        TheWatcher(hostPid); return 0;
    }
    LocalFree(argv);

    if (!IsUserAdmin()) { ElevateAndExit(); return 0; }
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "SafeExit_FC27_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(hMutex); return 0; }

    LoadSettings();

    CoInitializeEx(0, COINIT_APARTMENTTHREADED);

    if (!VerifyConnectivity()) {
        MessageBoxW(NULL, L"No internet connection was detected.\n\nSafe Exit requires an active connection to run.", L"Safe Exit", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex); CoUninitialize(); return 0;
    }
    if (!EnsureFirewallEnabled()) {
        MessageBoxW(NULL, L"Windows Firewall could not be enabled.\n\nSafe Exit requires the firewall to be turned on. Enable it manually and try again.", L"Safe Exit", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex); CoUninitialize(); return 0;
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput; ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    DeployWatcher();
    InitOverlay(hInstance);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc; wc.hInstance = hInstance;
    wc.lpszClassName = L"SafeExitHost";
    wc.hbrBackground = CreateSolidBrush(RGB(4, 5, 7));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    RegisterClassExW(&wc);

    int winW = 800, winH = 580; 
    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"SAFE EXIT",
        WS_OVERLAPPEDWINDOW, 
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH, NULL, NULL, hInstance, NULL);

    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(g_hWnd, &margins);
    ApplyTransparency();

    int xPos = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int yPos = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;
    SetWindowPos(g_hWnd, g_AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, xPos, yPos, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);

    ShowWindow(g_hWnd, nCmdShow); UpdateWindow(g_hWnd);
    RegisterAppHotkeys();

    std::wstring userDataFolder;
    {
        wchar_t appData[MAX_PATH] = { 0 };
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH) > 0) {
            userDataFolder = std::wstring(appData) + L"\\Safe Exit";
            CreateDirectoryW(userDataFolder.c_str(), nullptr);
        }
    }

    CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.empty() ? nullptr : userDataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                env->CreateCoreWebView2Controller(g_hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (controller) { g_pWebViewController = controller; g_pWebViewController->get_CoreWebView2(&g_pWebView); }

                            ComPtr<ICoreWebView2Settings> settings; g_pWebView->get_Settings(&settings);
                            settings->put_AreDevToolsEnabled(FALSE); settings->put_AreDefaultContextMenusEnabled(FALSE);
                            settings->put_IsZoomControlEnabled(FALSE); settings->put_IsStatusBarEnabled(FALSE);

                            RECT bounds; GetClientRect(g_hWnd, &bounds);
                            g_pWebViewController->put_Bounds(bounds);

                            g_pWebView->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2* webview, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR messageRaw; args->TryGetWebMessageAsString(&messageRaw);
                                        string msg = WideToAcp(messageRaw);

                                        if (msg.find("\"action\":\"engage\"") != string::npos) SequenceEngage();
                                        else if (msg.find("\"action\":\"release\"") != string::npos) SequenceRelease();
                                        else if (msg.find("\"action\":\"browse\"") != string::npos) BrowseForGame();
                                        else if (msg.find("\"action\":\"close\"") != string::npos) DestroyWindow(g_hWnd);
                                        else if (msg.find("\"action\":\"focus\"") != string::npos) FocusMainWindow();
                                        else if (msg.find("\"action\":\"minimize\"") != string::npos) ShowWindow(g_hWnd, SW_MINIMIZE);
                                        else if (msg.find("\"action\":\"drag\"") != string::npos) {
                                            ReleaseCapture(); SendMessage(g_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                                        }
                                        else if (msg.find("\"action\":\"listen_start\"") != string::npos) g_IsBindingKey = true;
                                        else if (msg.find("\"action\":\"set_hotkeys\"") != string::npos) {
                                            smatch m1, m2;
                                            if (regex_search(msg, m1, regex("\"block\":(\\d+)"))) { string val = m1[1].str(); if (val != "null") g_BlockKey = stoi(val); }
                                            if (regex_search(msg, m2, regex("\"unblock\":(\\d+)"))) { string val = m2[1].str(); if (val != "null") g_UnblockKey = stoi(val); }
                                            g_IsBindingKey = false; SaveSettings();
                                        }
                                        else if (msg.find("\"action\":\"set_always_on_top\"") != string::npos) {
                                            g_AlwaysOnTop = msg.find("\"value\":true") != string::npos;
                                            SetWindowPos(g_hWnd, g_AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                                            SaveSettings();
                                        }
                                        else if (msg.find("\"action\":\"set_overlay\"") != string::npos) {
                                            g_OverlayEnabled = msg.find("\"value\":true") != string::npos; SaveSettings();
                                            if (g_OverlayEnabled && g_IsArmed) { SetTimer(g_hWnd, TIMER_OVERLAY_FOCUS, 250, NULL); RefreshOverlay(); }
                                            else { KillTimer(g_hWnd, TIMER_OVERLAY_FOCUS); if (g_hOverlay) ShowWindow(g_hOverlay, SW_HIDE); }
                                        }
                                        else if (msg.find("\"action\":\"set_timeout_enabled\"") != string::npos) {
                                            g_AutoReleaseEnabled = msg.find("\"value\":true") != string::npos; SaveSettings();
                                        }
                                        else if (msg.find("\"action\":\"set_timeout\"") != string::npos) {
                                            smatch m; if (regex_search(msg, m, regex("\"value\":(\\d+)"))) g_AutoReleaseSeconds = stoi(m[1].str());
                                            SaveSettings();
                                        }
                                        else if (msg.find("\"action\":\"set_transparency\"") != string::npos) {
                                            smatch m; if (regex_search(msg, m, regex("\"value\":(\\d+)"))) g_TransparencyLevel = stoi(m[1].str());
                                            ApplyTransparency(); SaveSettings();
                                        }
                                        else if (msg.find("\"action\":\"ready\"") != string::npos) {
                                            string payload = "{\"bKey\":\"" + VKToEng(g_BlockKey) + "\",\"uKey\":\"" + VKToEng(g_UnblockKey) + "\",\"top\":" + (g_AlwaysOnTop ? "true" : "false") + ",\"over\":" + (g_OverlayEnabled ? "true" : "false") + ",\"timeE\":" + (g_AutoReleaseEnabled ? "true" : "false") + ",\"timeS\":" + to_string(g_AutoReleaseSeconds) + ",\"trans\":" + to_string(g_TransparencyLevel) + "}";
                                            DispatchUIEvent("onLoadSettings", payload);
                                            StartTargetMonitor();
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

                            string html = GetDecryptedHTML();
                            g_pWebView->NavigateToString(wstring(html.begin(), html.end()).c_str());
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    Gdiplus::GdiplusShutdown(gdiplusToken); CloseHandle(hMutex); CoUninitialize();
    return (int)msg.wParam;
}