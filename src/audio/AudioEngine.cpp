#include "AudioEngine.h"
#include "../platform/SettingsManager.h"
#include "../third_party/miniaudio.h"
#include "../third_party/json.hpp"
#include <fstream>
#include <cmath>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(pDevice->pUserData);
    if (engine) {
        engine->onAudioFrames(reinterpret_cast<float*>(pOutput), frameCount);
    }
}

static ma_result initDecoderFile(const std::string& filePath, const ma_decoder_config* pConfig, ma_decoder* pDecoder) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        std::wstring wpath(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, &wpath[0], wlen);
        ma_result res = ma_decoder_init_file_w(wpath.c_str(), pConfig, pDecoder);
        if (res == MA_SUCCESS) return MA_SUCCESS;
    }
#endif
    return ma_decoder_init_file(filePath.c_str(), pConfig, pDecoder);
}

AudioEngine::AudioEngine() {
    m_device = new ma_device();
    m_decoder = new ma_decoder();
    m_smoothedBars.fill(0.0f);
}

AudioEngine::~AudioEngine() {
    shutdown();
    delete reinterpret_cast<ma_device*>(m_device);
    delete reinterpret_cast<ma_decoder*>(m_decoder);
}

bool AudioEngine::init() {
    loadAudioConfig();
    bool ok = reinitAudioDevice(m_driverType, m_selectedDeviceIndex, m_bufferLatencyMs, m_selectedDeviceName, false);
    if (ok && m_releaseDriverWhenPaused.load()) {
        releaseDevice();
    }
    return ok;
}

void AudioEngine::shutdown() {
    stop();
    releaseDevice();
    {
        std::lock_guard<std::mutex> lock(m_decoderMutex);
        if (m_decoderInitialized) {
            ma_decoder_uninit(reinterpret_cast<ma_decoder*>(m_decoder));
            m_decoderInitialized = false;
        }
    }
}

std::vector<AudioDeviceInfo> AudioEngine::getAudioDevices(AudioDriverType driver) {
    std::vector<AudioDeviceInfo> devices;
    ma_backend backend = ma_backend_wasapi;
    if (driver == AudioDriverType::DirectSound) {
        backend = ma_backend_dsound;
    }

    ma_backend backends[] = { backend };
    ma_context tempContext;
    ma_result res = ma_context_init(backends, 1, nullptr, &tempContext);
    if (res != MA_SUCCESS) {
        return devices;
    }

    ma_device_info* pPlaybackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    res = ma_context_get_devices(&tempContext, &pPlaybackInfos, &playbackCount, nullptr, nullptr);
    if (res == MA_SUCCESS) {
        devices.reserve(playbackCount);
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            AudioDeviceInfo info;
            info.index = static_cast<int>(i);
            info.name = pPlaybackInfos[i].name;
            info.isDefault = (pPlaybackInfos[i].isDefault != 0);
            devices.push_back(info);
        }
    }
    ma_context_uninit(&tempContext);
    return devices;
}

