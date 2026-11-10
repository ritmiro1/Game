// LoopDesk - live video wallpaper for Windows 10/11.
// Written from scratch. Uses only Windows system APIs (Win32, GDI, Media Foundation, waveOut).
// No third-party code or assets. MIT License.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <initguid.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <wtsapi32.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "resampler.h"

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

// ------------------------------------------------------------------ constants
static const wchar_t* APP_NAME = L"LoopDesk";
static const wchar_t* MAIN_CLASS = L"LoopDeskMain";
static const wchar_t* WALL_CLASS = L"LoopDeskWall";
static const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

enum : UINT {
    WM_APP_TRAY = WM_APP + 1,
    WM_APP_THUMB,
    WM_APP_STATE,
    WM_APP_SHOW,
};

enum {
    ID_OPEN = 1, ID_PAUSE, ID_STOP, ID_OK, ID_REMOVE,
    ID_FIT0 = 10, ID_FIT1, ID_FIT2,
    ID_AUTORUN = 20, ID_FSPAUSE, ID_ECOPAUSE,
    ID_VOL = 30, ID_SPEED_DOWN, ID_SPEED_UP,
    ID_TILE = 1000,
};
enum { IC_NONE, IC_PLUS, IC_PLAY, IC_PAUSE, IC_STOP };
enum { MENU_SHOW = 1, MENU_PAUSE, MENU_STOP, MENU_EXIT };

static const COLORREF C_BG = RGB(27, 31, 38), C_TOP = RGB(19, 22, 28), C_PANEL = RGB(33, 38, 47),
                      C_TILE = RGB(44, 50, 61), C_CAP = RGB(16, 19, 24), C_LINE = RGB(55, 62, 76),
                      C_TEXT = RGB(233, 237, 242), C_DIM = RGB(139, 149, 165), C_ACC = RGB(46, 128, 230),
                      C_ACC_H = RGB(72, 150, 245), C_BTN = RGB(48, 55, 68), C_BTN_H = RGB(62, 71, 87),
                      C_BOOST = RGB(240, 150, 50), C_TRACK = RGB(58, 66, 80), C_INPUT = RGB(40, 46, 57),
                      C_WARN = RGB(245, 170, 80);

// ------------------------------------------------------------------ data
struct Item {
    std::wstring path, name;
    int volume = 100;  // 0..500 (%)
    int fit = 0;       // 0 fill (cover), 1 fit (letterbox), 2 stretch
    int speed = 100;   // 50..200 percent
    HBITMAP thumb = nullptr;
    int tw = 0, th = 0;
    int vw = 0, vh = 0;
    double dur = 0;
    unsigned long long bytes = 0;
};
struct ThumbRes {
    std::wstring path;
    HBITMAP bmp = nullptr;
    int w = 0, h = 0, vw = 0, vh = 0;
    double dur = 0;
    unsigned long long bytes = 0;
};
struct Hit { RECT r; int id; };
struct DesktopInfo { HWND progman = nullptr, defview = nullptr, worker = nullptr; bool raised = false; };

static HINSTANCE g_hi;
static HWND g_main = nullptr, g_edit = nullptr;
static std::atomic<HWND> g_wall{nullptr};
static HWND g_progman = nullptr;
static bool g_raised = false;
static UINT g_dpi = 96;
static CRITICAL_SECTION g_cs;
static HFONT g_fUI, g_fBold, g_fTitle, g_fSmall;
static HBRUSH g_brInput;
static HICON g_icon, g_iconSm;
static UINT g_msgTaskbar = 0;

static std::vector<Item> g_items;
static int g_sel = -1, g_active = -1;
static std::vector<Hit> g_hits;
static int g_hot = 0, g_scroll = 0, g_maxScroll = 0;
static bool g_dragVol = false, g_tracking = false;
static RECT g_volTrack = {}, g_gridRc = {}, g_panelRc = {};
static std::wstring g_filter;
static bool g_optFsPause = true, g_optAutorun = false, g_mfOk = false;
static bool g_optEcoPause = false, g_sessionLocked = false, g_displayOff = false;
static HPOWERNOTIFY g_displayNotify = nullptr;
// GUID_CONSOLE_DISPLAY_STATE (0 = off, 1 = on, 2 = dimmed).
static const GUID kConsoleDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

static std::vector<std::wstring> g_jobs;  // guarded by g_cs
static HANDLE g_jobEvt;

// player shared state
static std::atomic<bool> p_quit{false}, p_userPause{false}, p_autoPause{false}, p_repaint{false};
static std::atomic<int> p_volume{100}, p_fit{0}, p_layoutGen{0}, p_speed{100};
static std::atomic<bool> p_mute{false};
static HANDLE p_thread = nullptr;
static std::wstring p_path;
static std::vector<RECT> g_mon;  // monitor rects in wallpaper-window coords, guarded by g_cs
static std::wstring p_err;       // guarded by g_cs

// Media Foundation entry points are resolved at run time so the program can start
// (and explain the problem) even on systems without the media feature pack.
typedef HRESULT(WINAPI* FnMFStartup)(ULONG, DWORD);
typedef HRESULT(WINAPI* FnMFShutdown)();
typedef HRESULT(WINAPI* FnMFCreateAttributes)(IMFAttributes**, UINT32);
typedef HRESULT(WINAPI* FnMFCreateMediaType)(IMFMediaType**);
typedef HRESULT(WINAPI* FnMFCreateReader)(LPCWSTR, IMFAttributes*, IMFSourceReader**);
static FnMFStartup fMFStartup;
static FnMFShutdown fMFShutdown;
static FnMFCreateAttributes fMFCreateAttributes;
static FnMFCreateMediaType fMFCreateMediaType;
static FnMFCreateReader fMFCreateReader;

