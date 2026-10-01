#pragma once

#include <string>
#include <functional>
#include <cstdint>

struct SMTCCallbacks {
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onTogglePlayPause;
    std::function<void()> onNext;
    std::function<void()> onPrevious;
    std::function<void()> onStop;
};

class SMTCManager {
public:
    static SMTCManager& instance();

    bool initialize(void* hWnd, const SMTCCallbacks& callbacks);
    void shutdown();

    void updateTrack(const std::string& title, const std::string& artist, const std::string& album, const std::string& coverArtPath = "");
    void setPlaybackState(bool isPlaying, bool isPaused);
    void clear();

private:
    SMTCManager();
    ~SMTCManager();
    SMTCManager(const SMTCManager&) = delete;
    SMTCManager& operator=(const SMTCManager&) = delete;

    struct Impl;
    Impl* m_impl = nullptr;
};