bool AudioEngine::reinitAudioDevice(AudioDriverType driver, int deviceIndex, int bufferLatencyMs, const std::string& preferredName, bool saveConfig) {
    std::lock_guard<std::recursive_mutex> lock(m_deviceMutex);
    bool wasPlaying = m_isPlaying.load() && !m_isPaused.load();

    if (m_deviceInitialized) {
        ma_device_stop(reinterpret_cast<ma_device*>(m_device));
        ma_device_uninit(reinterpret_cast<ma_device*>(m_device));
        m_deviceInitialized = false;
    }

    if (m_context) {
        ma_context_uninit(reinterpret_cast<ma_context*>(m_context));
        delete reinterpret_cast<ma_context*>(m_context);
        m_context = nullptr;
    }

    m_driverType = driver;
    m_selectedDeviceIndex = deviceIndex;
    m_bufferLatencyMs = std::clamp(bufferLatencyMs, 5, 500);

    ma_backend backend = ma_backend_wasapi;
    if (driver == AudioDriverType::DirectSound) {
        backend = ma_backend_dsound;
    }

    ma_backend backends[] = { backend };
    ma_context* pContext = new ma_context();
    ma_result res = ma_context_init(backends, 1, nullptr, pContext);
    if (res != MA_SUCCESS) {
        delete pContext;
        pContext = nullptr;
        m_driverStatusString = "Context initialization failed. Using default audio device.";
        std::cerr << "[AudioEngine] " << m_driverStatusString << " (error: " << res << ")" << std::endl;
    } else {
        m_context = pContext;
    }

    ma_device_id* pTargetDeviceId = nullptr;
    ma_device_id targetDeviceId;
    std::string deviceName = "Default System Device";

    if (m_context) {
        ma_device_info* pInfos = nullptr;
        ma_uint32 count = 0;
        if (ma_context_get_devices(reinterpret_cast<ma_context*>(m_context), &pInfos, &count, nullptr, nullptr) == MA_SUCCESS && count > 0) {
            std::string matchName = preferredName.empty() ? m_selectedDeviceName : preferredName;
            int matchedIdx = -1;

            // 1. Try matching by preferred device name if specified
            if (!matchName.empty() && matchName != "Default System Device" && matchName != "Default") {
                for (ma_uint32 i = 0; i < count; ++i) {
                    if (std::string(pInfos[i].name).find(matchName) != std::string::npos ||
                        matchName.find(pInfos[i].name) != std::string::npos) {
                        matchedIdx = static_cast<int>(i);
                        break;
                    }
                }
            }

            // 2. Fall back to deviceIndex if valid
            if (matchedIdx == -1 && deviceIndex >= 0 && static_cast<ma_uint32>(deviceIndex) < count) {
                matchedIdx = deviceIndex;
            }

            // 3. If deviceIndex == -1 and no specific device was saved, auto-detect headphones / USB DAC!
            if (matchedIdx == -1 && deviceIndex == -1 && (matchName.empty() || matchName == "Default System Device")) {
                for (ma_uint32 i = 0; i < count; ++i) {
                    std::string n = pInfos[i].name;
                    std::string lowerN = n;
                    std::transform(lowerN.begin(), lowerN.end(), lowerN.begin(), ::tolower);
                    if (lowerN.find("headphone") != std::string::npos || 
                        lowerN.find("usb audio") != std::string::npos ||
                        lowerN.find("dac") != std::string::npos) {
                        matchedIdx = static_cast<int>(i);
                        break;
                    }
                }
            }

            if (matchedIdx >= 0 && static_cast<ma_uint32>(matchedIdx) < count) {
                targetDeviceId = pInfos[matchedIdx].id;
                pTargetDeviceId = &targetDeviceId;
                deviceName = pInfos[matchedIdx].name;
                m_selectedDeviceIndex = matchedIdx;
                m_selectedDeviceName = deviceName;
            } else {
                m_selectedDeviceIndex = -1;
                m_selectedDeviceName = "Default System Device";
                pTargetDeviceId = nullptr;
                deviceName = "Default System Device";
                for (ma_uint32 i = 0; i < count; ++i) {
                    if (pInfos[i].isDefault) {
                        deviceName = std::string(pInfos[i].name) + " [System Default]";
                        break;
                    }
                }
            }
        }
    }
    m_activeDeviceName = deviceName;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.pDeviceID = pTargetDeviceId;
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 44100;
    config.dataCallback = maDataCallback;
    config.pUserData = this;
    config.periodSizeInMilliseconds = m_bufferLatencyMs;

    bool tryExclusive = (driver == AudioDriverType::WASAPI_Exclusive);
    ma_device* pDevice = reinterpret_cast<ma_device*>(m_device);

    if (tryExclusive) {
        config.playback.shareMode = ma_share_mode_exclusive;

        // Step 1: Try requested buffer latency and 44100 Hz
        res = ma_device_init(reinterpret_cast<ma_context*>(m_context), &config, pDevice);

        // Step 2: If rejected by driver (e.g. Realtek packet alignment), try driver native hardware period (0ms)
        if (res != MA_SUCCESS) {
            config.periodSizeInMilliseconds = 0;
            res = ma_device_init(reinterpret_cast<ma_context*>(m_context), &config, pDevice);
        }

        // Step 3: If still rejected, try 48000 Hz with native period
        if (res != MA_SUCCESS) {
            config.sampleRate = 48000;
            config.periodSizeInMilliseconds = 0;
            res = ma_device_init(reinterpret_cast<ma_context*>(m_context), &config, pDevice);
        }

        // Step 4: Try native sample rate (0)
        if (res != MA_SUCCESS) {
            config.sampleRate = 0;
            config.periodSizeInMilliseconds = 0;
            res = ma_device_init(reinterpret_cast<ma_context*>(m_context), &config, pDevice);
        }

        if (res == MA_SUCCESS) {
            m_isExclusiveActive = true;
            m_driverStatusString = "WASAPI Exclusive: Bit-Perfect direct hardware output (" + 
                                   std::to_string(pDevice->sampleRate) + "Hz" + 
                                   (config.periodSizeInMilliseconds > 0 ? ", " + std::to_string(config.periodSizeInMilliseconds) + "ms" : ", native period") + ")";
        } else {
            std::cerr << "[AudioEngine] WASAPI Exclusive init failed (" << res << "). Falling back to Shared mode..." << std::endl;
            config.sampleRate = 44100;
            config.periodSizeInMilliseconds = m_bufferLatencyMs;
            config.playback.shareMode = ma_share_mode_shared;
            res = ma_device_init(reinterpret_cast<ma_context*>(m_context), &config, pDevice);
            if (res == MA_SUCCESS) {
                m_isExclusiveActive = false;
                m_driverStatusString = "WASAPI Exclusive unavailable on this device. Operating in Shared fallback mode.";
            }
        }
    } else {
        config.playback.shareMode = ma_share_mode_shared;
        res = ma_device_init(reinterpret_cast<ma_context*>(m_context), &config, pDevice);
        if (res != MA_SUCCESS && driver == AudioDriverType::DirectSound) {
            std::cerr << "[AudioEngine] DirectSound init failed (" << res << "). Falling back to WASAPI Shared mode..." << std::endl;
            if (m_context) {
                ma_context_uninit(reinterpret_cast<ma_context*>(m_context));
                delete reinterpret_cast<ma_context*>(m_context);
                m_context = nullptr;
            }
            ma_backend backends[] = { ma_backend_wasapi };
            ma_context* pFallbackCtx = new ma_context();
            if (ma_context_init(backends, 1, nullptr, pFallbackCtx) == MA_SUCCESS) {
                m_context = pFallbackCtx;
                config.playback.pDeviceID = nullptr;
                config.playback.shareMode = ma_share_mode_shared;
                res = ma_device_init(pFallbackCtx, &config, pDevice);
                if (res == MA_SUCCESS) {
                    m_driverType = AudioDriverType::WASAPI_Shared;
                    m_isExclusiveActive = false;
                    m_driverStatusString = "WASAPI Shared active (" + std::to_string(m_bufferLatencyMs) + "ms latency)";
                }
            }
        } else if (res == MA_SUCCESS) {
            m_isExclusiveActive = false;
            if (driver == AudioDriverType::DirectSound) {
                m_driverStatusString = "DirectSound active (" + std::to_string(m_bufferLatencyMs) + "ms latency)";
            } else {
                m_driverStatusString = "WASAPI Shared active (" + std::to_string(m_bufferLatencyMs) + "ms latency)";
            }
        }
    }

    if (res != MA_SUCCESS && pTargetDeviceId != nullptr) {
        std::cerr << "[AudioEngine] Specific device init failed (" << res << "). Falling back to system default device..." << std::endl;
        config.playback.pDeviceID = nullptr;
        config.playback.shareMode = ma_share_mode_shared;
        res = ma_device_init(reinterpret_cast<ma_context*>(m_context), &config, pDevice);
        if (res == MA_SUCCESS) {
            m_selectedDeviceIndex = -1;
            m_selectedDeviceName = "Default System Device";
            m_activeDeviceName = "Default System Device";
            m_driverType = AudioDriverType::WASAPI_Shared;
            m_isExclusiveActive = false;
            m_driverStatusString = "WASAPI Shared (Fallback Default Device)";
        }
    }

    if (res != MA_SUCCESS) {
        std::cerr << "[AudioEngine] Failed to initialize audio device: " << res << std::endl;
        m_driverStatusString = "Failed to initialize playback device (error code: " + std::to_string(res) + ")";
        return false;
    }

    m_deviceInitialized = true;
    m_equalizer.setSampleRate(static_cast<float>(pDevice->sampleRate > 0 ? pDevice->sampleRate : 44100));

    res = ma_device_start(pDevice);
    if (res != MA_SUCCESS) {
        std::cerr << "[AudioEngine] Failed to start audio device: " << res << std::endl;
        return false;
    }

    if (wasPlaying) {
        m_isPlaying.store(true);
        m_isPaused.store(false);
    }

    if (saveConfig) {
        saveAudioConfig();
    }
    return true;
}

