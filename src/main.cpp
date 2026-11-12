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
#include <d3d11.h>
#include <d3d10.h>
#include <wincodec.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "resampler.h"
#include "video_edit.h"
#include "video_presets.h"
#include "frame_rate_gate.h"
#include "rotation.h"
#include "filter_pool.h"
#include "audio_gain.h"
#include "latest_frame.h"
#include <memory>
#include "slider_math.h"

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
    WM_APP_PREVIEW,
};

enum {
    ID_OPEN = 1, ID_PAUSE, ID_STOP, ID_OK, ID_REMOVE,
    ID_FIT0 = 10, ID_FIT1, ID_FIT2,
    ID_AUTORUN = 20, ID_FSPAUSE, ID_ECOPAUSE, ID_GPU, ID_ROTATE, ID_BATTERY_FPS,
    ID_VOL = 30, ID_SPEED, ID_EDIT, ID_FAVORITE, ID_FAVORITES_ONLY, ID_SORT,
    ID_TILE = 1000,
};
enum { IC_NONE, IC_PLUS, IC_PLAY, IC_PAUSE, IC_STOP };
enum { MENU_SHOW = 1, MENU_PAUSE, MENU_STOP, MENU_EXIT };

static const COLORREF C_BG = RGB(12, 17, 28), C_TOP = RGB(14, 19, 31), C_PANEL = RGB(18, 25, 39),
                      C_TILE = RGB(27, 36, 54), C_CAP = RGB(24, 32, 48), C_LINE = RGB(46, 58, 78),
                      C_TEXT = RGB(238, 242, 251), C_DIM = RGB(167, 180, 201), C_ACC = RGB(63, 90, 189),
                      C_ACC_H = RGB(82, 111, 211), C_BTN = RGB(34, 44, 63), C_BTN_H = RGB(46, 59, 82),
                      C_BOOST = RGB(245, 183, 112), C_TRACK = RGB(64, 78, 101), C_INPUT = RGB(25, 34, 50),
                      C_WARN = RGB(255, 194, 126), C_MINT = RGB(130, 222, 214);

