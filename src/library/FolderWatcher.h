#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

class FolderWatcher {
public:
    using ChangeCallback = std::function<void(const std::string& changedFolder)>;

    FolderWatcher();
    ~FolderWatcher();

    void start(const std::vector<std::string>& directories, ChangeCallback callback);
    void stop();
    void updateDirectories(const std::vector<std::string>& directories);

    bool isRunning() const { return m_running.load(); }

private:
    void watchWorker();

    std::vector<std::string> m_directories;
    ChangeCallback m_callback;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    mutable std::mutex m_mutex;
    void* m_stopEvent = nullptr;
};