bool AudioEngine::saveAudioConfig(const std::string& path) {
    (void)path;
    auto& s = SettingsManager::instance().settings();

    s.audioDriver = static_cast<int>(m_driverType);
    s.audioDeviceIndex = m_selectedDeviceIndex;
    s.audioDeviceName = m_selectedDeviceName;
    s.bufferLatencyMs = m_bufferLatencyMs;
    s.volume = m_volume.load();
    s.isMuted = m_muted.load();
    s.repeatMode = static_cast<int>(m_repeatMode);
    s.shuffle = m_shuffle;
    s.playbackSpeed = m_playbackSpeed.load();
    s.stereoBalance = m_stereoBalance.load();
    s.crossfadeDuration = m_crossfadeDuration.load();
    s.silenceSkipping = m_silenceSkipping.load();
    s.replayGainPreamp = m_replayGainDb.load();
    s.releaseDriverWhenPaused = m_releaseDriverWhenPaused.load();

    s.eqEnabled = m_equalizer.isEnabled();
    s.eqPreamp = m_equalizer.getPreamp();
    s.eqStereoPan = m_equalizer.getStereoPan();
    s.eqStereoWidth = m_equalizer.getStereoWidth();
    s.eqCurrentPreset = m_equalizer.getCurrentPresetName();
    const auto& gains = m_equalizer.getBandGains();
    s.eqBandGains.assign(gains.begin(), gains.end());

    return SettingsManager::instance().save();
}

