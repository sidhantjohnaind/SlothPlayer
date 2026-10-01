#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>

enum class TargetFormat {
    WAV_16Bit = 0,
    WAV_32BitFloat = 1,
    WAV_24Bit = 2
};

struct ConvertJob {
    uint64_t trackId = 0;
    std::string sourcePath;
    std::string destPath;
    std::string trackTitle;
    TargetFormat targetFormat = TargetFormat::WAV_16Bit;
    uint32_t targetSampleRate = 44100; // 0 = keep source
};

struct ConvertProgress {
    size_t currentJobIndex = 0;
    size_t totalJobs = 0;
    float currentJobProgress = 0.0f; // 0.0 to 1.0
    float totalProgress = 0.0f;      // 0.0 to 1.0
    std::string currentTrackName;
    std::string statusMessage;
    bool isCompleted = false;
    bool hadError = false;
};

class AudioConverter {
public:
    // Convert a single file synchronously
    static bool convertFile(
        const std::string& srcPath,
        const std::string& dstPath,
        TargetFormat format = TargetFormat::WAV_16Bit,
        uint32_t targetSampleRate = 44100,
        std::function<void(float prog)> fileProgressCb = nullptr
    );

    // Multithreaded batch conversion with progress callback
    static void startBatchConvertAsync(
        std::vector<ConvertJob> jobs,
        std::function<void(const ConvertProgress& prog)> progressCb,
        std::function<void(size_t succeeded, size_t failed)> completionCb
    );

    static void cancelBatch();
    static bool isConverting();

private:
    static std::atomic<bool> s_isConverting;
    static std::atomic<bool> s_cancelRequested;
};