static const GUID kTimeFormatDefault = {};  // all zeros = 100 ns units
static const DWORD SR_VIDEO = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
static const DWORD SR_AUDIO = (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM;
static const DWORD SR_ALL = (DWORD)MF_SOURCE_READER_ALL_STREAMS;
static const DWORD SR_SOURCE = (DWORD)MF_SOURCE_READER_MEDIASOURCE;

// ------------------------------------------------------------------ helpers
static int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

static std::wstring U8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string WToU8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring ConfigDir() {
    wchar_t b[MAX_PATH] = L"";
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, b);
    std::wstring d = std::wstring(b) + L"\\LoopDesk";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
static void Log(const wchar_t* fmt, ...) {
    wchar_t b[900];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(b, 899, fmt, ap);
    va_end(ap);
    b[899] = 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t l[1000];
    _snwprintf(l, 999, L"%02d:%02d:%02d.%03d %ls\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, b);
    l[999] = 0;
    std::string u = WToU8(l);
    EnterCriticalSection(&g_cs);
    HANDLE f = CreateFileW((ConfigDir() + L"\\log.txt").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        WriteFile(f, u.data(), (DWORD)u.size(), &n, nullptr);
        CloseHandle(f);
    }
    LeaveCriticalSection(&g_cs);
}
static std::wstring BaseName(const std::wstring& p) {
    size_t k = p.find_last_of(L"\\/");
    std::wstring n = (k == std::wstring::npos) ? p : p.substr(k + 1);
    size_t d = n.find_last_of(L'.');
    if (d != std::wstring::npos && d > 0) n = n.substr(0, d);
    return n;
}
static bool ContainsNoCase(std::wstring hay, std::wstring needle) {
    if (needle.empty()) return true;
    if (hay.empty()) return false;
    CharUpperBuffW(&hay[0], (DWORD)hay.size());
    CharUpperBuffW(&needle[0], (DWORD)needle.size());
    return hay.find(needle) != std::wstring::npos;
}
static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static double NowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
static UINT WindowDpi(HWND h) {
    typedef UINT(WINAPI * Fn)(HWND);
    static Fn fn = (Fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT d = fn ? fn(h) : 0;
    if (!d) {
        HDC dc = GetDC(nullptr);
        d = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
    }
    return d ? d : 96;
}
static bool LoadMF() {
    HMODULE a = LoadLibraryW(L"mfplat.dll"), b = LoadLibraryW(L"mfreadwrite.dll");
    if (!a || !b) return false;
    fMFStartup = (FnMFStartup)(void*)GetProcAddress(a, "MFStartup");
    fMFShutdown = (FnMFShutdown)(void*)GetProcAddress(a, "MFShutdown");
    fMFCreateAttributes = (FnMFCreateAttributes)(void*)GetProcAddress(a, "MFCreateAttributes");
    fMFCreateMediaType = (FnMFCreateMediaType)(void*)GetProcAddress(a, "MFCreateMediaType");
    fMFCreateReader = (FnMFCreateReader)(void*)GetProcAddress(b, "MFCreateSourceReaderFromURL");
    if (!fMFStartup || !fMFShutdown || !fMFCreateAttributes || !fMFCreateMediaType || !fMFCreateReader) return false;
    return SUCCEEDED(fMFStartup(MF_VERSION, MFSTARTUP_FULL));
}

// ------------------------------------------------------------------ config
static void SaveConfig() {
    std::string s = "version=1\r\n";
    s += std::string("fspause=") + (g_optFsPause ? "1" : "0") + "\r\n";
    s += std::string("ecopause=") + (g_optEcoPause ? "1" : "0") + "\r\n";
    s += "active=" + (g_active >= 0 ? WToU8(g_items[(size_t)g_active].path) : std::string()) + "\r\n";
    s += "selected=" + (g_sel >= 0 ? WToU8(g_items[(size_t)g_sel].path) : std::string()) + "\r\n";
    for (auto& it : g_items) {
        s += "item=" + std::to_string(it.volume) + "|" + std::to_string(it.fit) + "|" + WToU8(it.path) + "\r\n";
        s += "speed=" + std::to_string(it.speed) + "|" + WToU8(it.path) + "\r\n";
    }
    HANDLE f = CreateFileW((ConfigDir() + L"\\config.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD n = 0;
    WriteFile(f, s.data(), (DWORD)s.size(), &n, nullptr);
    CloseHandle(f);
}
static void QueueThumb(const std::wstring& p) {
    EnterCriticalSection(&g_cs);
    g_jobs.push_back(p);
    LeaveCriticalSection(&g_cs);
    SetEvent(g_jobEvt);
}
static int FindItem(const std::wstring& p) {
    for (size_t i = 0; i < g_items.size(); i++)
        if (!lstrcmpiW(g_items[i].path.c_str(), p.c_str())) return (int)i;
    return -1;
}
static int AddItem(const std::wstring& p, int vol = 100, int fit = 0) {
    int k = FindItem(p);
    if (k >= 0) return k;
    Item it;
    it.path = p;
    it.name = BaseName(p);
    it.volume = std::clamp(vol, 0, 500);
    it.fit = std::clamp(fit, 0, 2);
    g_items.push_back(it);
    QueueThumb(p);
    return (int)g_items.size() - 1;
}
static std::wstring LoadConfig() {  // returns path of the wallpaper that was active
    std::wstring active, selected;
    HANDLE f = CreateFileW((ConfigDir() + L"\\config.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return active;
    std::string s;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(f, buf, sizeof(buf), &n, nullptr) && n) s.append(buf, n);
    CloseHandle(f);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\n', pos);
        if (e == std::string::npos) e = s.size();
        std::string line = s.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t q = line.find('=');
        if (q == std::string::npos) continue;
        std::string key = line.substr(0, q), val = line.substr(q + 1);
        if (key == "fspause") g_optFsPause = (val != "0");
        else if (key == "ecopause") g_optEcoPause = (val != "0");
        else if (key == "active") active = U8ToW(val);
        else if (key == "selected") selected = U8ToW(val);
        else if (key == "speed") {
            size_t a = val.find('|');
            if (a != std::string::npos) {
                int k = FindItem(U8ToW(val.substr(a + 1)));
                if (k >= 0) g_items[(size_t)k].speed = std::clamp(atoi(val.substr(0, a).c_str()), 50, 200);
            }
        } else if (key == "item") {
            size_t a = val.find('|'), b = (a == std::string::npos) ? a : val.find('|', a + 1);
            if (b == std::string::npos) continue;
            std::wstring p = U8ToW(val.substr(b + 1));
            if (!p.empty()) AddItem(p, atoi(val.substr(0, a).c_str()), atoi(val.substr(a + 1, b - a - 1).c_str()));
        }
    }
    g_sel = selected.empty() ? -1 : FindItem(selected);
    return active;
}
static bool AutorunGet() {
    HKEY k;
    bool on = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExW(k, APP_NAME, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}
static void AutorunSet(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(exe) + L"\" -tray";
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, APP_NAME);
    }
    RegCloseKey(k);
}

// ------------------------------------------------------------------ desktop attachment
// The video window is placed behind the desktop icons. Two desktop layouts exist:
//  * classic: after message 0x052C the shell creates a top-level "WorkerW" behind the icon host;
//    our window becomes a child of that WorkerW.
//  * "raised" desktop (Windows 11 24H2+): Progman has WS_EX_NOREDIRECTIONBITMAP and hosts both the
//    icon view and a WorkerW as children; our window must be a layered child of Progman placed
//    between the two.
static BOOL CALLBACK EnumTop(HWND top, LPARAM lp) {
    DesktopInfo* d = (DesktopInfo*)lp;
    HWND dv = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
    if (dv) {
        HWND w = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
        if (w) { d->defview = dv; d->worker = w; }
    }
    return TRUE;
}
static bool FindDesktop(DesktopInfo& d) {
    d = DesktopInfo();
    d.progman = FindWindowW(L"Progman", nullptr);
    if (!d.progman) return false;
    d.raised = (GetWindowLongPtrW(d.progman, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0;
    DWORD_PTR res = 0;
    SendMessageTimeoutW(d.progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &res);
    if (d.raised) {
        d.defview = FindWindowExW(d.progman, nullptr, L"SHELLDLL_DefView", nullptr);
        d.worker = FindWindowExW(d.progman, nullptr, L"WorkerW", nullptr);
        if (d.defview) return true;
        d.raised = false;
    }
    EnumWindows(EnumTop, (LPARAM)&d);
    if (!d.worker) {
        SendMessageTimeoutW(d.progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &res);
        EnumWindows(EnumTop, (LPARAM)&d);
    }
    if (!d.worker) d.worker = FindWindowExW(d.progman, nullptr, L"WorkerW", nullptr);
    return d.worker != nullptr;
}
static BOOL CALLBACK MonEnum(HMONITOR, HDC, LPRECT rc, LPARAM lp) {
    ((std::vector<RECT>*)lp)->push_back(*rc);
    return TRUE;
}
static void UpdateMonitorRects() {
    HWND w = g_wall.load();
    if (!w) return;
    std::vector<RECT> v;
    EnumDisplayMonitors(nullptr, nullptr, MonEnum, (LPARAM)&v);
    for (auto& r : v) MapWindowPoints(nullptr, w, (POINT*)&r, 2);
    if (v.empty()) {
        RECT r;
        GetClientRect(w, &r);
        v.push_back(r);
    }
    EnterCriticalSection(&g_cs);
    g_mon = v;
    LeaveCriticalSection(&g_cs);
    p_layoutGen++;
}
static bool AttachWall() {
    DesktopInfo d;
    bool ok = false;
    for (int i = 0; i < 8 && !ok; i++) {
        ok = FindDesktop(d);
        if (!ok) Sleep(120);
    }
    if (!ok) {
        Log(L"desktop host not found (progman=%p)", (void*)d.progman);
        return false;
    }
    HWND parent = d.raised ? d.progman : d.worker;
    HWND w = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (d.raised ? WS_EX_LAYERED : 0), WALL_CLASS, L"",
                             WS_POPUP, 0, 0, 32, 32, nullptr, nullptr, g_hi, nullptr);
    if (!w) {
        Log(L"CreateWindowEx(wall) failed %lu", GetLastError());
        return false;
    }
    if (d.raised) SetLayeredWindowAttributes(w, 0, 255, LWA_ALPHA);
    EnableWindow(w, FALSE);
    LONG_PTR st = GetWindowLongPtrW(w, GWL_STYLE);
    SetWindowLongPtrW(w, GWL_STYLE, (st & ~(LONG_PTR)WS_POPUP) | WS_CHILD);
    if (!SetParent(w, parent)) {
        Log(L"SetParent failed %lu", GetLastError());
        DestroyWindow(w);
        return false;
    }
    RECT pr = {};
    GetClientRect(parent, &pr);
    if (pr.right <= 0 || pr.bottom <= 0) {
        pr.right = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        pr.bottom = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }
    const UINT fl = SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED;
    if (d.raised) {
        SetWindowPos(w, d.defview, 0, 0, pr.right, pr.bottom, fl);
        if (d.worker) SetWindowPos(d.worker, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        SetWindowPos(w, HWND_TOP, 0, 0, pr.right, pr.bottom, fl);
    }
    g_progman = d.progman;
    g_raised = d.raised;
    g_wall = w;
    Log(L"attached: raised=%d progman=%p defview=%p worker=%p size=%ldx%ld", (int)d.raised, (void*)d.progman,
        (void*)d.defview, (void*)d.worker, pr.right, pr.bottom);
    UpdateMonitorRects();
    return true;
}
static void KeepZOrder() {
    HWND w = g_wall.load();
    if (!g_raised || !w || !g_progman) return;
    const UINT fl = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    HWND dv = FindWindowExW(g_progman, nullptr, L"SHELLDLL_DefView", nullptr);
    if (dv && GetWindow(dv, GW_HWNDNEXT) != w) SetWindowPos(w, dv, 0, 0, 0, 0, fl);
    HWND ww = FindWindowExW(g_progman, nullptr, L"WorkerW", nullptr);
    if (ww && GetWindow(ww, GW_HWNDNEXT) != nullptr) SetWindowPos(ww, HWND_BOTTOM, 0, 0, 0, 0, fl);
}
static LRESULT CALLBACK WallProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        p_repaint = true;
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        if (g_wall.load() == h) g_wall = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ------------------------------------------------------------------ audio output (waveOut, 16-bit PCM)
struct WaveBlock {
    WAVEHDR hdr;
    std::vector<char> data;
    bool used = false;
};
struct AudioOut {
    HWAVEOUT h = nullptr;
    int rate = 0, ch = 0;
    std::vector<WaveBlock*> blocks;

    bool Open(int r, int c) {
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = (WORD)c;
        wf.nSamplesPerSec = (DWORD)r;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = (WORD)(c * 2);
        wf.nAvgBytesPerSec = (DWORD)(r * c * 2);
        if (waveOutOpen(&h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            h = nullptr;
            return false;
        }
        rate = r;
        ch = c;
        return true;
    }
    void Reap() {
        for (auto b : blocks)
            if (b->used && (b->hdr.dwFlags & WHDR_DONE)) {
                waveOutUnprepareHeader(h, &b->hdr, sizeof(WAVEHDR));
                b->used = false;
            }
    }
    void Write(const short* pcm, size_t frames) {
        if (!h || !frames) return;
        Reap();
        WaveBlock* b = nullptr;
        for (auto x : blocks)
            if (!x->used) { b = x; break; }
        if (!b) {
            b = new WaveBlock();
            blocks.push_back(b);
        }
        size_t bytes = frames * (size_t)ch * 2;
        b->data.assign((const char*)pcm, (const char*)pcm + bytes);
        memset(&b->hdr, 0, sizeof(WAVEHDR));
        b->hdr.lpData = b->data.data();
        b->hdr.dwBufferLength = (DWORD)bytes;
        if (waveOutPrepareHeader(h, &b->hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return;
        if (waveOutWrite(h, &b->hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) b->used = true;
        else waveOutUnprepareHeader(h, &b->hdr, sizeof(WAVEHDR));
    }
    void Reset(bool keepPaused) {
        if (!h) return;
        waveOutReset(h);
        for (auto b : blocks)
            if (b->used) {
                waveOutUnprepareHeader(h, &b->hdr, sizeof(WAVEHDR));
                b->used = false;
            }
        if (keepPaused) waveOutPause(h);
        else waveOutRestart(h);
    }
    void Pause(bool p) {
        if (!h) return;
        if (p) waveOutPause(h);
        else waveOutRestart(h);
    }
    void Close() {
        if (h) {
            Reset(false);
            waveOutClose(h);
            h = nullptr;
        }
        for (auto b : blocks) delete b;
        blocks.clear();
    }
};

// ------------------------------------------------------------------ player
static void PlayerFail(const wchar_t* what, HRESULT hr) {
    wchar_t b[300];
    _snwprintf(b, 299, L"%ls (0x%08X)", what, (unsigned)hr);
    b[299] = 0;
    EnterCriticalSection(&g_cs);
    p_err = b;
    LeaveCriticalSection(&g_cs);
    Log(L"player: %ls", b);
    PostMessageW(g_main, WM_APP_STATE, 0, 0);
}
static inline float Limit(float x) {  // linear up to 0.8, then a smooth knee that never exceeds 1.0
    float a = fabsf(x);
    if (a <= 0.8f) return x;
    float y = 0.8f + 0.2f * tanhf((a - 0.8f) / 0.2f);
    return x < 0 ? -y : y;
}

struct Player {
    IMFSourceReader* rd = nullptr;
    // video
    UINT32 fw = 0, fh = 0;
    LONG stride = 0;
    RECT vis = {};
    HDC mem = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ oldBmp = nullptr;
    BYTE* bits = nullptr;
    UINT32 dw = 0, dh = 0;
    bool haveFrame = false;
    std::vector<RECT> mons;
    int layoutSeen = -1;
    // audio
    bool hasAudio = false, aFloat = false;
    int aRate = 0, aCh = 0, aBits = 16, outCh = 2;
    AudioOut out;
    std::vector<short> pcm;
    std::vector<float> inputPcm, resampled;
    SpeedResampler resampler{2};
    float smoothedGain = 1.0f;
    double audioSourceFrames = 0;
    bool noScale = false;
    long long shown = 0;

    void LoadLayout() {
        EnterCriticalSection(&g_cs);
        mons = g_mon;
        LeaveCriticalSection(&g_cs);
        layoutSeen = p_layoutGen.load();
    }
    bool QueryVideoType() {
        IMFMediaType* cur = nullptr;
        if (FAILED(rd->GetCurrentMediaType(SR_VIDEO, &cur)) || !cur) return false;
        UINT64 fs = 0;
        cur->GetUINT64(MF_MT_FRAME_SIZE, &fs);
        fw = (UINT32)(fs >> 32);
        fh = (UINT32)fs;
        UINT32 st = 0;
        stride = SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st)) ? (LONG)st : (LONG)(fw * 4);
        vis.left = 0; vis.top = 0; vis.right = (LONG)fw; vis.bottom = (LONG)fh;
        MFVideoArea area = {};
        UINT32 got = 0;
        if (SUCCEEDED(cur->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&area, sizeof(area), &got)) &&
            got == sizeof(area) && area.Area.cx > 0 && area.Area.cy > 0) {
            LONG x = area.OffsetX.value, y = area.OffsetY.value;
            if (x >= 0 && y >= 0 && x + area.Area.cx <= (LONG)fw && y + area.Area.cy <= (LONG)fh) {
                vis.left = x; vis.top = y; vis.right = x + area.Area.cx; vis.bottom = y + area.Area.cy;
            }
        }
        cur->Release();
        if (!fw || !fh || fw > 16384 || fh > 16384) return false;
        if (fw != dw || fh != dh || !bits) {
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = (LONG)fw;
            bi.bmiHeader.biHeight = -(LONG)fh;  // top-down
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            if (!mem) mem = CreateCompatibleDC(nullptr);
            void* pv = nullptr;
            HBITMAP nb = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &pv, nullptr, 0);
            if (!nb || !pv) return false;
            HGDIOBJ o = SelectObject(mem, nb);
            if (!oldBmp) oldBmp = o;
            if (dib) DeleteObject(dib);
            dib = nb;
            bits = (BYTE*)pv;
            dw = fw;
            dh = fh;
            haveFrame = false;
        }
        return true;
    }
    void SetupAudio() {
        hasAudio = false;
        if (FAILED(rd->SetStreamSelection(SR_AUDIO, TRUE))) return;
        IMFMediaType* t = nullptr;
        HRESULT hr = fMFCreateMediaType(&t);
        if (SUCCEEDED(hr)) {
            t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            hr = rd->SetCurrentMediaType(SR_AUDIO, nullptr, t);
            t->Release();
        }
        IMFMediaType* cur = nullptr;
        if (SUCCEEDED(hr) && SUCCEEDED(rd->GetCurrentMediaType(SR_AUDIO, &cur)) && cur) {
            UINT32 r = 0, c = 0, b = 0;
            GUID sub = {};
            cur->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r);
            cur->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &c);
            cur->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &b);
            cur->GetGUID(MF_MT_SUBTYPE, &sub);
            cur->Release();
            aFloat = IsEqualGUID(sub, MFAudioFormat_Float) != 0;
            aRate = (int)r;
            aCh = (int)c;
            aBits = (int)b;
            bool fmtOk = r >= 8000 && r <= 384000 && c >= 1 && c <= 8 &&
                         (aFloat ? b == 32 : (b == 8 || b == 16 || b == 24 || b == 32));
            outCh = 2;  // always stereo so the streaming resampler has a fixed layout
            resampler.reset(p_speed.load() / 100.0);
            smoothedGain = (p_mute.load() ? 0.0f : p_volume.load() / 100.0f);
            if (fmtOk && out.Open((int)r, outCh)) hasAudio = true;
            Log(L"audio: %u Hz, %u ch, %u bit, float=%d, ok=%d", r, c, b, (int)aFloat, (int)hasAudio);
        }
        if (!hasAudio) rd->SetStreamSelection(SR_AUDIO, FALSE);
    }
    bool Open() {
        IMFAttributes* at = nullptr;
        fMFCreateAttributes(&at, 2);
        if (at) at->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        HRESULT hr = fMFCreateReader(p_path.c_str(), at, &rd);
        if (at) at->Release();
        if (FAILED(hr) || !rd) {
            rd = nullptr;
            PlayerFail(L"Не удалось открыть видео", hr);
            return false;
        }
        rd->SetStreamSelection(SR_ALL, FALSE);
        hr = rd->SetStreamSelection(SR_VIDEO, TRUE);
        if (FAILED(hr)) {
            PlayerFail(L"В файле нет видеодорожки", hr);
            return false;
        }
        UINT32 nw = 0, nh = 0;
        IMFMediaType* nt = nullptr;
        if (SUCCEEDED(rd->GetNativeMediaType(SR_VIDEO, 0, &nt)) && nt) {
            UINT64 fs = 0;
            nt->GetUINT64(MF_MT_FRAME_SIZE, &fs);
            nw = (UINT32)(fs >> 32);
            nh = (UINT32)fs;
            nt->Release();
        }
        LoadLayout();
        int bw = 0, bh = 0;  // largest monitor decides the decode size
        for (auto& m : mons) {
            int w = m.right - m.left, h = m.bottom - m.top;
            if ((long long)w * h > (long long)bw * bh) { bw = w; bh = h; }
        }
        UINT32 tw = 0, th = 0;
        if (nw && nh && bw > 0 && bh > 0) {
            double sx = bw / (double)nw, sy = bh / (double)nh;
            double sc = (p_fit.load() == 1) ? std::min(sx, sy) : std::max(sx, sy);
            tw = ((UINT32)(nw * sc + 0.5) + 1) & ~1u;
            th = ((UINT32)(nh * sc + 0.5) + 1) & ~1u;
            if (tw < 16 || th < 16 || tw > 8192 || th > 8192) tw = th = 0;
        }
        bool ok = false;
        for (int pass = ((tw && !noScale) ? 0 : 1); pass < 2 && !ok; pass++) {
            IMFMediaType* t = nullptr;
            hr = fMFCreateMediaType(&t);
            if (FAILED(hr)) break;
            t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (pass == 0) t->SetUINT64(MF_MT_FRAME_SIZE, ((UINT64)tw << 32) | th);
            hr = rd->SetCurrentMediaType(SR_VIDEO, nullptr, t);
            t->Release();
            ok = SUCCEEDED(hr);
            if (!ok) Log(L"video type pass %d rejected 0x%08X", pass, (unsigned)hr);
        }
        if (!ok) {
            PlayerFail(L"Формат видео не поддерживается (нет кодека)", hr);
            return false;
        }
        if (!QueryVideoType()) {
            PlayerFail(L"Не удалось подготовить кадр", E_FAIL);
            return false;
        }
        Log(L"video: native %ux%u, decode %ux%u (asked %ux%u), stride %ld, monitors %d", nw, nh, fw, fh, tw, th,
            stride, (int)mons.size());
        SetupAudio();
        return true;
    }
    void CopyFrame(IMFSample* s) {
        if (!bits) return;
        IMFMediaBuffer* buf = nullptr;
        if (FAILED(s->ConvertToContiguousBuffer(&buf)) || !buf) return;
        const size_t row = (size_t)dw * 4;
        IMF2DBuffer* b2 = nullptr;
        BYTE* p = nullptr;
        LONG pitch = 0;
        bool done = false;
        if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2))) && b2) {
            if (SUCCEEDED(b2->Lock2D(&p, &pitch)) && p) {
                if ((size_t)std::abs(pitch) >= row) {
                    GdiFlush();
                    for (UINT32 y = 0; y < dh; y++) memcpy(bits + y * row, p + (ptrdiff_t)y * pitch, row);
                    done = true;
                }
                b2->Unlock2D();
            }
            b2->Release();
        }
        if (!done) {
            BYTE* d = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(buf->Lock(&d, nullptr, &len)) && d) {
                LONG st = stride;
                if ((size_t)std::abs(st) < row) st = (LONG)row;
                size_t ast = (size_t)std::abs(st);
                UINT32 rows = (UINT32)std::min<size_t>(dh, len / ast);
                GdiFlush();
                if (st > 0) {
                    for (UINT32 y = 0; y < rows; y++) memcpy(bits + y * row, d + y * ast, row);
                } else {
                    for (UINT32 y = 0; y < rows; y++) memcpy(bits + y * row, d + (size_t)(rows - 1 - y) * ast, row);
                }
                buf->Unlock();
            }
        }
        buf->Release();
        haveFrame = true;
    }
    void Draw() {
        if (!haveFrame || !mem) return;
        HWND w = g_wall.load();
        if (!w) return;
        HDC dc = GetDC(w);
        if (!dc) return;
        const int fit = p_fit.load();
        const int ax = vis.left, ay = vis.top, aw = vis.right - vis.left, ah = vis.bottom - vis.top;
        SetStretchBltMode(dc, COLORONCOLOR);
        for (auto& m : mons) {
            int mw = m.right - m.left, mh = m.bottom - m.top;
            if (mw <= 0 || mh <= 0 || aw <= 0 || ah <= 0) continue;
            if (fit == 2) {
                StretchBlt(dc, m.left, m.top, mw, mh, mem, ax, ay, aw, ah, SRCCOPY);
            } else if (fit == 0) {
                double sc = std::max(mw / (double)aw, mh / (double)ah);
                int cw = std::min(aw, (int)(mw / sc + 0.5)), ch = std::min(ah, (int)(mh / sc + 0.5));
                int sx = ax + (aw - cw) / 2, sy = ay + (ah - ch) / 2;
                if (cw == mw && ch == mh) BitBlt(dc, m.left, m.top, mw, mh, mem, sx, sy, SRCCOPY);
                else StretchBlt(dc, m.left, m.top, mw, mh, mem, sx, sy, cw, ch, SRCCOPY);
            } else {
                double sc = std::min(mw / (double)aw, mh / (double)ah);
                int tw = std::min(mw, (int)(aw * sc + 0.5)), th = std::min(mh, (int)(ah * sc + 0.5));
                int dx = m.left + (mw - tw) / 2, dy = m.top + (mh - th) / 2;
                if (dy > m.top) PatBlt(dc, m.left, m.top, mw, dy - m.top, BLACKNESS);
                if (dy + th < m.bottom) PatBlt(dc, m.left, dy + th, mw, m.bottom - dy - th, BLACKNESS);
                if (dx > m.left) PatBlt(dc, m.left, dy, dx - m.left, th, BLACKNESS);
                if (dx + tw < m.right) PatBlt(dc, dx + tw, dy, m.right - dx - tw, th, BLACKNESS);
                if (tw == aw && th == ah) BitBlt(dc, dx, dy, tw, th, mem, ax, ay, SRCCOPY);
                else StretchBlt(dc, dx, dy, tw, th, mem, ax, ay, aw, ah, SRCCOPY);
            }
        }
        ReleaseDC(w, dc);
    }
    inline float Smp(const BYTE* p) const {
        if (aFloat) { float f; memcpy(&f, p, 4); return f; }
        switch (aBits) {
        case 8: return ((int)p[0] - 128) / 128.0f;
        case 16: { short v; memcpy(&v, p, 2); return v / 32768.0f; }
        case 24: { int v = (int)p[0] | ((int)p[1] << 8) | ((int)(signed char)p[2] << 16); return v / 8388608.0f; }
        default: { int v; memcpy(&v, p, 4); return (float)(v / 2147483648.0); }
        }
    }
    int SubmitAudio(IMFSample* s) {  // returns number of audio frames queued
        IMFMediaBuffer* buf = nullptr;
        if (FAILED(s->ConvertToContiguousBuffer(&buf)) || !buf) return 0;
        BYTE* d = nullptr;
        DWORD len = 0;
        int frames = 0;
        audioSourceFrames = 0;
        if (SUCCEEDED(buf->Lock(&d, nullptr, &len)) && d) {
            const int bps = aBits / 8;
            frames = (int)(len / (DWORD)(bps * aCh));
            inputPcm.resize((size_t)frames * 2);
            for (int i = 0; i < frames; i++) {
                const BYTE* p = d + (size_t)i * (size_t)(bps * aCh);
                float L = 0, R = 0;
                if (aCh == 1) {
                    L = R = Smp(p);
                } else {
                    for (int c = 0; c < aCh; c++) {
                        float v = Smp(p + c * bps);
                        if (c == 0) L += v;
                        else if (c == 1) R += v;
                        else if (c == 2) { L += v * 0.707f; R += v * 0.707f; }
                        else if (c == 3) { /* LFE is skipped */ }
                        else if (c & 1) R += v * 0.6f;
                        else L += v * 0.6f;
                    }
                    if (aCh > 2) { L *= 0.75f; R *= 0.75f; }
                }
                inputPcm[(size_t)i * 2] = std::isfinite(L) ? L : 0;
                inputPcm[(size_t)i * 2 + 1] = std::isfinite(R) ? R : 0;
            }
            resampler.process(inputPcm.data(), (size_t)frames, p_speed.load() / 100.0, resampled);
            audioSourceFrames = resampler.consumedFrames();
            pcm.resize(resampled.size());
            const float targetGain = p_mute.load() ? 0.0f : (float)std::clamp(p_volume.load(), 0, 500) / 100.0f;
            // A short ramp eliminates clicks when changing gain or muting.
            const float smoothing = 1.0f - expf(-1.0f / (0.01f * aRate));
            for (size_t i = 0; i + 1 < resampled.size(); i += 2) {
                smoothedGain += (targetGain - smoothedGain) * smoothing;
                for (int c = 0; c < 2; ++c) {
                    float v = resampled[i + c] * smoothedGain;
                    v = smoothedGain > 1.0f ? Limit(v) : std::clamp(v, -1.0f, 1.0f);
                    pcm[i + c] = (short)lrintf(v * 32767.0f);
                }
            }
            frames = (int)(pcm.size() / 2);
            buf->Unlock();
            out.Write(pcm.data(), (size_t)frames);
        }
        buf->Release();
        return frames;
    }
    void CloseMedia() {
        out.Close();
        if (rd) { rd->Release(); rd = nullptr; }
    }
    void Close() {
        CloseMedia();
        if (mem) {
            if (oldBmp) SelectObject(mem, oldBmp);
            DeleteDC(mem);
            mem = nullptr;
        }
        if (dib) { DeleteObject(dib); dib = nullptr; }
        bits = nullptr;
    }

    enum { R_QUIT, R_FAIL, R_REOPEN };
    void Run() {
        bool retried = false;
        for (;;) {
            if (!Open()) return;
            HRESULT hr = S_OK;
            int r = Play(hr);
            if (r == R_QUIT) return;
            CloseMedia();
            if (r == R_REOPEN) continue;
            if (shown == 0 && !noScale && !retried) {  // maybe the scaler is the problem: decode at native size
                retried = true;
                noScale = true;
                Log(L"retrying without scaling (0x%08X)", (unsigned)hr);
                continue;
            }
            PlayerFail(L"Ошибка воспроизведения", hr);
            return;
        }
    }
    int Play(HRESULT& hr) {
        const double LEAD = 0.12;  // seconds of audio kept queued
        IMFSample *vS = nullptr, *aS = nullptr;
        double vPts = 0, aPts = 0, base = 0, aUntil = 0, lastV = 0, frameDur = 1.0 / 30, pauseAt = 0;
        bool clockSet = false, vEos = false, aEos = !hasAudio, wasPaused = false;
        double speed = p_speed.load() / 100.0;
        int framesThisPass = 0, skipped = 0, result = R_QUIT;
        bool failed = false;
        hr = S_OK;

        while (!p_quit.load()) {
            if (layoutSeen != p_layoutGen.load()) {
                LoadLayout();
                p_repaint = true;
            }
            const bool paused = p_userPause.load() || p_autoPause.load();
            if (paused) {
                if (!wasPaused) {
                    wasPaused = true;
                    pauseAt = NowSec();
                    out.Pause(true);
                }
                if (p_repaint.exchange(false)) Draw();
                Sleep(25);
                continue;
            }
            if (wasPaused) {
                wasPaused = false;
                base += NowSec() - pauseAt;
                out.Pause(false);
            }
            if (p_repaint.exchange(false)) Draw();

            // 1) keep one decoded video frame waiting
            if (!vS && !vEos) {
                DWORD idx = 0, flags = 0;
                LONGLONG ts = 0;
                IMFSample* s = nullptr;
                hr = rd->ReadSample(SR_VIDEO, 0, &idx, &flags, &ts, &s);
                if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
                    if (s) s->Release();
                    failed = true;
                    break;
                }
                if (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
                    if (!QueryVideoType()) {
                        if (s) s->Release();
                        hr = E_FAIL;
                        failed = true;
                        break;
                    }
                }
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) vEos = true;
                if (s) {
                    vS = s;
                    vPts = (double)ts / 1e7;
                    if (!clockSet) {
                        base = NowSec() - vPts / speed;
                        aUntil = vPts;
                        clockSet = true;
                    }
                }
            }
            // Rebase the media clock continuously when the speed changes.
            double wall = NowSec();
            double nextSpeed = p_speed.load() / 100.0;
            if (clockSet && nextSpeed != speed) {
                double mediaNow = (wall - base) * speed;
                base = wall - mediaNow / nextSpeed;
            }
            speed = nextSpeed;
            double now = (wall - base) * speed;
            if (vS && now - vPts > 0.5) {
                // far behind (PC woke from sleep, long stall): continue from this frame instead of racing to catch up
                base = NowSec() - vPts / speed;
                now = vPts;
                aUntil = now;
                out.Reset(false);
            }

            // 2) feed audio a little ahead of the clock
            if (hasAudio && clockSet) {
                int guard = 0;
                while (!aEos && aUntil - now < LEAD * speed && guard++ < 96) {
                    if (!aS) {
                        DWORD idx = 0, flags = 0;
                        LONGLONG ts = 0;
                        IMFSample* s = nullptr;
                        HRESULT ha = rd->ReadSample(SR_AUDIO, 0, &idx, &flags, &ts, &s);
                        if (FAILED(ha) || (flags & MF_SOURCE_READERF_ERROR)) {
                            if (s) s->Release();
                            aEos = true;  // keep the picture going even if the sound breaks
                            break;
                        }
                        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) aEos = true;
                        if (!s) continue;
                        aS = s;
                        aPts = (double)ts / 1e7;
                    }
                    double playAt = std::max(now, aUntil);
                    if (aPts - playAt > 0.03) break;  // not due yet
                    if (playAt - aPts > 0.5) {        // severe desynchronisation only; avoid chopping short packets
                        aS->Release();
                        aS = nullptr;
                        continue;
                    }
                    SubmitAudio(aS);
                    aUntil = playAt + audioSourceFrames / (double)aRate;
                    aS->Release();
                    aS = nullptr;
                }
            }

            // 3) show the frame when its time comes
            if (vS) {
                double d = vPts - now;
                if (d <= 0.001) {
                    bool late = d < -0.12;
                    if (!late || ++skipped > 4) {
                        CopyFrame(vS);
                        Draw();
                        skipped = 0;
                    }
                    if (vPts > lastV && vPts - lastV < 0.5) frameDur = vPts - lastV;
                    lastV = vPts;
                    framesThisPass++;
                    shown++;
                    vS->Release();
                    vS = nullptr;
                } else {
                    Sleep(d / speed > 0.012 ? 8 : 1);
                }
            } else if (vEos && aEos) {
                double endT = std::max(lastV + frameDur, hasAudio ? aUntil : 0.0);
                if (!clockSet || now >= endT - 0.004) {
                    if (framesThisPass == 0) {
                        hr = E_FAIL;
                        failed = true;
                        break;
                    }
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    pv.vt = VT_I8;
                    pv.hVal.QuadPart = 0;
                    hr = rd->SetCurrentPosition(kTimeFormatDefault, pv);
                    if (FAILED(hr)) {  // source cannot seek: start over with a fresh reader
                        result = R_REOPEN;
                        break;
                    }
                    out.Reset(false);
                    resampler.reset(speed);
                    if (aS) { aS->Release(); aS = nullptr; }
                    vEos = false;
                    aEos = !hasAudio;
                    clockSet = false;
                    framesThisPass = 0;
                    lastV = 0;
                } else {
                    Sleep(3);
                }
            } else {
                Sleep(3);
            }
        }
        if (vS) vS->Release();
        if (aS) aS->Release();
        return failed ? (int)R_FAIL : result;
    }
};