bool AudioEngine::loadAudioConfig(const std::string& path) {
    (void)path;
    SettingsManager::instance().load();
    const auto& s = SettingsManager::instance().settings();

    if (s.audioDriver >= 0 && s.audioDriver <= 2) {
        m_driverType = static_cast<AudioDriverType>(s.audioDriver);
    }
    m_selectedDeviceIndex = s.audioDeviceIndex;
    m_selectedDeviceName = s.audioDeviceName;
    if (s.bufferLatencyMs >= 10 && s.bufferLatencyMs <= 500) {
        m_bufferLatencyMs = s.bufferLatencyMs;
    }

    m_volume.store(std::clamp(s.volume, 0.0f, 1.0f));
    m_muted.store(s.isMuted);
    if (s.repeatMode >= 0 && s.repeatMode <= 2) {
        m_repeatMode = static_cast<RepeatMode>(s.repeatMode);
    }
    m_shuffle = s.shuffle;
    m_playbackSpeed.store(std::clamp(s.playbackSpeed, 0.5f, 2.0f));
    m_stereoBalance.store(std::clamp(s.stereoBalance, -1.0f, 1.0f));
    m_crossfadeDuration.store(std::clamp(s.crossfadeDuration, 0.0f, 8.0f));
    m_silenceSkipping.store(s.silenceSkipping);
    m_replayGainDb.store(std::clamp(s.replayGainPreamp, -12.0f, 12.0f));
    m_releaseDriverWhenPaused.store(s.releaseDriverWhenPaused);

    m_equalizer.setEnabled(s.eqEnabled);
    m_equalizer.setPreamp(s.eqPreamp);
    m_equalizer.setStereoPan(s.eqStereoPan);
    m_equalizer.setStereoWidth(s.eqStereoWidth);
    for (size_t b = 0; b < s.eqBandGains.size() && b < EqualizerDSP::NUM_BANDS; ++b) {
        m_equalizer.setBandGain(b, s.eqBandGains[b]);
    }
    if (!s.eqCurrentPreset.empty()) {
        m_equalizer.setCurrentPresetName(s.eqCurrentPreset);
    }

    return true;
}

