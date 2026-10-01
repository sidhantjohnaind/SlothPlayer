#include "Platform.h"
#include <cstdlib>
#include <iostream>
#include <algorithm>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <dwmapi.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace Platform {

static bool* s_quitFlag = nullptr;

void setQuitFlag(bool* pFlag) {
    s_quitFlag = pFlag;
}

std::string getAppDataDir() {
#if defined(_WIN32) || defined(_WIN64)
    const char* appData = std::getenv("APPDATA");
    if (appData) {
        std::filesystem::path p = std::filesystem::path(appData) / "SlothPlayer";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p.string();
    }
    return ".";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        std::filesystem::path p = std::filesystem::path(xdg) / "SlothPlayer";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p.string();
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        std::filesystem::path p = std::filesystem::path(home) / ".config" / "SlothPlayer";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p.string();
    }
    return ".";
#endif
}

void openInFileManager(const std::string& filePath) {
    if (filePath.empty()) return;
#if defined(_WIN32) || defined(_WIN64)
    std::string cmd = "/select,\"" + filePath + "\"";
    ShellExecuteA(NULL, "open", "explorer.exe", cmd.c_str(), NULL, SW_SHOWNORMAL);
#else
    std::filesystem::path p(filePath);
    std::string dir = std::filesystem::is_directory(p) ? p.string() : p.parent_path().string();
    if (dir.empty()) dir = ".";
    std::string cmd = "xdg-open \"" + dir + "\" >/dev/null 2>&1 &";
    (void)std::system(cmd.c_str());
#endif
}

void openDirectory(const std::string& dirPath) {
    if (dirPath.empty()) return;
#if defined(_WIN32) || defined(_WIN64)
    ShellExecuteA(NULL, "open", dirPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    std::string cmd = "xdg-open \"" + dirPath + "\" >/dev/null 2>&1 &";
    (void)std::system(cmd.c_str());
#endif
}

std::string openFileDialog(const std::string& filterTitle, const std::string& filterSpec) {
#if defined(_WIN32) || defined(_WIN64)
    char szFile[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;

    std::string filterBuf;
    filterBuf += filterTitle;
    filterBuf.push_back('\0');
    filterBuf += filterSpec;
    filterBuf.push_back('\0');
    filterBuf += "All Files (*.*)";
    filterBuf.push_back('\0');
    filterBuf += "*.*";
    filterBuf.push_back('\0');
    filterBuf.push_back('\0');

    ofn.lpstrFilter = filterBuf.c_str();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(szFile);
    }
    return "";
#else
    FILE* fp = popen("zenity --file-selection 2>/dev/null", "r");
    if (fp) {
        char buf[1024] = { 0 };
        if (fgets(buf, sizeof(buf), fp)) {
            std::string res = buf;
            while (!res.empty() && (res.back() == '\n' || res.back() == '\r')) res.pop_back();
            pclose(fp);
            if (!res.empty()) return res;
        } else {
            pclose(fp);
        }
    }
    return "";
#endif
}

std::string openFolderDialog(const std::string& title) {
#if defined(_WIN32) || defined(_WIN64)
    BROWSEINFOA bi = {};
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != 0) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::string(path);
        }
        CoTaskMemFree(pidl);
    }
    return "";
#else
    std::string cmd = "zenity --file-selection --directory --title=\"" + title + "\" 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp) {
        char buf[1024] = { 0 };
        if (fgets(buf, sizeof(buf), fp)) {
            std::string res = buf;
            while (!res.empty() && (res.back() == '\n' || res.back() == '\r')) res.pop_back();
            pclose(fp);
            if (!res.empty()) return res;
        } else {
            pclose(fp);
        }
    }
    return "";
#endif
}

void requestQuit() {
    if (s_quitFlag) {
        *s_quitFlag = true;
    }
#if defined(_WIN32) || defined(_WIN64)
    PostQuitMessage(0);
#endif
}

#if defined(_WIN32) || defined(_WIN64)
static bool s_hasSavedRect = false;
static RECT s_savedRect = { 0, 0, 1260, 760 };
#endif

