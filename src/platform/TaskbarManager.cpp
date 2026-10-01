#include "TaskbarManager.h"
#include "../ui/MainWindow.h"
#include <iostream>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <filesystem>
#include "../library/TagReader.h"
#include "../third_party/stb_image.h"

namespace {
    constexpr UINT ID_THB_PREV      = 1001;
    constexpr UINT ID_THB_REWIND    = 1002;
    constexpr UINT ID_THB_STOP      = 1003;
    constexpr UINT ID_THB_PLAYPAUSE = 1004;
    constexpr UINT ID_THB_FORWARD   = 1005;
    constexpr UINT ID_THB_NEXT      = 1006;
    constexpr UINT ID_THB_RESTORE   = 1007;

    UINT s_uTaskbarButtonCreatedMsg = 0;

    HICON createVectorIcon(int type) {
        const int w = 20;
        const int h = 20;

        BITMAPV5HEADER bi = {};
        bi.bV5Size = sizeof(BITMAPV5HEADER);
        bi.bV5Width = w;
        bi.bV5Height = -h; // Top-down
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask   = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask  = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;

        HDC hdc = GetDC(nullptr);
        uint32_t* pixels = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        ReleaseDC(nullptr, hdc);

        if (!hBitmap || !pixels) return nullptr;

        memset(pixels, 0, w * h * sizeof(uint32_t));

        auto setPixel = [&](int x, int y, uint32_t argb) {
            if (x >= 0 && x < w && y >= 0 && y < h) {
                pixels[y * w + x] = argb;
            }
        };

        auto fillRect = [&](int x0, int y0, int x1, int y1, uint32_t argb) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    setPixel(x, y, argb);
                }
            }
        };

        auto drawLine = [&](int x0, int y0, int x1, int y1, uint32_t argb) {
            int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
            int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
            int err = dx + dy, e2;
            while (true) {
                setPixel(x0, y0, argb);
                if (x0 == x1 && y0 == y1) break;
                e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
        };

        const uint32_t cWhite = 0xFFF0F4F8; // Crisp off-white
        const uint32_t cBlue  = 0xFF2196F3; // Vibrant Accent Blue for Expand button

        switch (type) {
            case 0: { // 0: Prev Track (|<<)
                fillRect(2, 4, 3, 15, cWhite);
                for (int y = 4; y <= 15; ++y) {
                    int dist = std::abs(y - 9);
                    int startX = 4 + dist / 2;
                    for (int x = startX; x <= 9; ++x) setPixel(x, y, cWhite);
                    int startX2 = 10 + dist / 2;
                    for (int x = startX2; x <= 16; ++x) setPixel(x, y, cWhite);
                }
                break;
            }
            case 1: { // 1: Rewind (<<)
                for (int y = 4; y <= 15; ++y) {
                    int dist = std::abs(y - 9);
                    int startX1 = 3 + dist / 2;
                    for (int x = startX1; x <= 9; ++x) setPixel(x, y, cWhite);
                    int startX2 = 10 + dist / 2;
                    for (int x = startX2; x <= 16; ++x) setPixel(x, y, cWhite);
                }
                break;
            }
            case 2: { // 2: Stop (■)
                fillRect(4, 4, 15, 15, cWhite);
                break;
            }
            case 3: { // 3: Play (▶)
                for (int y = 4; y <= 15; ++y) {
                    int dist = std::abs(y - 9);
                    int endX = 16 - dist;
                    for (int x = 4; x <= endX; ++x) setPixel(x, y, cWhite);
                }
                break;
            }
            case 4: { // 4: Pause (||)
                fillRect(4, 4, 7, 15, cWhite);
                fillRect(12, 4, 15, 15, cWhite);
                break;
            }
            case 5: { // 5: Forward (>>)
                for (int y = 4; y <= 15; ++y) {
                    int dist = std::abs(y - 9);
                    int endX1 = 9 - dist / 2;
                    for (int x = 3; x <= endX1; ++x) setPixel(x, y, cWhite);
                    int endX2 = 16 - dist / 2;
                    for (int x = 10; x <= endX2; ++x) setPixel(x, y, cWhite);
                }
                break;
            }
            case 6: { // 6: Next Track (>>|)
                for (int y = 4; y <= 15; ++y) {
                    int dist = std::abs(y - 9);
                    int endX1 = 8 - dist / 2;
                    for (int x = 3; x <= endX1; ++x) setPixel(x, y, cWhite);
                    int endX2 = 14 - dist / 2;
                    for (int x = 9; x <= endX2; ++x) setPixel(x, y, cWhite);
                }
                fillRect(16, 4, 17, 15, cWhite);
                break;
            }
            case 7: { // 7: Expand / Restore (⤢)
                fillRect(12, 3, 16, 4, cBlue);
                fillRect(15, 3, 16, 7, cBlue);
                drawLine(10, 9, 15, 4, cBlue);
                drawLine(10, 10, 15, 5, cBlue);

                fillRect(3, 15, 7, 16, cBlue);
                fillRect(3, 12, 4, 16, cBlue);
                drawLine(9, 10, 4, 15, cBlue);
                drawLine(10, 10, 5, 15, cBlue);
                break;
            }
        }

        HBITMAP hMonoMask = CreateBitmap(w, h, 1, 1, nullptr);
        ICONINFO ii = {};
        ii.fIcon = TRUE;
        ii.hbmColor = hBitmap;
        ii.hbmMask = hMonoMask;
        HICON hIcon = CreateIconIndirect(&ii);

        DeleteObject(hBitmap);
        DeleteObject(hMonoMask);
        return hIcon;
    }
}
#endif

