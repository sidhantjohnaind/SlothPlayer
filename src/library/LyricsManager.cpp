#include "LyricsManager.h"
#include "../network/HttpClient.h"
#include "../third_party/json.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

bool LyricsManager::parseLRCContent(const std::string& content, LyricsData& outData) {
    std::istringstream stream(content);
    std::string line;

    // Matches [mm:ss.xx] or [mm:ss.xxx] or [mm:ss]
    std::regex timeRegex(R"(\[(\d{1,2}):(\d{2})(?:\.(\d{1,3}))?\])");
    
    std::vector<LyricLine> parsedLines;
    std::vector<std::string> rawLines;

    while (std::getline(stream, line)) {
        // Strip trailing \r if any
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        rawLines.push_back(line);

        // Find all timestamp tags in this line
        std::vector<double> timestamps;
        auto words_begin = std::sregex_iterator(line.begin(), line.end(), timeRegex);
        auto words_end = std::sregex_iterator();

        size_t lastTagEnd = 0;
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            try {
                int mins = std::stoi(match[1].str());
                int secs = std::stoi(match[2].str());
                double frac = 0.0;
                if (match[3].matched) {
                    std::string fracStr = match[3].str();
                    if (fracStr.length() == 1) frac = std::stod(fracStr) / 10.0;
                    else if (fracStr.length() == 2) frac = std::stod(fracStr) / 100.0;
                    else frac = std::stod(fracStr) / 1000.0;
                }
                if (mins >= 0 && secs >= 0 && secs < 60 && mins < 10000) {
                    double totalSec = mins * 60.0 + secs + frac;
                    timestamps.push_back(totalSec);
                    lastTagEnd = match.position() + match.length();
                }
            } catch (...) {
                // Ignore malformed timestamp tag gracefully
            }
        }

        if (!timestamps.empty()) {
            std::string lyricText = line.substr(lastTagEnd);
            // Trim leading whitespace
            size_t start = lyricText.find_first_not_of(" \t");
            if (start != std::string::npos) lyricText = lyricText.substr(start);
            else lyricText.clear();

            for (double t : timestamps) {
                parsedLines.push_back({ t, lyricText });
            }
        }
    }

    if (!parsedLines.empty()) {
        std::sort(parsedLines.begin(), parsedLines.end(), [](const LyricLine& a, const LyricLine& b) {
            return a.timeSeconds < b.timeSeconds;
        });
        outData.isSynced = true;
        outData.lines = std::move(parsedLines);
        outData.hasLyrics = true;
        return true;
    }

    // Fallback: unsynced plain text lyrics
    if (!rawLines.empty()) {
        std::string fullText;
        for (const auto& l : rawLines) {
            // Ignore metadata tags like [ti:Title]
            if (l.rfind("[ti:", 0) == 0 || l.rfind("[ar:", 0) == 0 || l.rfind("[al:", 0) == 0 || l.rfind("[by:", 0) == 0) continue;
            fullText += l + "\n";
        }
        if (!fullText.empty()) {
            outData.isSynced = false;
            outData.plainText = fullText;
            outData.hasLyrics = true;
            return true;
        }
    }

    outData.hasLyrics = false;
    return false;
}

int LyricsManager::findActiveLineIndex(const std::vector<LyricLine>& lines, double currentPosSeconds) {
    if (lines.empty()) return -1;
    if (currentPosSeconds < lines[0].timeSeconds) return 0;

    int activeIdx = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].timeSeconds <= currentPosSeconds) {
            activeIdx = static_cast<int>(i);
        } else {
            break;
        }
    }
    return activeIdx;
}

LyricsData LyricsManager::getLyricsForTrack(const Track& track) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cache.find(track.id);
    if (it != m_cache.end()) {
        return it->second;
    }

    LyricsData data = loadLyrics(track);
    m_cache[track.id] = data;
    return data;
}

void LyricsManager::setCustomLyricsFile(uint64_t trackId, const std::string& lrcFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_customFiles[trackId] = lrcFilePath;
    m_cache.erase(trackId);
}