// ------------------------------------------------------------------ data
struct Item {
    std::wstring path, name;
    int volume = 100;  // 0..500 (%)
    int fit = 0;       // 0 fill (cover), 1 fit (letterbox), 2 stretch
    int speed = 100;   // 15..200 percent
    bool favorite = false;
    VideoEdit edit;
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
static HFONT g_fUI, g_fBold, g_fTitle, g_fSmall, g_fHero;
static HBRUSH g_brInput;
static HICON g_icon, g_iconSm;
static HBITMAP g_ambientArt = nullptr;
static UINT g_msgTaskbar = 0;

static std::vector<Item> g_items;
static int g_sel = -1, g_active = -1;
static std::vector<Hit> g_hits;
static int g_hot = 0, g_scroll = 0, g_maxScroll = 0;
static int g_dragControl = 0, g_lastControl = 0, g_panelScroll = 0, g_panelMaxScroll = 0;
static bool g_tracking = false;
static RECT g_volTrack = {}, g_speedTrack = {}, g_gridRc = {}, g_panelRc = {};
static std::wstring g_filter;
static bool g_optFsPause = true, g_optAutorun = false, g_mfOk = false;
static bool g_optEcoPause = false, g_sessionLocked = false, g_displayOff = false, g_editorDark = true;
static bool g_optHardware = false; // opt-in until tested on the user's GPU/driver
static bool g_favoritesOnly = false, g_sortName = false, g_optRotation = false, g_optBatteryFps = false;
static RotationClock g_rotationClock;
static bool g_hasUserPreset = false;
static VideoEdit g_userPreset;
static HPOWERNOTIFY g_displayNotify = nullptr;
// GUID_CONSOLE_DISPLAY_STATE (0 = off, 1 = on, 2 = dimmed).
static const GUID kConsoleDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

static std::vector<std::wstring> g_jobs;  // guarded by g_cs
static HANDLE g_jobEvt;

// player shared state
static std::atomic<bool> p_quit{false}, p_userPause{false}, p_autoPause{false}, p_repaint{false};
static std::atomic<int> p_volume{100}, p_fit{0}, p_layoutGen{0}, p_speed{100}, p_fpsLimit{0};
static std::atomic<bool> p_mute{false}, p_hardware{false};
// 0=off, 1=starting, 2=D3D manager enabled, 3=software fallback.
static std::atomic<int> p_gpuState{0};
static VideoEdit p_edit; // guarded by g_cs
static std::atomic<HWND> g_previewWindow{nullptr};
static std::atomic<bool> g_previewMessage{false}, g_previewFrozen{true}, g_previewRefresh{false};
static std::vector<BYTE> g_previewPixels; // guarded by g_cs
static int g_previewWidth = 0, g_previewHeight = 0;
static unsigned long long g_previewGeneration = 0;
static HANDLE p_thread = nullptr;
static std::wstring p_path;
// UI requests are deferred until the old player thread has actually exited.
static int g_pendingAction = 0; // 0 none, 1 start, 2 stop
static std::wstring g_pendingPath;
static bool g_reattachPending = false, g_abandonedPlayer = false;
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
#ifdef LOOPDESK_TESTING
static std::wstring g_testConfigDir;
#endif
static std::wstring ConfigDir() {
#ifdef LOOPDESK_TESTING
    if (!g_testConfigDir.empty()) return g_testConfigDir;
#endif
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
static bool IsStaticImage(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    const wchar_t* ext = path.c_str() + dot;
    return !lstrcmpiW(ext,L".png") || !lstrcmpiW(ext,L".jpg") || !lstrcmpiW(ext,L".jpeg") || !lstrcmpiW(ext,L".bmp");
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
    s += std::string("hardware=") + (g_optHardware ? "1" : "0") + "\r\n";
    s += std::string("sortname=") + (g_sortName ? "1" : "0") + "\r\n";
    s += std::string("rotate=") + (g_optRotation ? "1" : "0") + "\r\n";
    s += std::string("batteryfps=") + (g_optBatteryFps ? "1" : "0") + "\r\n";
    if (g_hasUserPreset) s += "preset=" + std::to_string(g_userPreset.brightness) + "|" +
        std::to_string(g_userPreset.contrast) + "|" + std::to_string(g_userPreset.saturation) + "|" +
        std::to_string(g_userPreset.gamma) + "|" + std::to_string(g_userPreset.warmth) + "|" +
        std::to_string(g_userPreset.hue) + "|" + std::to_string((int)g_userPreset.invert) + "\r\n";
    s += std::string("editordark=") + (g_editorDark ? "1" : "0") + "\r\n";
    s += "active=" + (g_active >= 0 ? WToU8(g_items[(size_t)g_active].path) : std::string()) + "\r\n";
    s += "selected=" + (g_sel >= 0 ? WToU8(g_items[(size_t)g_sel].path) : std::string()) + "\r\n";
    for (auto& it : g_items) {
        s += "item=" + std::to_string(it.volume) + "|" + std::to_string(it.fit) + "|" + WToU8(it.path) + "\r\n";
        if (it.favorite) s += "favorite=1|" + WToU8(it.path) + "\r\n";
        s += "speed=" + std::to_string(it.speed) + "|" + WToU8(it.path) + "\r\n";
        const VideoEdit& e = it.edit;
        s += "edit=" + std::to_string(e.left) + "|" + std::to_string(e.top) + "|" +
             std::to_string(e.right) + "|" + std::to_string(e.bottom) + "|" +
             std::to_string(e.brightness) + "|" + std::to_string(e.contrast) + "|" +
             std::to_string(e.saturation) + "|" + WToU8(it.path) + "\r\n";
        s += "mirror=" + std::to_string((int)e.flipX) + "|" + std::to_string((int)e.flipY) + "|" + WToU8(it.path) + "\r\n";
        s += "fx=" + std::to_string(e.gamma) + "|" + std::to_string(e.warmth) + "|" +
             std::to_string(e.hue) + "|" + std::to_string((int)e.invert) + "|" + WToU8(it.path) + "\r\n";
    }
    // Write a complete replacement before touching the previous settings file.
    // A crash or full disk must not silently erase the user's library.
    const std::wstring dir = ConfigDir();
    const std::wstring tmp = dir + L"\\config.tmp", dst = dir + L"\\config.txt";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { Log(L"config: cannot create temporary file (%lu)", GetLastError()); return; }
    DWORD n = 0;
    bool ok = s.size() <= MAXDWORD && WriteFile(f, s.data(), (DWORD)s.size(), &n, nullptr) && n == s.size();
    if (ok) ok = FlushFileBuffers(f) != FALSE;
    CloseHandle(f);
    if (ok) ok = MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) { Log(L"config: failed to save settings (%lu)", GetLastError()); DeleteFileW(tmp.c_str()); }
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
        else if (key == "hardware") g_optHardware = (val == "1");
        else if (key == "sortname") g_sortName = (val == "1");
        else if (key == "rotate") g_optRotation = (val == "1");
        else if (key == "batteryfps") g_optBatteryFps = (val == "1");
        else if (key == "preset") {
            VideoEdit e; int invert=0;
            if (sscanf(val.c_str(), "%d|%d|%d|%d|%d|%d|%d", &e.brightness, &e.contrast,
                       &e.saturation, &e.gamma, &e.warmth, &e.hue, &invert)==7) {
                e.invert=invert!=0; NormalizeEdit(e); g_userPreset=e; g_hasUserPreset=true;
            }
        } else if (key == "favorite") {
            size_t a=val.find('|');
            if (a!=std::string::npos) { int k=FindItem(U8ToW(val.substr(a+1)));
                if (k>=0) g_items[(size_t)k].favorite=(val.substr(0,a)=="1"); }
        }
        else if (key == "editordark") g_editorDark = (val != "0");
        else if (key == "active") active = U8ToW(val);
        else if (key == "selected") selected = U8ToW(val);
        else if (key == "speed") {
            size_t a = val.find('|');
            if (a != std::string::npos) {
                int k = FindItem(U8ToW(val.substr(a + 1)));
                if (k >= 0) g_items[(size_t)k].speed = std::clamp(atoi(val.substr(0, a).c_str()), 15, 200);
            }
        } else if (key == "edit") {
            size_t pathPos = 0;
            for (int i = 0; i < 7 && pathPos != std::string::npos; ++i) {
                pathPos = val.find('|', pathPos);
                if (pathPos != std::string::npos) ++pathPos;
            }
            VideoEdit e;
            if (pathPos != std::string::npos && sscanf(val.c_str(), "%d|%d|%d|%d|%d|%d|%d|",
                &e.left, &e.top, &e.right, &e.bottom, &e.brightness, &e.contrast, &e.saturation) == 7) {
                int k = FindItem(U8ToW(val.substr(pathPos)));
                if (k >= 0) { NormalizeEdit(e); g_items[(size_t)k].edit = e; }
            }
        } else if (key == "fx") {
            size_t pos = 0;
            for (int i=0; i<4 && pos!=std::string::npos; ++i) {
                pos = val.find('|', pos); if (pos != std::string::npos) ++pos;
            }
            int gamma=100, warmth=0, hue=0, invert=0;
            if (pos != std::string::npos && sscanf(val.c_str(), "%d|%d|%d|%d|", &gamma,&warmth,&hue,&invert)==4) {
                int k = FindItem(U8ToW(val.substr(pos)));
                if (k >= 0) {
                    VideoEdit& e = g_items[(size_t)k].edit;
                    e.gamma = gamma; e.warmth = warmth; e.hue = hue; e.invert = invert != 0; NormalizeEdit(e);
                }
            }
        } else if (key == "mirror") {
            size_t a = val.find('|'), b = a == std::string::npos ? a : val.find('|', a + 1);
            if (b != std::string::npos) {
                int k = FindItem(U8ToW(val.substr(b + 1)));
                if (k >= 0) {
                    g_items[(size_t)k].edit.flipX = atoi(val.substr(0, a).c_str()) != 0;
                    g_items[(size_t)k].edit.flipY = atoi(val.substr(a + 1, b - a - 1).c_str()) != 0;
                }
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
    bool Drained() {
        if (!h) return true;
        Reap();
        for (auto b : blocks) if (b->used) return false;
        return true;
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

struct Player {
    IMFSourceReader* rd = nullptr;
    ID3D11Device* gpuDevice = nullptr;
    ID3D11DeviceContext* gpuContext = nullptr;
    ID3D11Texture2D* gpuStaging = nullptr;
    IMFDXGIDeviceManager* gpuManager = nullptr;
    bool hardwareActive = false, noHardware = false;
    std::atomic<bool> frameCopyFailed{false};
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
    std::vector<BYTE> rawFrame;
    bool frameProcessed = false;
    VideoEdit appliedColor;
    FilterPool filterPool;
    struct SampleRelease { void operator()(IMFSample* s) const { if (s) s->Release(); } };
    using SamplePtr = std::unique_ptr<IMFSample, SampleRelease>;
    LatestFrameMailbox<SamplePtr> renderMailbox;
    std::thread renderThread;
    bool renderAsync = false;
    std::atomic<int> renderReplaced{0};
    HDC previewDC = nullptr;
    HBITMAP previewBmp = nullptr;
    HGDIOBJ previewOld = nullptr;
    BYTE* previewBits = nullptr;
    int previewW = 0, previewH = 0;
    double previewAt = 0;
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
    StereoGainLimiter limiter;
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
        return AllocateFrame(fw, fh);
    }
    bool AllocateFrame(UINT32 width, UINT32 height) {
        if (!width || !height || width > 16384 || height > 16384 || (uint64_t)width*height > 67108864) return false;
        if (width != dw || height != dh || !bits) {
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = (LONG)width; bi.bmiHeader.biHeight = -(LONG)height;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            if (!mem) mem = CreateCompatibleDC(nullptr);
            if (!mem) return false;
            void* pv = nullptr;
            HBITMAP nb = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &pv, nullptr, 0);
            if (!nb || !pv) { if (nb) DeleteObject(nb); return false; }
            HGDIOBJ o = SelectObject(mem, nb);
            if (!oldBmp) oldBmp = o;
            if (dib) DeleteObject(dib);
            dib = nb; bits = (BYTE*)pv; dw = width; dh = height; haveFrame = false;
        }
        return true;
    }
    bool OpenImage() {
        IWICImagingFactory* factory = nullptr; IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr; IWICBitmapScaler* scaler = nullptr;
        IWICFormatConverter* convert = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(p_path.c_str(), nullptr, GENERIC_READ,
                                                                  WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
        UINT width=0, height=0;
        if (SUCCEEDED(hr)) hr = frame->GetSize(&width, &height);
        if (SUCCEEDED(hr) && (!width || !height || width > 32768 || height > 32768 || (uint64_t)width*height > 120000000)) hr=E_FAIL;
        LoadLayout();
        int bw=1920, bh=1080;
        for (const auto& monitor : mons) {
            int w=monitor.right-monitor.left, h=monitor.bottom-monitor.top;
            if ((int64_t)w*h > (int64_t)bw*bh) { bw=w; bh=h; }
        }
        UINT targetW=width, targetH=height;
        if (SUCCEEDED(hr)) {
            double sx=bw/(double)width, sy=bh/(double)height;
            double scale=std::min(1.0, p_fit.load()==1 ? std::min(sx,sy) : std::max(sx,sy));
            scale=std::min(scale, std::sqrt(32000000.0/((double)width*height)));
            targetW=std::max(1u,(UINT)(width*scale)); targetH=std::max(1u,(UINT)(height*scale));
            if (targetW > 16384 || targetH > 16384) hr=E_FAIL;
        }
        IWICBitmapSource* input = frame;
        if (SUCCEEDED(hr) && (targetW != width || targetH != height)) {
            hr=factory->CreateBitmapScaler(&scaler);
            if (SUCCEEDED(hr)) hr=scaler->Initialize(frame,targetW,targetH,WICBitmapInterpolationModeFant);
            input=scaler;
        }
        if (SUCCEEDED(hr)) hr=factory->CreateFormatConverter(&convert);
        if (SUCCEEDED(hr)) hr=convert->Initialize(input, GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone,
                                                  nullptr, 0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(hr) && !AllocateFrame(targetW,targetH)) hr=E_OUTOFMEMORY;
        if (SUCCEEDED(hr)) hr=convert->CopyPixels(nullptr, targetW*4, targetW*targetH*4, bits);
        if (convert) convert->Release(); if (scaler) scaler->Release(); if (frame) frame->Release();
        if (decoder) decoder->Release(); if (factory) factory->Release();
        if (FAILED(hr)) { PlayerFail(L"Не удалось открыть изображение", hr); return false; }
        fw=dw; fh=dh; stride=(LONG)(dw*4); vis=RECT{0,0,(LONG)dw,(LONG)dh};
        rawFrame.clear(); frameProcessed=false; haveFrame=true;
        Log(L"image: original %ux%u, displayed %ux%u", width,height,dw,dh);
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
            resampler.warm();  // prepare before the playback clock starts
            resampler.reset(p_speed.load() / 100.0);
            smoothedGain = (p_mute.load() ? 0.0f : p_volume.load() / 100.0f);
            limiter.reset(aRate);
            if (fmtOk && out.Open((int)r, outCh)) hasAudio = true;
            Log(L"audio: %u Hz, %u ch, %u bit, float=%d, ok=%d", r, c, b, (int)aFloat, (int)hasAudio);
        }
        if (!hasAudio) rd->SetStreamSelection(SR_AUDIO, FALSE);
    }
    void ReleaseHardware() {
        if (gpuStaging) { gpuStaging->Release(); gpuStaging = nullptr; }
        if (gpuManager) { gpuManager->Release(); gpuManager = nullptr; }
        if (gpuContext) { gpuContext->Release(); gpuContext = nullptr; }
        if (gpuDevice) { gpuDevice->Release(); gpuDevice = nullptr; }
        hardwareActive = false;
    }
    bool SetupHardware(IMFAttributes* attributes) {
        if (!attributes || !p_hardware.load() || noHardware) return false;
        using MakeManager = HRESULT (WINAPI*)(UINT*, IMFDXGIDeviceManager**);
        HMODULE mf = GetModuleHandleW(L"mfplat.dll");
        auto makeManager = mf ? (MakeManager)(void*)GetProcAddress(mf, "MFCreateDXGIDeviceManager") : nullptr;
        if (!makeManager) { p_gpuState = 3; PostMessageW(g_main, WM_APP_STATE, 0, 0); return false; }
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &gpuDevice, nullptr, &gpuContext);
        ID3D10Multithread* multithread = nullptr;
        if (SUCCEEDED(hr)) hr = gpuDevice->QueryInterface(IID_PPV_ARGS(&multithread));
        if (SUCCEEDED(hr) && multithread) { multithread->SetMultithreadProtected(TRUE); multithread->Release(); }
        UINT token = 0;
        if (SUCCEEDED(hr)) hr = makeManager(&token, &gpuManager);
        if (SUCCEEDED(hr)) hr = gpuManager->ResetDevice(gpuDevice, token);
        if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, gpuManager);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (FAILED(hr)) {
            attributes->DeleteItem(MF_SOURCE_READER_D3D_MANAGER);
            attributes->DeleteItem(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS);
            Log(L"gpu: unavailable (0x%08X), using software", (unsigned)hr);
            ReleaseHardware(); p_gpuState = 3; PostMessageW(g_main, WM_APP_STATE, 0, 0); return false;
        }
        hardwareActive = true;
        p_gpuState = 2; PostMessageW(g_main, WM_APP_STATE, 0, 0);
        Log(L"gpu: D3D11 manager enabled; decoder selection is controlled by Windows");
        return true;
    }
    bool CopyGpuBuffer(IMFDXGIBuffer* surface) {
        if (!surface || !gpuDevice || !gpuContext) return false;
        ID3D11Texture2D* source = nullptr;
        UINT index = 0;
        HRESULT hr = surface->GetResource(IID_PPV_ARGS(&source));
        if (SUCCEEDED(hr)) hr = surface->GetSubresourceIndex(&index);
        if (FAILED(hr) || !source) { if (source) source->Release(); return false; }
        D3D11_TEXTURE2D_DESC desc = {}; source->GetDesc(&desc);
        bool supported = desc.Width >= dw && desc.Height >= dh && desc.SampleDesc.Count == 1 &&
            (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM);
        if (!supported) { source->Release(); return false; }
        if (gpuStaging) {
            D3D11_TEXTURE2D_DESC prev = {}; gpuStaging->GetDesc(&prev);
            if (prev.Width != desc.Width || prev.Height != desc.Height || prev.Format != desc.Format) {
                gpuStaging->Release(); gpuStaging = nullptr;
            }
        }
        if (!gpuStaging) {
            D3D11_TEXTURE2D_DESC staging = desc;
            staging.MipLevels = 1; staging.ArraySize = 1; staging.Usage = D3D11_USAGE_STAGING;
            staging.BindFlags = 0; staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ; staging.MiscFlags = 0;
            hr = gpuDevice->CreateTexture2D(&staging, nullptr, &gpuStaging);
        }
        if (SUCCEEDED(hr) && gpuStaging) {
            gpuContext->CopySubresourceRegion(gpuStaging, 0, 0, 0, 0, source, index, nullptr);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            hr = gpuContext->Map(gpuStaging, 0, D3D11_MAP_READ, 0, &mapped);
            if (SUCCEEDED(hr)) {
                if (mapped.RowPitch < (size_t)dw * 4) hr = E_FAIL;
                else {
                    GdiFlush();
                    for (UINT32 y = 0; y < dh; ++y)
                        memcpy(bits + (size_t)y * dw * 4, (BYTE*)mapped.pData + (size_t)y * mapped.RowPitch, (size_t)dw * 4);
                }
                gpuContext->Unmap(gpuStaging, 0);
            }
        }
        source->Release();
        return SUCCEEDED(hr) && gpuStaging;
    }
    bool Open() {
        frameCopyFailed = false;
        IMFAttributes* at = nullptr;
        fMFCreateAttributes(&at, 2);
        if (at) {
            at->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
            SetupHardware(at);
        } else if (p_hardware.load() && !noHardware) {
            p_gpuState = 3; PostMessageW(g_main, WM_APP_STATE, 0, 0);
        }
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
            double sc = std::min(1.0, (p_fit.load() == 1) ? std::min(sx, sy) : std::max(sx, sy));
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
        DWORD count = 0;
        if (FAILED(s->GetBufferCount(&count)) ||
            FAILED(count == 1 ? s->GetBufferByIndex(0, &buf) : s->ConvertToContiguousBuffer(&buf)) || !buf) {
            if (hardwareActive) frameCopyFailed = true;
            return;
        }
        const size_t row = (size_t)dw * 4;
        IMF2DBuffer* b2 = nullptr;
        BYTE* p = nullptr;
        LONG pitch = 0;
        bool done = false;
        IMFDXGIBuffer* gpuBuffer = nullptr;
        if (hardwareActive && SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&gpuBuffer))) && gpuBuffer) {
            done = CopyGpuBuffer(gpuBuffer);
            gpuBuffer->Release();
            // Do not treat an unsupported GPU surface as readable CPU memory.
            if (!done) { buf->Release(); frameCopyFailed = true; return; }
        }
        if (!done && SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2))) && b2) {
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
                done = rows == dh;
            }
        }
        buf->Release();
        if (!done) { if (hardwareActive) frameCopyFailed = true; return; }
        VideoEdit current;
        EnterCriticalSection(&g_cs); current = p_edit; LeaveCriticalSection(&g_cs);
        bool previewNeedsFrame = g_previewWindow.load() && (!g_previewFrozen.load() || g_previewRefresh.load());
        if (previewNeedsFrame || HasColorAdjustments(current) || current.flipX || current.flipY)
            rawFrame.assign(bits, bits + (size_t)dw * dh * 4);
        else rawFrame.clear();
        frameProcessed = false;
        haveFrame = true;
    }
    RECT VisibleForEdit(const VideoEdit& e) const {
        RECT r = vis;
        if (e.flipX) { r.left = (LONG)dw - vis.right; r.right = (LONG)dw - vis.left; }
        if (e.flipY) { r.top = (LONG)dh - vis.bottom; r.bottom = (LONG)dh - vis.top; }
        return r;
    }
    void CapturePreview(bool force, const RECT& visible) {
        HWND target = g_previewWindow.load();
        bool refresh = g_previewRefresh.load();
        if (!target || (g_previewFrozen.load() && !refresh) || (!force && !refresh && NowSec() - previewAt < 1.0 / 65.0)) return;
        int aw = visible.right - visible.left, ah = visible.bottom - visible.top;
        if (aw <= 0 || ah <= 0) return;
        double scale = std::min(1.0, 640.0 / std::max(aw, ah));
        int nw = std::max(1, (int)(aw * scale)), nh = std::max(1, (int)(ah * scale));
        if (!previewDC || nw != previewW || nh != previewH) {
            if (previewDC && previewOld) SelectObject(previewDC, previewOld);
            if (previewBmp) DeleteObject(previewBmp);
            if (!previewDC) previewDC = CreateCompatibleDC(nullptr);
            previewBmp = nullptr; previewBits = nullptr; previewOld = nullptr;
            BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = nw; bi.bmiHeader.biHeight = -nh;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            void* ptr = nullptr;
            previewBmp = CreateDIBSection(previewDC, &bi, DIB_RGB_COLORS, &ptr, nullptr, 0);
            if (!previewBmp || !ptr) return;
            previewBits = (BYTE*)ptr; previewOld = SelectObject(previewDC, previewBmp);
            previewW = nw; previewH = nh;
        }
        SetStretchBltMode(previewDC, COLORONCOLOR);
        BITMAPINFO source = {}; source.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        source.bmiHeader.biWidth = (LONG)dw; source.bmiHeader.biHeight = -(LONG)dh;
        source.bmiHeader.biPlanes = 1; source.bmiHeader.biBitCount = 32; source.bmiHeader.biCompression = BI_RGB;
        if (rawFrame.empty() || StretchDIBits(previewDC, 0, 0, previewW, previewH,
                visible.left, visible.top, aw, ah, rawFrame.data(), &source, DIB_RGB_COLORS, SRCCOPY) == (int)GDI_ERROR) return;
        GdiFlush();
        EnterCriticalSection(&g_cs);
        if (g_previewWindow.load() != target) { LeaveCriticalSection(&g_cs); return; }
        g_previewPixels.assign(previewBits, previewBits + (size_t)previewW * previewH * 4);
        g_previewWidth = previewW; g_previewHeight = previewH;
        ++g_previewGeneration;
        g_previewRefresh = false;
        LeaveCriticalSection(&g_cs);
        previewAt = NowSec();
        if (!g_previewMessage.exchange(true)) {
            if (!PostMessageW(target, WM_APP_PREVIEW, 0, 0)) g_previewMessage = false;
        }
    }
    void Draw(bool forcePreview = false) {
        if (!haveFrame || !mem) return;
        // If editing starts while a neutral frame is paused, retain it on demand.
        if (g_previewWindow.load() && (!g_previewFrozen.load() || g_previewRefresh.load()) && rawFrame.empty()) {
            GdiFlush(); rawFrame.assign(bits, bits + (size_t)dw * dh * 4);
        }
        CapturePreview(forcePreview, vis); // raw source; editor applies its draft at preview size
        VideoEdit e;
        EnterCriticalSection(&g_cs); e = p_edit; LeaveCriticalSection(&g_cs);
        if (!frameProcessed || !SameColorAdjustments(e, appliedColor)) {
            bool hadColor = frameProcessed && (HasColorAdjustments(appliedColor) || appliedColor.flipX || appliedColor.flipY);
            bool needsColor = HasColorAdjustments(e) || e.flipX || e.flipY;
            if (hadColor || needsColor) {
                GdiFlush();
                if (needsColor && !hadColor && rawFrame.empty()) rawFrame.assign(bits, bits + (size_t)dw * dh * 4);
                if (hadColor && rawFrame.size() == (size_t)dw * dh * 4) memcpy(bits, rawFrame.data(), rawFrame.size());
                FlipImage(bits, (int)dw, (int)dh, e.flipX, e.flipY);
                filterPool.apply(bits, (size_t)dw * dh, e);
            }
            appliedColor = e; frameProcessed = true;
        }
        RECT visible = VisibleForEdit(e);
        HWND w = g_wall.load();
        if (!w) return;
        HDC dc = GetDC(w);
        if (!dc) return;
        const int fit = p_fit.load();
        VideoRect crop = CropRect(visible.left, visible.top, visible.right - visible.left, visible.bottom - visible.top, e);
        const int ax = crop.x, ay = crop.y, aw = crop.width, ah = crop.height;
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
    void DestroyPreviewDC() {
        if (previewDC && previewOld) SelectObject(previewDC, previewOld);
        if (previewBmp) DeleteObject(previewBmp);
        if (previewDC) DeleteDC(previewDC);
        previewDC = nullptr; previewBmp = nullptr; previewOld = nullptr;
        previewBits = nullptr; previewW = previewH = 0;
    }
    void RenderFrame(IMFSample* sample, bool force) {
        if (layoutSeen != p_layoutGen.load()) LoadLayout();
        if (sample) CopyFrame(sample);
        Draw(force);
    }
    void StartRenderer() {
        renderMailbox.open();
        try {
            renderThread = std::thread([this] {
                HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                LatestFrameMailbox<SamplePtr>::Work work;
                int draws = 0; double totalMs = 0, maxMs = 0, start = NowSec();
                while (renderMailbox.wait(work)) {
                    double before = NowSec();
                    RenderFrame(work.frame ? work.frame->get() : nullptr, work.redraw);
                    double ms = (NowSec() - before) * 1000.0;
                    ++draws; totalMs += ms; maxMs = std::max(maxMs, ms);
                    if (NowSec() - start >= 10.0) {
                        int replaced = renderReplaced.exchange(0);
                        if (replaced || maxMs > 25.0)
                            Log(L"perf: renderer draws=%d avg=%.1fms max=%.1fms superseded=%d",
                                draws, totalMs / draws, maxMs, replaced);
                        draws = 0; totalMs = maxMs = 0; start = NowSec();
                    }
                }
                GdiFlush();
                // A memory DC made with CreateCompatibleDC(nullptr) belongs to
                // its creating thread; destroy it before that thread exits.
                DestroyPreviewDC();
                if (SUCCEEDED(co)) CoUninitialize();
            });
            renderAsync = true;
        } catch (...) {
            renderMailbox.close(); renderAsync = false;
            Log(L"renderer: thread unavailable; using synchronous fallback");
        }
    }
    void StopRenderer() {
        if (!renderAsync) return;
        renderMailbox.close();
        if (renderThread.joinable()) renderThread.join();
        renderAsync = false;
    }
    void QueueFrame(IMFSample* sample) {
        if (!renderAsync) { RenderFrame(sample, false); return; }
        sample->AddRef();
        if (renderMailbox.submit(SamplePtr(sample))) ++renderReplaced;
    }
    void RequestRedraw() {
        if (renderAsync) renderMailbox.redraw();
        else RenderFrame(nullptr, true);
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
                float left = resampled[i], right = resampled[i + 1];
                limiter.process(left, right, smoothedGain);
                pcm[i] = (short)lrintf(left * 32767.0f);
                pcm[i + 1] = (short)lrintf(right * 32767.0f);
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
        ReleaseHardware();
    }
    void Close() {
        CloseMedia();
        if (mem) {
            if (oldBmp) SelectObject(mem, oldBmp);
            DeleteDC(mem);
            mem = nullptr;
        }
        if (dib) { DeleteObject(dib); dib = nullptr; }
        DestroyPreviewDC(); // also covers the synchronous fallback
        bits = nullptr;
    }

    bool OpenWithFallback() {
        if (Open()) return true;
        bool retrySoftware = hardwareActive && !noHardware;
        CloseMedia();
        if (!retrySoftware) return false;
        noHardware = true; p_gpuState = 3; PostMessageW(g_main, WM_APP_STATE, 0, 0);
        Log(L"gpu: opening failed; retrying with software decoding");
        EnterCriticalSection(&g_cs); p_err.clear(); LeaveCriticalSection(&g_cs);
        return Open();
    }
    enum { R_QUIT, R_FAIL, R_REOPEN };
    void Run() {
        if (IsStaticImage(p_path)) {
            p_gpuState=0; // WIC images do not use the video decoder
            if (!OpenImage()) return;
            StartRenderer(); RequestRedraw();
            int layout = p_layoutGen.load();
            while (!p_quit.load()) {
                int current = p_layoutGen.load();
                if (current != layout) { layout=current; p_repaint=true; }
                if (p_repaint.exchange(false)) RequestRedraw();
                Sleep(25);
            }
            StopRenderer();
            return;
        }
        bool retried = false;
        for (;;) {
            if (!OpenWithFallback()) return;
            HRESULT hr = S_OK;
            StartRenderer();
            int r = Play(hr);
            StopRenderer();
            if (r == R_QUIT) return;
            bool retrySoftware = hardwareActive && !noHardware && (r == R_FAIL || frameCopyFailed.load());
            CloseMedia();
            if (retrySoftware) { noHardware = true; p_gpuState = 3; PostMessageW(g_main, WM_APP_STATE, 0, 0); Log(L"gpu: playback failed; retrying with software decoding"); continue; }
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
    HRESULT Seek(double seconds) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_I8;
        pv.hVal.QuadPart = (LONGLONG)(seconds * 1e7);
        return rd->SetCurrentPosition(kTimeFormatDefault, pv);
    }
    int Play(HRESULT& hr) {
        const double LEAD = 0.12;  // seconds of audio kept queued
        IMFSample *vS = nullptr, *aS = nullptr;
        double vPts = 0, aPts = 0, base = 0, aUntil = 0, lastV = 0, frameDur = 1.0 / 30, pauseAt = 0, eosWait = 0;
        bool clockSet = false, vEos = false, aEos = !hasAudio, wasPaused = false;
        double speed = p_speed.load() / 100.0;
        int framesThisPass = 0, skipped = 0, result = R_QUIT;
        int statDecoded = 0, statDrawn = 0, statDropped = 0;
        double statStart = NowSec(), statDecodeMs = 0;
        FrameRateGate frameRateGate;
        int requestedLayout = p_layoutGen.load();
        bool failed = false;
        hr = S_OK;

        while (!p_quit.load()) {
            if (hardwareActive && frameCopyFailed.load()) { result = R_REOPEN; break; }
            int layout = p_layoutGen.load();
            if (requestedLayout != layout) {
                requestedLayout = layout;
                p_repaint = true;
            }
            const bool paused = p_userPause.load() || p_autoPause.load();
            if (paused) {
                eosWait = 0;
                if (!wasPaused) {
                    wasPaused = true;
                    pauseAt = NowSec();
                    out.Pause(true);
                }
                if (p_repaint.exchange(false)) RequestRedraw();
                Sleep(25);
                continue;
            }
            if (wasPaused) {
                wasPaused = false;
                base += NowSec() - pauseAt;
                out.Pause(false);
            }
            if (p_repaint.exchange(false)) RequestRedraw();

            // 1) keep one decoded video frame waiting
            if (!vS && !vEos) {
                DWORD idx = 0, flags = 0;
                LONGLONG ts = 0;
                IMFSample* s = nullptr;
                double decodeStart = NowSec();
                hr = rd->ReadSample(SR_VIDEO, 0, &idx, &flags, &ts, &s);
                statDecodeMs += (NowSec() - decodeStart) * 1000.0; ++statDecoded;
                if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
                    if (s) s->Release();
                    failed = true;
                    break;
                }
                if (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
                    StopRenderer(); // drain all users of the old DIB before replacing it
                    if (!QueryVideoType()) {
                        if (s) s->Release();
                        hr = E_FAIL;
                        failed = true;
                        break;
                    }
                    StartRenderer();
                }
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) vEos = true;
                if (s) {
                    vPts = (double)ts / 1e7;
                    vS = s;
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
            if (vS && now - vPts > 0.5 * speed) {
                // far behind (PC woke from sleep, long stall): continue from this frame instead of racing to catch up
                base = NowSec() - vPts / speed;
                now = vPts;
                aUntil = now;
                out.Reset(false);
            }

            // 2) feed audio a little ahead of the clock
            if (hasAudio && clockSet) {
                int guard = 0;
                while ((!aEos || aS) && aUntil - now < LEAD * speed && guard++ < 96) {
                    now = (NowSec() - base) * speed;
                    if (aUntil - now >= LEAD * speed) break;
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
                    now = (NowSec() - base) * speed;
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

            // Decode and audio processing take time; do not schedule against a stale clock.
            now = (NowSec() - base) * speed;
            // 3) show the frame when its time comes
            if (vS) {
                double d = vPts - now;
                if (d <= 0.001) {
                    bool late = d / speed < -0.12;
                    if (!late || ++skipped > 4) {
                        if (frameRateGate.allow(NowSec(), p_fpsLimit.load())) {
                            QueueFrame(vS);
                            ++statDrawn;
                        }
                        skipped = 0;
                    } else ++statDropped;
                    if (NowSec() - statStart >= 10.0 && statDecoded > 0) {
                        if (statDropped > 0 || statDecodeMs / statDecoded > 20.0)
                            Log(L"perf: video reads=%d avg=%.1fms queued=%d dropped=%d speed=%d%%",
                                statDecoded, statDecodeMs/statDecoded, statDrawn, statDropped, p_speed.load());
                        statStart = NowSec(); statDecoded = statDrawn = statDropped = 0;
                        statDecodeMs = 0;
                    }
                    if (vPts > lastV && vPts - lastV < 0.5) frameDur = vPts - lastV;
                    lastV = vPts;
                    framesThisPass++;
                    shown++;
                    vS->Release();
                    vS = nullptr;
                } else {
                    Sleep(d / speed > 0.008 ? 3 : 1);
                }
            } else if (vEos && aEos && !aS) {
                double endT = std::max(lastV + frameDur, hasAudio ? aUntil : 0.0);
                if (!clockSet || now >= endT - 0.004) {
                    // Do not reset waveOut while the last queued block is still
                    // playing. Bound the wait for broken/stalled drivers.
                    if (hasAudio && !out.Drained()) {
                        if (eosWait == 0) eosWait = NowSec();
                        if (NowSec() - eosWait < 1.0) { Sleep(3); continue; }
                        Log(L"audio: loop drain timed out, resetting output");
                    }
                    eosWait = 0;
                    if (framesThisPass == 0) {
                        hr = E_FAIL;
                        failed = true;
                        break;
                    }
                    hr = Seek(0);
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
static bool StopPlayer() {
    if (p_thread) {
        p_quit = true;
        if (WaitForSingleObject(p_thread, 10000) != WAIT_OBJECT_0) {
            // Never reuse shared player state while a decoder is still running.
            Log(L"player did not stop in time");
            g_abandonedPlayer = true;
            return false;
        }
        CloseHandle(p_thread);
        p_thread = nullptr;
    }
    p_quit = false;
    return true;
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
static void CloseEditor(bool apply);
static bool StartWallpaper(int idx) {
    CloseEditor(false);
    if (idx < 0 || idx >= (int)g_items.size()) return false;
    if (p_thread && WaitForSingleObject(p_thread, 0) != WAIT_OBJECT_0) {
        g_pendingAction = 1;
        g_pendingPath = g_items[(size_t)idx].path;
        g_sel = idx;
        p_quit = true;
        SetTimer(g_main, 3, 50, nullptr);
        InvalidateRect(g_main, nullptr, FALSE);
        return true;
    }
    if (!StopPlayer()) return false;
    g_pendingAction = 0; g_pendingPath.clear(); KillTimer(g_main, 3);
    if (g_reattachPending) { DestroyWall(); g_reattachPending = false; }
    SetErr(L"");
    g_active = idx;
    g_sel = idx;
    g_rotationClock.reset();
    if (!g_mfOk && !IsStaticImage(g_items[(size_t)idx].path)) {
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
    p_hardware = g_optHardware; p_gpuState = g_optHardware ? 1 : 0;
    EnterCriticalSection(&g_cs); p_edit = g_items[(size_t)idx].edit; LeaveCriticalSection(&g_cs);
    p_userPause = false;
    p_thread = CreateThread(nullptr, 0, PlayerThread, nullptr, 0, nullptr);
    if (!p_thread) {
        g_active = -1;
        SetErr(L"Не удалось запустить поток воспроизведения");
        SaveConfig(); InvalidateRect(g_main, nullptr, FALSE);
        return false;
    }
    Log(L"start: %ls", p_path.c_str());
    SaveConfig();
    InvalidateRect(g_main, nullptr, FALSE);
    return true;
}
static void StopWallpaper() {
    CloseEditor(false);
    g_reattachPending = false;
    if (p_thread && WaitForSingleObject(p_thread, 0) != WAIT_OBJECT_0) {
        g_pendingAction = 2; g_pendingPath.clear(); p_quit = true;
        g_active = -1; p_userPause = false; p_autoPause = false;
        SetTimer(g_main, 3, 50, nullptr);
        SaveConfig(); InvalidateRect(g_main, nullptr, FALSE);
        return;
    }
    if (!StopPlayer()) return;
    g_pendingAction = 0; g_pendingPath.clear(); KillTimer(g_main, 3);
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
                                    L"LoopDeskMain", L"LoopDeskWall", L"LoopDeskEditor", L"LoopDeskPreview"};
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
    if (!RectVisible(dc, &r)) return;
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
// Cover-fit the embedded artwork inside the empty-library card.
static void DrawAmbientArt(HDC dc, const RECT& r) {
    if (!g_ambientArt) { Round(dc, r, S(14), C_TILE, C_LINE); return; }
    BITMAP bm = {};
    if (!GetObjectW(g_ambientArt, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return;
    int width = r.right-r.left, height = r.bottom-r.top;
    if (width <= 0 || height <= 0) return;
    double scale = std::max(width / (double)bm.bmWidth, height / (double)bm.bmHeight);
    int sw = std::min((int)bm.bmWidth, (int)(width / scale + .5));
    int sh = std::min((int)bm.bmHeight, (int)(height / scale + .5));
    HDC src = CreateCompatibleDC(dc); if (!src) return;
    HGDIOBJ old = SelectObject(src, g_ambientArt);
    int saved = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(r.left, r.top, r.right, r.bottom, S(14), S(14));
    if (clip) { ExtSelectClipRgn(dc, clip, RGN_AND); DeleteObject(clip); }
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchBlt(dc, r.left, r.top, width, height, src, (bm.bmWidth-sw)/2, (bm.bmHeight-sh)/2, sw, sh, SRCCOPY);
    RestoreDC(dc, saved); SelectObject(src, old); DeleteDC(src);
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

// ------------------------------------------------------------------ non-destructive frame editor
static HWND g_editor = nullptr, g_edPreview = nullptr;
static HWND g_edTracks[6] = {}, g_edLabels[6] = {}, g_edButtons[9] = {}, g_edMirror[3] = {}, g_edTitle = nullptr, g_edPreset = nullptr;
static HBRUSH g_editorDarkBrush = nullptr;
static HDC g_previewPaintDC = nullptr;
static HBITMAP g_previewPaintBitmap = nullptr;
static HGDIOBJ g_previewPaintOld = nullptr;
static int g_previewPaintW = 0, g_previewPaintH = 0;
static std::vector<BYTE> g_previewDisplay;
static unsigned long long g_displayGeneration = 0;
static int g_displayWidth = 0, g_displayHeight = 0;
static VideoEdit g_displayEdit;
static std::wstring g_editorPath;
static VideoEdit g_editDraft;
static RECT g_previewImage = {};
static int g_cropDrag = 0, g_cropLast = 16;
static POINT g_cropAnchor = {};
static VideoEdit g_dragOriginal;
enum { ED_BRIGHT = 101, ED_CONTRAST, ED_SAT, ED_GAMMA, ED_WARMTH, ED_HUE,
       ED_RESET = 110, ED_FREE, ED_169, ED_43, ED_11, ED_APPLY, ED_CANCEL,
       ED_FLIP_X, ED_FLIP_Y, ED_PAUSE, ED_INVERT, ED_THEME, ED_PRESETS };
static const wchar_t* EDITOR_CLASS = L"LoopDeskEditor";
static const wchar_t* PREVIEW_CLASS = L"LoopDeskPreview";
static const wchar_t* SLIDER_CLASS = L"LoopDeskEditorSlider";

static void PublishEdit() {
    NormalizeEdit(g_editDraft);
    // Preview draft at reduced resolution. The wallpaper is updated only on Apply.
    if (g_edPreview) InvalidateRect(g_edPreview, nullptr, FALSE);
}
static void SetEditorLabels() {
    const wchar_t* names[] = {L"Яркость", L"Контраст", L"Насыщенность", L"Гамма", L"Теплота", L"Цветовой тон"};
    int values[] = {g_editDraft.brightness, g_editDraft.contrast, g_editDraft.saturation,
                    g_editDraft.gamma, g_editDraft.warmth, g_editDraft.hue};
    for (int i = 0; i < 6; ++i) {
        wchar_t text[96]; _snwprintf(text, 95, L"%ls: %d", names[i], values[i]); text[95] = 0;
        if (g_edLabels[i]) {
            wchar_t old[96]; GetWindowTextW(g_edLabels[i], old, 96);
            if (lstrcmpW(old, text)) SetWindowTextW(g_edLabels[i], text);
        }
        int pos = i == 0 || i == 4 ? values[i] + 100 : i == 5 ? values[i] + 180 : values[i];
        if (g_edTracks[i]) SendMessageW(g_edTracks[i], TBM_SETPOS, TRUE, pos);
    }
}
static void CloseEditor(bool apply) {
    if (!g_editor) return;
    int idx = FindItem(g_editorPath);
    if (apply && idx >= 0) {
        g_items[(size_t)idx].edit = g_editDraft; SaveConfig();
        if (idx == g_active) {
            EnterCriticalSection(&g_cs); p_edit = g_editDraft; LeaveCriticalSection(&g_cs);
            p_repaint = true;
        }
    }
    EnterCriticalSection(&g_cs);
    g_previewWindow = nullptr; g_previewRefresh = false;
    g_previewPixels.clear(); g_previewWidth = 0; g_previewHeight = 0; ++g_previewGeneration;
    LeaveCriticalSection(&g_cs);
    HWND h = g_editor; g_editor = nullptr; g_edPreview = nullptr; g_edPreset = nullptr;
    DestroyWindow(h);
    g_previewMessage = false;
    g_previewDisplay.clear(); g_displayWidth = g_displayHeight = 0; g_displayGeneration = 0;
    InvalidateRect(g_main, nullptr, FALSE);
}
static RECT PreviewImageRect(HWND h, int vw, int vh) {
    RECT r; GetClientRect(h, &r);
    if (vw <= 0 || vh <= 0 || r.right <= 0 || r.bottom <= 0) return RECT{};
    double scale = std::min((double)r.right / vw, (double)r.bottom / vh);
    int w = std::max(1, (int)(vw * scale)), hgt = std::max(1, (int)(vh * scale));
    int x = (r.right - w) / 2, y = (r.bottom - hgt) / 2;
    return RECT{x, y, x + w, y + hgt};
}
static RECT CropOnPreview() {
    int w = g_previewImage.right - g_previewImage.left, h = g_previewImage.bottom - g_previewImage.top;
    return RECT{g_previewImage.left + MulDiv(w, g_editDraft.left, 10000),
                g_previewImage.top + MulDiv(h, g_editDraft.top, 10000),
                g_previewImage.left + MulDiv(w, g_editDraft.right, 10000),
                g_previewImage.top + MulDiv(h, g_editDraft.bottom, 10000)};
}
static LRESULT CALLBACK PreviewProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_APP_PREVIEW:
        g_previewMessage = false; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC screen = BeginPaint(h, &ps);
        RECT client; GetClientRect(h, &client);
        int width = std::max(1L, client.right), height = std::max(1L, client.bottom);
        if (!g_previewPaintDC) g_previewPaintDC = CreateCompatibleDC(screen);
        if (g_previewPaintDC && (!g_previewPaintBitmap || width != g_previewPaintW || height != g_previewPaintH)) {
            if (g_previewPaintOld) SelectObject(g_previewPaintDC, g_previewPaintOld);
            if (g_previewPaintBitmap) DeleteObject(g_previewPaintBitmap);
            g_previewPaintBitmap = CreateCompatibleBitmap(screen, width, height);
            g_previewPaintOld = g_previewPaintBitmap ? SelectObject(g_previewPaintDC, g_previewPaintBitmap) : nullptr;
            g_previewPaintW = width; g_previewPaintH = height;
        }
        HDC dc = g_previewPaintBitmap ? g_previewPaintDC : screen;
        FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
        bool changed = false;
        EnterCriticalSection(&g_cs);
        if (g_previewGeneration != g_displayGeneration || !SameColorAdjustments(g_displayEdit, g_editDraft)) {
            LeaveCriticalSection(&g_cs);
            GdiFlush(); // finish any previous use of the preview buffer before modifying it
            EnterCriticalSection(&g_cs);
            g_previewDisplay = g_previewPixels;
            g_displayWidth = g_previewWidth; g_displayHeight = g_previewHeight;
            g_displayGeneration = g_previewGeneration; g_displayEdit = g_editDraft; changed = true;
        }
        LeaveCriticalSection(&g_cs);
        int w = g_displayWidth, ht = g_displayHeight;
        if (changed && w > 0 && ht > 0 && g_previewDisplay.size() == (size_t)w * ht * 4) {
            FlipImage(g_previewDisplay.data(), w, ht, g_editDraft.flipX, g_editDraft.flipY);
            AdjustColors(g_previewDisplay.data(), (size_t)w * ht, g_editDraft);
        }
        g_previewImage = PreviewImageRect(h, w, ht);
        if (w > 0 && ht > 0 && g_previewDisplay.size() == (size_t)w * ht * 4) {
            BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -ht; bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, g_previewImage.left, g_previewImage.top, g_previewImage.right-g_previewImage.left,
                          g_previewImage.bottom-g_previewImage.top, 0, 0, w, ht, g_previewDisplay.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        if (w > 0 && ht > 0) {
            RECT c = CropOnPreview();
            // A solid outline avoids the moving hatch pattern that caused visual noise.
            HPEN shadow = CreatePen(PS_SOLID, std::max(4, S(4)), RGB(0,0,0));
            HGDIOBJ p0 = SelectObject(dc, shadow), b0 = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, c.left, c.top, c.right, c.bottom);
            SelectObject(dc, b0); SelectObject(dc, p0); DeleteObject(shadow);
            HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), RGB(255,255,255));
            HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, c.left, c.top, c.right, c.bottom);
            SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
            HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
            int px[] = {c.left, (c.left+c.right)/2, c.right};
            int py[] = {c.top, (c.top+c.bottom)/2, c.bottom};
            for (int ix=0; ix<3; ++ix) for (int iy=0; iy<3; ++iy) {
                if (ix==1 && iy==1) continue;
                RECT b = {px[ix]-S(4), py[iy]-S(4), px[ix]+S(5), py[iy]+S(5)}; FillRect(dc, &b, white);
            }
        } else {
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(220,220,220));
            DrawTextW(dc, L"Ожидание кадра видео…", -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (g_previewPaintBitmap) BitBlt(screen, 0, 0, client.right, client.bottom, g_previewPaintDC, 0, 0, SRCCOPY);
        EndPaint(h, &ps); return 0;
    }
    case WM_LBUTTONDOWN: {
        int width = g_previewImage.right - g_previewImage.left, height = g_previewImage.bottom - g_previewImage.top;
        if (width <= 0 || height <= 0) return 0;
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT r = CropOnPreview(); int tolerance = S(14);
        int flags = 0;
        if (p.y >= r.top-tolerance && p.y <= r.bottom+tolerance) {
            if (std::abs(p.x-r.left) <= tolerance) flags |= 1;
            else if (std::abs(p.x-r.right) <= tolerance) flags |= 2;
        }
        if (p.x >= r.left-tolerance && p.x <= r.right+tolerance) {
            if (std::abs(p.y-r.top) <= tolerance) flags |= 4;
            else if (std::abs(p.y-r.bottom) <= tolerance) flags |= 8;
        }
        if (!flags && PtInRect(&r,p)) flags = 16; // move the selection
        if (!flags) return 0;
        g_cropDrag = flags; g_cropLast = flags; g_cropAnchor = p; g_dragOriginal = g_editDraft;
        SetFocus(h); SetCapture(h); return 0;
    }
    case WM_MOUSEMOVE:
        if (g_cropDrag) {
            int width = g_previewImage.right-g_previewImage.left, height = g_previewImage.bottom-g_previewImage.top;
            if (width <= 0 || height <= 0) return 0;
            int dx = MulDiv(GET_X_LPARAM(lp)-g_cropAnchor.x, 10000, width);
            int dy = MulDiv(GET_Y_LPARAM(lp)-g_cropAnchor.y, 10000, height);
            g_editDraft = g_dragOriginal;
            if (g_cropDrag == 16) {
                dx = std::clamp(dx, -g_dragOriginal.left, 10000-g_dragOriginal.right);
                dy = std::clamp(dy, -g_dragOriginal.top, 10000-g_dragOriginal.bottom);
                g_editDraft.left += dx; g_editDraft.right += dx; g_editDraft.top += dy; g_editDraft.bottom += dy;
            } else {
                if (g_cropDrag & 1) g_editDraft.left = std::clamp(g_dragOriginal.left+dx, 0, g_dragOriginal.right-100);
                if (g_cropDrag & 2) g_editDraft.right = std::clamp(g_dragOriginal.right+dx, g_dragOriginal.left+100, 10000);
                if (g_cropDrag & 4) g_editDraft.top = std::clamp(g_dragOriginal.top+dy, 0, g_dragOriginal.bottom-100);
                if (g_cropDrag & 8) g_editDraft.bottom = std::clamp(g_dragOriginal.bottom+dy, g_dragOriginal.top+100, 10000);
            }
            PublishEdit();
        }
        return 0;
    case WM_LBUTTONUP: if (g_cropDrag) { g_cropDrag = 0; ReleaseCapture(); } return 0;
    case WM_KEYDOWN: {
        int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 100 : 10; // 1% or 0.1%
        int dx = wp == VK_LEFT ? -step : wp == VK_RIGHT ? step : 0;
        int dy = wp == VK_UP ? -step : wp == VK_DOWN ? step : 0;
        if (!dx && !dy) break;
        if (g_cropLast == 16) {
            dx = std::clamp(dx, -g_editDraft.left, 10000-g_editDraft.right);
            dy = std::clamp(dy, -g_editDraft.top, 10000-g_editDraft.bottom);
            g_editDraft.left += dx; g_editDraft.right += dx; g_editDraft.top += dy; g_editDraft.bottom += dy;
        } else {
            if (g_cropLast & 1) g_editDraft.left = std::clamp(g_editDraft.left+dx, 0, g_editDraft.right-100);
            if (g_cropLast & 2) g_editDraft.right = std::clamp(g_editDraft.right+dx, g_editDraft.left+100, 10000);
            if (g_cropLast & 4) g_editDraft.top = std::clamp(g_editDraft.top+dy, 0, g_editDraft.bottom-100);
            if (g_cropLast & 8) g_editDraft.bottom = std::clamp(g_editDraft.bottom+dy, g_editDraft.top+100, 10000);
        }
        PublishEdit(); return 0;
    }
    case WM_CAPTURECHANGED: g_cropDrag = 0; return 0;
    case WM_DESTROY:
        if (g_previewPaintDC && g_previewPaintOld) SelectObject(g_previewPaintDC, g_previewPaintOld);
        if (g_previewPaintBitmap) DeleteObject(g_previewPaintBitmap);
        if (g_previewPaintDC) DeleteDC(g_previewPaintDC);
        g_previewPaintDC = nullptr; g_previewPaintBitmap = nullptr; g_previewPaintOld = nullptr;
        g_previewPaintW = g_previewPaintH = 0;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static void EditorPreset(double aspect) {
    if (aspect <= 0) { g_editDraft.left=0; g_editDraft.top=0; g_editDraft.right=10000; g_editDraft.bottom=10000; }
    else {
        EnterCriticalSection(&g_cs); int w=g_previewWidth, h=g_previewHeight; LeaveCriticalSection(&g_cs);
        if (w <= 0 || h <= 0) return;
        double source = (double)w / h;
        int cw = 10000, ch = 10000;
        if (source > aspect) cw = (int)(10000 * aspect / source);
        else ch = (int)(10000 * source / aspect);
        cw = std::clamp(cw,100,10000); ch = std::clamp(ch,100,10000);
        g_editDraft.left = (10000-cw)/2; g_editDraft.right = g_editDraft.left+cw;
        g_editDraft.top = (10000-ch)/2; g_editDraft.bottom = g_editDraft.top+ch;
    }
    PublishEdit();
}
static void EditorLayout(HWND h) {
    RECT r; GetClientRect(h, &r); int w=r.right, ht=r.bottom;
    int x=w-S(270), leftW=std::max(S(100), x-S(28)), half=S(121);
    MoveWindow(g_edPreview, S(12), S(46), leftW, std::max(S(100), ht-S(64)), TRUE);
    MoveWindow(g_edTitle, S(12), S(10), leftW, S(28), TRUE);
    for (int i=0;i<6;i++) {
        MoveWindow(g_edLabels[i], x, S(8+i*43), S(250), S(20), TRUE);
        MoveWindow(g_edTracks[i], x, S(27+i*43), S(250), S(24), TRUE);
    }
    for (int i=0;i<3;i++) MoveWindow(g_edMirror[i], x, S(269+i*24), S(250), S(23), TRUE);
    MoveWindow(g_edButtons[1], x, S(344), half, S(27), TRUE);
    MoveWindow(g_edButtons[2], x+S(129), S(344), half, S(27), TRUE);
    MoveWindow(g_edButtons[3], x, S(375), half, S(27), TRUE);
    MoveWindow(g_edButtons[4], x+S(129), S(375), half, S(27), TRUE);
    MoveWindow(g_edButtons[0], x, S(406), S(250), S(27), TRUE);
    MoveWindow(g_edButtons[7], x, S(437), S(250), S(27), TRUE);
    MoveWindow(g_edButtons[8], x, S(468), S(250), S(27), TRUE);
    MoveWindow(g_edPreset, x, S(499), S(250), S(27), TRUE);
    MoveWindow(g_edButtons[5], x, ht-S(43), half, S(30), TRUE);
    MoveWindow(g_edButtons[6], x+S(129), ht-S(43), half, S(30), TRUE);
}
static void EditorTitleTheme(HWND h) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        typedef HRESULT(WINAPI * Fn)(HWND, DWORD, LPCVOID, DWORD);
        Fn fn = (Fn)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
        BOOL dark = g_editorDark ? TRUE : FALSE;
        if (fn && FAILED(fn(h,20,&dark,sizeof(dark)))) fn(h,19,&dark,sizeof(dark));
        FreeLibrary(dwm);
    }
}
// Own slider drawing avoids Windows theme-dependent invisible trackbars.
struct EditorSlider { int lo=0, hi=100, value=0, page=10; bool drag=false; };
static LRESULT CALLBACK EditorSliderProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    EditorSlider* data = (EditorSlider*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        data = new EditorSlider(); SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)data);
    }
    if (!data) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
    case TBM_SETRANGE:
        data->lo = (short)LOWORD(lp); data->hi = (short)HIWORD(lp);
        if (data->hi <= data->lo) data->hi = data->lo + 1;
        data->value = std::clamp(data->value, data->lo, data->hi);
        InvalidateRect(h, nullptr, FALSE); return 0;
    case TBM_SETPOS: {
        int v=std::clamp((int)lp, data->lo, data->hi);
        if (data->value != v) { data->value=v; if (wp) InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case TBM_GETPOS: return data->value;
    case TBM_SETPAGESIZE: { int old = data->page; data->page = std::max(1, (int)lp); return old; }
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_ERASEBKGND: return 1;
    case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC screen=BeginPaint(h, &ps); RECT r; GetClientRect(h, &r);
        HDC mem=CreateCompatibleDC(screen); HBITMAP bitmap=mem ? CreateCompatibleBitmap(screen, std::max(1L,r.right), std::max(1L,r.bottom)) : nullptr;
        HGDIOBJ old=bitmap ? SelectObject(mem, bitmap) : nullptr; HDC dc=bitmap ? mem : screen;
        Fill(dc,r,g_editorDark ? RGB(12,12,12) : GetSysColor(COLOR_BTNFACE));
        int margin=S(10), left=margin, right=std::max(left+1,(int)r.right-margin), cy=(int)r.bottom/2;
        int x=left+SliderPixel(data->value,data->lo,data->hi,right-left);
        RECT track={left,cy-S(3),right,cy+S(3)};
        Round(dc,track,S(5),g_editorDark ? RGB(112,126,145) : RGB(165,175,190),g_editorDark ? RGB(112,126,145) : RGB(165,175,190));
        RECT active={left,cy-S(3),x,cy+S(3)}; Fill(dc,active,C_ACC);
        HGDIOBJ ob=SelectObject(dc,GetStockObject(DC_BRUSH)), op=SelectObject(dc,GetStockObject(DC_PEN));
        SetDCBrushColor(dc,RGB(255,255,255)); SetDCPenColor(dc,C_ACC);
        int radius=S(7); Ellipse(dc,x-radius,cy-radius,x+radius+1,cy+radius+1);
        SelectObject(dc,ob); SelectObject(dc,op);
        if (GetFocus()==h) { RECT focus=r; InflateRect(&focus,-1,-1); DrawFocusRect(dc,&focus); }
        if (bitmap) { BitBlt(screen,0,0,r.right,r.bottom,mem,0,0,SRCCOPY); SelectObject(mem,old); DeleteObject(bitmap); }
        if (mem) DeleteDC(mem); EndPaint(h,&ps); return 0;
    }
    case WM_LBUTTONDOWN: SetFocus(h); SetCapture(h); data->drag=true; [[fallthrough]];
    case WM_MOUSEMOVE:
        if (data->drag) {
            RECT r; GetClientRect(h,&r); int left=S(10), right=std::max(left+1,(int)r.right-S(10));
            int x=std::clamp(GET_X_LPARAM(lp),left,right);
            int v=SliderValue(x-left,data->lo,data->hi,right-left);
            if (v != data->value) { data->value=v; InvalidateRect(h,nullptr,FALSE);
                SendMessageW(GetParent(h),WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,v),(LPARAM)h); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (data->drag) { data->drag=false; ReleaseCapture(); SendMessageW(GetParent(h),WM_HSCROLL,MAKEWPARAM(TB_ENDTRACK,0),(LPARAM)h); }
        return 0;
    case WM_CAPTURECHANGED: data->drag=false; return 0;
    case WM_KEYDOWN: {
        int v=data->value;
        if (wp==VK_LEFT || wp==VK_DOWN) --v; else if (wp==VK_RIGHT || wp==VK_UP) ++v;
        else if (wp==VK_PRIOR) v+=data->page; else if (wp==VK_NEXT) v-=data->page;
        else if (wp==VK_HOME) v=data->lo; else if (wp==VK_END) v=data->hi; else break;
        v=std::clamp(v,data->lo,data->hi);
        if (v!=data->value) { data->value=v; InvalidateRect(h,nullptr,FALSE);
            SendMessageW(GetParent(h),WM_HSCROLL,MAKEWPARAM(TB_THUMBPOSITION,v),(LPARAM)h); }
        return 0;
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(h,GWLP_USERDATA,0); delete data; return DefWindowProcW(h,m,wp,lp);
    }
    return DefWindowProcW(h,m,wp,lp);
}
static HWND EditorChild(HWND parent, const wchar_t* cls, const wchar_t* label, DWORD style, int id) {
    HWND w = CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
                             (HMENU)(INT_PTR)id, g_hi, nullptr);
    if (w) SendMessageW(w, WM_SETFONT, (WPARAM)g_fUI, TRUE);
    return w;
}
static LRESULT CALLBACK EditorProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g_edPreview = EditorChild(h, PREVIEW_CLASS, L"", 0, 200);
        g_edTitle = EditorChild(h, L"STATIC", L"Обрезка: перетащите рамку; стрелки — 0,1%, Shift — 1%", 0, 201);
        if (!g_editorDarkBrush) g_editorDarkBrush = CreateSolidBrush(RGB(12,12,12));
        EditorTitleTheme(h);
        for (int i=0;i<6;i++) {
            g_edLabels[i] = EditorChild(h, L"STATIC", L"", 0, 210+i);
            g_edTracks[i] = EditorChild(h, SLIDER_CLASS, L"", WS_TABSTOP, ED_BRIGHT+i);
            int low = i == 3 ? 25 : 0, high = i == 3 ? 300 : i == 5 ? 360 : 200;
            SendMessageW(g_edTracks[i], TBM_SETRANGE, TRUE, MAKELONG(low, high));
            SendMessageW(g_edTracks[i], TBM_SETPAGESIZE, 0, 10);
        }
        g_edMirror[0] = EditorChild(h, L"BUTTON", L"Отразить по горизонтали", WS_TABSTOP | BS_OWNERDRAW, ED_FLIP_X);
        g_edMirror[1] = EditorChild(h, L"BUTTON", L"Отразить по вертикали", WS_TABSTOP | BS_OWNERDRAW, ED_FLIP_Y);
        g_edMirror[2] = EditorChild(h, L"BUTTON", L"Инверсия цветов", WS_TABSTOP | BS_OWNERDRAW, ED_INVERT);
        const wchar_t* labels[] = {L"Сбросить всё",L"Весь кадр",L"Обрезка 16:9",L"Обрезка 4:3",L"Обрезка 1:1",L"Применить",L"Отмена", L"Остановить кадр", g_editorDark ? L"Тема: чёрная" : L"Тема: светлая"};
        int ids[] = {ED_RESET,ED_FREE,ED_169,ED_43,ED_11,ED_APPLY,ED_CANCEL,ED_PAUSE,ED_THEME};
        for(int i=0;i<9;i++) g_edButtons[i]=EditorChild(h,L"BUTTON",labels[i],WS_TABSTOP | BS_OWNERDRAW,ids[i]);
        g_edPreset = EditorChild(h, L"BUTTON", L"Цветовые пресеты…", WS_TABSTOP | BS_OWNERDRAW, ED_PRESETS);
        SetEditorLabels(); EditorLayout(h);
        g_previewMessage = false;
        EnterCriticalSection(&g_cs);
        g_previewWindow = g_edPreview; g_previewFrozen = false; g_previewRefresh = true;
        LeaveCriticalSection(&g_cs);
        p_repaint = true;
        return 0;
    }
    case WM_SIZE: if (g_edPreview) EditorLayout(h); return 0;
    case WM_ERASEBKGND: {
        RECT r; GetClientRect(h, &r);
        FillRect((HDC)wp, &r, g_editorDark && g_editorDarkBrush ? g_editorDarkBrush : GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, g_editorDark ? RGB(235,238,245) : RGB(25,25,25));
        SetBkColor(dc, g_editorDark ? RGB(12,12,12) : GetSysColor(COLOR_BTNFACE));
        return (LRESULT)(g_editorDark && g_editorDarkBrush ? g_editorDarkBrush : GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (!d || d->CtlType != ODT_BUTTON) break;
        HDC dc = d->hDC; RECT r = d->rcItem;
        int id = (int)d->CtlID;
        bool check = id == ED_FLIP_X || id == ED_FLIP_Y || id == ED_INVERT;
        COLORREF bg = g_editorDark ? RGB(12,12,12) : GetSysColor(COLOR_BTNFACE);
        COLORREF fg = g_editorDark ? RGB(235,238,245) : RGB(30,30,30);
        Fill(dc, r, bg);
        wchar_t text[128]; GetWindowTextW(d->hwndItem, text, 128);
        HFONT font = g_fUI; HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, fg);
        RECT textRect = r;
        if (check) {
            bool checked = id == ED_FLIP_X ? g_editDraft.flipX : id == ED_FLIP_Y ? g_editDraft.flipY : g_editDraft.invert;
            int boxSize = S(16); int cy = (r.top + r.bottom) / 2;
            RECT box = {r.left + S(3), cy - boxSize/2, r.left + S(3) + boxSize, cy + (boxSize+1)/2};
            Round(dc, box, S(3), checked ? C_ACC : (g_editorDark ? RGB(48,54,65) : RGB(255,255,255)),
                  checked ? C_ACC : (g_editorDark ? RGB(120,130,145) : RGB(110,110,110)));
            if (checked) {
                HPEN pen = CreatePen(PS_SOLID, std::max(1, S(2)), RGB(255,255,255));
                HGDIOBJ old = SelectObject(dc, pen);
                MoveToEx(dc, box.left+S(3), box.top+S(8), nullptr); LineTo(dc, box.left+S(7), box.top+S(12));
                LineTo(dc, box.left+S(13), box.top+S(4)); SelectObject(dc, old); DeleteObject(pen);
            }
            textRect.left = box.right + S(8);
            DrawTextW(dc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        } else {
            bool primary = id == ED_APPLY;
            COLORREF button = primary ? RGB(46,128,230) : (g_editorDark ? RGB(48,55,68) : RGB(248,248,248));
            if (d->itemState & ODS_SELECTED) button = primary ? RGB(30,100,190) : (g_editorDark ? RGB(66,74,88) : RGB(215,215,215));
            Round(dc, r, S(7), button, primary ? button : (g_editorDark ? RGB(80,90,105) : RGB(170,170,170)));
            if (primary) SetTextColor(dc, RGB(255,255,255));
            DrawTextW(dc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        if (d->itemState & ODS_FOCUS) { RECT focus=r; InflateRect(&focus, -S(3), -S(3)); DrawFocusRect(dc, &focus); }
        SelectObject(dc, oldFont); return TRUE;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mi=(MINMAXINFO*)lp; mi->ptMinTrackSize.x=S(800); mi->ptMinTrackSize.y=S(620); return 0;
    }
    case WM_HSCROLL:
        for(int i=0;i<6;i++) if ((HWND)lp == g_edTracks[i]) {
            int val=(int)SendMessageW(g_edTracks[i],TBM_GETPOS,0,0);
            if(i==0) g_editDraft.brightness=val-100;
            else if(i==1) g_editDraft.contrast=val;
            else if(i==2) g_editDraft.saturation=val;
            else if(i==3) g_editDraft.gamma=val;
            else if(i==4) g_editDraft.warmth=val-100;
            else g_editDraft.hue=val-180;
            SetEditorLabels(); PublishEdit(); break;
        }
        return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case ED_RESET: g_editDraft=VideoEdit{}; SetEditorLabels(); for (HWND c : g_edMirror) InvalidateRect(c,nullptr,FALSE); PublishEdit(); break;
        case ED_FREE: EditorPreset(0); break;
        case ED_169: EditorPreset(16.0/9.0); break;
        case ED_43: EditorPreset(4.0/3.0); break;
        case ED_11: EditorPreset(1.0); break;
        case ED_PAUSE: {
            bool frozen = !g_previewFrozen.load();
            g_previewFrozen = frozen; g_previewRefresh = true; p_repaint = true;
            SetWindowTextW(g_edButtons[7], frozen ? L"Оживить предпросмотр" : L"Остановить кадр");
            InvalidateRect(g_edButtons[7], nullptr, FALSE);
            break;
        }
        case ED_FLIP_X: g_editDraft.flipX = !g_editDraft.flipX; InvalidateRect(g_edMirror[0],nullptr,FALSE); PublishEdit(); break;
        case ED_FLIP_Y: g_editDraft.flipY = !g_editDraft.flipY; InvalidateRect(g_edMirror[1],nullptr,FALSE); PublishEdit(); break;
        case ED_INVERT: g_editDraft.invert = !g_editDraft.invert; InvalidateRect(g_edMirror[2],nullptr,FALSE); PublishEdit(); break;
        case ED_PRESETS: {
            HMENU menu = CreatePopupMenu();
            if (!menu) break;
            AppendMenuW(menu, MF_STRING, 1, L"Оригинальные цвета");
            AppendMenuW(menu, MF_STRING, 2, L"Кино");
            AppendMenuW(menu, MF_STRING, 3, L"Ночь");
            AppendMenuW(menu, MF_STRING, 4, L"Чёрно-белый");
            AppendMenuW(menu, MF_STRING, 5, L"Тёплый");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 6, L"Сохранить текущие цвета как свой пресет");
            AppendMenuW(menu, MF_STRING | (g_hasUserPreset ? 0 : MF_GRAYED), 7, L"Применить свой пресет");
            RECT r = {}; GetWindowRect(g_edPreset, &r);
            int choice = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, r.left, r.bottom, 0, h, nullptr);
            DestroyMenu(menu);
            if (choice >= 1 && choice <= 5) {
                ApplyPresetColors(g_editDraft, BuiltinColorPreset(choice-1)); SetEditorLabels();
                InvalidateRect(g_edMirror[2], nullptr, FALSE); PublishEdit();
            } else if (choice == 6) {
                g_userPreset = g_editDraft; g_hasUserPreset = true; SaveConfig();
            } else if (choice == 7 && g_hasUserPreset) {
                ApplyPresetColors(g_editDraft, g_userPreset); SetEditorLabels();
                InvalidateRect(g_edMirror[2], nullptr, FALSE); PublishEdit();
            }
            break;
        }
        case ED_THEME:
            g_editorDark = !g_editorDark; EditorTitleTheme(h); SaveConfig();
            SetWindowTextW(g_edButtons[8], g_editorDark ? L"Тема: чёрная" : L"Тема: светлая");
            RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            break;
        case ED_APPLY: CloseEditor(true); break;
        case ED_CANCEL: CloseEditor(false); break;
        }
        return 0;
    case WM_CLOSE: CloseEditor(false); return 0;
    }
    return DefWindowProcW(h,m,wp,lp);
}
static void OpenEditor() {
    if (g_editor) { SetForegroundWindow(g_editor); return; }
    if (g_sel < 0 || g_sel != g_active || g_pendingAction || !p_thread || WaitForSingleObject(p_thread,0) != WAIT_TIMEOUT) {
        MessageBoxW(g_main, L"Выберите и запустите видео, затем откройте редактор.", L"Редактирование", MB_OK | MB_ICONINFORMATION);
        return;
    }
    g_editorPath = g_items[(size_t)g_sel].path;
    g_editDraft = g_items[(size_t)g_sel].edit;
    g_editor = CreateWindowExW(WS_EX_DLGMODALFRAME, EDITOR_CLASS, L"Редактор кадра — LoopDesk",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, S(1050), S(700), g_main, nullptr, g_hi, nullptr);
    if (!g_editor) {
        EnterCriticalSection(&g_cs);
        g_previewWindow = nullptr; g_previewRefresh = false;
        LeaveCriticalSection(&g_cs);
        MessageBoxW(g_main, L"Не удалось открыть редактор.", L"LoopDesk", MB_OK | MB_ICONERROR); return;
    }
    ShowWindow(g_editor, SW_SHOW); UpdateWindow(g_editor);
}

// ------------------------------------------------------------------ main window painting
static void PaintAll(HDC dc, int W, int H) {
    g_hits.clear();
    const int topH = S(64), toolH = S(64), botH = S(68), panW = S(312);
    RECT rAll = {0, 0, W, H};
    Fill(dc, rAll, C_BG);

    // ----- wallpaper grid
    RECT rg = {0, topH + toolH, W - panW, H - botH};
    g_gridRc = rg;
    std::vector<int> vis;
    for (int i = 0; i < (int)g_items.size(); i++)
        if ((!g_favoritesOnly || g_items[(size_t)i].favorite) && ContainsNoCase(g_items[(size_t)i].name, g_filter)) vis.push_back(i);
    if (g_sortName) std::stable_sort(vis.begin(), vis.end(), [](int a, int b) {
        return _wcsicmp(g_items[(size_t)a].name.c_str(), g_items[(size_t)b].name.c_str()) < 0;
    });
    const int gap = S(16), gw = (int)(rg.right - rg.left) - S(8);
    const int cols = std::max(1, (gw - gap) / (S(190) + gap));
    const int tw = std::max(S(60), (gw - gap * (cols + 1)) / cols), imageH = tw * 9 / 16;
    const int th = imageH + S(44);
    const int rows = ((int)vis.size() + cols - 1) / cols;
    const int contentH = rows * (th + gap) + gap, viewH = (int)(rg.bottom - rg.top);
    g_maxScroll = std::max(0, contentH - viewH);
    g_scroll = std::clamp(g_scroll, 0, g_maxScroll);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, rg.left, rg.top, rg.right, rg.bottom);
    if (vis.empty()) {
        if (g_items.empty()) {
            const int inset = S(16);
            RECT hero = {rg.left + inset, rg.top + inset, rg.right - inset - S(8),
                         std::min((int)rg.bottom - inset, (int)rg.top + S(320))};
            if (hero.right > hero.left && hero.bottom > hero.top) {
                // The artwork is a self-contained resource, not a network request.
                DrawAmbientArt(dc, hero);
                RECT kicker = {hero.left + S(24), hero.top + S(20), hero.right - S(24), hero.top + S(40)};
                Txt(dc, L"ПРОСТРАНСТВО ДЛЯ ВДОХНОВЕНИЯ", kicker, g_fSmall, C_MINT);
                RECT title = {hero.left + S(24), hero.top + S(45), hero.right - S(24), hero.top + S(93)};
                Txt(dc, L"Оживите рабочий стол", title, g_fHero, C_TEXT);
                RECT sub = {hero.left + S(24), hero.top + S(93), hero.right - S(24), hero.top + S(120)};
                Txt(dc, L"Любимое видео — теперь часть вашего пространства.", sub, g_fUI, C_DIM);
                RECT add = {hero.left + S(24), hero.top + S(132), hero.left + S(200), hero.top + S(170)};
                if (add.bottom < hero.bottom) Button(dc, add, ID_OPEN, L"Добавить видео", IC_PLUS, true);
                Frame(dc, hero, 1, C_LINE);
            }
            RECT hint = {rg.left + S(20), hero.bottom + S(10), rg.right - S(20), hero.bottom + S(38)};
            Txt(dc, L"Или перетащите видео или изображение в это окно", hint, g_fSmall, C_DIM, DT_CENTER | DT_ONE);
        } else {
            RECT a = rg; a.bottom = (rg.top + rg.bottom) / 2;
            Txt(dc, g_favoritesOnly && g_filter.empty() ? L"В избранном пока пусто" : L"Ничего не найдено", a, g_fTitle, C_TEXT,
                DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
            RECT b = rg; b.top = a.bottom + S(8);
            Txt(dc, g_favoritesOnly && g_filter.empty() ? L"Добавьте обои в избранное на панели справа" : L"Попробуйте изменить запрос", b, g_fUI, C_DIM, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }
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
        Round(dc, t, S(12), C_CAP, i == g_sel ? C_ACC_H : (g_hot == ID_TILE + i ? C_DIM : C_LINE));
        RECT image = {t.left + S(2), t.top + S(2), t.right - S(2), t.top + imageH};
        // Clip the image to the rounded card; captions remain readable below it.
        int cardDC = SaveDC(dc);
        HRGN clip = CreateRoundRectRgn(t.left + 1, t.top + 1, t.right - 1, t.bottom - 1, S(12), S(12));
        if (clip) { ExtSelectClipRgn(dc, clip, RGN_AND); DeleteObject(clip); }
        DrawThumb(dc, image, it);
        RestoreDC(dc, cardDC);
        RECT ct = {t.left + S(12), image.bottom + S(2), t.right - S(12), t.bottom - S(2)};
        Txt(dc, it.name, ct, g_fBold, C_TEXT);
        if (i == g_active) {
            RECT bd = {t.left + S(9), t.top + S(9), t.left + S(98), t.top + S(32)};
            Round(dc, bd, S(10), C_TOP, C_LINE);
            Icon(dc, p_userPause.load() ? IC_PAUSE : IC_PLAY, bd.left + S(12), (bd.top + bd.bottom) / 2, S(4), C_MINT);
            RECT badgeText = {bd.left + S(23), bd.top, bd.right - S(4), bd.bottom};
            Txt(dc, p_userPause.load() ? L"ПАУЗА" : L"АКТИВНО", badgeText, g_fSmall, C_TEXT);
        }
        if (it.favorite) {
            RECT star = {t.right-S(34), t.top+S(8), t.right-S(8), t.top+S(33)};
            Round(dc, star, S(8), C_TOP, C_LINE);
            Txt(dc, L"★", star, g_fBold, C_BOOST, DT_CENTER | DT_ONE);
        }
        if (i == g_sel) Frame(dc, t, std::max(1, S(1)), C_ACC_H);
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
    RECT topLine = {0, topH - 1, W, topH}; Fill(dc, topLine, C_LINE);
    RECT lg = {S(20), S(18), S(48), S(46)};
    if (g_icon) DrawIconEx(dc, lg.left, lg.top, g_icon, lg.right-lg.left, lg.bottom-lg.top, 0, nullptr, DI_NORMAL);
    else { Round(dc, lg, S(8), C_ACC, C_ACC); Icon(dc, IC_PLAY, (lg.left + lg.right)/2, (lg.top + lg.bottom)/2, S(6), C_TEXT); }
    RECT rn = {S(59), 0, S(168), topH};
    Txt(dc, APP_NAME, rn, g_fTitle, C_TEXT);
    RECT tab = {S(188), 0, S(302), topH};
    Txt(dc, L"Коллекция", tab, g_fBold, C_TEXT, DT_CENTER | DT_ONE);
    RECT ul = {tab.left + S(20), topH - S(3), tab.right - S(20), topH};
    Fill(dc, ul, C_ACC);
    {
        std::wstring err;
        EnterCriticalSection(&g_cs);
        err = p_err;
        LeaveCriticalSection(&g_cs);
        std::wstring st;
        COLORREF sc = C_DIM;
        if (!err.empty()) { st = L"Ошибка: " + err; sc = C_WARN; }
        else if (g_pendingAction) { st = L"Переключение обоев…"; sc = C_DIM; }
        else if (g_active >= 0) {
            const wchar_t* pre = p_userPause.load() ? L"Пауза: "
                                 : (p_autoPause.load() ? L"Автопауза: " : L"Сейчас на рабочем столе: ");
            st = pre + g_items[(size_t)g_active].name;
            if (p_mute.load()) st += L" (без звука)";
            sc = C_TEXT;
        } else st = L"Обои не установлены";
        RECT rs = {S(325), 0, W - S(20), topH};
        Txt(dc, st, rs, g_fUI, sc, DT_RIGHT | DT_ONE);
    }

    // ----- search row
    RECT rtool = {0, topH, W - panW, topH + toolH};
    Fill(dc, rtool, C_BG);
    RECT sbx = {S(16), topH + S(14), S(16) + S(280), topH + S(50)};
    Round(dc, sbx, S(8), C_INPUT, C_LINE);
    RECT favFilter = {sbx.right + S(10), topH + S(14), sbx.right + S(132), topH + S(50)};
    Button(dc, favFilter, ID_FAVORITES_ONLY, g_favoritesOnly ? L"★ Избранное" : L"☆ Избранное", IC_NONE, g_favoritesOnly);
    RECT sort = {favFilter.right + S(8), topH + S(14), favFilter.right + S(108), topH + S(50)};
    Button(dc, sort, ID_SORT, g_sortName ? L"А–Я" : L"По добавл.", IC_NONE, false);

    // ----- right panel
    RECT rp = {W - panW, topH, W, H - botH};
    g_panelRc = rp;
    Fill(dc, rp, C_PANEL);
    RECT pl = {rp.left, rp.top, rp.left + 1, rp.bottom};
    Fill(dc, pl, C_LINE);
    const int px = W - panW + S(16), pw = panW - S(32);
    int panelDC = SaveDC(dc);
    IntersectClipRect(dc, rp.left, rp.top, rp.right, rp.bottom);
    int y = topH + S(14) - g_panelScroll;
    RECT section = {px, y, px + pw, y + S(20)};
    Txt(dc, L"ПАРАМЕТРЫ ОБОЕВ", section, g_fSmall, C_DIM);
    y += S(26);
    g_volTrack = RECT{}; g_speedTrack = RECT{};
    if (g_sel >= 0 && g_sel < (int)g_items.size()) {
        Item& it = g_items[(size_t)g_sel];
        // the preview takes whatever height is left after the fixed-size controls below it
        const int ph = std::clamp((H - botH) - (topH + S(14)) - S(442), S(64), pw * 9 / 16);
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
        RECT editButton = {px, y, px + pw, y + S(34)};
        Button(dc, editButton, ID_EDIT, L"Редактировать кадр…", IC_NONE, true);
        y += S(40);
        RECT favorite = {px, y, px + pw, y + S(30)};
        Button(dc, favorite, ID_FAVORITE, it.favorite ? L"★ Убрать из избранного" : L"☆ В избранное", IC_NONE, false);
        y += S(38);
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

        // Speed slider, 15–200% in 1% steps.
        RECT rspeed = {px, y, px + pw, y + S(20)};
        Txt(dc, L"СКОРОСТЬ", rspeed, g_fSmall, C_DIM);
        wchar_t sb[80];
        _snwprintf(sb, 79, L"%d%%", it.speed); sb[79] = 0;
        Txt(dc, sb, rspeed, g_fBold, C_TEXT, DT_RIGHT | DT_ONE);
        y += S(24);
        RECT speedTrack = {px + S(8), y + S(5), px + pw - S(8), y + S(11)};
        g_speedTrack = speedTrack;
        int speedX = speedTrack.left + MulDiv(it.speed - 15, speedTrack.right - speedTrack.left, 185);
        Round(dc, speedTrack, S(6), C_TRACK, C_TRACK);
        RECT speedFill = {speedTrack.left, speedTrack.top, speedX, speedTrack.bottom};
        Fill(dc, speedFill, C_ACC);
        {
            HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH)), op = SelectObject(dc, GetStockObject(DC_PEN));
            SetDCBrushColor(dc, RGB(255,255,255)); SetDCPenColor(dc, C_ACC);
            int cy = (speedTrack.top + speedTrack.bottom) / 2, rr = S(8);
            Ellipse(dc, speedX - rr, cy - rr, speedX + rr + 1, cy + rr + 1);
            SelectObject(dc, ob); SelectObject(dc, op);
        }
        RECT sh = {px, y - S(5), px + pw, y + S(23)};
        g_hits.push_back({sh, ID_SPEED});
        y += S(23);
        RECT smin = {px, y, px + S(50), y + S(16)}, smax = {px + pw - S(55), y, px + pw, y + S(16)};
        Txt(dc, L"15%", smin, g_fSmall, C_DIM); Txt(dc, L"200%", smax, g_fSmall, C_DIM, DT_RIGHT | DT_ONE);
        y += S(22);

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
    y += S(26);
    Checkbox(dc, px, y, pw, ID_ROTATE, L"Менять обои каждые 15 минут", g_optRotation);
    y += S(26);
    Checkbox(dc, px, y, pw, ID_BATTERY_FPS, L"30 кадров/с от батареи", g_optBatteryFps);
    y += S(26);
    Checkbox(dc, px, y, pw, ID_GPU, L"GPU-декодирование (эксперимент)", g_optHardware);
    y += S(26);
    if (g_optHardware && g_active >= 0) {
        int state = p_gpuState.load();
        const wchar_t* text = IsStaticImage(g_items[(size_t)g_active].path) ? L"Для изображения используется WIC" :
                              state == 2 ? L"D3D включён; декодер выбирает Windows" :
                              state == 3 ? L"GPU недоступен: программный режим" : L"Подготовка GPU…";
        RECT hint = {px + S(27), y, px + pw, y + S(32)};
        Txt(dc, text, hint, g_fSmall, state == 3 ? C_WARN : C_DIM, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        y += S(32);
    } else y += S(2);
    g_panelMaxScroll = std::max(0, y + g_panelScroll - (int)rp.bottom + S(8));
    if (g_panelScroll > g_panelMaxScroll) { g_panelScroll = g_panelMaxScroll; InvalidateRect(g_main, &rp, FALSE); }
    RestoreDC(dc, panelDC);
    if (g_panelMaxScroll > 0) {
        int h = rp.bottom - rp.top, bar = std::max(S(30), h * h / (h + g_panelMaxScroll));
        int by = rp.top + MulDiv(h - bar, g_panelScroll, g_panelMaxScroll);
        RECT scrollbar = {rp.right - S(5), by, rp.right - S(2), by + bar}; Fill(dc, scrollbar, C_LINE);
    }

    // ----- bottom bar
    RECT rbm = {0, H - botH, W, H};
    Fill(dc, rbm, C_TOP);
    RECT bl = {0, H - botH, W, H - botH + 1};
    Fill(dc, bl, C_LINE);
    const int by = H - botH + S(15), bh = S(38);
    const bool act = g_active >= 0;
    int x = S(12);
    RECT b1 = {x, by, x + S(160), by + bh};
    Button(dc, b1, ID_OPEN, L"Открыть файл", IC_PLUS, true);
    x += S(172);
    RECT b2 = {x, by, x + S(150), by + bh};
    Button(dc, b2, ID_PAUSE, p_userPause.load() ? L"Продолжить" : L"Пауза", p_userPause.load() ? IC_PLAY : IC_PAUSE,
           false, act);
    x += S(162);
    RECT b3 = {x, by, x + S(150), by + bh};
    Button(dc, b3, ID_STOP, L"Остановить", IC_STOP, false, act);
    RECT b4 = {W - S(12) - S(120), by, W - S(12), by + bh};
    Button(dc, b4, ID_OK, L"В трей", IC_NONE, false);
}

// ------------------------------------------------------------------ main window logic
static void MakeFonts() {
    for (HFONT* f : {&g_fUI, &g_fBold, &g_fTitle, &g_fSmall, &g_fHero})
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
    g_fHero = mk(185, FW_SEMIBOLD);
    if (g_edit) SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_fUI, TRUE);
}
static void PlaceEdit() {
    if (!g_edit) return;
    const int topH = S(64);
    MoveWindow(g_edit, S(28), topH + S(22), S(256), S(20), TRUE);
}
static int HitAt(POINT pt) {
    for (size_t i = g_hits.size(); i-- > 0;) {
        int id = g_hits[i].id;
        if ((id == ID_VOL || id == ID_SPEED || id == ID_EDIT ||
             id == ID_REMOVE || id == ID_FAVORITE || (id >= ID_FIT0 && id <= ID_FIT2) ||
             id == ID_AUTORUN || id == ID_FSPAUSE || id == ID_ECOPAUSE || id == ID_GPU || id == ID_ROTATE || id == ID_BATTERY_FPS) && !PtInRect(&g_panelRc, pt)) continue;
        if (PtInRect(&g_hits[i].r, pt)) return id;
    }
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
    InvalidateRect(g_main, &g_panelRc, FALSE);
}
static void SetSpeedFromX(int x) {
    if (g_sel < 0 || g_sel >= (int)g_items.size()) return;
    int width = (int)(g_speedTrack.right - g_speedTrack.left);
    if (width <= 0) return;
    int v = 15 + MulDiv(std::clamp(x - (int)g_speedTrack.left, 0, width), 185, width);
    if (std::abs(v - 100) <= 1) v = 100;
    g_items[(size_t)g_sel].speed = v;
    if (g_sel == g_active) p_speed = v;
    InvalidateRect(g_main, &g_panelRc, FALSE);
}
static void AddFiles(const std::vector<std::wstring>& files) {
    int first = -1;
    for (auto& f : files) {
        if (!FileExists(f)) continue;
        int k = AddItem(f);
        if (first < 0) first = k;
    }
    if (first >= 0) {
        g_filter.clear(); g_favoritesOnly = false;
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
    of.lpstrFilter = L"Видео и изображения (*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm;*.png;*.jpg;*.jpeg;*.bmp)\0*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm;*.png;*.jpg;*.jpeg;*.bmp\0"
                     L"Все файлы (*.*)\0*.*\0";
    of.lpstrFile = buf.data();
    of.nMaxFile = (DWORD)buf.size();
    of.lpstrTitle = L"Выберите видео или изображение для рабочего стола";
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
        g_lastControl = 0;
        if (i == g_active && p_thread && !p_quit.load() && WaitForSingleObject(p_thread, 0) == WAIT_TIMEOUT) {
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
    case ID_EDIT: OpenEditor(); break;
    case ID_FAVORITE:
        if (g_sel >= 0 && g_sel < (int)g_items.size()) {
            Item& item = g_items[(size_t)g_sel]; item.favorite = !item.favorite;
            SaveConfig(); InvalidateRect(h, nullptr, FALSE);
        }
        break;
    case ID_FAVORITES_ONLY:
        g_favoritesOnly = !g_favoritesOnly; g_scroll = 0; InvalidateRect(h, nullptr, FALSE); break;
    case ID_SORT:
        g_sortName = !g_sortName; g_scroll = 0; SaveConfig(); InvalidateRect(h, nullptr, FALSE); break;
    case ID_ROTATE:
        g_optRotation = !g_optRotation; g_rotationClock.reset(); SaveConfig(); InvalidateRect(h, nullptr, FALSE); break;
    case ID_BATTERY_FPS:
        g_optBatteryFps = !g_optBatteryFps;
        if (!g_optBatteryFps) p_fpsLimit = 0;
        SaveConfig(); InvalidateRect(h, nullptr, FALSE); break;
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
    case ID_GPU:
        g_optHardware = !g_optHardware;
        SaveConfig();
        if (g_active >= 0) StartWallpaper(g_active);
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
    CloseEditor(false);
    SaveConfig();  // remembers the active wallpaper for the next start
    bool stopped = StopPlayer();
    if (stopped) DestroyWall();
    if (stopped && !g_raised && g_progman) SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_UPDATEINIFILE);
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
        SendMessageW(g_edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Поиск в коллекции · Ctrl+F");
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
            IntersectClipRect(mem, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom);
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
        if (g_dragControl) {
            if (g_dragControl == ID_VOL) SetVolFromX(pt.x);
            else if (g_dragControl == ID_SPEED) SetSpeedFromX(pt.x);
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
        if (id == ID_VOL || id == ID_SPEED) {
            g_lastControl = id;
            g_dragControl = id;
            SetCapture(h);
            if (id == ID_VOL) SetVolFromX(pt.x); else SetSpeedFromX(pt.x);
        } else if (id) {
            g_lastControl = 0;
            Command(h, id);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_dragControl) {
            g_dragControl = 0;
            ReleaseCapture();
            SaveConfig();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (g_dragControl) {
            g_dragControl = 0;
            SaveConfig();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &pt);
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        if (PtInRect(&g_panelRc, pt)) {
            // Scrolling the panel makes every control accessible on small displays.
            g_panelScroll = std::clamp(g_panelScroll - d * S(70) / WHEEL_DELTA, 0, g_panelMaxScroll);
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
        if ((wp == VK_LEFT || wp == VK_RIGHT) && g_sel >= 0 && g_sel < (int)g_items.size() && g_lastControl == ID_SPEED) {
            Item& it = g_items[(size_t)g_sel];
            it.speed = std::clamp(it.speed + (wp == VK_RIGHT ? 1 : -1), 15, 200);
            if (g_sel == g_active) p_speed = it.speed;
            SaveConfig(); InvalidateRect(h, nullptr, FALSE);
        } else if ((wp == VK_LEFT || wp == VK_RIGHT) && g_sel >= 0 && g_sel < (int)g_items.size() && g_lastControl == ID_VOL) {
            Item& it = g_items[(size_t)g_sel];
            it.volume = std::clamp(it.volume + (wp == VK_RIGHT ? 5 : -5), 0, 500);
            if (g_sel == g_active) p_volume = it.volume;
            SaveConfig(); InvalidateRect(h, nullptr, FALSE);
        } else if (wp == VK_DELETE && g_sel >= 0) RemoveItem(g_sel);
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
            p_fpsLimit = g_optBatteryFps && battery ? 30 : 0;
            bool fs = g_active >= 0 && ((g_optFsPause && ForegroundIsFullscreen()) ||
                      (g_optEcoPause && (g_sessionLocked || g_displayOff || battery)));
            if (fs != p_autoPause.load()) {
                p_autoPause = fs;
                InvalidateRect(h, nullptr, FALSE);
            }
            bool rotationBlocked = g_active < 0 || g_pendingAction || g_editor || p_userPause.load() || p_autoPause.load();
            if (g_rotationClock.tick(GetTickCount64(), g_optRotation, rotationBlocked, 15ULL * 60 * 1000)) {
                int next = NextPlayable(g_items.size(), g_active, [](size_t i) { return FileExists(g_items[i].path); });
                if (next >= 0) StartWallpaper(next);
            }
            if (g_active >= 0 && !g_pendingAction) {
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
                g_reattachPending = true;
                StartWallpaper(a);
            }
        } else if (wp == 3 && g_pendingAction &&
                   (!p_thread || WaitForSingleObject(p_thread, 0) == WAIT_OBJECT_0)) {
            KillTimer(h, 3);
            int action = g_pendingAction; std::wstring path = g_pendingPath;
            g_pendingAction = 0; g_pendingPath.clear();
            if (action == 1) {
                int idx = FindItem(path);
                if (idx >= 0) StartWallpaper(idx); else StopWallpaper();
            } else StopWallpaper();
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
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    g_mfOk = LoadMF();
    Log(L"---- LoopDesk start, mf=%d", (int)g_mfOk);
    g_jobEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
    g_ambientArt = (HBITMAP)LoadImageW(hi, MAKEINTRESOURCEW(2), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
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

    WNDCLASSEXW ed = {};
    ed.cbSize = sizeof(ed); ed.hInstance = hi; ed.lpfnWndProc = EditorProc;
    ed.lpszClassName = EDITOR_CLASS; ed.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    ed.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); ed.hIcon = g_icon;
    RegisterClassExW(&ed);
    WNDCLASSEXW slider = {};
    slider.cbSize = sizeof(slider); slider.hInstance = hi; slider.lpfnWndProc = EditorSliderProc;
    slider.lpszClassName = SLIDER_CLASS; slider.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&slider);
    WNDCLASSEXW pv = {};
    pv.cbSize = sizeof(pv); pv.hInstance = hi; pv.lpfnWndProc = PreviewProc;
    pv.lpszClassName = PREVIEW_CLASS; pv.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    pv.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&pv);
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
        // Local shortcuts work even when the search edit has keyboard focus,
        // without intercepting keystrokes in the separate editor window.
        if (msg.message == WM_KEYDOWN && GetAncestor(msg.hwnd, GA_ROOT) == g_main &&
            (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (msg.wParam == 'F') { SetFocus(g_edit); SendMessageW(g_edit, EM_SETSEL, 0, -1); continue; }
            if (msg.wParam == 'O') { Command(g_main, ID_OPEN); continue; }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_ambientArt) { DeleteObject(g_ambientArt); g_ambientArt = nullptr; }
    if (g_mfOk && !g_abandonedPlayer) fMFShutdown();
    CoUninitialize();
    if (mtx) CloseHandle(mtx);
    return 0;
}
