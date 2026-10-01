#pragma once

#include "EqualizerDSP.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <algorithm>

enum class RepeatMode {
    Off = 0,
    All = 1,
    One = 2
};

enum class AudioDriverType {
    WASAPI_Shared = 0,
    WASAPI_Exclusive = 1,
    DirectSound = 2
};

struct AudioDeviceInfo {
    int index = 0;
    std::string name;
    bool isDefault = false;
};

class AudioEngine {
public:
    using TrackEndedCallback = std::function<void()>;

    AudioEngine();
    ~AudioEngine();

    bool init();
    void shutdown();

    bool loadAndPlay(const std::string& filePath);
    void pause();
    void resume();
    void stop();
    void togglePlayPause();

    void seekTo(double seconds);
    void setVolume(float volume); // 0.0f to 1.0f
    float getVolume() const { return m_volume.load(); }

    void setMuted(bool muted);
    bool isMuted() const { return m_muted.load(); }

    void setFadeMultiplier(float fade) { m_fadeMultiplier.store(std::clamp(fade, 0.0f, 1.0f)); }
    float getFadeMultiplier() const { return m_fadeMultiplier.load(); }

    void setPlaybackSpeed(float speed) { m_playbackSpeed.store(std::clamp(speed, 0.5f, 2.0f)); }
    float getPlaybackSpeed() const { return m_playbackSpeed.load(); }

    // Stereo Balance (-1.0f full left, 0.0f center, +1.0f full right)
    void setStereoBalance(float balance) { m_stereoBalance.store(std::clamp(balance, -1.0f, 1.0f)); }
    float getStereoBalance() const { return m_stereoBalance.load(); }

    // ReplayGain Preamp (-12.0f dB to +12.0f dB)
    void setReplayGainPreamp(float gainDb) { m_replayGainDb.store(std::clamp(gainDb, -12.0f, 12.0f)); }
    float getReplayGainPreamp() const { return m_replayGainDb.load(); }

    // Crossfade Duration (0.0s = off, up to 8.0s)
    void setCrossfadeDuration(float seconds) { m_crossfadeDuration.store(std::clamp(seconds, 0.0f, 8.0f)); }
    float getCrossfadeDuration() const { return m_crossfadeDuration.load(); }

    // Silence Skipping
    void setSilenceSkipping(bool enable) { m_silenceSkipping.store(enable); }
    bool isSilenceSkipping() const { return m_silenceSkipping.load(); }

    // Waveform Peak Extractor (Wavebar)
    static std::vector<float> generateWaveformPeaks(const std::string& filePath, size_t numPoints = 140);

    bool isPlaying() const;
    bool isPaused() const;

    double getCurrentTime() const;
    double getTotalDuration() const;

    // Queue & Shuffle/Repeat controls
    void setRepeatMode(RepeatMode mode) { m_repeatMode = mode; }
    RepeatMode getRepeatMode() const { return m_repeatMode; }
    void cycleRepeatMode();

    void setShuffle(bool enabled) { m_shuffle = enabled; }
    bool isShuffle() const { return m_shuffle; }
    void toggleShuffle() { m_shuffle = !m_shuffle; }

    void setTrackEndedCallback(TrackEndedCallback cb) { m_trackEndedCallback = cb; }

    // Equalizer
    EqualizerDSP& getEqualizer() { return m_equalizer; }

    // Visualizer data extraction
    void getSpectrum(std::vector<float>& outBars, size_t numBars = 32);
    void getVUMeters(float& left, float& right);
    void getOscilloscope(std::vector<float>& outSamples, size_t numSamples = 64);

    // Audio callback for miniaudio
    void onAudioFrames(float* pOutput, size_t frameCount);

    // Audio Driver & Hardware Output Management (WASAPI Exclusive, WASAPI Shared, DirectSound)
    bool reinitAudioDevice(AudioDriverType driver, int deviceIndex = -1, int bufferLatencyMs = 50, const std::string& deviceName = "", bool saveConfig = true);
    bool ensureDeviceStarted();
    void releaseDevice();
    void setReleaseDriverWhenPaused(bool release);
    bool isReleaseDriverWhenPaused() const { return m_releaseDriverWhenPaused.load(); }
    bool isDeviceInitialized() const { return m_deviceInitialized; }
    std::vector<AudioDeviceInfo> getAudioDevices(AudioDriverType driver);
    AudioDriverType getAudioDriverType() const { return m_driverType; }
    int getSelectedDeviceIndex() const { return m_selectedDeviceIndex; }
    std::string getSelectedDeviceName() const { return m_selectedDeviceName; }
    void setSelectedDeviceName(const std::string& name) { m_selectedDeviceName = name; }
    int getBufferLatencyMs() const { return m_bufferLatencyMs; }
    bool isExclusiveActive() const { return m_isExclusiveActive; }
    std::string getActiveDeviceName() const { return m_activeDeviceName; }
    std::string getDriverStatusString() const { return m_driverStatusString; }

    bool saveAudioConfig(const std::string& path = "slothplayer_audio_config.json");
    bool loadAudioConfig(const std::string& path = "slothplayer_audio_config.json");

private:
    void audioDeviceCallback(void* pOutput, const void* pInput, unsigned int frameCount);

    void* m_context = nullptr; // ma_context pointer
    void* m_device = nullptr;  // ma_device pointer
    void* m_decoder = nullptr; // ma_decoder pointer
    bool m_deviceInitialized = false;
    bool m_decoderInitialized = false;

    AudioDriverType m_driverType = AudioDriverType::WASAPI_Shared;
    int m_selectedDeviceIndex = -1;
    std::string m_selectedDeviceName;
    int m_bufferLatencyMs = 50;
    bool m_isExclusiveActive = false;
    std::string m_activeDeviceName = "Default Device";
    std::string m_driverStatusString = "WASAPI Shared (Default)";

    std::string m_currentFilePath;
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<float> m_volume{0.8f};
    std::atomic<bool> m_muted{false};
    std::atomic<float> m_fadeMultiplier{1.0f};
    std::atomic<float> m_playbackSpeed{1.0f};
    std::atomic<float> m_stereoBalance{0.0f};
    std::atomic<float> m_replayGainDb{0.0f};
    std::atomic<float> m_crossfadeDuration{0.0f};
    std::atomic<bool> m_silenceSkipping{false};
    std::atomic<double> m_currentTime{0.0};
    std::atomic<double> m_totalDuration{0.0};
    std::atomic<bool> m_trackEndedTriggered{false};

    RepeatMode m_repeatMode = RepeatMode::Off;
    bool m_shuffle = false;

    EqualizerDSP m_equalizer;
    TrackEndedCallback m_trackEndedCallback;

    // Visualizer data
    static constexpr size_t VIS_BUFFER_SIZE = 1024;
    std::array<float, VIS_BUFFER_SIZE> m_visBuffer{};
    std::array<float, 64> m_smoothedBars{};
    size_t m_visWritePos = 0;
    std::atomic<float> m_vuLeft{0.0f};
    std::atomic<float> m_vuRight{0.0f};
    std::atomic<bool> m_releaseDriverWhenPaused{true};
    mutable std::mutex m_visMutex;
    mutable std::mutex m_decoderMutex;
    mutable std::recursive_mutex m_deviceMutex;
};
