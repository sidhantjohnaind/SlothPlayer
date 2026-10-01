#pragma once

#include <string>

class MainWindow;

class TaskbarManager {
public:
    static TaskbarManager& instance();

    bool initialize(void* hWnd, MainWindow* pMainWindow);
    void shutdown();

    void update(void* hWnd, bool isPlaying, bool isPaused, double curTime, double totalDur,
                const std::string& title, const std::string& artist);
    void onTaskbarButtonCreated(void* hWnd);
    void handleThumbbarCommand(void* hWnd, unsigned int buttonId);
    void onSendIconicThumbnail(void* hWnd, int maxW, int maxH);

    static unsigned int getTaskbarButtonCreatedMsg();

private:
    TaskbarManager();
    ~TaskbarManager();
    TaskbarManager(const TaskbarManager&) = delete;
    TaskbarManager& operator=(const TaskbarManager&) = delete;

    struct Impl;
    Impl* m_impl = nullptr;
};
