#include "FileOrganizer.h"
#include <thread>
#include <regex>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

std::string FileOrganizer::sanitizePathComponent(const std::string& input) {
    if (input.empty()) return "Unknown";
    std::string clean = input;
    // Replace invalid Win32 path characters: \ / : * ? " < > |
    const std::string invalidChars = "\\/:*?\"<>|";
    for (char& c : clean) {
        if (invalidChars.find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) {
            c = '_';
        }
    }
    // Trim trailing spaces or dots
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) {
        clean.pop_back();
    }
    return clean.empty() ? "Unknown" : clean;
}

std::string FileOrganizer::expandPattern(const std::string& pattern, const Track& track, const std::string& baseDir) {
    std::string pat = pattern.empty() ? "<Artist>/<Album>/<Track#> - <Title>" : pattern;

    std::string artist = sanitizePathComponent(track.getDisplayArtist());
    std::string album = sanitizePathComponent(track.getDisplayAlbum());
    std::string title = sanitizePathComponent(track.getDisplayTitle());
    std::string genre = sanitizePathComponent(track.genre.empty() ? "Other" : track.genre);
    std::string year = (track.year > 0) ? std::to_string(track.year) : "Unknown Year";

    std::ostringstream numOss;
    if (track.trackNumber > 0) {
        numOss << std::setfill('0') << std::setw(2) << track.trackNumber;
    } else {
        numOss << "01";
    }
    std::string trackNum = numOss.str();

    auto replaceToken = [](std::string& target, const std::string& token, const std::string& value) {
        size_t pos = 0;
        while ((pos = target.find(token, pos)) != std::string::npos) {
            target.replace(pos, token.length(), value);
            pos += value.length();
        }
    };

    replaceToken(pat, "<Artist>", artist);
    replaceToken(pat, "<Album>", album);
    replaceToken(pat, "<Title>", title);
    replaceToken(pat, "<Track#>", trackNum);
    replaceToken(pat, "<Genre>", genre);
    replaceToken(pat, "<Year>", year);

    // Extract original extension
    std::string ext = "";
    try {
        fs::path orig(track.filePath);
        ext = orig.extension().string();
    } catch (...) {
        ext = ".mp3";
    }

    // Standardize directory slashes
    for (char& c : pat) {
        if (c == '\\') c = '/';
    }

    fs::path base = baseDir.empty() ? fs::path(track.filePath).parent_path() : fs::path(baseDir);
    fs::path resultPath = base / (pat + ext);
    return resultPath.lexically_normal().string();
}

std::vector<OrganizePreviewItem> FileOrganizer::generatePreview(
    const std::vector<Track>& tracks,
    const std::string& pattern,
    const std::string& baseDir
) {
    std::vector<OrganizePreviewItem> items;
    items.reserve(tracks.size());

    for (const auto& tr : tracks) {
        OrganizePreviewItem item;
        item.trackId = tr.id;
        item.originalPath = tr.filePath;

        if (tr.isStream) {
            item.isValid = false;
            item.statusMessage = "Cannot move internet radio stream";
            items.push_back(item);
            continue;
        }

        if (!fs::exists(tr.filePath)) {
            item.isValid = false;
            item.statusMessage = "Source file not found on disk";
            items.push_back(item);
            continue;
        }

        item.proposedPath = expandPattern(pattern, tr, baseDir);
        if (item.proposedPath == item.originalPath) {
            item.isValid = true;
            item.statusMessage = "Already matches pattern (No move needed)";
        } else if (fs::exists(item.proposedPath)) {
            item.isValid = false;
            item.statusMessage = "Destination path already exists";
        } else {
            item.isValid = true;
            item.statusMessage = "Ready to move";
        }

        items.push_back(item);
    }
    return items;
}

void FileOrganizer::executeAsync(
    std::vector<OrganizePreviewItem> items,
    std::function<void(float progress, const std::string& currentFile)> progressCallback,
    std::function<void(size_t succeeded, size_t failed, const std::vector<std::pair<uint64_t, std::string>>& updatedPaths)> completionCallback
) {
    std::thread([items = std::move(items), progressCallback, completionCallback]() mutable {
        size_t succeeded = 0;
        size_t failed = 0;
        std::vector<std::pair<uint64_t, std::string>> updatedPaths;
        size_t total = items.size();

        for (size_t i = 0; i < total; ++i) {
            auto& it = items[i];
            float prog = (total > 0) ? (static_cast<float>(i) / static_cast<float>(total)) : 1.0f;
            if (progressCallback) {
                progressCallback(prog, it.proposedPath);
            }

            if (!it.isValid || it.originalPath == it.proposedPath) {
                if (it.isValid) succeeded++;
                continue;
            }

            try {
                fs::path newPath(it.proposedPath);
                fs::create_directories(newPath.parent_path());
                fs::rename(it.originalPath, it.proposedPath);
                updatedPaths.push_back({it.trackId, it.proposedPath});
                succeeded++;
            } catch (...) {
                failed++;
            }
        }

        if (progressCallback) progressCallback(1.0f, "Completed");
        if (completionCallback) completionCallback(succeeded, failed, updatedPaths);
    }).detach();
}
