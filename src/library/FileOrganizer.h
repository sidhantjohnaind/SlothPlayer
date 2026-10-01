#pragma once

#include "Track.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <filesystem>

struct OrganizePreviewItem {
    uint64_t trackId = 0;
    std::string originalPath;
    std::string proposedPath;
    bool isValid = true;
    std::string statusMessage;
};

class FileOrganizer {
public:
    // Expand template pattern: e.g. "<Artist>/<Album>/<Track#> - <Title>"
    static std::string expandPattern(const std::string& pattern, const Track& track, const std::string& baseDir);

    // Generate previews for a list of tracks without modifying disk
    static std::vector<OrganizePreviewItem> generatePreview(
        const std::vector<Track>& tracks,
        const std::string& pattern,
        const std::string& baseDir
    );

    // Multithreaded execution of file reorganization
    // Returns pair of (successCount, failCount)
    static void executeAsync(
        std::vector<OrganizePreviewItem> items,
        std::function<void(float progress, const std::string& currentFile)> progressCallback,
        std::function<void(size_t succeeded, size_t failed, const std::vector<std::pair<uint64_t, std::string>>& updatedPaths)> completionCallback
    );

    static std::string sanitizePathComponent(const std::string& input);
};