static DWORD WINAPI PlayerThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    timeBeginPeriod(1);
    {
        Player p;
        p.Run();
        p.Close();
    }
    timeEndPeriod(1);
    CoUninitialize();
    return 0;
}
static void StopPlayer() {
    if (p_thread) {
        p_quit = true;
        WaitForSingleObject(p_thread, 10000);
        CloseHandle(p_thread);
        p_thread = nullptr;
    }
    p_quit = false;
}
static void SetErr(const wchar_t* s) {
    EnterCriticalSection(&g_cs);
    p_err = s;
    LeaveCriticalSection(&g_cs);
}
static void DestroyWall() {
    HWND w = g_wall.exchange(nullptr);
    if (w) DestroyWindow(w);
}
static bool StartWallpaper(int idx) {
    if (idx < 0 || idx >= (int)g_items.size()) return false;
    StopPlayer();
    SetErr(L"");
    g_active = idx;
    g_sel = idx;
    if (!g_mfOk) {
        SetErr(L"Media Foundation недоступен (установите Media Feature Pack)");
        g_active = -1;
        InvalidateRect(g_main, nullptr, FALSE);
        return false;
    }
    if (!FileExists(g_items[(size_t)idx].path)) {
        SetErr(L"Файл не найден");
        g_active = -1;
        InvalidateRect(g_main, nullptr, FALSE);
        return false;
    }
    HWND w = g_wall.load();
    if (!w || !IsWindow(w)) {
        g_wall = nullptr;
        if (!AttachWall()) {
            SetErr(L"Не удалось встроиться в рабочий стол");
            g_active = -1;
            InvalidateRect(g_main, nullptr, FALSE);
            return false;
        }
    } else {
        UpdateMonitorRects();
    }
    p_path = g_items[(size_t)idx].path;
    p_volume = g_items[(size_t)idx].volume;
    p_fit = g_items[(size_t)idx].fit;
    p_speed = g_items[(size_t)idx].speed;
    p_userPause = false;
    p_thread = CreateThread(nullptr, 0, PlayerThread, nullptr, 0, nullptr);
    Log(L"start: %ls", p_path.c_str());
    SaveConfig();
    InvalidateRect(g_main, nullptr, FALSE);
    return true;
}
static void StopWallpaper() {
    StopPlayer();
    DestroyWall();
    g_active = -1;
    p_userPause = false;
    p_autoPause = false;
    SetErr(L"");
    // classic desktop keeps the last frame until the shell repaints its wallpaper
    if (!g_raised) SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_UPDATEINIFILE);
    SaveConfig();
    InvalidateRect(g_main, nullptr, FALSE);
}
static void RemoveItem(int idx) {
    if (idx < 0 || idx >= (int)g_items.size()) return;
    if (idx == g_active) StopWallpaper();
    if (g_items[(size_t)idx].thumb) DeleteObject(g_items[(size_t)idx].thumb);
    g_items.erase(g_items.begin() + idx);
    if (g_active > idx) g_active--;
    if (g_sel > idx) g_sel--;
    else if (g_sel == idx) g_sel = g_items.empty() ? -1 : std::min(idx, (int)g_items.size() - 1);
    SaveConfig();
    InvalidateRect(g_main, nullptr, FALSE);
}
static bool ForegroundIsFullscreen() {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_main || fg == GetDesktopWindow() || fg == GetShellWindow()) return false;
    if (!IsWindowVisible(fg) || IsIconic(fg)) return false;
    wchar_t cls[80] = L"";
    GetClassNameW(fg, cls, 80);
    static const wchar_t* skip[] = {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
                                    L"LoopDeskMain", L"LoopDeskWall"};
    for (auto s : skip)
        if (!lstrcmpW(cls, s)) return false;
    if (IsZoomed(fg)) return true;
    RECT wr;
    if (!GetWindowRect(fg, &wr)) return false;
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    return wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top && wr.right >= mi.rcMonitor.right &&
           wr.bottom >= mi.rcMonitor.bottom;
}