struct TaskbarManager::Impl {
#if defined(_WIN32) || defined(_WIN64)
    ITaskbarList3* pTaskbarList = nullptr;
    MainWindow* pMainWindow = nullptr;
    HWND hWnd = nullptr;
    bool buttonsAdded = false;
    bool lastIsPlaying = false;
    uint64_t lastTrackId = 0;
    std::string lastTitle;

    // Thumbnail cache — avoids re-decoding album art on every DWM request
    uint64_t thumbCachedTrackId = 0;
    std::vector<uint8_t> thumbPixels; // RGBA decoded pixels
    int thumbSrcW = 0;
    int thumbSrcH = 0;
    DWORD thumbLastCallTick = 0; // Rate limiter

    HICON hIconPrev = nullptr;
    HICON hIconRewind = nullptr;
    HICON hIconStop = nullptr;
    HICON hIconPlay = nullptr;
    HICON hIconPause = nullptr;
    HICON hIconForward = nullptr;
    HICON hIconNext = nullptr;
    HICON hIconRestore = nullptr;

    void initIcons() {
        if (!hIconPrev)    hIconPrev    = createVectorIcon(0);
        if (!hIconRewind)  hIconRewind  = createVectorIcon(1);
        if (!hIconStop)    hIconStop    = createVectorIcon(2);
        if (!hIconPlay)    hIconPlay    = createVectorIcon(3);
        if (!hIconPause)   hIconPause   = createVectorIcon(4);
        if (!hIconForward) hIconForward = createVectorIcon(5);
        if (!hIconNext)    hIconNext    = createVectorIcon(6);
        if (!hIconRestore) hIconRestore = createVectorIcon(7);
    }

    void freeIcons() {
        if (hIconPrev)    { DestroyIcon(hIconPrev);    hIconPrev    = nullptr; }
        if (hIconRewind)  { DestroyIcon(hIconRewind);  hIconRewind  = nullptr; }
        if (hIconStop)    { DestroyIcon(hIconStop);    hIconStop    = nullptr; }
        if (hIconPlay)    { DestroyIcon(hIconPlay);    hIconPlay    = nullptr; }
        if (hIconPause)   { DestroyIcon(hIconPause);   hIconPause   = nullptr; }
        if (hIconForward) { DestroyIcon(hIconForward); hIconForward = nullptr; }
        if (hIconNext)    { DestroyIcon(hIconNext);    hIconNext    = nullptr; }
        if (hIconRestore) { DestroyIcon(hIconRestore); hIconRestore = nullptr; }
    }

