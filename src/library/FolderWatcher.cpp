#include "FolderWatcher.h"
#include <iostream>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

FolderWatcher::FolderWatcher() {
#ifdef _WIN32
    m_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
#endif
}

FolderWatcher::~FolderWatcher() {
    stop();
#ifdef _WIN32
    if (m_stopEvent) {
        CloseHandle(static_cast<HANDLE>(m_stopEvent));
        m_stopEvent = nullptr;
    }
#endif
}

void FolderWatcher::start(const std::vector<std::string>& directories, ChangeCallback callback) {
    stop();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_directories = directories;
        m_callback = std::move(callback);
        m_stopRequested = false;
        m_running = true;
#ifdef _WIN32
        if (m_stopEvent) {
            ResetEvent(static_cast<HANDLE>(m_stopEvent));
        }
#endif
    }

    if (!m_directories.empty()) {
        m_thread = std::thread(&FolderWatcher::watchWorker, this);
    }
}

void FolderWatcher::stop() {
    if (!m_running.load()) return;

    m_stopRequested = true;
#ifdef _WIN32
    if (m_stopEvent) {
        SetEvent(static_cast<HANDLE>(m_stopEvent));
    }
#endif

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_running = false;
}

void FolderWatcher::updateDirectories(const std::vector<std::string>& directories) {
    ChangeCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_callback;
    }
    start(directories, cb);
}

void FolderWatcher::watchWorker() {
#ifdef _WIN32
    std::vector<std::string> dirs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        dirs = m_directories;
    }

    if (dirs.empty()) {
        m_running = false;
        return;
    }

    std::vector<HANDLE> changeHandles;
    std::vector<std::string> validDirs;

    // First handle is stop event
    changeHandles.push_back(static_cast<HANDLE>(m_stopEvent));

    for (const auto& d : dirs) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, d.c_str(), -1, NULL, 0);
        if (wlen <= 0) continue;
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, d.c_str(), -1, &wpath[0], wlen);

        HANDLE h = FindFirstChangeNotificationW(
            wpath.c_str(),
            TRUE, // watch subtree
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE
        );

        if (h != INVALID_HANDLE_VALUE && h != NULL) {
            changeHandles.push_back(h);
            validDirs.push_back(d);
            if (changeHandles.size() >= MAXIMUM_WAIT_OBJECTS) break;
        }
    }

    if (changeHandles.size() <= 1) {
        m_running = false;
        return;
    }

    while (!m_stopRequested.load()) {
        DWORD waitStatus = WaitForMultipleObjects(
            static_cast<DWORD>(changeHandles.size()),
            changeHandles.data(),
            FALSE, // wait for ANY
            1000   // 1s timeout for periodic stop check
        );

        if (waitStatus == WAIT_OBJECT_0) {
            // Stop event signaled
            break;
        }

        if (waitStatus > WAIT_OBJECT_0 && waitStatus < (WAIT_OBJECT_0 + changeHandles.size())) {
            size_t idx = waitStatus - WAIT_OBJECT_0;
            size_t dirIdx = idx - 1;
            std::string triggerDir = (dirIdx < validDirs.size()) ? validDirs[dirIdx] : "";

            // Reset notification
            FindNextChangeNotification(changeHandles[idx]);

            // Debounce: Wait 750ms so file copy/write finishes before triggering scan
            std::this_thread::sleep_for(std::chrono::milliseconds(750));

            if (!m_stopRequested.load()) {
                ChangeCallback cb;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    cb = m_callback;
                }
                if (cb) {
                    cb(triggerDir);
                }
            }
        }
    }

    // Cleanup notification handles (skip index 0 which is m_stopEvent)
    for (size_t i = 1; i < changeHandles.size(); ++i) {
        if (changeHandles[i] != INVALID_HANDLE_VALUE && changeHandles[i] != NULL) {
            FindCloseChangeNotification(changeHandles[i]);
        }
    }

    m_running = false;
#endif
}
