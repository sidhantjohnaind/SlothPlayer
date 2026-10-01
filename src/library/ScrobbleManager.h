#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include "Track.h"

struct ScrobbleRecord {
    std::string title;
    std::string artist;
    std::string album;
    uint64_t timestamp = 0;
    std::string timestampStr;
    bool submitted = true;
};

class ScrobbleManager {
public:
    ScrobbleManager();

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    void onTrackStarted(const Track& track);
    void onTrackProgress(double currentSec, double totalSec);
    void onTrackEnded();

    void setLastFmCredentials(const std::string& username, const std::string& sessionKey, const std::string& apiKey = "", const std::string& apiSecret = "");
    std::string getLastFmUsername() const;
    std::string getLastFmSessionKey() const;
    bool hasLastFmAuth() const;
    void retryUnsubmitted();

    std::vector<ScrobbleRecord> getHistory() const;
    void clearHistory();
    size_t getScrobbleCount() const;

    bool saveToFile(const std::string& path = "scrobbles.json");
    bool loadFromFile(const std::string& path = "scrobbles.json");

private:
    void doScrobbleCurrent();
    void sendLastFmNowPlaying(const Track& track);
    bool sendLastFmScrobble(const std::string& artist, const std::string& title, const std::string& album, uint64_t timestamp);

    mutable std::recursive_mutex m_mutex;
    bool m_enabled = true;
    std::vector<ScrobbleRecord> m_history;

    std::string m_lastFmUsername;
    std::string m_lastFmSessionKey;
    std::string m_lastFmApiKey;
    std::string m_lastFmApiSecret;

    // Current playing session state
    Track m_currentTrack;
    bool m_hasActiveTrack = false;
    bool m_alreadyScrobbled = false;
    double m_maxElapsed = 0.0;
};