void setMiniPlayerWindow(void* windowHandle, bool isMini, int targetWidth, int targetHeight, bool alwaysOnTop) {
#if defined(_WIN32) || defined(_WIN64)
    if (!windowHandle) return;
    HWND hwnd = reinterpret_cast<HWND>(windowHandle);
    if (isMini) {
        if (!s_hasSavedRect) {
            GetWindowRect(hwnd, &s_savedRect);
            s_hasSavedRect = true;
        }

        if (IsZoomed(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        }

        // Switch to pure WS_POPUP to eliminate WS_CAPTION height constraints and sizing frames
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        style |= WS_POPUP;
        SetWindowLongPtr(hwnd, GWL_STYLE, style);

        // Extend frame into client area so DWM manages shadow & rounded corners on borderless window
        MARGINS margins = { 0, 0, 0, 1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        // Windows 11 DWM rounded corners
        enum DWM_WINDOW_CORNER_PREFERENCE {
            DWMWCP_DEFAULT    = 0,
            DWMWCP_DONOTROUND = 1,
            DWMWCP_ROUND      = 2,
            DWMWCP_ROUNDSMALL = 3
        };
        DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
        HRESULT hr = DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));

        // Suppress Windows system blue accent border color on active/click/drag state
        COLORREF darkBorderColor = RGB(42, 47, 56);
        DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &darkBorderColor, sizeof(darkBorderColor));
        COLORREF darkCaptionColor = RGB(23, 26, 31);
        DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &darkCaptionColor, sizeof(darkCaptionColor));

        if (FAILED(hr)) {
            // Fallback for Windows 10 or non-DWM composited environments:
            HRGN rgn = CreateRoundRectRgn(0, 0, targetWidth + 1, targetHeight + 1, 20, 20);
            SetWindowRgn(hwnd, rgn, TRUE);
        } else {
            // Native Windows 11 DWM anti-aliased rounding active; clear GDI region
            SetWindowRgn(hwnd, NULL, TRUE);
        }

        RECT cur;
        GetWindowRect(hwnd, &cur);
        SetWindowPos(hwnd, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, cur.left, cur.top, targetWidth, targetHeight, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    } else {
        // Remove region clipping
        SetWindowRgn(hwnd, NULL, TRUE);

        // Restore WS_OVERLAPPEDWINDOW
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style &= ~WS_POPUP;
        style |= WS_OVERLAPPEDWINDOW;
        SetWindowLongPtr(hwnd, GWL_STYLE, style);

        MARGINS margins = { 0, 0, 0, 1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        enum DWM_WINDOW_CORNER_PREFERENCE {
            DWMWCP_DEFAULT    = 0,
            DWMWCP_DONOTROUND = 1,
            DWMWCP_ROUND      = 2,
            DWMWCP_ROUNDSMALL = 3
        };
        DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));

        COLORREF darkBorderColor = RGB(42, 47, 56);
        DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &darkBorderColor, sizeof(darkBorderColor));
        COLORREF darkCaptionColor = RGB(23, 26, 31);
        DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &darkCaptionColor, sizeof(darkCaptionColor));

        int w = s_savedRect.right - s_savedRect.left;
        int h = s_savedRect.bottom - s_savedRect.top;
        if (w < 800) w = 1260;
        if (h < 500) h = 760;
        SetWindowPos(hwnd, HWND_NOTOPMOST, s_savedRect.left, s_savedRect.top, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
        s_hasSavedRect = false;
    }
#else
    (void)windowHandle;
    (void)isMini;
    (void)targetWidth;
    (void)targetHeight;
    (void)alwaysOnTop;
#endif
}

void resizeNativeWindow(void* windowHandle, int targetWidth, int targetHeight, bool alwaysOnTop) {
#if defined(_WIN32) || defined(_WIN64)
    if (!windowHandle) return;
    HWND hwnd = reinterpret_cast<HWND>(windowHandle);

    if (IsZoomed(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    MARGINS margins = { 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    enum DWM_WINDOW_CORNER_PREFERENCE {
        DWMWCP_DEFAULT    = 0,
        DWMWCP_DONOTROUND = 1,
        DWMWCP_ROUND      = 2,
        DWMWCP_ROUNDSMALL = 3
    };
    DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
    HRESULT hr = DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));

    COLORREF darkBorderColor = RGB(42, 47, 56);
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &darkBorderColor, sizeof(darkBorderColor));
    COLORREF darkCaptionColor = RGB(23, 26, 31);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &darkCaptionColor, sizeof(darkCaptionColor));

    if (FAILED(hr)) {
        HRGN rgn = CreateRoundRectRgn(0, 0, targetWidth + 1, targetHeight + 1, 20, 20);
        SetWindowRgn(hwnd, rgn, TRUE);
    } else {
        SetWindowRgn(hwnd, NULL, TRUE);
    }

    RECT cur;
    GetWindowRect(hwnd, &cur);
    SetWindowPos(hwnd, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, cur.left, cur.top, targetWidth, targetHeight, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
#else
    (void)windowHandle;
    (void)targetWidth;
    (void)targetHeight;
    (void)alwaysOnTop;
#endif
}

void setWindowAlwaysOnTop(void* windowHandle, bool alwaysOnTop) {
#if defined(_WIN32) || defined(_WIN64)
    if (!windowHandle) return;
    HWND hwnd = reinterpret_cast<HWND>(windowHandle);
    SetWindowPos(hwnd, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    (void)windowHandle;
    (void)alwaysOnTop;
#endif
}

void setWindowOpacity(void* windowHandle, float opacity) {
#if defined(_WIN32) || defined(_WIN64)
    if (!windowHandle) return;
    HWND hwnd = reinterpret_cast<HWND>(windowHandle);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (opacity >= 0.999f) {
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
        RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    } else {
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        BYTE alpha = static_cast<BYTE>(std::clamp(opacity, 0.2f, 1.0f) * 255.0f);
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
    }
#else
    (void)windowHandle;
    (void)opacity;
#endif
}

} // namespace Platform
