// overlay_lua.cpp - x64 DLL: Page Up menu + Lua script loader
// Build: Visual Studio / CMake, x64, Unicode, Dynamic Library (DLL)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <atomic>
#include <string>
#include <fstream>
#include <sstream>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace {
    constexpr int ID_BTN_RUN = 1001;
    constexpr int ID_EDT_LOG = 1002;
    constexpr wchar_t kClassName[] = L"DiagOverlayWnd";

    HMODULE           g_hModule = nullptr;
    HWND              g_hWnd    = nullptr;
    HWND              g_hLog    = nullptr;
    HANDLE            g_hThread = nullptr;
    std::atomic<bool> g_running{ true };
    bool              g_visible = false;
    lua_State*        g_L       = nullptr;
}

// ---------------------------------------------------------------------------
// String helpers and log (menu text box + debugger output)
// ---------------------------------------------------------------------------
static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static void Log(const std::wstring& text)
{
    OutputDebugStringW((text + L"\n").c_str());
    if (!g_hLog) return;

    std::wstring line = text + L"\r\n";
    int len = GetWindowTextLengthW(g_hLog);
    SendMessageW(g_hLog, EM_SETSEL, len, len);
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
}

static void LogUtf8(const std::string& s) { Log(Utf8ToWide(s)); }

// ---------------------------------------------------------------------------
// Lua
// ---------------------------------------------------------------------------
static int l_print(lua_State* L)
{
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) out += '\t';
        out.append(s, len);
        lua_pop(L, 1);
    }
    LogUtf8(out);
    return 0;
}

static bool ReadFileToString(const std::wstring& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static void RunLuaScript(const std::wstring& path)
{
    if (g_L) { lua_close(g_L); g_L = nullptr; }

    g_L = luaL_newstate();
    if (!g_L) { Log(L"[Lua] Failed to create Lua state"); return; }
    luaL_openlibs(g_L);
    lua_register(g_L, "print", l_print);

    Log(L"[Lua] Running: " + path);

    std::string code;
    if (!ReadFileToString(path, code)) {
        Log(L"[Lua] Could not read file");
        return;
    }

    // Equivalent of luaL_dofile: load + protected call
    std::string chunk = "@" + WideToUtf8(path);
    int rc = luaL_loadbuffer(g_L, code.data(), code.size(), chunk.c_str());
    if (rc == LUA_OK) rc = lua_pcall(g_L, 0, LUA_MULTRET, 0);

    if (rc != LUA_OK) {
        const char* err = lua_tostring(g_L, -1);
        Log(L"[Lua][ERROR] " + Utf8ToWide(err ? err : "unknown error"));
        lua_pop(g_L, 1);
    } else {
        Log(L"[Lua] Script finished successfully");
    }
}

// ---------------------------------------------------------------------------
// File dialog
// ---------------------------------------------------------------------------
static void PickAndRunScript(HWND owner)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"Lua scripts (*.lua)\0*.lua\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Select a Lua script";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn))
        RunLuaScript(file);
}

// ---------------------------------------------------------------------------
// Menu window
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HWND btn = CreateWindowExW(0, L"BUTTON", L"Выбрать и запустить Lua-скрипт",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 360, 30, hWnd, (HMENU)(INT_PTR)ID_BTN_RUN, g_hModule, nullptr);
        SendMessageW(btn, WM_SETFONT, (WPARAM)font, TRUE);

        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
            ES_READONLY | ES_AUTOVSCROLL,
            10, 50, 360, 200, hWnd, (HMENU)(INT_PTR)ID_EDT_LOG, g_hModule, nullptr);
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)font, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_BTN_RUN && HIWORD(wp) == BN_CLICKED)
            PickAndRunScript(hWnd);
        return 0;

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        g_visible = false;
        return 0;

    case WM_DESTROY:
        g_hLog = nullptr;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

struct FindCtx { DWORD pid; HWND self; HWND result; };

static BOOL CALLBACK EnumProc(HWND h, LPARAM lp)
{
    auto* c = (FindCtx*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == c->pid && h != c->self && IsWindowVisible(h) &&
        GetWindow(h, GW_OWNER) == nullptr) {
        c->result = h;
        return FALSE;
    }
    return TRUE;
}

static HWND FindAppWindow()
{
    FindCtx c{ GetCurrentProcessId(), g_hWnd, nullptr };
    EnumWindows(EnumProc, (LPARAM)&c);
    return c.result;
}

static void ToggleMenu()
{
    g_visible = !g_visible;
    if (g_visible) {
        RECT r{};
        HWND app = FindAppWindow();
        int x = 100, y = 100;
        if (app && GetWindowRect(app, &r)) { x = r.left + 30; y = r.top + 30; }
        SetWindowPos(g_hWnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_hWnd);
    } else {
        ShowWindow(g_hWnd, SW_HIDE);
    }
}

// ---------------------------------------------------------------------------
// Worker thread: window + message loop + key polling
// ---------------------------------------------------------------------------
static DWORD WINAPI UiThread(LPVOID)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = g_hModule;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    RECT rc = { 0, 0, 380, 260 };
    DWORD style = WS_CAPTION;
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    g_hWnd = CreateWindowExW(exStyle, kClassName, L"Diagnostics Overlay", style,
        100, 100, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, g_hModule, nullptr);
    if (!g_hWnd) return 1;

    bool prevDown = false;
    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        bool down = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;   // Page Up
        if (down && !prevDown) {
            HWND fg = GetForegroundWindow();
            DWORD fgPid = 0;
            if (fg) GetWindowThreadProcessId(fg, &fgPid);
            if (fgPid == GetCurrentProcessId())
                ToggleMenu();
        }
        prevDown = down;

        Sleep(10);
    }

    if (g_L) { lua_close(g_L); g_L = nullptr; }
    DestroyWindow(g_hWnd);
    UnregisterClassW(kClassName, g_hModule);
    return 0;
}

// ---------------------------------------------------------------------------
// DLL entry point
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        g_hThread = CreateThread(nullptr, 0, UiThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        if (!reserved) g_running = false;
        break;
    }
    return TRUE;
}
