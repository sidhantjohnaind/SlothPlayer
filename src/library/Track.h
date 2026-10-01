#pragma once

#include <string>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include "../third_party/json.hpp"

struct Track {
    uint64_t id = 0;
    std::string filePath;
    std::string fileName;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    int year = 0;
    int trackNumber = 0;
    int trackTotal = 0;
    int discNumber = 1;
    int totalDiscs = 1;
    std::string albumArtist;
    std::string composer;
    std::string comment;
    std::string isrc;
    double duration = 0.0; // in seconds
    int bitrate = 0;       // in kbps
    int sampleRate = 0;    // in Hz
    int channels = 2;
    int bitsPerSample = 16;
    uint64_t fileSizeBytes = 0;
    std::string codec;     // e.g. FLAC, MP3, WAV
    float replayGainTrackGain = 0.0f; // dB
    float replayGainTrackPeak = 1.0f; // peak multiplier
    bool isFavorite = false;
    bool isDisliked = false;
    bool isInbox = false; // Flag for unfiled tracks in Inbox holding area
    int playCount = 0;
    int rating = 0;        // 0 to 5
    uint64_t fileModifiedTime = 0; // seconds since epoch
    uint64_t dateAdded = 0;
    uint64_t lastPlayedTime = 0;

    std::string albumArtPath; // external file path if any
    bool hasEmbeddedArt = false;
    uint64_t embeddedArtOffset = 0;
    uint32_t embeddedArtSize = 0;

    bool isStream = false;
    std::string streamUrl;

    // CUE Sheet virtual sub-track fields
    bool isCueSubtrack = false;
    double cueStartSeconds = 0.0;
    double cueDurationSeconds = 0.0;

    // Raw Tag Inspector map (key -> value)
    std::map<std::string, std::string> rawTags;

    std::string getDisplayTitle() const {
        if (!title.empty()) return title;
        // Fallback: file name without extension
        size_t lastDot = fileName.find_last_of('.');
        if (lastDot != std::string::npos) {
            return fileName.substr(0, lastDot);
        }
        return fileName.empty() ? "Unknown Title" : fileName;
    }

    std::string getDisplayArtist() const {
        return artist.empty() ? "Unknown Artist" : artist;
    }

    std::string getDisplayAlbumArtist() const {
        if (!albumArtist.empty()) return albumArtist;
        return getDisplayArtist();
    }

    std::string getDisplayAlbum() const {
        return album.empty() ? "Unknown Album" : album;
    }

    std::string formatDuration() const {
        if (isStream) return "Live Stream";
        int totalSec = static_cast<int>(duration);
        int hours = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;
        std::ostringstream oss;
        if (hours > 0) {
            oss << hours << ":" << std::setfill('0') << std::setw(2) << mins << ":" << std::setfill('0') << std::setw(2) << secs;
        } else {
            oss << std::setfill('0') << std::setw(2) << mins << ":" << std::setfill('0') << std::setw(2) << secs;
        }
        return oss.str();
    }
};

enum class CaseConversion {
    TitleCase = 0,
    UpperCase = 1,
    LowerCase = 2,
    SentenceCase = 3
};