// ------------------------------------------------------------------ thumbnails + metadata (worker thread)
static DWORD WINAPI ThumbThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (;;) {
        WaitForSingleObject(g_jobEvt, INFINITE);
        for (;;) {
            std::wstring p;
            EnterCriticalSection(&g_cs);
            if (!g_jobs.empty()) {
                p = g_jobs.front();
                g_jobs.erase(g_jobs.begin());
            }
            LeaveCriticalSection(&g_cs);
            if (p.empty()) break;
            ThumbRes* r = new ThumbRes();
            r->path = p;
            WIN32_FILE_ATTRIBUTE_DATA fa = {};
            if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa))
                r->bytes = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
            IShellItemImageFactory* f = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(p.c_str(), nullptr, IID_PPV_ARGS(&f))) && f) {
                SIZE sz = {640, 360};
                HBITMAP hb = nullptr;
                if (SUCCEEDED(f->GetImage(sz, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hb)) && hb) {
                    BITMAP bm = {};
                    GetObjectW(hb, sizeof(bm), &bm);
                    r->bmp = hb;
                    r->w = bm.bmWidth;
                    r->h = bm.bmHeight;
                }
                f->Release();
            }
            if (g_mfOk) {
                IMFSourceReader* rd = nullptr;
                if (SUCCEEDED(fMFCreateReader(p.c_str(), nullptr, &rd)) && rd) {
                    IMFMediaType* t = nullptr;
                    if (SUCCEEDED(rd->GetNativeMediaType(SR_VIDEO, 0, &t)) && t) {
                        UINT64 fs = 0;
                        t->GetUINT64(MF_MT_FRAME_SIZE, &fs);
                        r->vw = (int)(fs >> 32);
                        r->vh = (int)(UINT32)fs;
                        t->Release();
                    }
                    PROPVARIANT v;
                    PropVariantInit(&v);
                    if (SUCCEEDED(rd->GetPresentationAttribute(SR_SOURCE, MF_PD_DURATION, &v)) && v.vt == VT_UI8)
                        r->dur = (double)v.uhVal.QuadPart / 1e7;
                    PropVariantClear(&v);
                    rd->Release();
                }
            }
            if (!PostMessageW(g_main, WM_APP_THUMB, 0, (LPARAM)r)) {
                if (r->bmp) DeleteObject(r->bmp);
                delete r;
            }
        }
    }
    return 0;
}

