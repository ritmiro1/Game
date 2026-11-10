// Real Windows/Media Foundation integration smoke test, with an isolated test
// window (never attaches to Explorer or changes the user's wallpaper).
#define WinMain LoopDeskGuiEntry
#include "../src/main.cpp"
#include <cstdio>

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
    if (!result) std::puts("Windows MF decoder, async rendering, preview and PCM gain: OK");
    return result;
}