inline std::string convertStringToCase(const std::string& input, CaseConversion mode) {
    if (input.empty()) return "";
    std::string result = input;
    if (mode == CaseConversion::UpperCase) {
        for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    } else if (mode == CaseConversion::LowerCase) {
        for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (mode == CaseConversion::TitleCase) {
        bool newWord = true;
        for (char& c : result) {
            if (std::isspace(static_cast<unsigned char>(c)) || c == '-' || c == '(' || c == '[' || c == '.' || c == '/') {
                newWord = true;
            } else if (newWord) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                newWord = false;
            } else {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
    } else if (mode == CaseConversion::SentenceCase) {
        bool newSentence = true;
        for (char& c : result) {
            if (c == '.' || c == '!' || c == '?') {
                newSentence = true;
            } else if (std::isalnum(static_cast<unsigned char>(c))) {
                if (newSentence) {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    newSentence = false;
                } else {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
        }
    }
    return result;
}

inline void to_json(nlohmann::json& j, const Track& t) {
    j = nlohmann::json{
        {"id", t.id},
        {"filePath", t.filePath},
        {"fileName", t.fileName},
        {"title", t.title},
        {"artist", t.artist},
        {"albumArtist", t.albumArtist},
        {"album", t.album},
        {"genre", t.genre},
        {"year", t.year},
        {"trackNumber", t.trackNumber},
        {"trackTotal", t.trackTotal},
        {"discNumber", t.discNumber},
        {"totalDiscs", t.totalDiscs},
        {"composer", t.composer},
        {"comment", t.comment},
        {"isrc", t.isrc},
        {"duration", t.duration},
        {"bitrate", t.bitrate},
        {"sampleRate", t.sampleRate},
        {"channels", t.channels},
        {"bitsPerSample", t.bitsPerSample},
        {"fileSizeBytes", t.fileSizeBytes},
        {"codec", t.codec},
        {"replayGainTrackGain", t.replayGainTrackGain},
        {"replayGainTrackPeak", t.replayGainTrackPeak},
        {"isFavorite", t.isFavorite},
        {"isDisliked", t.isDisliked},
        {"isInbox", t.isInbox},
        {"playCount", t.playCount},
        {"rating", t.rating},
        {"fileModifiedTime", t.fileModifiedTime},
        {"dateAdded", t.dateAdded},
        {"lastPlayedTime", t.lastPlayedTime},
        {"albumArtPath", t.albumArtPath},
        {"hasEmbeddedArt", t.hasEmbeddedArt},
        {"embeddedArtOffset", t.embeddedArtOffset},
        {"embeddedArtSize", t.embeddedArtSize},
        {"isStream", t.isStream},
        {"streamUrl", t.streamUrl},
        {"isCueSubtrack", t.isCueSubtrack},
        {"cueStartSeconds", t.cueStartSeconds},
        {"cueDurationSeconds", t.cueDurationSeconds},
        {"rawTags", t.rawTags}
    };
}

inline void from_json(const nlohmann::json& j, Track& t) {
    if (j.contains("id")) j.at("id").get_to(t.id);
    if (j.contains("filePath")) j.at("filePath").get_to(t.filePath);
    if (j.contains("fileName")) j.at("fileName").get_to(t.fileName);
    if (j.contains("title")) j.at("title").get_to(t.title);
    if (j.contains("artist")) j.at("artist").get_to(t.artist);
    if (j.contains("albumArtist")) j.at("albumArtist").get_to(t.albumArtist);
    if (j.contains("album")) j.at("album").get_to(t.album);
    if (j.contains("genre")) j.at("genre").get_to(t.genre);
    if (j.contains("year")) j.at("year").get_to(t.year);
    if (j.contains("trackNumber")) j.at("trackNumber").get_to(t.trackNumber);
    if (j.contains("trackTotal")) j.at("trackTotal").get_to(t.trackTotal);
    if (j.contains("discNumber")) j.at("discNumber").get_to(t.discNumber);
    if (j.contains("totalDiscs")) j.at("totalDiscs").get_to(t.totalDiscs);
    if (j.contains("composer")) j.at("composer").get_to(t.composer);
    if (j.contains("comment")) j.at("comment").get_to(t.comment);
    if (j.contains("isrc")) j.at("isrc").get_to(t.isrc);
    if (j.contains("duration")) j.at("duration").get_to(t.duration);
    if (j.contains("bitrate")) j.at("bitrate").get_to(t.bitrate);
    if (j.contains("sampleRate")) j.at("sampleRate").get_to(t.sampleRate);
    if (j.contains("channels")) j.at("channels").get_to(t.channels);
    if (j.contains("bitsPerSample")) j.at("bitsPerSample").get_to(t.bitsPerSample);
    if (j.contains("fileSizeBytes")) j.at("fileSizeBytes").get_to(t.fileSizeBytes);
    if (j.contains("codec")) j.at("codec").get_to(t.codec);
    if (j.contains("replayGainTrackGain")) j.at("replayGainTrackGain").get_to(t.replayGainTrackGain);
    if (j.contains("replayGainTrackPeak")) j.at("replayGainTrackPeak").get_to(t.replayGainTrackPeak);
    if (j.contains("isFavorite")) j.at("isFavorite").get_to(t.isFavorite);
    if (j.contains("isDisliked")) j.at("isDisliked").get_to(t.isDisliked);
    if (j.contains("isInbox")) j.at("isInbox").get_to(t.isInbox);
    if (j.contains("playCount")) j.at("playCount").get_to(t.playCount);
    if (j.contains("rating")) j.at("rating").get_to(t.rating);
    if (j.contains("fileModifiedTime")) j.at("fileModifiedTime").get_to(t.fileModifiedTime);
    if (j.contains("dateAdded")) j.at("dateAdded").get_to(t.dateAdded);
    if (j.contains("lastPlayedTime")) j.at("lastPlayedTime").get_to(t.lastPlayedTime);
    if (j.contains("albumArtPath")) j.at("albumArtPath").get_to(t.albumArtPath);
    if (j.contains("hasEmbeddedArt")) j.at("hasEmbeddedArt").get_to(t.hasEmbeddedArt);
    if (j.contains("embeddedArtOffset")) j.at("embeddedArtOffset").get_to(t.embeddedArtOffset);
    if (j.contains("embeddedArtSize")) j.at("embeddedArtSize").get_to(t.embeddedArtSize);
    if (j.contains("isStream")) j.at("isStream").get_to(t.isStream);
    if (j.contains("streamUrl")) j.at("streamUrl").get_to(t.streamUrl);
    if (j.contains("isCueSubtrack")) j.at("isCueSubtrack").get_to(t.isCueSubtrack);
    if (j.contains("cueStartSeconds")) j.at("cueStartSeconds").get_to(t.cueStartSeconds);
    if (j.contains("cueDurationSeconds")) j.at("cueDurationSeconds").get_to(t.cueDurationSeconds);
    if (j.contains("rawTags") && j.at("rawTags").is_object()) {
        try {
            t.rawTags = j.at("rawTags").get<std::map<std::string, std::string>>();
        } catch (...) {}
    }
}