// ------------------------------------------------------------------ drawing helpers
static void Fill(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}
static void Frame(HDC dc, RECT r, int t, COLORREF c) {
    SetDCBrushColor(dc, c);
    for (int i = 0; i < t; i++) {
        FrameRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
        InflateRect(&r, -1, -1);
    }
}
static void Round(HDC dc, const RECT& r, int rad, COLORREF fill, COLORREF border) {
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH)), op = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, border);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}
static const UINT DT_ONE = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
static void Txt(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt = DT_LEFT | DT_ONE) {
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt);
    SelectObject(dc, of);
}
static void Icon(HDC dc, int kind, int cx, int cy, int r, COLORREF c) {
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH)), op = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, c);
    SetDCPenColor(dc, c);
    if (kind == IC_PLAY) {
        POINT p[3] = {{cx - r * 2 / 3, cy - r}, {cx - r * 2 / 3, cy + r}, {cx + r, cy}};
        Polygon(dc, p, 3);
    } else if (kind == IC_PAUSE) {
        Rectangle(dc, cx - r, cy - r, cx - r / 4, cy + r + 1);
        Rectangle(dc, cx + r / 4 + 1, cy - r, cx + r + 1, cy + r + 1);
    } else if (kind == IC_STOP) {
        Rectangle(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    } else if (kind == IC_PLUS) {
        int t = std::max(1, r / 4);
        Rectangle(dc, cx - r, cy - t, cx + r + 1, cy + t + 1);
        Rectangle(dc, cx - t, cy - r, cx + t + 1, cy + r + 1);
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
}
static void Button(HDC dc, const RECT& r, int id, const wchar_t* label, int icon, bool primary, bool enabled = true) {
    bool hot = enabled && g_hot == id;
    COLORREF bg = primary ? (hot ? C_ACC_H : C_ACC) : (hot ? C_BTN_H : C_BTN);
    COLORREF fg = enabled ? C_TEXT : C_DIM;
    if (!enabled) bg = C_PANEL;
    Round(dc, r, S(8), bg, primary ? bg : C_LINE);
    RECT tr = r;
    if (icon != IC_NONE) {
        Icon(dc, icon, r.left + S(20), (r.top + r.bottom) / 2, S(5), fg);
        tr.left = r.left + S(34);
        tr.right -= S(8);
        Txt(dc, label, tr, g_fBold, fg, DT_LEFT | DT_ONE);
    } else {
        Txt(dc, label, tr, g_fBold, fg, DT_CENTER | DT_ONE);
    }
    if (enabled) g_hits.push_back({r, id});
}
static void Checkbox(HDC dc, int x, int y, int w, int id, const wchar_t* label, bool on) {
    int b = S(18);
    RECT box = {x, y + S(3), x + b, y + S(3) + b};
    bool hot = g_hot == id;
    Round(dc, box, S(5), on ? C_ACC : C_INPUT, on ? C_ACC : (hot ? C_DIM : C_LINE));
    if (on) {
        HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), RGB(255, 255, 255));
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, box.left + S(4), box.top + S(9), nullptr);
        LineTo(dc, box.left + S(8), box.top + S(13));
        LineTo(dc, box.left + S(14), box.top + S(5));
        SelectObject(dc, op);
        DeleteObject(pen);
    }
    RECT tr = {x + b + S(10), y, x + w, y + S(24)};
    Txt(dc, label, tr, g_fUI, C_TEXT);
    RECT hr = {x, y, x + w, y + S(24)};
    g_hits.push_back({hr, id});
}
static void DrawThumb(HDC dc, const RECT& r, const Item& it) {
    int rw = r.right - r.left, rh = r.bottom - r.top;
    if (rw <= 0 || rh <= 0) return;
    if (!it.thumb || it.tw <= 0 || it.th <= 0) {
        Fill(dc, r, C_TILE);
        Icon(dc, IC_PLAY, (r.left + r.right) / 2, (r.top + r.bottom) / 2 - S(8), S(12), C_LINE);
        return;
    }
    double sc = std::max(rw / (double)it.tw, rh / (double)it.th);
    int sw = std::min(it.tw, (int)(rw / sc + 0.5)), sh = std::min(it.th, (int)(rh / sc + 0.5));
    int sx = (it.tw - sw) / 2, sy = (it.th - sh) / 2;
    HDC m = CreateCompatibleDC(dc);
    HGDIOBJ o = SelectObject(m, it.thumb);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, r.left, r.top, rw, rh, m, sx, sy, sw, sh, SRCCOPY);
    SelectObject(m, o);
    DeleteDC(m);
}
static std::wstring MetaText(const Item& it) {
    std::wstring s;
    wchar_t b[96];
    if (it.vw > 0 && it.vh > 0) {
        _snwprintf(b, 95, L"%d×%d", it.vw, it.vh);
        b[95] = 0;
        s += b;
    }
    if (it.dur > 0) {
        int t = (int)(it.dur + 0.5);
        _snwprintf(b, 95, L"%d:%02d", t / 60, t % 60);
        b[95] = 0;
        if (!s.empty()) s += L"   •   ";
        s += b;
    }
    if (it.bytes) {
        _snwprintf(b, 95, L"%.1f МБ", (double)it.bytes / 1048576.0);
        b[95] = 0;
        if (!s.empty()) s += L"   •   ";
        s += b;
    }
    return s;
}