LyricsData LyricsManager::loadLyrics(const Track& track) {
    LyricsData data;

    // 1. Check custom file override
    auto itCustom = m_customFiles.find(track.id);
    if (itCustom != m_customFiles.end() && fs::exists(itCustom->second)) {
        std::ifstream file(itCustom->second);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (parseLRCContent(content, data)) {
                data.sourceInfo = fs::path(itCustom->second).filename().string();
                return data;
            }
        }
    }

    // 2. Check for .lrc and .txt next to track file
    if (!track.filePath.empty()) {
        try {
            fs::path p(track.filePath);
            fs::path lrcPath = p;
            lrcPath.replace_extension(".lrc");
            if (fs::exists(lrcPath)) {
                std::ifstream file(lrcPath);
                if (file.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    if (parseLRCContent(content, data)) {
                        data.sourceInfo = lrcPath.filename().string();
                        return data;
                    }
                }
            }

            fs::path txtPath = p;
            txtPath.replace_extension(".txt");
            if (fs::exists(txtPath)) {
                std::ifstream file(txtPath);
                if (file.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    if (parseLRCContent(content, data)) {
                        data.sourceInfo = txtPath.filename().string();
                        return data;
                    }
                }
            }

            // Also check directory for "<Artist> - <Title>.lrc"
            fs::path parent = p.parent_path();
            std::string artistTitleLrc = track.getDisplayArtist() + " - " + track.getDisplayTitle() + ".lrc";
            fs::path customArtistTitle = parent / artistTitleLrc;
            if (fs::exists(customArtistTitle)) {
                std::ifstream file(customArtistTitle);
                if (file.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    if (parseLRCContent(content, data)) {
                        data.sourceInfo = customArtistTitle.filename().string();
                        return data;
                    }
                }
            }
        } catch (...) {}
    }

    // 3. Fallback: Online Synced Lyrics Search via Lrclib
    if (fetchOnlineLyrics(track, data)) {
        return data;
    }

    data.hasLyrics = false;
    return data;
}

bool LyricsManager::fetchOnlineLyrics(const Track& track, LyricsData& outData) {
    if (track.title.empty()) return false;

    std::string artist = HttpClient::urlEncode(track.getDisplayArtist());
    std::string title = HttpClient::urlEncode(track.getDisplayTitle());
    std::string album = HttpClient::urlEncode(track.getDisplayAlbum());
    std::string durationStr = std::to_string(static_cast<int>(track.duration));

    std::string url = "https://lrclib.net/api/get?artist_name=" + artist +
                      "&track_name=" + title +
                      "&album_name=" + album +
                      "&duration=" + durationStr;

    HttpResponse resp = HttpClient::get(url);
    if (!resp.success || resp.body.empty()) {
        url = "https://lrclib.net/api/get?artist_name=" + artist + "&track_name=" + title;
        resp = HttpClient::get(url);
        if (!resp.success || resp.body.empty()) {
            return false;
        }
    }

    try {
        auto j = nlohmann::json::parse(resp.body);
        std::string syncedLyrics = j.value("syncedLyrics", "");
        std::string plainLyrics = j.value("plainLyrics", "");

        if (!syncedLyrics.empty() && parseLRCContent(syncedLyrics, outData)) {
            outData.sourceInfo = "Lrclib (Online Synced)";
            try {
                fs::create_directories("lyrics");
                std::string lrcFileName = "lyrics/" + track.getDisplayArtist() + " - " + track.getDisplayTitle() + ".lrc";
                std::ofstream out(lrcFileName);
                if (out.is_open()) {
                    out << syncedLyrics;
                }
            } catch (...) {}
            return true;
        } else if (!plainLyrics.empty()) {
            outData.isSynced = false;
            outData.plainText = plainLyrics;
            outData.hasLyrics = true;
            outData.sourceInfo = "Lrclib (Online Plain)";
            return true;
        }
    } catch (...) {}

    return false;
}

void LyricsManager::fetchOnlineLyricsAsync(const Track& track, std::function<void(bool success)> onDone) {
    std::thread([this, track, onDone]() {
        LyricsData data;
        bool ok = fetchOnlineLyrics(track, data);
        if (ok) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cache[track.id] = data;
        }
        if (onDone) {
            onDone(ok);
        }
    }).detach();
}

