#include <iostream>
#include <filesystem>
#include <chrono>

#include "../third_party/imgui/imgui.h"
#include "audio/AudioEngine.h"
#include "library/LibraryManager.h"
#include "graphics/TextureManager.h"
#include "ui/Theme.h"
#include "ui/MainWindow.h"
#include "platform/Platform.h"
#include "platform/SMTCManager.h"
#include "platform/TaskbarManager.h"
#include "platform/SettingsManager.h"

namespace fs = std::filesystem;

#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <tchar.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <dwmapi.h>

#include "../third_party/imgui/backends/imgui_impl_win32.h"
#include "../third_party/imgui/backends/imgui_impl_dx11.h"

static MainWindow* g_pMainWindow = nullptr;

// Global DirectX 11 pointers
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static UINT                     g_CurrentBufferWidth = 0, g_CurrentBufferHeight = 0;
static bool                     g_ForceRedraw = true;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static bool                     g_InSizeMove = false;
static bool                     g_IsRenderingFrame = false;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void DoResizeSwapChain(UINT newWidth, UINT newHeight);
void RenderSingleFrame(HWND hWnd, MainWindow* pMainWindow, bool vsync);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    // Ensure thread attaches to interactive user desktop "Default" if launched from background sandbox
    HDESK hDefaultDesk = OpenDesktopW(L"Default", 0, FALSE, 0x01FF);
    if (hDefaultDesk) {
        SetThreadDesktop(hDefaultDesk);
    }

    // Enable per-monitor DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Set explicit AppUserModelID for Windows 10/11 Shell, Action Center, and SMTC
    typedef HRESULT (WINAPI *pfnSetCurrentProcessExplicitAppUserModelID)(PCWSTR);
    HMODULE hShell = LoadLibraryW(L"shell32.dll");
    if (hShell) {
        auto setAppID = (pfnSetCurrentProcessExplicitAppUserModelID)GetProcAddress(hShell, "SetCurrentProcessExplicitAppUserModelID");
        if (setAppID) {
            setAppID(L"SlothPlayer.App");
        }
    }

    // Load Custom Application Icon
    HICON hAppIcon = LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(1));
    if (!hAppIcon) {
        hAppIcon = (HICON)LoadImageW(nullptr, L"resources\\app_icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    }

    // Register Win32 Window Class (nullptr background brush: DirectX 11 owns complete surface, prevents GDI black/white flash)
    WNDCLASSEXW wc = {
        sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
        GetModuleHandle(nullptr), hAppIcon, nullptr, nullptr, nullptr,
        L"SlothPlayerClass", hAppIcon
    };
    wc.hIcon = hAppIcon;
    wc.hIconSm = hAppIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    SettingsManager::instance().load();
    const auto& settings = SettingsManager::instance().settings();

    // Create Main Window (Restoring saved geometry and display monitor placement)
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = (settings.windowWidth >= 320) ? settings.windowWidth : 1260;
    int winH = (settings.windowHeight >= 240) ? settings.windowHeight : 760;
    int posX = settings.windowX;
    int posY = settings.windowY;

    // Validate that the saved window position is visible on an active monitor, else center
    POINT pt = { posX + winW / 2, posY + winH / 2 };
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    if (!hMon || posX == -1 || posY == -1) {
        posX = (screenW - winW) / 2;
        posY = (screenH - winH) / 2;
    }

    HWND hWnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        wc.lpszClassName,
        L"SlothPlayer",
        WS_OVERLAPPEDWINDOW,
        posX, posY, winW, winH,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Windows 11 Dark Mode Titlebar & Mica/Immersive Dark Header Styling
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    MARGINS margins = { 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea(hWnd, &margins);

    enum DWM_WINDOW_CORNER_PREFERENCE {
        DWMWCP_DEFAULT    = 0,
        DWMWCP_DONOTROUND = 1,
        DWMWCP_ROUND      = 2,
        DWMWCP_ROUNDSMALL = 3
    };
    DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));

    // Hard-lock the border and caption to our sleek dark palette (#2A2F38 & #171A1F)
    COLORREF darkBorderColor = RGB(42, 47, 56);
    DwmSetWindowAttribute(hWnd, 34 /* DWMWA_BORDER_COLOR */, &darkBorderColor, sizeof(darkBorderColor));

    COLORREF darkCaptionColor = RGB(23, 26, 31);
    DwmSetWindowAttribute(hWnd, 35 /* DWMWA_CAPTION_COLOR */, &darkCaptionColor, sizeof(darkCaptionColor));

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Stateless configuration

    // Load High-DPI Fonts: Segoe UI at crisp 16px with optimal 2x horizontal oversampling (standard LCD subpixel)
    // Vertical oversampling is disabled (1) as scanlines are discrete, saving 50% CPU raster time.
    ImFontConfig fontCfg;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 1;
    fontCfg.PixelSnapH = true;

    static const ImWchar baseRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x2000, 0x26FF, // Arrows, geometric shapes (▾, ▼, etc.), punctuation
        0,
    };
    static const ImWchar symRanges[] = {
        0x2000, 0x26FF, // General Punctuation, Arrows, Geometric Shapes (including 0x25BE ▾), Misc Symbols
        0,
    };

    ImFont* mainFont = nullptr;
    char winDir[MAX_PATH];
    if (GetWindowsDirectoryA(winDir, MAX_PATH)) {
        std::string segoePath = std::string(winDir) + "\\Fonts\\segoeui.ttf";
        if (fs::exists(segoePath)) {
            mainFont = io.Fonts->AddFontFromFileTTF(segoePath.c_str(), 16.0f, &fontCfg, baseRanges);
        }
        std::string symPath = std::string(winDir) + "\\Fonts\\seguisym.ttf";
        if (fs::exists(symPath)) {
            ImFontConfig symCfg = fontCfg;
            symCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(symPath.c_str(), 14.0f, &symCfg, symRanges);
        }
        std::string msyhPath = std::string(winDir) + "\\Fonts\\msyh.ttc";
        if (fs::exists(msyhPath)) {
            // Critical performance optimization: CJK contains ~2500 glyphs.
            // Oversampling CJK at 3x2 causes 15,000 glyph rasterizations taking >4.4 seconds.
            // Setting 1x1 oversampling provides identical crispness for ideographs and builds in ~200ms.
            ImFontConfig cjkCfg;
            cjkCfg.OversampleH = 1;
            cjkCfg.OversampleV = 1;
            cjkCfg.PixelSnapH = true;
            cjkCfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(msyhPath.c_str(), 16.0f, &cjkCfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        }
    }
    if (!mainFont) {
        io.Fonts->AddFontDefault();
    }

    // Apply saved Theme
    Theme::applyTheme(static_cast<AppTheme>(settings.theme));

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Audio Engine & Library
    AudioEngine audioEngine;
    if (!audioEngine.init()) {
        std::cerr << "[Warning] AudioEngine initialization failed" << std::endl;
    }

    LibraryManager library;
    library.loadLibrary();

    // Auto-discover Windows Music directory on first run
    if (library.getTracks().empty()) {
        PWSTR musicPathW = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, NULL, &musicPathW))) {
            char musicPathA[MAX_PATH * 3];
            WideCharToMultiByte(CP_UTF8, 0, musicPathW, -1, musicPathA, sizeof(musicPathA), nullptr, nullptr);
            CoTaskMemFree(musicPathW);

            if (fs::exists(musicPathA)) {
                library.addMonitoredFolder(musicPathA);
                library.scanDirectories({musicPathA});
            }
        }
    }

    // Texture Manager
    TextureManager textureManager;
    textureManager.init(g_pd3dDevice, g_pd3dDeviceContext);

    // Main Window UI Component
    MainWindow mainWindow(audioEngine, library, textureManager);
    mainWindow.setWindowHandle(hWnd);
    g_pMainWindow = &mainWindow;

    if (lpCmdLine && (strstr(lpCmdLine, "--audio-modal") != nullptr || strstr(lpCmdLine, "--show-audio-modal") != nullptr)) {
        mainWindow.openAudioDriverModal();
    }

    // Initialize Windows System Media Transport Controls (SMTC)
    SMTCCallbacks smtcCallbacks;
    smtcCallbacks.onPlay = [&mainWindow]() { mainWindow.triggerMediaAction(MediaAction::Play); };
    smtcCallbacks.onPause = [&mainWindow]() { mainWindow.triggerMediaAction(MediaAction::Pause); };
    smtcCallbacks.onTogglePlayPause = [&mainWindow]() { mainWindow.triggerMediaAction(MediaAction::TogglePlayPause); };
    smtcCallbacks.onNext = [&mainWindow]() { mainWindow.triggerMediaAction(MediaAction::Next); };
    smtcCallbacks.onPrevious = [&mainWindow]() { mainWindow.triggerMediaAction(MediaAction::Previous); };
    smtcCallbacks.onStop = [&mainWindow]() { mainWindow.triggerMediaAction(MediaAction::Stop); };
    SMTCManager::instance().initialize(hWnd, smtcCallbacks);
    TaskbarManager::instance().initialize(hWnd, &mainWindow);

    // Render the very first frame to the swapchain before showing window
    RenderSingleFrame(hWnd, &mainWindow, false);

    // Now reveal the fully-painted window to the user (zero blank frame!)
    ShowWindow(hWnd, settings.windowMaximized ? SW_MAXIMIZE : SW_SHOWDEFAULT);
    UpdateWindow(hWnd);
    BringWindowToTop(hWnd);
    SetForegroundWindow(hWnd);

    // 1ms high-resolution timer for precise frame pacing without GPU clock boost
    timeBeginPeriod(1);

    LARGE_INTEGER perfFreq;
    QueryPerformanceFrequency(&perfFreq);
    double invFreqMs = 1000.0 / static_cast<double>(perfFreq.QuadPart);

    LARGE_INTEGER lastInputTime;
    QueryPerformanceCounter(&lastInputTime);

    // Main message loop
    bool done = false;
    Platform::setQuitFlag(&done);

    while (!done) {
        LARGE_INTEGER frameStart;
        QueryPerformanceCounter(&frameStart);

        MSG msg;
        bool hadInput = false;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
            if (msg.message == WM_MOUSEMOVE || msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN ||
                msg.message == WM_LBUTTONUP || msg.message == WM_RBUTTONUP ||
                msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN || msg.message == WM_MOUSEWHEEL) {
                hadInput = true;
            }
        }
        if (done) break;

        if (hadInput || g_ForceRedraw) {
            lastInputTime = frameStart;
            g_ForceRedraw = false;
        }

        // 1. Minimized: Keep playback engine, queue transitions, and SMTC active while skipping DirectX drawing (0% GPU usage).
        bool isIconicNow = (IsIconic(hWnd) != 0);
        if (isIconicNow) {
            mainWindow.update();
            Sleep(50);
            continue;
        }

        // 2. Active Window Client Geometry Synchronization:
        // Automatically check if window client dimensions differ from current backbuffer size.
        // Valid desktop window client area must be at least 320x200; in-flight iconic transitions (< 320x200) are ignored.
        RECT rcClient{};
        if (GetClientRect(hWnd, &rcClient)) {
            UINT cw = static_cast<UINT>(rcClient.right - rcClient.left);
            UINT ch = static_cast<UINT>(rcClient.bottom - rcClient.top);
            if (cw >= 120 && ch >= 40) {
                if (cw != g_CurrentBufferWidth || ch != g_CurrentBufferHeight) {
                    g_ResizeWidth = cw;
                    g_ResizeHeight = ch;
                }
            }
        }

        bool isFocused = (GetForegroundWindow() == hWnd);
        bool isPlaying = audioEngine.isPlaying();

        // Check if mouse cursor is currently over SlothPlayer window client area
        POINT ptCursor;
        bool isHoveredWindow = false;
        POINT ptClient = { 0, 0 };
        if (GetCursorPos(&ptCursor)) {
            ptClient = ptCursor;
            ScreenToClient(hWnd, &ptClient);
            if (ptClient.x >= 0 && ptClient.x < rcClient.right && ptClient.y >= 0 && ptClient.y < rcClient.bottom) {
                isHoveredWindow = true;
                hadInput = true;
                lastInputTime = frameStart;
            }
        }

        bool isMini = (g_pMainWindow && g_pMainWindow->isMiniPlayer());

        // 3. High-Performance Fluid FPS Governor
        double sinceInputSecs = (frameStart.QuadPart - lastInputTime.QuadPart) * (invFreqMs * 0.001);
        double targetFps = 60.0;
        if (isFocused || isHoveredWindow || isMini) {
            // Full 60+ FPS whenever interacting, playing, or in mini-player mode
            if (isPlaying || sinceInputSecs < 3.0) {
                targetFps = 60.0;
            } else {
                targetFps = 30.0; // Responsive idle
            }
        } else {
            // Window in background
            if (isPlaying) {
                targetFps = 30.0; // Smooth progress & instant resume
            } else {
                targetFps = 15.0; // Light background idle
            }
        }
        double targetFrameMs = 1000.0 / targetFps;

        // Handle resize if queued and valid
        if (g_ResizeWidth >= 120 && g_ResizeHeight >= 40) {
            DoResizeSwapChain(g_ResizeWidth, g_ResizeHeight);
        }

        // Render frame
        bool vsync = (targetFps >= 60.0 && !g_InSizeMove);
        RenderSingleFrame(hWnd, &mainWindow, vsync);

        // Diagnostic: log frame stats periodically
        {
            static DWORD s_lastDiagTick = 0;
            static int s_frameCount = 0;
            static int s_skippedFrames = 0;
            ++s_frameCount;
            DWORD nowTick = GetTickCount();
            if (nowTick - s_lastDiagTick >= 5000) {
                std::cout << "[DIAG] frames=" << s_frameCount
                          << " skipped=" << s_skippedFrames
                          << " iconic=" << (IsIconic(hWnd) ? 1 : 0)
                          << " focused=" << (isFocused ? 1 : 0)
                          << " renderTarget=" << (g_mainRenderTargetView ? 1 : 0)
                          << " bufW=" << g_CurrentBufferWidth
                          << " bufH=" << g_CurrentBufferHeight
                          << " targetFps=" << targetFps
                          << std::endl;
                s_frameCount = 0;
                s_skippedFrames = 0;
                s_lastDiagTick = nowTick;
            }
        }

        // Precision frame pacing: only sleep when VSync is OFF to prevent double-pacing stutter
        if (!vsync) {
            LARGE_INTEGER frameEnd;
            QueryPerformanceCounter(&frameEnd);
            double elapsedMs = (frameEnd.QuadPart - frameStart.QuadPart) * invFreqMs;
            if (elapsedMs < targetFrameMs) {
                DWORD sleepMs = static_cast<DWORD>(targetFrameMs - elapsedMs);
                if (sleepMs > 0) {
                    Sleep(sleepMs);
                }
            }
        }
    }


    timeEndPeriod(1);

    // Save window placement & all preferences before teardown
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(WINDOWPLACEMENT);
    if (GetWindowPlacement(hWnd, &wp)) {
        auto& s = SettingsManager::instance().settings();
        s.windowMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
        if (!s.windowMaximized) {
            s.windowX = wp.rcNormalPosition.left;
            s.windowY = wp.rcNormalPosition.top;
            s.windowWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
            s.windowHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
        }
    }
    mainWindow.savePreferences();
    SettingsManager::instance().save();

    // Teardown
    SMTCManager::instance().shutdown();
    TaskbarManager::instance().shutdown();
    g_pMainWindow = nullptr;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    textureManager.shutdown();
    library.saveLibrary();
    audioEngine.shutdown();

    CleanupDeviceD3D();
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();

    return 0;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(), SW_SHOW);
}