    void addThumbnailButtons(bool isPlaying) {
        if (!pTaskbarList || !hWnd) return;
        initIcons();

        THUMBBUTTON tb[7] = {};

        // 1. Prev Track
        tb[0].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[0].iId = ID_THB_PREV;
        tb[0].hIcon = hIconPrev;
        wcscpy_s(tb[0].szTip, L"Previous Track");
        tb[0].dwFlags = THBF_ENABLED;

        // 2. Rewind 5s
        tb[1].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[1].iId = ID_THB_REWIND;
        tb[1].hIcon = hIconRewind;
        wcscpy_s(tb[1].szTip, L"Rewind 5s");
        tb[1].dwFlags = THBF_ENABLED;

        // 3. Stop
        tb[2].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[2].iId = ID_THB_STOP;
        tb[2].hIcon = hIconStop;
        wcscpy_s(tb[2].szTip, L"Stop");
        tb[2].dwFlags = THBF_ENABLED;

        // 4. Play / Pause
        tb[3].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[3].iId = ID_THB_PLAYPAUSE;
        tb[3].hIcon = isPlaying ? hIconPause : hIconPlay;
        wcscpy_s(tb[3].szTip, isPlaying ? L"Pause" : L"Play");
        tb[3].dwFlags = THBF_ENABLED;

        // 5. Forward 5s
        tb[4].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[4].iId = ID_THB_FORWARD;
        tb[4].hIcon = hIconForward;
        wcscpy_s(tb[4].szTip, L"Forward 5s");
        tb[4].dwFlags = THBF_ENABLED;

        // 6. Next Track
        tb[5].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[5].iId = ID_THB_NEXT;
        tb[5].hIcon = hIconNext;
        wcscpy_s(tb[5].szTip, L"Next Track");
        tb[5].dwFlags = THBF_ENABLED;

        // 7. Restore / Expand
        tb[6].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[6].iId = ID_THB_RESTORE;
        tb[6].hIcon = hIconRestore;
        wcscpy_s(tb[6].szTip, L"Restore Window");
        tb[6].dwFlags = THBF_ENABLED;

        HRESULT hr = pTaskbarList->ThumbBarAddButtons(hWnd, 7, tb);
        if (SUCCEEDED(hr)) {
            buttonsAdded = true;
            lastIsPlaying = isPlaying;
        }
    }

    void updatePlayPauseButton(bool isPlaying) {
        if (!pTaskbarList || !hWnd || !buttonsAdded) return;
        initIcons();

        THUMBBUTTON tb[7] = {};

        tb[0].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[0].iId = ID_THB_PREV;
        tb[0].hIcon = hIconPrev;
        wcscpy_s(tb[0].szTip, L"Previous Track");
        tb[0].dwFlags = THBF_ENABLED;

        tb[1].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[1].iId = ID_THB_REWIND;
        tb[1].hIcon = hIconRewind;
        wcscpy_s(tb[1].szTip, L"Rewind 5s");
        tb[1].dwFlags = THBF_ENABLED;

        tb[2].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[2].iId = ID_THB_STOP;
        tb[2].hIcon = hIconStop;
        wcscpy_s(tb[2].szTip, L"Stop");
        tb[2].dwFlags = THBF_ENABLED;

        tb[3].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[3].iId = ID_THB_PLAYPAUSE;
        tb[3].hIcon = isPlaying ? hIconPause : hIconPlay;
        wcscpy_s(tb[3].szTip, isPlaying ? L"Pause" : L"Play");
        tb[3].dwFlags = THBF_ENABLED;

        tb[4].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[4].iId = ID_THB_FORWARD;
        tb[4].hIcon = hIconForward;
        wcscpy_s(tb[4].szTip, L"Forward 5s");
        tb[4].dwFlags = THBF_ENABLED;

        tb[5].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[5].iId = ID_THB_NEXT;
        tb[5].hIcon = hIconNext;
        wcscpy_s(tb[5].szTip, L"Next Track");
        tb[5].dwFlags = THBF_ENABLED;

        tb[6].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[6].iId = ID_THB_RESTORE;
        tb[6].hIcon = hIconRestore;
        wcscpy_s(tb[6].szTip, L"Restore Window");
        tb[6].dwFlags = THBF_ENABLED;

        pTaskbarList->ThumbBarUpdateButtons(hWnd, 7, tb);
        lastIsPlaying = isPlaying;
    }
#endif
};

