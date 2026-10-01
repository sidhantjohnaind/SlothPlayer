#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include "Track.h"

struct LyricLine {
    double timeSeconds = 0.0;
    std::string text;
};

struct LyricsData {
    bool isSynced = false;
    std::vector<LyricLine> lines; // For synced LRC
    std::string plainText;        // For unsynced text
    std::string sourceInfo;       // e.g. "Embedded Tag", "External .lrc"
    bool hasLyrics = false;
};

class LyricsManager {
public:
    LyricsManager() = default;
    ~LyricsManager() = default;

    LyricsData getLyricsForTrack(const Track& track);
    void setCustomLyricsFile(uint64_t trackId, const std::string& lrcFilePath);
    static bool parseLRCContent(const std::string& content, LyricsData& outData);
    bool fetchOnlineLyrics(const Track& track, LyricsData& outData);
    void fetchOnlineLyricsAsync(const Track& track, std::function<void(bool success)> onDone = nullptr);

    // Find the currently active line index for a given playback timestamp
    static int findActiveLineIndex(const std::vector<LyricLine>& lines, double currentPosSeconds);

private:
    std::map<uint64_t, LyricsData> m_cache;
    std::map<uint64_t, std::string> m_customFiles;
    std::mutex m_mutex;

    LyricsData loadLyrics(const Track& track);
};