// ---------------- Helper D3D11 functions ----------------

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
    );
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
        );
    }
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    if (!g_pSwapChain || !g_pd3dDevice) return;
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (SUCCEEDED(hr) && pBackBuffer) {
        D3D11_TEXTURE2D_DESC desc;
        pBackBuffer->GetDesc(&desc);
        g_CurrentBufferWidth = desc.Width;
        g_CurrentBufferHeight = desc.Height;
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_pd3dDeviceContext) {
        // Essential: Unbind render targets and flush D3D11 pipeline before swapchain buffer resize
        g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
        g_pd3dDeviceContext->ClearState();
        g_pd3dDeviceContext->Flush();
    }
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

void DoResizeSwapChain(UINT newWidth, UINT newHeight) {
    if (!g_pSwapChain || newWidth < 120 || newHeight < 40) return;
    if (newWidth == g_CurrentBufferWidth && newHeight == g_CurrentBufferHeight && g_mainRenderTargetView != nullptr) return;

    CleanupRenderTarget();
    HRESULT hr = g_pSwapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr)) {
        g_ResizeWidth = 0;
        g_ResizeHeight = 0;
    }
    CreateRenderTarget();
}

void RenderSingleFrame(HWND hWnd, MainWindow* pMainWindow, bool vsync) {
    if (!g_pd3dDevice || !g_pd3dDeviceContext || !g_pSwapChain || !pMainWindow) return;
    if (g_IsRenderingFrame) return; // Reentrancy protection
    g_IsRenderingFrame = true;

    // RAII guard ensures flag is always cleared, even on exception
    struct RenderGuard { ~RenderGuard() { g_IsRenderingFrame = false; } } guard;

    if (!g_mainRenderTargetView) {
        CreateRenderTarget();
        if (!g_mainRenderTargetView) {
            return;
        }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    POINT ptCursor;
    if (GetCursorPos(&ptCursor)) {
        POINT ptClient = ptCursor;
        ScreenToClient(hWnd, &ptClient);
        ImGui::GetIO().AddMousePosEvent((float)ptClient.x, (float)ptClient.y);
    }

    ImGui::NewFrame();
    pMainWindow->render();
    ImGui::Render();

    if (g_mainRenderTargetView) {
        const float clear_color_with_alpha[4] = { 0.09f, 0.10f, 0.12f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    // Presentation: vsync = false during modal resizing/dragging gives instant 0-latency tracking
    UINT syncInterval = vsync ? 1 : 0;
    HRESULT hrPresent = g_pSwapChain->Present(syncInterval, 0);
    if (FAILED(hrPresent)) {
        static DWORD s_lastPresentErr = 0;
        DWORD now = GetTickCount();
        if (now - s_lastPresentErr > 2000) {
            std::cerr << "[DX11] Present error: 0x" << std::hex << hrPresent << std::dec << std::endl;
            s_lastPresentErr = now;
        }
        if (hrPresent == DXGI_ERROR_DEVICE_REMOVED || hrPresent == DXGI_ERROR_DEVICE_RESET) {
            CleanupDeviceD3D();
            CreateDeviceD3D(hWnd);
        }
    }
}


// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    if (msg != 0 && msg == TaskbarManager::getTaskbarButtonCreatedMsg()) {
        TaskbarManager::instance().onTaskbarButtonCreated(hWnd);
        return 0;
    }

    switch (msg) {
    case WM_NCCALCSIZE: {
        if (wParam == TRUE) {
            NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            if (IsZoomed(hWnd)) {
                HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(MONITORINFO);
                if (GetMonitorInfo(hMon, &mi)) {
                    params->rgrc[0] = mi.rcWork;
                }
            }
            return 0;
        }
        return 0;
    }
    case WM_NCPAINT:
        // Suppress Windows GDI non-client border painting
        return 0;
    case WM_NCACTIVATE: {
        // Explicitly set dark theme border color so Windows 11 DWM compositor never paints accent blue
        COLORREF darkBorder = RGB(42, 47, 56);
        DwmSetWindowAttribute(hWnd, 34 /* DWMWA_BORDER_COLOR */, &darkBorder, sizeof(darkBorder));
        // Return TRUE directly without calling DefWindowProcW to prevent Windows from repainting the accent blue border
        return TRUE;
    }
    case WM_ENTERSIZEMOVE:
        g_InSizeMove = true;
        return 0;
    case WM_EXITSIZEMOVE:
        g_InSizeMove = false;
        g_ForceRedraw = true;
        {
            RECT rcFinal;
            if (GetClientRect(hWnd, &rcFinal)) {
                UINT cw = static_cast<UINT>(rcFinal.right - rcFinal.left);
                UINT ch = static_cast<UINT>(rcFinal.bottom - rcFinal.top);
                if (cw >= 120 && ch >= 40) {
                    DoResizeSwapChain(cw, ch);
                    if (g_pMainWindow) {
                        RenderSingleFrame(hWnd, g_pMainWindow, false);
                    }
                }
            }
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        (void)BeginPaint(hWnd, &ps);
        if (g_InSizeMove && g_pMainWindow) {
            RenderSingleFrame(hWnd, g_pMainWindow, false);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        if (g_pMainWindow && g_pMainWindow->isMiniPlayer()) {
            mmi->ptMinTrackSize.x = 100;
            mmi->ptMinTrackSize.y = 32; // Allows compact slim bars and mini widgets
        } else {
            mmi->ptMinTrackSize.x = 480;
            mmi->ptMinTrackSize.y = 300;
        }
        return 0;
    }
    case WM_ACTIVATE:
    case WM_SETFOCUS: {
        COLORREF darkBorder = RGB(42, 47, 56);
        DwmSetWindowAttribute(hWnd, 34 /* DWMWA_BORDER_COLOR */, &darkBorder, sizeof(darkBorder));
        g_ForceRedraw = true;
        break;
    }
    case WM_NCHITTEST: {
        // When in mini player mode, entire area is client area (no border resizing stealing clicks or triggering Windows blue outline)
        if (g_pMainWindow && g_pMainWindow->isMiniPlayer()) {
            return HTCLIENT;
        }

        POINT pt = { (LONG)GET_X_LPARAM(lParam), (LONG)GET_Y_LPARAM(lParam) };
        RECT rc;
        GetWindowRect(hWnd, &rc);

        // Native resize borders when not maximized for full player
        if (!IsZoomed(hWnd)) {
            const int border = 8;
            bool left = (pt.x < rc.left + border);
            bool right = (pt.x >= rc.right - border);
            bool top = (pt.y < rc.top + border);
            bool bottom = (pt.y >= rc.bottom - border);

            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }

        return HTCLIENT;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        {
            UINT w = (UINT)LOWORD(lParam);
            UINT h = (UINT)HIWORD(lParam);
            if (w >= 120 && h >= 40) {
                g_ResizeWidth = w;
                g_ResizeHeight = h;
                g_ForceRedraw = true;

                // Live continuous resizing during drag or maximize/restore
                if (g_InSizeMove || wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED) {
                    DoResizeSwapChain(w, h);
                    if (g_pMainWindow) {
                        RenderSingleFrame(hWnd, g_pMainWindow, false);
                    }
                }
            }
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        if ((wParam & 0xfff0) == SC_RESTORE || (wParam & 0xfff0) == SC_MAXIMIZE) {
            g_ForceRedraw = true;
        }
        break;
    case WM_CLOSE: {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(hWnd, &wp)) {
            auto& s = SettingsManager::instance().settings();
            s.windowMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
            if (!s.windowMaximized) {
                s.windowX = wp.rcNormalPosition.left;
                s.windowY = wp.rcNormalPosition.top;
                s.windowWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
                s.windowHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            }
        }
        if (g_pMainWindow) {
            g_pMainWindow->savePreferences();
        }
        SettingsManager::instance().save();
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::string> droppedPaths;

        for (UINT i = 0; i < fileCount; ++i) {
            UINT cch = DragQueryFileW(hDrop, i, nullptr, 0);
            if (cch > 0) {
                std::vector<WCHAR> filePathW(cch + 1, L'\0');
                if (DragQueryFileW(hDrop, i, filePathW.data(), cch + 1)) {
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, filePathW.data(), -1, nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 0) {
                        std::string filePathA(utf8Len, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, filePathW.data(), -1, &filePathA[0], utf8Len, nullptr, nullptr);
                        if (!filePathA.empty() && filePathA.back() == '\0') filePathA.pop_back();
                        droppedPaths.push_back(std::move(filePathA));
                    }
                }
            }
        }
        DragFinish(hDrop);

        if (g_pMainWindow && !droppedPaths.empty()) {
            g_pMainWindow->handleDroppedFiles(droppedPaths);
        }
        return 0;
    }
    case WM_COMMAND: {
        #ifndef THBN_CLICKED
        #define THBN_CLICKED 0x1800
        #endif
        if (HIWORD(wParam) == THBN_CLICKED) {
            TaskbarManager::instance().handleThumbbarCommand(hWnd, LOWORD(wParam));
            return 0;
        }
        break;
    }
    case WM_APPCOMMAND: {
        DWORD cmd = GET_APPCOMMAND_LPARAM(lParam);
        switch (cmd) {
            case APPCOMMAND_MEDIA_PLAY_PAUSE:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::TogglePlayPause);
                return TRUE;
            case APPCOMMAND_MEDIA_PLAY:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::Play);
                return TRUE;
            case APPCOMMAND_MEDIA_PAUSE:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::Pause);
                return TRUE;
            case APPCOMMAND_MEDIA_NEXTTRACK:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::Next);
                return TRUE;
            case APPCOMMAND_MEDIA_PREVIOUSTRACK:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::Previous);
                return TRUE;
            case APPCOMMAND_MEDIA_STOP:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::Stop);
                return TRUE;
        }
        break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        switch (wParam) {
            case VK_MEDIA_PLAY_PAUSE:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::TogglePlayPause);
                return 0;
            case VK_MEDIA_NEXT_TRACK:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::Next);
                return 0;
            case VK_MEDIA_PREV_TRACK:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::Previous);
                return 0;
            case VK_MEDIA_STOP:
                if (g_pMainWindow) g_pMainWindow->triggerMediaAction(MediaAction::Stop);
                return 0;
        }
        break;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