bool AudioEngine::ensureDeviceStarted() {
    std::lock_guard<std::recursive_mutex> lock(m_deviceMutex);
    if (m_deviceInitialized) {
        ma_device* pDevice = reinterpret_cast<ma_device*>(m_device);
        if (pDevice && ma_device_get_state(pDevice) != ma_device_state_started) {
            ma_result res = ma_device_start(pDevice);
            return (res == MA_SUCCESS);
        }
        return true;
    }
    return reinitAudioDevice(m_driverType, m_selectedDeviceIndex, m_bufferLatencyMs, m_selectedDeviceName, false);
}

void AudioEngine::releaseDevice() {
    std::lock_guard<std::recursive_mutex> lock(m_deviceMutex);
    if (m_deviceInitialized) {
        ma_device* pDevice = reinterpret_cast<ma_device*>(m_device);
        if (pDevice) {
            ma_device_stop(pDevice);
            ma_device_uninit(pDevice);
        }
        m_deviceInitialized = false;
    }
    if (m_context) {
        ma_context_uninit(reinterpret_cast<ma_context*>(m_context));
        delete reinterpret_cast<ma_context*>(m_context);
        m_context = nullptr;
    }
    m_vuLeft.store(0.0f);
    m_vuRight.store(0.0f);
}

void AudioEngine::setReleaseDriverWhenPaused(bool release) {
    m_releaseDriverWhenPaused.store(release);
    if (release) {
        if (!m_isPlaying.load() || m_isPaused.load()) {
            releaseDevice();
        }
    } else {
        if (!m_deviceInitialized) {
            ensureDeviceStarted();
        }
    }
}

bool AudioEngine::loadAndPlay(const std::string& filePath) {
    ensureDeviceStarted();

    std::lock_guard<std::mutex> lock(m_decoderMutex);

    if (m_decoderInitialized) {
        ma_decoder_uninit(reinterpret_cast<ma_decoder*>(m_decoder));
        m_decoderInitialized = false;
    }

    ma_device* pDevice = reinterpret_cast<ma_device*>(m_device);
    uint32_t targetSampleRate = (m_deviceInitialized && pDevice && pDevice->sampleRate > 0) ? pDevice->sampleRate : 44100;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, targetSampleRate);
    ma_result res = initDecoderFile(filePath, &config, reinterpret_cast<ma_decoder*>(m_decoder));
    if (res != MA_SUCCESS) {
        std::cerr << "Failed to open audio file: " << filePath << " (error: " << res << ")" << std::endl;
        return false;
    }

    m_decoderInitialized = true;
    m_currentFilePath = filePath;

    ma_decoder* pDec = reinterpret_cast<ma_decoder*>(m_decoder);
    uint32_t decSampleRate = (pDec && pDec->outputSampleRate > 0) ? pDec->outputSampleRate : targetSampleRate;
    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(pDec, &totalFrames) == MA_SUCCESS) {
        m_totalDuration.store(static_cast<double>(totalFrames) / static_cast<double>(decSampleRate));
    } else {
        m_totalDuration.store(0.0);
    }

    m_currentTime.store(0.0);
    m_fadeMultiplier.store(1.0f);
    m_trackEndedTriggered.store(false);
    m_isPlaying.store(true);
    m_isPaused.store(false);

    return true;
}

void AudioEngine::pause() {
    if (m_isPlaying.load() && !m_isPaused.load()) {
        m_isPaused.store(true);
        if (m_releaseDriverWhenPaused.load()) {
            releaseDevice();
        }
    }
}

void AudioEngine::resume() {
    if (m_isPlaying.load() && m_isPaused.load()) {
        if (m_releaseDriverWhenPaused.load()) {
            ensureDeviceStarted();
        }
        m_isPaused.store(false);
    }
}