TaskbarManager& TaskbarManager::instance() {
    static TaskbarManager s_inst;
    return s_inst;
}

TaskbarManager::TaskbarManager() : m_impl(new Impl()) {}

TaskbarManager::~TaskbarManager() {
    shutdown();
    delete m_impl;
}

unsigned int TaskbarManager::getTaskbarButtonCreatedMsg() {
#if defined(_WIN32) || defined(_WIN64)
    if (s_uTaskbarButtonCreatedMsg == 0) {
        s_uTaskbarButtonCreatedMsg = RegisterWindowMessageW(L"TaskbarButtonCreated");
    }
    return s_uTaskbarButtonCreatedMsg;
#else
    return 0;
#endif
}

bool TaskbarManager::initialize(void* hWnd, MainWindow* pMainWindow) {
#if defined(_WIN32) || defined(_WIN64)
    if (!hWnd) return false;
    getTaskbarButtonCreatedMsg();

    m_impl->hWnd = static_cast<HWND>(hWnd);
    m_impl->pMainWindow = pMainWindow;

    if (!m_impl->pTaskbarList) {
        HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_impl->pTaskbarList));
        if (FAILED(hr) || !m_impl->pTaskbarList) {
            return false;
        }
        m_impl->pTaskbarList->HrInit();
    }

    if (!m_impl->buttonsAdded) {
        m_impl->addThumbnailButtons(false);
    }

    return true;
#else
    (void)hWnd; (void)pMainWindow;
    return false;
#endif
}

void TaskbarManager::shutdown() {
#if defined(_WIN32) || defined(_WIN64)
    if (m_impl->pTaskbarList) {
        if (m_impl->hWnd) {
            m_impl->pTaskbarList->SetProgressState(m_impl->hWnd, TBPF_NOPROGRESS);
        }
        m_impl->pTaskbarList->Release();
        m_impl->pTaskbarList = nullptr;
    }
    m_impl->freeIcons();
    m_impl->buttonsAdded = false;
    m_impl->hWnd = nullptr;
    m_impl->pMainWindow = nullptr;
#endif
}

void TaskbarManager::onTaskbarButtonCreated(void* hWnd) {
#if defined(_WIN32) || defined(_WIN64)
    if (!hWnd) return;
    m_impl->hWnd = static_cast<HWND>(hWnd);
    m_impl->buttonsAdded = false;
    if (m_impl->pTaskbarList) {
        m_impl->addThumbnailButtons(m_impl->lastIsPlaying);
    } else {
        initialize(hWnd, m_impl->pMainWindow);
    }
#else
    (void)hWnd;
#endif
}