#else
// =========================================================================
// Linux / Cross-Platform Desktop Implementation (GLFW + OpenGL 3.3 / GLES)
// Architectures: AMD (x86_64), ARM (aarch64 / armhf), RISC-V (riscv64)
// =========================================================================

#include <GLFW/glfw3.h>
#include "../third_party/imgui/backends/imgui_impl_glfw.h"
#include "../third_party/imgui/backends/imgui_impl_opengl3.h"

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    // GL Version and Shader Configuration
    const char* glsl_version = "#version 130";
#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glsl_version = "#version 150";
#elif defined(__arm__) || defined(__aarch64__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glsl_version = "#version 130";
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glsl_version = "#version 130";
#endif

    GLFWwindow* window = glfwCreateWindow(1260, 760, "SlothPlayer", nullptr, nullptr);
    if (!window) {
        // Fallback for software rasterizers, llvmpipe, older drivers, or RISC-V SBCs
        glfwDefaultWindowHints();
        glsl_version = "#version 130";
        window = glfwCreateWindow(1260, 760, "SlothPlayer", nullptr, nullptr);
        if (!window) {
            glfwTerminate();
            return 1;
        }
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable V-Sync

    // Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    Theme::applyTheme(AppTheme::ClassicDark);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Audio & Library Init
    AudioEngine audioEngine;
    if (!audioEngine.init()) {
        std::cerr << "[Warning] AudioEngine initialization failed" << std::endl;
    }

    LibraryManager library;
    library.loadLibrary();

    TextureManager textureManager;
    textureManager.init();

    MainWindow mainWindow(audioEngine, library, textureManager);

    bool shouldQuit = false;
    Platform::setQuitFlag(&shouldQuit);

    // Main render loop
    while (!glfwWindowShouldClose(window) && !shouldQuit) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        mainWindow.render();

        ImGui::Render();
        int display_w = 0, display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Teardown
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    textureManager.shutdown();
    library.saveLibrary();
    audioEngine.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
#endif
