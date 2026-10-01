#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdint>
#include "Track.h"

struct VolumeScanResult {
    uint64_t trackId = 0;
    std::string filePath;
    float peak = 0.0f;
    float rms = 0.0f;
    float replayGainDb = 0.0f;
    bool success = false;
};

struct VolumeScanProgress {
    size_t processedCount = 0;
    size_t totalCount = 0;
    float progress = 0.0f; // 0.0 to 1.0
    std::string currentTrackTitle;
};

class VolumeScanner {
public:
    using ProgressCallback = std::function<void(const VolumeScanProgress&)>;
    using CompletionCallback = std::function<void(const std::vector<VolumeScanResult>&)>;

    static VolumeScanResult analyzeTrack(const Track& track);

    static void startBatchAnalysisAsync(
        const std::vector<Track>& tracks,
        ProgressCallback onProgress,
        CompletionCallback onComplete
    );

    static void cancel();
    static bool isScanning();

private:
    static std::atomic<bool> s_cancelled;
    static std::atomic<bool> s_scanning;
    static std::thread s_workerThread;
};