void AudioEngine::stop() {
    m_isPlaying.store(false);
    m_isPaused.store(false);
    m_currentTime.store(0.0);
    {
        std::lock_guard<std::mutex> lock(m_decoderMutex);
        if (m_decoderInitialized) {
            ma_decoder_seek_to_pcm_frame(reinterpret_cast<ma_decoder*>(m_decoder), 0);
        }
    }
    if (m_releaseDriverWhenPaused.load()) {
        releaseDevice();
    }
}

void AudioEngine::togglePlayPause() {
    if (!m_isPlaying.load()) {
        if (m_decoderInitialized) {
            if (m_releaseDriverWhenPaused.load()) {
                ensureDeviceStarted();
            }
            m_isPlaying.store(true);
            m_isPaused.store(false);
        }
    } else if (m_isPaused.load()) {
        resume();
    } else {
        pause();
    }
}

void AudioEngine::seekTo(double seconds) {
    std::lock_guard<std::mutex> lock(m_decoderMutex);
    if (m_decoderInitialized) {
        ma_decoder* pDec = reinterpret_cast<ma_decoder*>(m_decoder);
        uint32_t sRate = (pDec && pDec->outputSampleRate > 0) ? pDec->outputSampleRate : 44100;
        ma_uint64 targetFrame = static_cast<ma_uint64>(seconds * static_cast<double>(sRate));
        ma_decoder_seek_to_pcm_frame(pDec, targetFrame);
        m_currentTime.store(seconds);
    }
}

void AudioEngine::setVolume(float volume) {
    m_volume.store(std::clamp(volume, 0.0f, 1.0f));
}

void AudioEngine::setMuted(bool muted) {
    m_muted.store(muted);
}

bool AudioEngine::isPlaying() const {
    return m_isPlaying.load() && !m_isPaused.load();
}

bool AudioEngine::isPaused() const {
    return m_isPlaying.load() && m_isPaused.load();
}

double AudioEngine::getCurrentTime() const {
    return m_currentTime.load();
}

double AudioEngine::getTotalDuration() const {
    return m_totalDuration.load();
}

void AudioEngine::cycleRepeatMode() {
    if (m_repeatMode == RepeatMode::Off) {
        m_repeatMode = RepeatMode::All;
    } else if (m_repeatMode == RepeatMode::All) {
        m_repeatMode = RepeatMode::One;
    } else {
        m_repeatMode = RepeatMode::Off;
    }
}