// ------------------------------------------------------------------ main window painting
static void PaintAll(HDC dc, int W, int H) {
    g_hits.clear();
    const int topH = S(52), toolH = S(50), botH = S(60), panW = S(312);
    RECT rAll = {0, 0, W, H};
    Fill(dc, rAll, C_BG);

    // ----- wallpaper grid
    RECT rg = {0, topH + toolH, W - panW, H - botH};
    g_gridRc = rg;
    std::vector<int> vis;
    for (int i = 0; i < (int)g_items.size(); i++)
        if (ContainsNoCase(g_items[(size_t)i].name, g_filter)) vis.push_back(i);
    const int gap = S(12), gw = (int)(rg.right - rg.left) - S(8);
    const int cols = std::max(1, (gw - gap) / (S(190) + gap));
    const int tw = std::max(S(60), (gw - gap * (cols + 1)) / cols), th = tw * 9 / 16;
    const int rows = ((int)vis.size() + cols - 1) / cols;
    const int contentH = rows * (th + gap) + gap, viewH = (int)(rg.bottom - rg.top);
    g_maxScroll = std::max(0, contentH - viewH);
    g_scroll = std::clamp(g_scroll, 0, g_maxScroll);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, rg.left, rg.top, rg.right, rg.bottom);
    if (vis.empty()) {
        RECT a = rg;
        a.bottom = (rg.top + rg.bottom) / 2;
        Txt(dc, g_items.empty() ? L"Здесь пока пусто" : L"Ничего не найдено", a, g_fTitle, C_TEXT,
            DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
        RECT b = rg;
        b.top = a.bottom + S(8);
        if (g_items.empty())
            Txt(dc, L"Перетащите сюда MP4-файл или нажмите «Открыть файл»", b, g_fUI, C_DIM,
                DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
    for (size_t k = 0; k < vis.size(); k++) {
        const int i = vis[k];
        const Item& it = g_items[(size_t)i];
        RECT t;
        t.left = rg.left + gap + (int)(k % (size_t)cols) * (tw + gap);
        t.top = rg.top + gap + (int)(k / (size_t)cols) * (th + gap) - g_scroll;
        t.right = t.left + tw;
        t.bottom = t.top + th;
        if (t.bottom < rg.top || t.top > rg.bottom) continue;
        DrawThumb(dc, t, it);
        RECT cap = {t.left, t.bottom - S(24), t.right, t.bottom};
        Fill(dc, cap, C_CAP);
        RECT ct = {cap.left + S(8), cap.top, cap.right - S(8), cap.bottom};
        Txt(dc, it.name, ct, g_fSmall, C_TEXT);
        if (i == g_active) {
            RECT bd = {t.left + S(6), t.top + S(6), t.left + S(28), t.top + S(28)};
            Round(dc, bd, S(6), C_ACC, C_ACC);
            Icon(dc, p_userPause.load() ? IC_PAUSE : IC_PLAY, (bd.left + bd.right) / 2, (bd.top + bd.bottom) / 2, S(5),
                 RGB(255, 255, 255));
        }
        if (i == g_sel) Frame(dc, t, std::max(2, S(2)), C_ACC);
        else if (g_hot == ID_TILE + i) Frame(dc, t, 1, C_DIM);
        RECT hr;
        if (IntersectRect(&hr, &t, &rg)) g_hits.push_back({hr, ID_TILE + i});
    }
    if (g_maxScroll > 0) {
        int trackH = viewH - S(8), barH = std::max(S(30), trackH * viewH / contentH);
        int by = rg.top + S(4) + (int)((long long)(trackH - barH) * g_scroll / g_maxScroll);
        RECT sb = {rg.right - S(7), by, rg.right - S(3), by + barH};
        Fill(dc, sb, C_LINE);
    }
    RestoreDC(dc, saved);

    // ----- top bar
    RECT rt = {0, 0, W, topH};
    Fill(dc, rt, C_TOP);
    RECT lg = {S(16), S(14), S(40), S(38)};
    Round(dc, lg, S(8), C_ACC, C_ACC);
    Icon(dc, IC_PLAY, (lg.left + lg.right) / 2, (lg.top + lg.bottom) / 2, S(6), RGB(255, 255, 255));
    RECT rn = {S(50), 0, S(160), topH};
    Txt(dc, APP_NAME, rn, g_fTitle, C_TEXT);
    RECT tab = {S(170), 0, S(290), topH};
    Txt(dc, L"Установлено", tab, g_fBold, C_TEXT, DT_CENTER | DT_ONE);
    RECT ul = {tab.left + S(8), topH - S(3), tab.right - S(8), topH};
    Fill(dc, ul, C_ACC);
    {
        std::wstring err;
        EnterCriticalSection(&g_cs);
        err = p_err;
        LeaveCriticalSection(&g_cs);
        std::wstring st;
        COLORREF sc = C_DIM;
        if (!err.empty()) { st = L"Ошибка: " + err; sc = C_WARN; }
        else if (g_active >= 0) {
            const wchar_t* pre = p_userPause.load() ? L"Пауза: "
                                 : (p_autoPause.load() ? L"Автопауза: " : L"Сейчас на рабочем столе: ");
            st = pre + g_items[(size_t)g_active].name;
            if (p_mute.load()) st += L" (без звука)";
            sc = C_TEXT;
        } else st = L"Обои не установлены";
        RECT rs = {S(310), 0, W - S(16), topH};
        Txt(dc, st, rs, g_fUI, sc, DT_RIGHT | DT_ONE);
    }

    // ----- search row
    RECT rtool = {0, topH, W - panW, topH + toolH};
    Fill(dc, rtool, C_BG);
    RECT sbx = {S(12), topH + S(10), S(12) + S(280), topH + S(42)};
    Round(dc, sbx, S(8), C_INPUT, C_LINE);
    {
        wchar_t b[64];
        _snwprintf(b, 63, L"Обоев: %d", (int)g_items.size());
        b[63] = 0;
        RECT rc = {sbx.right + S(14), topH, W - panW - S(12), topH + toolH};
        Txt(dc, b, rc, g_fUI, C_DIM);
    }

    // ----- right panel
    RECT rp = {W - panW, topH, W, H - botH};
    g_panelRc = rp;
    Fill(dc, rp, C_PANEL);
    RECT pl = {rp.left, rp.top, rp.left + 1, rp.bottom};
    Fill(dc, pl, C_LINE);
    const int px = W - panW + S(16), pw = panW - S(32);
    int y = topH + S(12);
    g_volTrack = RECT{};
    if (g_sel >= 0 && g_sel < (int)g_items.size()) {
        Item& it = g_items[(size_t)g_sel];
        // the preview takes whatever height is left after the fixed-size controls below it
        const int ph = std::clamp((H - botH) - y - S(416), S(64), pw * 9 / 16);
        RECT pv = {px, y, px + pw, y + ph};
        DrawThumb(dc, pv, it);
        Frame(dc, pv, 1, C_LINE);
        y = pv.bottom + S(10);
        RECT r1 = {px, y, px + pw, y + S(24)};
        Txt(dc, it.name, r1, g_fTitle, C_TEXT);
        y += S(24);
        RECT r2 = {px, y, px + pw, y + S(18)};
        Txt(dc, MetaText(it), r2, g_fSmall, C_DIM);
        y += S(26);
        RECT l1 = {px, y, px + pw, y + 1};
        Fill(dc, l1, C_LINE);
        y += S(10);

        // volume 0..500 %
        RECT rv = {px, y, px + pw, y + S(20)};
        Txt(dc, L"ГРОМКОСТЬ", rv, g_fSmall, C_DIM);
        wchar_t vb[32];
        _snwprintf(vb, 31, L"%d%%", it.volume);
        vb[31] = 0;
        Txt(dc, vb, rv, g_fBold, it.volume > 100 ? C_BOOST : C_TEXT, DT_RIGHT | DT_ONE);
        y += S(26);
        RECT tr = {px + S(8), y + S(5), px + pw - S(8), y + S(11)};
        g_volTrack = tr;
        const int twd = (int)(tr.right - tr.left);
        const int pos = tr.left + MulDiv(it.volume, twd, 500), p100 = tr.left + twd / 5;
        Round(dc, tr, S(6), C_TRACK, C_TRACK);
        if (it.volume > 0) {
            RECT f1 = {tr.left, tr.top, std::min(pos, p100), tr.bottom};
            Fill(dc, f1, C_ACC);
            if (pos > p100) {
                RECT f2 = {p100, tr.top, pos, tr.bottom};
                Fill(dc, f2, C_BOOST);
            }
        }
        RECT tick = {p100, tr.top - S(3), p100 + std::max(1, S(1)), tr.bottom + S(3)};
        Fill(dc, tick, C_DIM);
        {
            int rr = S(8), cy = (tr.top + tr.bottom) / 2;
            HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH)), op = SelectObject(dc, GetStockObject(DC_PEN));
            SetDCBrushColor(dc, RGB(255, 255, 255));
            SetDCPenColor(dc, it.volume > 100 ? C_BOOST : C_ACC);
            Ellipse(dc, pos - rr, cy - rr, pos + rr + 1, cy + rr + 1);
            SelectObject(dc, ob);
            SelectObject(dc, op);
        }
        RECT vh = {px, y - S(6), px + pw, y + S(22)};
        g_hits.push_back({vh, ID_VOL});
        y += S(22);
        RECT s0 = {px, y, px + S(40), y + S(16)};
        Txt(dc, L"0%", s0, g_fSmall, C_DIM);
        RECT s1 = {p100 - S(30), y, p100 + S(30), y + S(16)};
        Txt(dc, L"100%", s1, g_fSmall, C_DIM, DT_CENTER | DT_ONE);
        RECT s5 = {px + pw - S(50), y, px + pw, y + S(16)};
        Txt(dc, L"500%", s5, g_fSmall, C_DIM, DT_RIGHT | DT_ONE);
        y += S(26);

        // Playback speed changes continuously; audio pitch follows speed.
        RECT rspeed = {px, y, px + pw - S(92), y + S(30)};
        wchar_t sb[80];
        _snwprintf(sb, 79, L"Скорость: %d%%", it.speed); sb[79] = 0;
        Txt(dc, sb, rspeed, g_fUI, C_TEXT);
        RECT sm = {px + pw - S(88), y, px + pw - S(48), y + S(30)};
        RECT sp = {px + pw - S(40), y, px + pw, y + S(30)};
        Button(dc, sm, ID_SPEED_DOWN, L"−", IC_NONE, false);
        Button(dc, sp, ID_SPEED_UP, L"+", IC_NONE, false);
        y += S(40);

        // placement
        RECT rf = {px, y, px + pw, y + S(20)};
        Txt(dc, L"ПОЛОЖЕНИЕ", rf, g_fSmall, C_DIM);
        y += S(24);
        static const wchar_t* fits[3] = {L"Заполнить", L"Вписать", L"Растянуть"};
        for (int k = 0; k < 3; k++) {
            RECT b = {px + pw * k / 3 + (k ? S(3) : 0), y, px + pw * (k + 1) / 3 - (k < 2 ? S(3) : 0), y + S(30)};
            bool on = it.fit == k, hot = g_hot == ID_FIT0 + k;
            Round(dc, b, S(8), on ? C_ACC : (hot ? C_BTN_H : C_BTN), on ? C_ACC : C_LINE);
            Txt(dc, fits[k], b, on ? g_fBold : g_fUI, C_TEXT, DT_CENTER | DT_ONE);
            g_hits.push_back({b, ID_FIT0 + k});
        }
        y += S(40);
        RECT rb = {px, y, px + pw, y + S(32)};
        Button(dc, rb, ID_REMOVE, L"Убрать из списка", IC_NONE, false);
        y += S(44);
    } else {
        RECT rh = {px, y, px + pw, y + S(60)};
        Txt(dc, L"Выберите обои в списке слева — здесь появятся их свойства.", rh, g_fUI, C_DIM,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        y += S(76);
    }
    RECT l2 = {px, y, px + pw, y + 1};
    Fill(dc, l2, C_LINE);
    y += S(10);
    RECT rs = {px, y, px + pw, y + S(20)};
    Txt(dc, L"НАСТРОЙКИ", rs, g_fSmall, C_DIM);
    y += S(24);
    Checkbox(dc, px, y, pw, ID_AUTORUN, L"Запускать вместе с Windows", g_optAutorun);
    y += S(26);
    Checkbox(dc, px, y, pw, ID_FSPAUSE, L"Пауза: полное или развёрнутое окно", g_optFsPause);
    y += S(26);
    Checkbox(dc, px, y, pw, ID_ECOPAUSE, L"Пауза: батарея / блокировка / экран", g_optEcoPause);

    // ----- bottom bar
    RECT rbm = {0, H - botH, W, H};
    Fill(dc, rbm, C_TOP);
    RECT bl = {0, H - botH, W, H - botH + 1};
    Fill(dc, bl, C_LINE);
    const int by = H - botH + S(13), bh = S(34);
    const bool act = g_active >= 0;
    int x = S(12);
    RECT b1 = {x, by, x + S(160), by + bh};
    Button(dc, b1, ID_OPEN, L"Открыть файл", IC_PLUS, false);
    x += S(172);
    RECT b2 = {x, by, x + S(150), by + bh};
    Button(dc, b2, ID_PAUSE, p_userPause.load() ? L"Продолжить" : L"Пауза", p_userPause.load() ? IC_PLAY : IC_PAUSE,
           false, act);
    x += S(162);
    RECT b3 = {x, by, x + S(150), by + bh};
    Button(dc, b3, ID_STOP, L"Остановить", IC_STOP, false, act);
    RECT b4 = {W - S(12) - S(120), by, W - S(12), by + bh};
    Button(dc, b4, ID_OK, L"OK", IC_NONE, true);
}

// ------------------------------------------------------------------ main window logic
static void MakeFonts() {
    for (HFONT* f : {&g_fUI, &g_fBold, &g_fTitle, &g_fSmall})
        if (*f) { DeleteObject(*f); *f = nullptr; }
    auto mk = [](int pt10, int weight) {
        LOGFONTW lf = {};
        lf.lfHeight = -MulDiv(pt10, (int)g_dpi, 720);
        lf.lfWeight = weight;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfCharSet = DEFAULT_CHARSET;
        lstrcpyW(lf.lfFaceName, L"Segoe UI");
        return CreateFontIndirectW(&lf);
    };
    g_fUI = mk(95, FW_NORMAL);
    g_fBold = mk(95, FW_SEMIBOLD);
    g_fTitle = mk(125, FW_SEMIBOLD);
    g_fSmall = mk(85, FW_NORMAL);
    if (g_edit) SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_fUI, TRUE);
}
static void PlaceEdit() {
    if (!g_edit) return;
    const int topH = S(52);
    MoveWindow(g_edit, S(12) + S(12), topH + S(10) + S(6), S(280) - S(24), S(20), TRUE);
}
static int HitAt(POINT pt) {
    for (size_t i = g_hits.size(); i-- > 0;)
        if (PtInRect(&g_hits[i].r, pt)) return g_hits[i].id;
    return 0;
}
static void SetVolFromX(int x) {
    if (g_sel < 0 || g_sel >= (int)g_items.size()) return;
    int w = (int)(g_volTrack.right - g_volTrack.left);
    if (w <= 0) return;
    int v = MulDiv(std::clamp(x - (int)g_volTrack.left, 0, w), 500, w);
    v = (v + 2) / 5 * 5;
    if (std::abs(v - 100) <= 5) v = 100;  // easy to hit the neutral level
    g_items[(size_t)g_sel].volume = v;
    if (g_sel == g_active) p_volume = v;
    InvalidateRect(g_main, nullptr, FALSE);
}
static void AddFiles(const std::vector<std::wstring>& files) {
    int first = -1;
    for (auto& f : files) {
        if (!FileExists(f)) continue;
        int k = AddItem(f);
        if (first < 0) first = k;
    }
    if (first >= 0) {
        g_filter.clear();
        if (g_edit) SetWindowTextW(g_edit, L"");
        StartWallpaper(first);
    }
    SaveConfig();
    InvalidateRect(g_main, nullptr, FALSE);
}
static void OpenDialog(HWND h) {
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW of = {};
    of.lStructSize = sizeof(of);
    of.hwndOwner = h;
    of.lpstrFilter = L"Видео (*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm)\0*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm\0"
                     L"Все файлы (*.*)\0*.*\0";
    of.lpstrFile = buf.data();
    of.nMaxFile = (DWORD)buf.size();
    of.lpstrTitle = L"Выберите видео для рабочего стола";
    of.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&of)) return;
    std::vector<std::wstring> files;
    std::wstring first = buf.data();
    const wchar_t* p = buf.data() + first.size() + 1;
    if (!*p) {
        files.push_back(first);
    } else {
        while (*p) {
            std::wstring n = p;
            files.push_back(first + L"\\" + n);
            p += n.size() + 1;
        }
    }
    AddFiles(files);
}
static void Tray(bool add) {
    NOTIFYICONDATAW n = {};
    n.cbSize = sizeof(n);
    n.hWnd = g_main;
    n.uID = 1;
    if (add) {
        n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        n.uCallbackMessage = WM_APP_TRAY;
        n.hIcon = g_iconSm ? g_iconSm : g_icon;
        lstrcpynW(n.szTip, L"LoopDesk — живые обои", ARRAYSIZE(n.szTip));
        Shell_NotifyIconW(NIM_ADD, &n);
    } else {
        Shell_NotifyIconW(NIM_DELETE, &n);
    }
}
static void HideMain() {  // the program keeps running in the notification area
    static bool told = false;
    ShowWindow(g_main, SW_HIDE);
    if (told) return;
    told = true;
    NOTIFYICONDATAW n = {};
    n.cbSize = sizeof(n);
    n.hWnd = g_main;
    n.uID = 1;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO;
    lstrcpynW(n.szInfoTitle, L"LoopDesk работает в фоне", ARRAYSIZE(n.szInfoTitle));
    lstrcpynW(n.szInfo, L"Обои продолжают играть. Значок в трее: открыть окно, пауза, выход.", ARRAYSIZE(n.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &n);
}
static void ShowMain() {
    ShowWindow(g_main, IsIconic(g_main) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_main);
}
static void TogglePause() {
    if (g_active < 0) return;
    p_userPause = !p_userPause.load();
    InvalidateRect(g_main, nullptr, FALSE);
}
static void Command(HWND h, int id) {
    if (id >= ID_TILE) {
        int i = id - ID_TILE;
        if (i < 0 || i >= (int)g_items.size()) return;
        if (i == g_active && p_thread && WaitForSingleObject(p_thread, 0) == WAIT_TIMEOUT) {
            g_sel = i;
            InvalidateRect(h, nullptr, FALSE);
        } else {
            StartWallpaper(i);
        }
        return;
    }
    switch (id) {
    case ID_OPEN: OpenDialog(h); break;
    case ID_PAUSE: TogglePause(); break;
    case ID_STOP: StopWallpaper(); break;
    case ID_OK: HideMain(); break;
    case ID_REMOVE: RemoveItem(g_sel); break;
    case ID_SPEED_DOWN: case ID_SPEED_UP:
        if (g_sel >= 0 && g_sel < (int)g_items.size()) {
            Item& it = g_items[(size_t)g_sel];
            it.speed = std::clamp(it.speed + (id == ID_SPEED_UP ? 5 : -5), 50, 200);
            if (g_sel == g_active) p_speed = it.speed;
            SaveConfig();
            InvalidateRect(h, nullptr, FALSE);
        }
        break;
    case ID_FIT0: case ID_FIT1: case ID_FIT2:
        if (g_sel >= 0 && g_sel < (int)g_items.size() && g_items[(size_t)g_sel].fit != id - ID_FIT0) {
            g_items[(size_t)g_sel].fit = id - ID_FIT0;
            if (g_sel == g_active) StartWallpaper(g_sel);  // decode size depends on the mode
            SaveConfig();
            InvalidateRect(h, nullptr, FALSE);
        }
        break;
    case ID_AUTORUN:
        g_optAutorun = !g_optAutorun;
        AutorunSet(g_optAutorun);
        g_optAutorun = AutorunGet();
        InvalidateRect(h, nullptr, FALSE);
        break;
    case ID_ECOPAUSE:
        g_optEcoPause = !g_optEcoPause;
        SaveConfig();
        InvalidateRect(h, nullptr, FALSE);
        break;
    case ID_FSPAUSE:
        g_optFsPause = !g_optFsPause;
        if (!g_optFsPause) p_autoPause = false;
        SaveConfig();
        InvalidateRect(h, nullptr, FALSE);
        break;
    }
}
static void Shutdown() {
    static bool done = false;
    if (done) return;
    done = true;
    SaveConfig();  // remembers the active wallpaper for the next start
    StopPlayer();
    DestroyWall();
    if (!g_raised && g_progman) SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_UPDATEINIFILE);
    Tray(false);
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (g_msgTaskbar && m == g_msgTaskbar) {
        Tray(true);
        return 0;
    }
    switch (m) {
    case WM_CREATE: {
        g_main = h;
        g_dpi = WindowDpi(h);
        g_brInput = CreateSolidBrush(C_INPUT);
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, h,
                                 (HMENU)(INT_PTR)100, g_hi, nullptr);
        MakeFonts();
        SendMessageW(g_edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Поиск");
        PlaceEdit();
        DragAcceptFiles(h, TRUE);
        BOOL dark = TRUE;
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            typedef HRESULT(WINAPI * Fn)(HWND, DWORD, LPCVOID, DWORD);
            Fn fn = (Fn)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (fn && FAILED(fn(h, 20, &dark, sizeof(dark)))) fn(h, 19, &dark, sizeof(dark));
        }
        if (!RegisterHotKey(h, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P')) Log(L"hotkey Ctrl+Alt+P unavailable");
        if (!RegisterHotKey(h, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'N')) Log(L"hotkey Ctrl+Alt+N unavailable");
        if (!RegisterHotKey(h, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M')) Log(L"hotkey Ctrl+Alt+M unavailable");
        if (!WTSRegisterSessionNotification(h, NOTIFY_FOR_THIS_SESSION)) Log(L"session notifications unavailable");
        g_displayNotify = RegisterPowerSettingNotification(h, &kConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (!g_displayNotify) Log(L"display power notifications unavailable");
        SetTimer(h, 1, 700, nullptr);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ ob = SelectObject(mem, bmp);
            PaintAll(mem, rc.right, rc.bottom);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:
        PlaceEdit();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mi = (MINMAXINFO*)lp;
        mi->ptMinTrackSize.x = S(900);
        mi->ptMinTrackSize.y = S(600);
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        MakeFonts();
        RECT* r = (RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        PlaceEdit();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_INPUT);
        return (LRESULT)g_brInput;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 100 && HIWORD(wp) == EN_CHANGE) {
            wchar_t b[128] = L"";
            GetWindowTextW(g_edit, b, 128);
            g_filter = b;
            g_scroll = 0;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (g_dragVol) {
            SetVolFromX(pt.x);
            return 0;
        }
        if (!g_tracking) {
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            g_tracking = true;
        }
        int id = HitAt(pt);
        if (id != g_hot) {
            g_hot = id;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = false;
        if (g_hot) {
            g_hot = 0;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        SetFocus(h);
        int id = HitAt(pt);
        if (id == ID_VOL) {
            g_dragVol = true;
            SetCapture(h);
            SetVolFromX(pt.x);
        } else if (id) {
            Command(h, id);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_dragVol) {
            g_dragVol = false;
            ReleaseCapture();
            SaveConfig();
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_dragVol = false;
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &pt);
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        if (PtInRect(&g_panelRc, pt) && g_sel >= 0 && g_sel < (int)g_items.size()) {
            int v = std::clamp(g_items[(size_t)g_sel].volume + (d > 0 ? 5 : -5), 0, 500);
            g_items[(size_t)g_sel].volume = v;
            if (g_sel == g_active) p_volume = v;
            SaveConfig();
        } else {
            g_scroll = std::clamp(g_scroll - d * S(70) / WHEEL_DELTA, 0, g_maxScroll);
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_HOTKEY:
        if (wp == 1) TogglePause();
        else if (wp == 2 && !g_items.empty()) StartWallpaper((g_active + 1) % (int)g_items.size());
        else if (wp == 3) { p_mute = !p_mute.load(); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_DELETE && g_sel >= 0) RemoveItem(g_sel);
        else if (wp == VK_SPACE) TogglePause();
        return 0;
    case WM_DROPFILES: {
        HDROP d = (HDROP)wp;
        UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> files;
        for (UINT i = 0; i < n; i++) {
            wchar_t b[1024] = L"";
            if (DragQueryFileW(d, i, b, 1024)) files.push_back(b);
        }
        DragFinish(d);
        AddFiles(files);
        return 0;
    }
    case WM_APP_THUMB: {
        ThumbRes* r = (ThumbRes*)lp;
        int k = FindItem(r->path);
        if (k >= 0) {
            Item& it = g_items[(size_t)k];
            if (r->bmp) {
                if (it.thumb) DeleteObject(it.thumb);
                it.thumb = r->bmp;
                it.tw = r->w;
                it.th = r->h;
                r->bmp = nullptr;
            }
            it.vw = r->vw;
            it.vh = r->vh;
            it.dur = r->dur;
            it.bytes = r->bytes;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (r->bmp) DeleteObject(r->bmp);
        delete r;
        return 0;
    }
    case WM_APP_STATE:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_APP_SHOW:
        ShowMain();
        return 0;
    case WM_APP_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) {
            ShowMain();
        } else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
            HMENU mn = CreatePopupMenu();
            AppendMenuW(mn, MF_STRING, MENU_SHOW, L"Открыть LoopDesk");
            AppendMenuW(mn, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(mn, MF_STRING | (g_active < 0 ? MF_GRAYED : 0), MENU_PAUSE,
                        p_userPause.load() ? L"Продолжить" : L"Пауза");
            AppendMenuW(mn, MF_STRING | (g_active < 0 ? MF_GRAYED : 0), MENU_STOP, L"Остановить обои");
            AppendMenuW(mn, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(mn, MF_STRING, MENU_EXIT, L"Выход");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(h);
            int c = (int)TrackPopupMenu(mn, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, h, nullptr);
            DestroyMenu(mn);
            PostMessageW(h, WM_NULL, 0, 0);
            if (c == MENU_SHOW) ShowMain();
            else if (c == MENU_PAUSE) TogglePause();
            else if (c == MENU_STOP) StopWallpaper();
            else if (c == MENU_EXIT) DestroyWindow(h);
        }
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK) g_sessionLocked = true;
        else if (wp == WTS_SESSION_UNLOCK) g_sessionLocked = false;
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_POWERSETTINGCHANGE && lp) {
            POWERBROADCAST_SETTING* setting = (POWERBROADCAST_SETTING*)lp;
            if (IsEqualGUID(setting->PowerSetting, kConsoleDisplayState) && setting->DataLength >= sizeof(DWORD)) {
                DWORD state; memcpy(&state, setting->Data, sizeof(state));
                g_displayOff = state == 0;
            }
        }
        return TRUE;
    case WM_TIMER:
        if (wp == 1) {
            SYSTEM_POWER_STATUS power = {};
            bool battery = GetSystemPowerStatus(&power) && power.ACLineStatus == 0;
            bool fs = g_active >= 0 && ((g_optFsPause && ForegroundIsFullscreen()) ||
                      (g_optEcoPause && (g_sessionLocked || g_displayOff || battery)));
            if (fs != p_autoPause.load()) {
                p_autoPause = fs;
                InvalidateRect(h, nullptr, FALSE);
            }
            if (g_active >= 0) {
                HWND w = g_wall.load();
                if (!w || !IsWindow(w)) {  // the shell was restarted: attach again
                    g_wall = nullptr;
                    AttachWall();
                } else {
                    KeepZOrder();
                }
            }
        } else if (wp == 2) {  // display configuration settled
            KillTimer(h, 2);
            if (g_active >= 0) {
                int a = g_active;
                StopPlayer();
                DestroyWall();
                StartWallpaper(a);
            }
        }
        return 0;
    case WM_DISPLAYCHANGE:
        SetTimer(h, 2, 1200, nullptr);
        return 0;
    case WM_CLOSE:
        HideMain();
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) Shutdown();
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        for (int i = 1; i <= 3; ++i) UnregisterHotKey(h, i);
        WTSUnRegisterSessionNotification(h);
        if (g_displayNotify) UnregisterPowerSettingNotification(g_displayNotify);
        Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ------------------------------------------------------------------ entry point
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    g_hi = hi;
    const wchar_t* cmd = GetCommandLineW();
    const bool startHidden = cmd && (ContainsNoCase(cmd, L" -tray") || ContainsNoCase(cmd, L" /tray"));

    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"LoopDesk.SingleInstance.Mutex");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(MAIN_CLASS, nullptr);
        if (other) PostMessageW(other, WM_APP_SHOW, 0, 0);
        return 0;
    }
    InitializeCriticalSection(&g_cs);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    g_mfOk = LoadMF();
    Log(L"---- LoopDesk start, mf=%d", (int)g_mfOk);
    g_jobEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
    g_icon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    if (!g_icon) g_icon = LoadIconW(nullptr, IDI_APPLICATION);
    g_iconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                 GetSystemMetrics(SM_CYSMICON), 0);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = MAIN_CLASS;
    wc.hIcon = g_icon;
    wc.hIconSm = g_iconSm ? g_iconSm : g_icon;
    RegisterClassExW(&wc);
    WNDCLASSEXW ww = {};
    ww.cbSize = sizeof(ww);
    ww.hInstance = hi;
    ww.lpfnWndProc = WallProc;
    ww.lpszClassName = WALL_CLASS;
    ww.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&ww);

    g_optAutorun = AutorunGet();
    HWND h = CreateWindowExW(0, MAIN_CLASS, L"LoopDesk — живые обои", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1200, 760, nullptr, nullptr, hi, nullptr);
    if (!h) return 1;
    {   // scale to the monitor DPI and centre in its work area
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
        int ww2 = std::min((int)(mi.rcWork.right - mi.rcWork.left), S(1200));
        int hh2 = std::min((int)(mi.rcWork.bottom - mi.rcWork.top), S(760));
        SetWindowPos(h, nullptr, mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww2) / 2,
                     mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - hh2) / 2, ww2, hh2,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    std::wstring active = LoadConfig();
    CloseHandle(CreateThread(nullptr, 0, ThumbThread, nullptr, 0, nullptr));
    Tray(true);
    if (!active.empty()) {
        int k = FindItem(active);
        if (k >= 0 && FileExists(active)) StartWallpaper(k);
    }
    if (!startHidden) {
        ShowWindow(h, SW_SHOW);
        UpdateWindow(h);
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_mfOk) fMFShutdown();
    CoUninitialize();
    if (mtx) CloseHandle(mtx);
    return 0;
}
