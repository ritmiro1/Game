// Real Windows/Media Foundation integration smoke test, with an isolated test
// window (never attaches to Explorer or changes the user's wallpaper).
#define WinMain LoopDeskGuiEntry
#include "../src/main.cpp"
#include <cstdio>

static void SaveLayoutCapture(const wchar_t* name, int width, int height, const void* pixels) {
    wchar_t dir[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOOPDESK_CAPTURE_DIR", dir, MAX_PATH)) return;
    std::wstring path = std::wstring(dir) + L"\\" + name;
    FILE* f = _wfopen(path.c_str(), L"wb"); if (!f) return;
    BITMAPFILEHEADER file = {}; BITMAPINFOHEADER info = {};
    info.biSize = sizeof(info); info.biWidth = width; info.biHeight = height;
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
    info.biSizeImage = (DWORD)((size_t)width * height * 4);
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + info.biSizeImage;
    GdiFlush();
    fwrite(&file, sizeof(file), 1, f); fwrite(&info, sizeof(info), 1, f);
    fwrite(pixels, 1, info.biSizeImage, f); fclose(f);
}

// Draw real UI into a memory DC at several DPIs and exercise hit testing.
static bool TestLibraryLayout(HWND window) {
    g_ambientArt = (HBITMAP)LoadImageW(g_hi, MAKEINTRESOURCEW(2), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    g_icon = LoadIconW(g_hi, MAKEINTRESOURCEW(1));
    if (!g_ambientArt || !g_icon) return false;
    HDC screen = GetDC(window), dc = CreateCompatibleDC(screen);
    if (!screen || !dc) return false;
    bool ok = true;
    for (UINT dpi : {96u, 144u, 192u}) {
        g_dpi = dpi; MakeFonts();
        const int width = S(1200), height = S(760);
        BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = width; bi.bmiHeader.biHeight = height;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels) { ok = false; break; }
        HGDIOBJ old = SelectObject(dc, bitmap);
        g_items.clear(); g_sel = g_active = -1; g_filter.clear();
        g_panelScroll = g_scroll = 0;
        PaintAll(dc, width, height);
        POINT heroButton = {S(60), S(295)};
        if (HitAt(heroButton) != ID_OPEN || GetPixel(dc, S(20), S(150)) == C_BG) ok = false;
        if (dpi == 96) SaveLayoutCapture(L"library-empty.bmp", width, height, pixels);
        Item item; item.name = L"Тестовые обои"; g_items.push_back(item);
        g_sel = g_active = 0;
        PaintAll(dc, width, height);
        if (dpi == 96) SaveLayoutCapture(L"library-selected.bmp", width, height, pixels);
        // Minimum supported logical window size, with the properties panel.
        PaintAll(dc, S(900), S(600));
        POINT volume = {(g_volTrack.left + g_volTrack.right)/2, (g_volTrack.top + g_volTrack.bottom)/2};
        POINT speed = {(g_speedTrack.left + g_speedTrack.right)/2, (g_speedTrack.top + g_speedTrack.bottom)/2};
        if (HitAt(volume) != ID_VOL || HitAt(speed) != ID_SPEED) ok = false;
        bool tile = false, edit = false;
        for (const auto& hit : g_hits) { if (hit.id == ID_TILE) tile = true; if (hit.id == ID_EDIT) edit = true; }
        if (!tile || !edit) ok = false;
        g_filter = L"not-in-library"; PaintAll(dc, width, height);
        for (const auto& hit : g_hits) if (hit.id == ID_TILE) ok = false;
        SelectObject(dc, old); DeleteObject(bitmap);
    }
    g_items.clear(); g_filter.clear(); g_sel = g_active = -1; g_dpi = 96;
    for (HFONT* f : {&g_fUI,&g_fBold,&g_fTitle,&g_fSmall,&g_fHero}) { if (*f) DeleteObject(*f); *f=nullptr; }
    DeleteDC(dc); ReleaseDC(window, screen);
    DeleteObject(g_ambientArt); g_ambientArt = nullptr;
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::fprintf(stderr, "expected path to test MP4\n"); return 1; }
    InitializeCriticalSection(&g_cs);
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    g_hi = GetModuleHandleW(nullptr);
    if (!LoadMF()) { std::fprintf(stderr, "Media Foundation startup failed\n"); return 2; }
    HWND window = CreateWindowExW(0, L"STATIC", L"LoopDesk smoke", WS_OVERLAPPEDWINDOW,
                                  0, 0, 320, 240, nullptr, nullptr, g_hi, nullptr);
    if (!window) return 3;
    g_main = window; g_wall = window; g_previewWindow = window;
    g_previewFrozen = false; g_previewRefresh = true;
    g_mon.push_back(RECT{0,0,320,240}); p_layoutGen = 1;
    p_path = argv[1]; p_speed = 100; p_volume = 500;
    p_edit.hue = 45; p_edit.saturation = 130; p_edit.gamma = 120;
    int result = 0;
    if (!TestLibraryLayout(window)) { std::fprintf(stderr, "library UI layout or resource check failed\n"); result = 11; }
    {
        Player p;
        if (!p.Open()) { std::fprintf(stderr, "player open failed\n"); result = 4; }
        else {
            // Exercise real decoder sample ownership across the bounded queue.
            p.rd->SetStreamSelection(SR_AUDIO, FALSE);
            p.StartRenderer();
            int decoded = 0;
            for (int tries=0; tries<100 && decoded<30; ++tries) {
                DWORD stream=0, flags=0; LONGLONG ts=0; IMFSample* sample=nullptr;
                HRESULT hr=p.rd->ReadSample(SR_VIDEO,0,&stream,&flags,&ts,&sample);
                if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) { result=5; break; }
                if (sample) { p.QueueFrame(sample); sample->Release(); ++decoded; }
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            }
            p.RequestRedraw();
            Sleep(1000); // let the real renderer complete before checking its state
            p.StopRenderer();
            if (decoded < 5 || !p.haveFrame || g_previewGeneration == 0) {
                std::fprintf(stderr,"decoder/render/preview failed: frames=%d frame=%d preview=%llu\n",
                             decoded,(int)p.haveFrame,g_previewGeneration); result=6;
            }
            // Media type changes restart the renderer. Its per-thread DC must
            // be recreated before the next preview refresh.
            auto generation = g_previewGeneration;
            p.StartRenderer(); p.RequestRedraw(); Sleep(500); p.StopRenderer();
            if (g_previewGeneration <= generation) {
                std::fprintf(stderr, "renderer restart/preview failed\n"); result=10;
            }
            // Exercise the production PCM conversion, resampling, and gain path,
            // independent of whether the CI machine has an audio output device.
            using CreateSample = HRESULT (WINAPI*)(IMFSample**);
            using CreateBuffer = HRESULT (WINAPI*)(DWORD, IMFMediaBuffer**);
            HMODULE mf=GetModuleHandleW(L"mfplat.dll");
            auto makeSample=(CreateSample)(void*)GetProcAddress(mf,"MFCreateSample");
            auto makeBuffer=(CreateBuffer)(void*)GetProcAddress(mf,"MFCreateMemoryBuffer");
            IMFSample* sample=nullptr; IMFMediaBuffer* buffer=nullptr;
            if (!makeSample || !makeBuffer || FAILED(makeSample(&sample)) || FAILED(makeBuffer(4096,&buffer))) result=7;
            else {
                BYTE* bytes=nullptr; DWORD cap=0;
                if (FAILED(buffer->Lock(&bytes,&cap,nullptr)) || cap<4096) result=8;
                else {
                    short* pcm=(short*)bytes; for (int i=0;i<2048;++i) pcm[i]=3277;
                    buffer->Unlock(); buffer->SetCurrentLength(4096); sample->AddBuffer(buffer);
                    p.aRate=48000; p.aCh=2; p.aBits=16; p.aFloat=false;
                    p.smoothedGain=5.0f; p.resampler.reset(1.0); p_mute=false;
                    p.SubmitAudio(sample);
                    if (p.pcm.size()<100 || std::abs((int)p.pcm[100]-16385)>100) {
                        std::fprintf(stderr,"audio gain path failed\n"); result=9;
                    }
                }
            }
            if (buffer) buffer->Release(); if (sample) sample->Release();
        }
        p.Close();
    }
    g_previewWindow=nullptr; g_wall=nullptr; DestroyWindow(window);
    fMFShutdown(); if (SUCCEEDED(co)) CoUninitialize(); DeleteCriticalSection(&g_cs);
    if (!result) std::puts("Windows library UI layout, MF decoder, renderer, preview and PCM gain: OK");
    return result;
}
