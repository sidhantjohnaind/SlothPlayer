#include "VolumeScanner.h"
#include "../third_party/miniaudio.h"
#include <cmath>
#include <algorithm>
#include <iostream>

std::atomic<bool> VolumeScanner::s_cancelled{false};
std::atomic<bool> VolumeScanner::s_scanning{false};
std::thread VolumeScanner::s_workerThread;

VolumeScanResult VolumeScanner::analyzeTrack(const Track& track) {
    VolumeScanResult result;
    result.trackId = track.id;
    result.filePath = track.filePath;
    result.success = false;

    if (track.filePath.empty() || track.isStream) {
        return result;
    }

    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 44100);
    if (ma_decoder_init_file(track.filePath.c_str(), &config, &decoder) != MA_SUCCESS) {
        return result;
    }

    constexpr size_t CHUNK_FRAMES = 4096;
    float buffer[CHUNK_FRAMES * 2];
    ma_uint64 framesRead = 0;
    double sumSquares = 0.0;
    uint64_t totalSamples = 0;
    float maxPeak = 0.0f;

    while (ma_decoder_read_pcm_frames(&decoder, buffer, CHUNK_FRAMES, &framesRead) == MA_SUCCESS && framesRead > 0) {
        if (s_cancelled.load()) {
            break;
        }

        size_t sampleCount = static_cast<size_t>(framesRead * 2);
        for (size_t i = 0; i < sampleCount; ++i) {
            float absVal = std::abs(buffer[i]);
            if (absVal > maxPeak) maxPeak = absVal;
            sumSquares += (static_cast<double>(absVal) * static_cast<double>(absVal));
        }
        totalSamples += sampleCount;
    }

    ma_decoder_uninit(&decoder);

    if (totalSamples > 0 && !s_cancelled.load()) {
        double meanSquare = sumSquares / static_cast<double>(totalSamples);
        float rms = static_cast<float>(std::sqrt(meanSquare));
        result.rms = rms;
        result.peak = maxPeak;

        // Target: -18 LUFS (approx. 89 dB SPL standard)
        // Loudness in dB FS roughly ~ 20 * log10(rms + 1e-7) + 3.01
        float dbFs = (rms > 0.000001f) ? static_cast<float>(20.0 * std::log10(rms) + 3.01) : -96.0f;
        float gain = -18.0f - dbFs;
        result.replayGainDb = std::clamp(gain, -15.0f, 15.0f);
        result.success = true;
    }

    return result;
}

void VolumeScanner::startBatchAnalysisAsync(
    const std::vector<Track>& tracks,
    ProgressCallback onProgress,
    CompletionCallback onComplete
) {
    if (s_scanning.load()) {
        return;
    }

    if (s_workerThread.joinable()) {
        s_workerThread.join();
    }

    s_cancelled.store(false);
    s_scanning.store(true);

    s_workerThread = std::thread([tracks, onProgress, onComplete]() {
        std::vector<VolumeScanResult> results;
        size_t total = tracks.size();

        for (size_t i = 0; i < total; ++i) {
            if (s_cancelled.load()) {
                break;
            }

            const auto& t = tracks[i];
            if (onProgress) {
                VolumeScanProgress p;
                p.processedCount = i;
                p.totalCount = total;
                p.progress = (total > 0) ? (static_cast<float>(i) / static_cast<float>(total)) : 0.0f;
                p.currentTrackTitle = t.getDisplayTitle();
                onProgress(p);
            }

            VolumeScanResult res = analyzeTrack(t);
            results.push_back(res);
        }

        if (onProgress) {
            VolumeScanProgress p;
            p.processedCount = total;
            p.totalCount = total;
            p.progress = 1.0f;
            p.currentTrackTitle = "Scan Complete";
            onProgress(p);
        }

        s_scanning.store(false);

        if (onComplete) {
            onComplete(results);
        }
    });
}

void VolumeScanner::cancel() {
    s_cancelled.store(true);
}

bool VolumeScanner::isScanning() {
    return s_scanning.load();
}