void AudioEngine::onAudioFrames(float* pOutput, size_t frameCount) {
    if (!m_isPlaying.load() || m_isPaused.load() || !m_decoderInitialized) {
        std::memset(pOutput, 0, frameCount * 2 * sizeof(float));
        m_vuLeft.store(0.0f);
        m_vuRight.store(0.0f);
        return;
    }

    std::unique_lock<std::mutex> lock(m_decoderMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        std::memset(pOutput, 0, frameCount * 2 * sizeof(float));
        return;
    }

    ma_uint64 framesRead = 0;
    ma_result res = ma_decoder_read_pcm_frames(reinterpret_cast<ma_decoder*>(m_decoder), pOutput, frameCount, &framesRead);

    if (res != MA_SUCCESS || framesRead < frameCount) {
        // Zero out unread frames
        size_t framesToZero = frameCount - framesRead;
        std::memset(pOutput + (framesRead * 2), 0, framesToZero * 2 * sizeof(float));

        if (framesRead == 0) {
            // Track reached the end!
            if (m_repeatMode == RepeatMode::One) {
                ma_decoder_seek_to_pcm_frame(reinterpret_cast<ma_decoder*>(m_decoder), 0);
                m_currentTime.store(0.0);
            } else {
                m_isPlaying.store(false);
                m_currentTime.store(m_totalDuration.load());
                if (m_trackEndedCallback && !m_trackEndedTriggered.load()) {
                    m_trackEndedTriggered.store(true);
                    // Trigger callback in a detached thread to prevent deadlock
                    std::thread([this]() {
                        if (m_trackEndedCallback) m_trackEndedCallback();
                    }).detach();
                }
            }
            return;
        }
    }

    // Update current time
    ma_decoder* pDec = reinterpret_cast<ma_decoder*>(m_decoder);
    uint32_t decSampleRate = (pDec && pDec->outputSampleRate > 0) ? pDec->outputSampleRate : 44100;
    ma_uint64 currentFrame = 0;
    if (ma_decoder_get_cursor_in_pcm_frames(pDec, &currentFrame) == MA_SUCCESS) {
        m_currentTime.store(static_cast<double>(currentFrame) / static_cast<double>(decSampleRate));
    }

    // Apply Volume, Sleep Fade, ReplayGain Preamp, and Stereo Pan
    float vol = (m_muted.load() ? 0.0f : m_volume.load()) * m_fadeMultiplier.load();
    float replayGainDb = m_replayGainDb.load();
    float gainLinear = (replayGainDb == 0.0f) ? 1.0f : std::pow(10.0f, replayGainDb / 20.0f);

    float bal = m_stereoBalance.load();
    float panL = (bal <= 0.0f) ? 1.0f : (1.0f - bal);
    float panR = (bal >= 0.0f) ? 1.0f : (1.0f + bal);

    float leftGain = vol * gainLinear * panL;
    float rightGain = vol * gainLinear * panR;

    for (size_t i = 0; i < framesRead; ++i) {
        pOutput[i * 2] *= leftGain;
        pOutput[i * 2 + 1] *= rightGain;
    }

    // Apply Equalizer DSP
    m_equalizer.process(pOutput, framesRead, 2);

    // Compute VU Meters & copy to visualizer buffer
    float peakL = 0.0f, peakR = 0.0f;
    {
        std::lock_guard<std::mutex> visLock(m_visMutex);
        for (size_t i = 0; i < framesRead; ++i) {
            float l = pOutput[i * 2];
            float r = pOutput[i * 2 + 1];
            if (std::abs(l) > peakL) peakL = std::abs(l);
            if (std::abs(r) > peakR) peakR = std::abs(r);

            float mono = (l + r) * 0.5f;
            m_visBuffer[m_visWritePos] = mono;
            m_visWritePos = (m_visWritePos + 1) % VIS_BUFFER_SIZE;
        }
    }

    // Smooth VU decay
    float prevL = m_vuLeft.load();
    float prevR = m_vuRight.load();
    m_vuLeft.store(peakL > prevL ? peakL : (prevL * 0.85f + peakL * 0.15f));
    m_vuRight.store(peakR > prevR ? peakR : (prevR * 0.85f + peakR * 0.15f));
}

void AudioEngine::getVUMeters(float& left, float& right) {
    left = m_vuLeft.load();
    right = m_vuRight.load();
}

void AudioEngine::getOscilloscope(std::vector<float>& outSamples, size_t numSamples) {
    outSamples.resize(numSamples, 0.0f);
    if (!m_isPlaying.load() || m_isPaused.load()) return;

    std::lock_guard<std::mutex> visLock(m_visMutex);
    size_t start = m_visWritePos;
    float step = static_cast<float>(VIS_BUFFER_SIZE) / static_cast<float>(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        size_t idx = (start + static_cast<size_t>(i * step)) % VIS_BUFFER_SIZE;
        outSamples[i] = std::clamp(m_visBuffer[idx], -1.0f, 1.0f);
    }
}