void TaskbarManager::update(void* hWnd, bool isPlaying, bool isPaused, double curTime, double totalDur,
                            const std::string& title, const std::string& artist) {
#if defined(_WIN32) || defined(_WIN64)
    if (!hWnd) return;
    HWND hwnd = static_cast<HWND>(hWnd);
    m_impl->hWnd = hwnd;

    if (!m_impl->pTaskbarList) {
        initialize(hWnd, m_impl->pMainWindow);
    } else if (!m_impl->buttonsAdded) {
        m_impl->addThumbnailButtons(isPlaying);
    }

    // Track ID tracking
    const Track* currTrack = m_impl->pMainWindow ? m_impl->pMainWindow->getCurrentTrack() : nullptr;
    uint64_t currTrackId = currTrack ? currTrack->id : 0;
    if (currTrackId != m_impl->lastTrackId) {
        m_impl->lastTrackId = currTrackId;
    }

    // 1. Update Play/Pause Button Icon and Tooltip when play state changes
    if (m_impl->buttonsAdded && m_impl->lastIsPlaying != isPlaying) {
        m_impl->updatePlayPauseButton(isPlaying);
    }

    // 2. Update Taskbar Progress Indicator on Taskbar Icon
    if (m_impl->pTaskbarList) {
        if (isPlaying && totalDur > 0.0) {
            ULONGLONG curVal = static_cast<ULONGLONG>(curTime * 1000.0);
            ULONGLONG totVal = static_cast<ULONGLONG>(totalDur * 1000.0);
            m_impl->pTaskbarList->SetProgressValue(hwnd, curVal, totVal);
            m_impl->pTaskbarList->SetProgressState(hwnd, TBPF_NORMAL);
        } else if (isPaused && totalDur > 0.0) {
            ULONGLONG curVal = static_cast<ULONGLONG>(curTime * 1000.0);
            ULONGLONG totVal = static_cast<ULONGLONG>(totalDur * 1000.0);
            m_impl->pTaskbarList->SetProgressValue(hwnd, curVal, totVal);
            m_impl->pTaskbarList->SetProgressState(hwnd, TBPF_PAUSED);
        } else {
            m_impl->pTaskbarList->SetProgressState(hwnd, TBPF_NOPROGRESS);
        }
    }

    // 3. Update Window Title for Taskbar Thumbnail Header
    std::string fullTitle;
    if (isPlaying || isPaused) {
        if (!artist.empty() && !title.empty()) {
            fullTitle = "[SlothPlayer] " + artist + " - " + title;
        } else if (!title.empty()) {
            fullTitle = "[SlothPlayer] " + title;
        } else {
            fullTitle = "SlothPlayer";
        }
    } else {
        fullTitle = "SlothPlayer";
    }

    if (fullTitle != m_impl->lastTitle) {
        m_impl->lastTitle = fullTitle;
        int len = MultiByteToWideChar(CP_UTF8, 0, fullTitle.c_str(), -1, nullptr, 0);
        if (len > 0) {
            std::vector<wchar_t> wbuf(len);
            MultiByteToWideChar(CP_UTF8, 0, fullTitle.c_str(), -1, wbuf.data(), len);
            SetWindowTextW(hwnd, wbuf.data());
        }
    }
#else
    (void)hWnd; (void)isPlaying; (void)isPaused; (void)curTime; (void)totalDur; (void)title; (void)artist;
#endif
}

void TaskbarManager::handleThumbbarCommand(void* hWnd, unsigned int buttonId) {
#if defined(_WIN32) || defined(_WIN64)
    if (!m_impl->pMainWindow) return;
    HWND hwnd = static_cast<HWND>(hWnd);

    switch (buttonId) {
        case ID_THB_PREV:
            m_impl->pMainWindow->playPrevious();
            break;
        case ID_THB_REWIND: {
            double cur = m_impl->pMainWindow->m_audio.getCurrentTime();
            m_impl->pMainWindow->m_audio.seekTo((std::max)(0.0, cur - 5.0));
            break;
        }
        case ID_THB_STOP:
            m_impl->pMainWindow->stop();
            break;
        case ID_THB_PLAYPAUSE:
            m_impl->pMainWindow->togglePlayPause();
            break;
        case ID_THB_FORWARD: {
            double cur = m_impl->pMainWindow->m_audio.getCurrentTime();
            double dur = m_impl->pMainWindow->m_audio.getTotalDuration();
            m_impl->pMainWindow->m_audio.seekTo((std::min)(dur, cur + 5.0));
            break;
        }
        case ID_THB_NEXT:
            m_impl->pMainWindow->playNext();
            break;
        case ID_THB_RESTORE:
            if (hwnd) {
                if (IsIconic(hwnd)) {
                    ShowWindow(hwnd, SW_RESTORE);
                }
                SetForegroundWindow(hwnd);
            }
            break;
    }
#else
    (void)hWnd; (void)buttonId;
#endif
}

