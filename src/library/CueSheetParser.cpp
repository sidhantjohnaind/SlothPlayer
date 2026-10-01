#include "CueSheetParser.h"
#include "TagReader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

static std::string trimString(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static std::string unquote(const std::string& str) {
    std::string s = trimString(str);
    if (s.length() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.length() - 2);
    }
    return s;
}

static double parseCueTimestamp(const std::string& timeStr) {
    // Format: mm:ss:ff (frames are 1/75 second)
    int mm = 0, ss = 0, ff = 0;
    char colon1 = 0, colon2 = 0;
    std::istringstream iss(timeStr);
    if (iss >> mm >> colon1 >> ss >> colon2 >> ff && colon1 == ':' && colon2 == ':') {
        return mm * 60.0 + ss + (ff / 75.0);
    }
    return 0.0;
}

std::vector<Track> CueSheetParser::parseCueFile(const std::string& cueFilePath) {
    std::ifstream file(cueFilePath, std::ios::binary);
    if (!file.is_open()) return {};

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    std::string cueDir = fs::path(cueFilePath).parent_path().string();
    return parseCueContent(content, cueDir, cueFilePath);
}

std::vector<Track> CueSheetParser::parseCueContent(
    const std::string& cueContent,
    const std::string& cueDirectory,
    const std::string& cueFilePath
) {
    std::vector<Track> tracks;
    std::istringstream stream(cueContent);
    std::string line;

    std::string globalPerformer;
    std::string globalTitle;
    std::string globalGenre;
    int globalYear = 0;
    int globalDisc = 1;
    std::string currentAudioFile;

    struct RawCueTrack {
        int number = 0;
        std::string title;
        std::string performer;
        std::string audioFile;
        double startSec = 0.0;
        bool hasIndex01 = false;
    };

    std::vector<RawCueTrack> rawTracks;
    RawCueTrack currentTrack;
    bool inTrack = false;

    while (std::getline(stream, line)) {
        std::string trimmed = trimString(line);
        if (trimmed.empty()) continue;

        std::string upper = trimmed;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        if (upper.rfind("REM GENRE ", 0) == 0) {
            globalGenre = unquote(trimmed.substr(10));
        } else if (upper.rfind("REM DATE ", 0) == 0 || upper.rfind("REM YEAR ", 0) == 0) {
            std::string yearStr = unquote(trimmed.substr(9));
            try { globalYear = std::stoi(yearStr); } catch (...) {}
        } else if (upper.rfind("REM DISCNUMBER ", 0) == 0) {
            std::string discStr = unquote(trimmed.substr(15));
            try { globalDisc = std::stoi(discStr); } catch (...) {}
        } else if (upper.rfind("PERFORMER ", 0) == 0) {
            std::string val = unquote(trimmed.substr(10));
            if (!inTrack) {
                globalPerformer = val;
            } else {
                currentTrack.performer = val;
            }
        } else if (upper.rfind("TITLE ", 0) == 0) {
            std::string val = unquote(trimmed.substr(6));
            if (!inTrack) {
                globalTitle = val;
            } else {
                currentTrack.title = val;
            }
        } else if (upper.rfind("FILE ", 0) == 0) {
            // e.g. FILE "album.flac" WAVE
            size_t firstQuote = trimmed.find('"');
            size_t lastQuote = trimmed.rfind('"');
            if (firstQuote != std::string::npos && lastQuote != std::string::npos && lastQuote > firstQuote) {
                currentAudioFile = trimmed.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            } else {
                std::istringstream iss(trimmed.substr(5));
                iss >> currentAudioFile;
            }
        } else if (upper.rfind("TRACK ", 0) == 0) {
            if (inTrack && currentTrack.hasIndex01) {
                rawTracks.push_back(currentTrack);
            }
            inTrack = true;
            currentTrack = RawCueTrack();
            currentTrack.audioFile = currentAudioFile;
            currentTrack.performer = globalPerformer;
            // Parse track number
            std::istringstream iss(trimmed.substr(6));
            iss >> currentTrack.number;
        } else if (inTrack && upper.rfind("INDEX 01 ", 0) == 0) {
            std::string timeStr = trimString(trimmed.substr(9));
            currentTrack.startSec = parseCueTimestamp(timeStr);
            currentTrack.hasIndex01 = true;
        }
    }

    if (inTrack && currentTrack.hasIndex01) {
        rawTracks.push_back(currentTrack);
    }

    if (rawTracks.empty()) return tracks;

    // Cache metadata for audio files referenced in the CUE sheet
    std::map<std::string, Track> audioFileMeta;

    for (size_t i = 0; i < rawTracks.size(); ++i) {
        const auto& r = rawTracks[i];
        Track t;

        // Resolve audio file path
        fs::path resolvedPath = r.audioFile;
        if (!resolvedPath.is_absolute() && !cueDirectory.empty()) {
            resolvedPath = fs::path(cueDirectory) / resolvedPath;
        }
        std::string fullAudioPath = resolvedPath.string();

        if (audioFileMeta.find(fullAudioPath) == audioFileMeta.end()) {
            Track baseTrack;
            if (fs::exists(fullAudioPath)) {
                TagReader::readMetadata(fullAudioPath, baseTrack);
            }
            audioFileMeta[fullAudioPath] = baseTrack;
        }

        const Track& base = audioFileMeta[fullAudioPath];

        // Inherit stream/codec properties from base audio track
        t = base;
        t.id = 0;
        t.filePath = fullAudioPath;
        t.fileName = fs::path(fullAudioPath).filename().string();

        t.title = r.title.empty() ? ("Track " + std::to_string(r.number)) : r.title;
        t.artist = r.performer.empty() ? globalPerformer : r.performer;
        t.album = globalTitle.empty() ? base.album : globalTitle;
        t.albumArtist = globalPerformer.empty() ? t.artist : globalPerformer;
        if (t.genre.empty()) t.genre = globalGenre;
        if (t.year == 0) t.year = globalYear;
        t.trackNumber = r.number;
        t.discNumber = globalDisc;

        t.isCueSubtrack = true;
        t.cueStartSeconds = r.startSec;

        // Determine duration based on next track's start or base audio file duration
        if (i + 1 < rawTracks.size() && rawTracks[i + 1].audioFile == r.audioFile) {
            t.cueDurationSeconds = std::max(0.0, rawTracks[i + 1].startSec - r.startSec);
        } else if (base.duration > r.startSec) {
            t.cueDurationSeconds = std::max(0.0, base.duration - r.startSec);
        } else {
            t.cueDurationSeconds = 0.0;
        }
        t.duration = t.cueDurationSeconds;

        tracks.push_back(t);
    }

    return tracks;
}