void AudioEngine::getSpectrum(std::vector<float>& outBars, size_t numBars) {
    outBars.resize(numBars, 0.0f);

    if (!m_isPlaying.load() || m_isPaused.load()) {
        for (size_t b = 0; b < numBars && b < m_smoothedBars.size(); ++b) {
            m_smoothedBars[b] *= 0.8f;
            outBars[b] = m_smoothedBars[b];
        }
        return;
    }

    std::array<float, VIS_BUFFER_SIZE> localBuf;
    {
        std::lock_guard<std::mutex> visLock(m_visMutex);
        size_t start = m_visWritePos;
        for (size_t i = 0; i < VIS_BUFFER_SIZE; ++i) {
            localBuf[i] = m_visBuffer[(start + i) % VIS_BUFFER_SIZE];
        }
    }

    // 32-band logarithmic spectrum filter bank
    float minF = 40.0f;
    float maxF = 15000.0f;
    float logMin = std::log10(minF);
    float logMax = std::log10(maxF);

    for (size_t b = 0; b < numBars; ++b) {
        float f1 = std::pow(10.0f, logMin + (logMax - logMin) * (static_cast<float>(b) / numBars));
        float f2 = std::pow(10.0f, logMin + (logMax - logMin) * (static_cast<float>(b + 1) / numBars));
        float centerF = (f1 + f2) * 0.5f;

        // Goertzel single frequency magnitude
        float k = (VIS_BUFFER_SIZE * centerF) / 44100.0f;
        float omega = static_cast<float>(2.0 * M_PI * k / VIS_BUFFER_SIZE);
        float coeff = 2.0f * std::cos(omega);

        float s_prev = 0.0f, s_prev2 = 0.0f;
        for (size_t i = 0; i < VIS_BUFFER_SIZE; ++i) {
            // Hann window
            float win = 0.5f * (1.0f - std::cos(static_cast<float>(2.0 * M_PI * i / VIS_BUFFER_SIZE)));
            float sample = localBuf[i] * win;

            float s = sample + coeff * s_prev - s_prev2;
            s_prev2 = s_prev;
            s_prev = s;
        }

        float power = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
        float mag = std::sqrt(std::max(0.0f, power)) / (VIS_BUFFER_SIZE * 0.25f);

        // Boost high frequencies slightly for balanced visual appearance
        float highBoost = 1.0f + (static_cast<float>(b) / numBars) * 2.5f;
        mag *= highBoost;
        mag = std::clamp(mag, 0.0f, 1.0f);

        // Attack & decay smoothing
        if (b < m_smoothedBars.size()) {
            if (mag > m_smoothedBars[b]) {
                m_smoothedBars[b] = m_smoothedBars[b] * 0.4f + mag * 0.6f; // fast attack
            } else {
                m_smoothedBars[b] = m_smoothedBars[b] * 0.82f + mag * 0.18f; // smooth decay
            }
            outBars[b] = m_smoothedBars[b];
        } else {
            outBars[b] = mag;
        }
    }
}

std::vector<float> AudioEngine::generateWaveformPeaks(const std::string& filePath, size_t numPoints) {
    if (numPoints == 0) numPoints = 140;
    std::vector<float> peaks(numPoints, 0.08f);
    if (filePath.empty()) return peaks;

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 44100);
    ma_decoder decoder;
    if (initDecoderFile(filePath, &config, &decoder) != MA_SUCCESS) {
        // Fallback procedural waveform shape
        for (size_t i = 0; i < numPoints; ++i) {
            float x = static_cast<float>(i) / static_cast<float>(numPoints);
            peaks[i] = 0.15f + 0.65f * std::sin(x * 3.14159f) * (0.6f + 0.4f * std::sin(x * 14.0f));
            peaks[i] = std::clamp(peaks[i], 0.08f, 1.0f);
        }
        return peaks;
    }

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) != MA_SUCCESS || totalFrames == 0) {
        ma_decoder_uninit(&decoder);
        return peaks;
    }

    ma_uint64 framesPerBucket = totalFrames / numPoints;
    if (framesPerBucket == 0) framesPerBucket = 1;

    constexpr size_t CHUNK_FRAMES = 512;
    std::vector<float> buffer(CHUNK_FRAMES * 2);

    for (size_t pt = 0; pt < numPoints; ++pt) {
        ma_uint64 targetFrame = pt * framesPerBucket;
        ma_decoder_seek_to_pcm_frame(&decoder, targetFrame);

        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, buffer.data(), std::min<size_t>(CHUNK_FRAMES, framesPerBucket), &framesRead);

        float maxPeak = 0.0f;
        for (size_t f = 0; f < framesRead * 2; ++f) {
            float val = std::abs(buffer[f]);
            if (val > maxPeak) maxPeak = val;
        }

        peaks[pt] = std::clamp(maxPeak, 0.08f, 1.0f);
    }

    ma_decoder_uninit(&decoder);
    return peaks;
}