void TaskbarManager::onSendIconicThumbnail(void* hWnd, int maxW, int maxH) {
#if defined(_WIN32) || defined(_WIN64)
    if (!hWnd || maxW <= 0 || maxH <= 0) return;
    HWND hwnd = static_cast<HWND>(hWnd);

    // Rate limiter: ignore requests within 100ms of the last one
    DWORD now = GetTickCount();
    if (now - m_impl->thumbLastCallTick < 100) return;
    m_impl->thumbLastCallTick = now;

    const Track* curr = m_impl->pMainWindow ? m_impl->pMainWindow->getCurrentTrack() : nullptr;
    uint64_t currId = curr ? curr->id : 0;

    // Only re-decode if the track changed
    if (currId != m_impl->thumbCachedTrackId) {
        m_impl->thumbPixels.clear();
        m_impl->thumbSrcW = 0;
        m_impl->thumbSrcH = 0;
        m_impl->thumbCachedTrackId = currId;

        if (curr) {
            std::vector<uint8_t> artBytes;
            int srcW = 0, srcH = 0, comp = 0;
            uint8_t* decoded = nullptr;

            // 1. Try extracting directly from embedded tags
            if (TagReader::extractAlbumArt(*curr, artBytes) && !artBytes.empty()) {
                decoded = stbi_load_from_memory(artBytes.data(), static_cast<int>(artBytes.size()), &srcW, &srcH, &comp, 4);
            }
            // 2. Try loading from albumArtPath
            if (!decoded && !curr->albumArtPath.empty()) {
                int len = MultiByteToWideChar(CP_UTF8, 0, curr->albumArtPath.c_str(), -1, nullptr, 0);
                if (len > 0) {
                    std::wstring wPath(len, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, curr->albumArtPath.c_str(), -1, &wPath[0], len);
                    FILE* f = _wfopen(wPath.c_str(), L"rb");
                    if (f) {
                        fseek(f, 0, SEEK_END);
                        long sz = ftell(f);
                        fseek(f, 0, SEEK_SET);
                        if (sz > 0 && sz <= 50 * 1024 * 1024) {
                            std::vector<uint8_t> buf(sz);
                            if (fread(buf.data(), 1, sz, f) == static_cast<size_t>(sz)) {
                                decoded = stbi_load_from_memory(buf.data(), static_cast<int>(buf.size()), &srcW, &srcH, &comp, 4);
                            }
                        }
                        fclose(f);
                    }
                }
            }
            // 3. Fallback to temp cached artwork file
            if (!decoded) {
                std::error_code ec;
                std::filesystem::path tempP = std::filesystem::temp_directory_path(ec) / "sloth_current_art.jpg";
                if (!ec && std::filesystem::exists(tempP)) {
                    FILE* f = _wfopen(tempP.wstring().c_str(), L"rb");
                    if (f) {
                        fseek(f, 0, SEEK_END);
                        long sz = ftell(f);
                        fseek(f, 0, SEEK_SET);
                        if (sz > 0 && sz <= 50 * 1024 * 1024) {
                            std::vector<uint8_t> buf(sz);
                            if (fread(buf.data(), 1, sz, f) == static_cast<size_t>(sz)) {
                                decoded = stbi_load_from_memory(buf.data(), static_cast<int>(buf.size()), &srcW, &srcH, &comp, 4);
                            }
                        }
                        fclose(f);
                    }
                }
            }

            if (decoded && srcW > 0 && srcH > 0) {
                m_impl->thumbSrcW = srcW;
                m_impl->thumbSrcH = srcH;
                m_impl->thumbPixels.assign(decoded, decoded + (srcW * srcH * 4));
                stbi_image_free(decoded);
            } else if (decoded) {
                stbi_image_free(decoded);
            }
        }
    }

    int srcW = m_impl->thumbSrcW;
    int srcH = m_impl->thumbSrcH;
    const uint8_t* cachedPx = m_impl->thumbPixels.empty() ? nullptr : m_impl->thumbPixels.data();

    // Calculate aspect-fit dimensions constrained within maxW x maxH
    int targetW = maxW;
    int targetH = maxH;
    if (srcW > 0 && srcH > 0) {
        float scale = (std::min)(static_cast<float>(maxW) / static_cast<float>(srcW), static_cast<float>(maxH) / static_cast<float>(srcH));
        targetW = (std::max)(16, (std::min)(maxW, static_cast<int>(srcW * scale)));
        targetH = (std::max)(16, (std::min)(maxH, static_cast<int>(srcH * scale)));
    } else {
        int sz = (std::min)(maxW, maxH);
        if (sz < 64) sz = 64;
        targetW = sz;
        targetH = sz;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = targetW;
    bi.bmiHeader.biHeight = -targetH; // Top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    uint32_t* pDIBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pDIBits), nullptr, 0);
    ReleaseDC(nullptr, hdc);

    if (hBitmap && pDIBits) {
        if (cachedPx && srcW > 0 && srcH > 0) {
            for (int y = 0; y < targetH; ++y) {
                float fSrcY = (static_cast<float>(y) / static_cast<float>(targetH)) * static_cast<float>(srcH - 1);
                int y0 = static_cast<int>(fSrcY);
                int y1 = (std::min)(y0 + 1, srcH - 1);
                float yLerp = fSrcY - y0;

                for (int x = 0; x < targetW; ++x) {
                    float fSrcX = (static_cast<float>(x) / static_cast<float>(targetW)) * static_cast<float>(srcW - 1);
                    int x0 = static_cast<int>(fSrcX);
                    int x1 = (std::min)(x0 + 1, srcW - 1);
                    float xLerp = fSrcX - x0;

                    const uint8_t* p00 = &cachedPx[(y0 * srcW + x0) * 4];
                    const uint8_t* p10 = &cachedPx[(y0 * srcW + x1) * 4];
                    const uint8_t* p01 = &cachedPx[(y1 * srcW + x0) * 4];
                    const uint8_t* p11 = &cachedPx[(y1 * srcW + x1) * 4];

                    auto bLerp = [&](int c) -> uint8_t {
                        float top = p00[c] * (1.0f - xLerp) + p10[c] * xLerp;
                        float bot = p01[c] * (1.0f - xLerp) + p11[c] * xLerp;
                        return static_cast<uint8_t>(std::clamp(top * (1.0f - yLerp) + bot * yLerp, 0.0f, 255.0f));
                    };

                    uint8_t r = bLerp(0);
                    uint8_t g = bLerp(1);
                    uint8_t b = bLerp(2);

                    pDIBits[y * targetW + x] = (0xFFu << 24) |
                                              (static_cast<uint32_t>(r) << 16) |
                                              (static_cast<uint32_t>(g) << 8)  |
                                              static_cast<uint32_t>(b);
                }
            }
        } else {
            // Sleek vinyl record placeholder with gold center label
            for (int y = 0; y < targetH; ++y) {
                float dy = static_cast<float>(y - targetH / 2);
                for (int x = 0; x < targetW; ++x) {
                    float dx = static_cast<float>(x - targetW / 2);
                    float dist = std::hypot(dx, dy);
                    float maxR = static_cast<float>((std::min)(targetW, targetH)) * 0.46f;

                    uint32_t col = 0xFF14171E;
                    if (dist <= maxR) {
                        float normD = dist / maxR;
                        if (normD < 0.32f) {
                            col = 0xFFE5A00D;
                            if (normD < 0.08f) col = 0xFF121418;
                        } else {
                            int groove = static_cast<int>(dist) % 4;
                            col = (groove == 0) ? 0xFF222630 : 0xFF181B22;
                        }
                    }
                    pDIBits[y * targetW + x] = col;
                }
            }
        }

        HRESULT hr = DwmSetIconicThumbnail(hwnd, hBitmap, 0);
        DeleteObject(hBitmap);
        std::cout << "[Taskbar] onSendIconicThumbnail (" << maxW << "x" << maxH << ") -> (" 
                  << targetW << "x" << targetH << "), hr=0x" << std::hex << hr << std::dec << std::endl;
    }
#else
    (void)hWnd; (void)maxW; (void)maxH;
#endif
}
