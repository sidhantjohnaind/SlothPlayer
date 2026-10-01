#include "AudioConverter.h"
#include "../third_party/miniaudio.h"
#include <thread>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

std::atomic<bool> AudioConverter::s_isConverting{false};
std::atomic<bool> AudioConverter::s_cancelRequested{false};

bool AudioConverter::isConverting() {
    return s_isConverting.load();
}

void AudioConverter::cancelBatch() {
    s_cancelRequested.store(true);
}

bool AudioConverter::convertFile(
    const std::string& srcPath,
    const std::string& dstPath,
    TargetFormat format,
    uint32_t targetSampleRate,
    std::function<void(float prog)> fileProgressCb
) {
    if (!fs::exists(srcPath)) return false;

    ma_format outFormat = (format == TargetFormat::WAV_32BitFloat) ? ma_format_f32 : ma_format_s16;
    uint32_t sRate = (targetSampleRate > 0) ? targetSampleRate : 44100;

    ma_decoder_config decConfig = ma_decoder_config_init(outFormat, 2, sRate);
    ma_decoder decoder;
    if (ma_decoder_init_file(srcPath.c_str(), &decConfig, &decoder) != MA_SUCCESS) {
        return false;
    }

    try {
        fs::create_directories(fs::path(dstPath).parent_path());
    } catch (...) {}

    ma_encoder_config encConfig = ma_encoder_config_init(
        ma_encoding_format_wav,
        outFormat,
        decoder.outputChannels,
        decoder.outputSampleRate
    );
    ma_encoder encoder;
    if (ma_encoder_init_file(dstPath.c_str(), &encConfig, &encoder) != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    constexpr size_t CHUNK = 4096;
    size_t bytesPerFrame = decoder.outputChannels * ma_get_bytes_per_sample(outFormat);
    std::vector<uint8_t> buffer(CHUNK * bytesPerFrame);

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    ma_uint64 processedFrames = 0;

    bool success = true;
    while (!s_cancelRequested.load()) {
        ma_uint64 framesRead = 0;
        ma_result r = ma_decoder_read_pcm_frames(&decoder, buffer.data(), CHUNK, &framesRead);
        if (r != MA_SUCCESS || framesRead == 0) break;

        ma_uint64 framesWritten = 0;
        ma_result wr = ma_encoder_write_pcm_frames(&encoder, buffer.data(), framesRead, &framesWritten);
        if (wr != MA_SUCCESS) {
            success = false;
            break;
        }

        processedFrames += framesRead;
        if (fileProgressCb && totalFrames > 0) {
            float p = std::clamp(static_cast<float>(processedFrames) / static_cast<float>(totalFrames), 0.0f, 1.0f);
            fileProgressCb(p);
        }
    }

    ma_encoder_uninit(&encoder);
    ma_decoder_uninit(&decoder);

    if (s_cancelRequested.load()) {
        try { fs::remove(dstPath); } catch (...) {}
        return false;
    }

    return success;
}

void AudioConverter::startBatchConvertAsync(
    std::vector<ConvertJob> jobs,
    std::function<void(const ConvertProgress& prog)> progressCb,
    std::function<void(size_t succeeded, size_t failed)> completionCb
) {
    if (s_isConverting.load()) return;

    s_isConverting.store(true);
    s_cancelRequested.store(false);

    std::thread([jobs = std::move(jobs), progressCb, completionCb]() {
        size_t succeeded = 0;
        size_t failed = 0;
        size_t total = jobs.size();

        for (size_t i = 0; i < total; ++i) {
            if (s_cancelRequested.load()) break;

            const auto& job = jobs[i];
            ConvertProgress prog;
            prog.currentJobIndex = i;
            prog.totalJobs = total;
            prog.currentTrackName = job.trackTitle.empty() ? fs::path(job.sourcePath).filename().string() : job.trackTitle;
            prog.currentJobProgress = 0.0f;
            prog.totalProgress = (total > 0) ? (static_cast<float>(i) / static_cast<float>(total)) : 0.0f;
            prog.statusMessage = "Converting: " + prog.currentTrackName;
            if (progressCb) progressCb(prog);

            bool ok = convertFile(
                job.sourcePath,
                job.destPath,
                job.targetFormat,
                job.targetSampleRate,
                [&](float curProg) {
                    if (progressCb) {
                        ConvertProgress subProg = prog;
                        subProg.currentJobProgress = curProg;
                        subProg.totalProgress = (static_cast<float>(i) + curProg) / static_cast<float>(total);
                        progressCb(subProg);
                    }
                }
            );

            if (ok) {
                succeeded++;
            } else {
                failed++;
            }
        }

        s_isConverting.store(false);
        bool cancelled = s_cancelRequested.load();

        if (progressCb) {
            ConvertProgress finalProg;
            finalProg.currentJobIndex = total;
            finalProg.totalJobs = total;
            finalProg.currentJobProgress = 1.0f;
            finalProg.totalProgress = 1.0f;
            finalProg.isCompleted = true;
            finalProg.statusMessage = cancelled ? "Conversion cancelled by user." : "All conversions completed successfully.";
            progressCb(finalProg);
        }

        if (completionCb) completionCb(succeeded, failed);
    }).detach();
}
