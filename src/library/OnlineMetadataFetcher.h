#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

struct ArtworkCandidate {
    std::string albumName;
    std::string artistName;
    std::string releaseDate;
    std::string highResUrl;
    std::string previewUrl;
};

class OnlineMetadataFetcher {
public:
    // Search online for album art candidates (iTunes Search API)
    static std::vector<ArtworkCandidate> searchAlbumArtwork(const std::string& artist, const std::string& album);

    // Asynchronous search
    static void searchAlbumArtworkAsync(
        const std::string& artist,
        const std::string& album,
        std::function<void(const std::vector<ArtworkCandidate>& candidates)> callback
    );

    // Download artwork image bytes from URL
    static bool downloadArtwork(const std::string& url, std::vector<uint8_t>& outBytes);

    // Download and save artwork to file path (e.g. "cover.jpg")
    static bool downloadAndSaveArtwork(const std::string& url, const std::string& destFilePath);
};
