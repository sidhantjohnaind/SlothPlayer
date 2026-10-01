#pragma once

#include <vector>
#include <string>
#include <array>
#include <map>
#include <mutex>

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
};

struct BiquadState {
    float s1 = 0.0f;
    float s2 = 0.0f;
};

class EqualizerDSP {
public:
    static constexpr size_t NUM_BANDS = 10;
    static constexpr std::array<float, NUM_BANDS> BAND_FREQUENCIES = {
        31.0f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    EqualizerDSP();

    void setSampleRate(float sampleRate);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setPreamp(float dB);
    float getPreamp() const { return m_preampDB; }

    void setBandGain(size_t band, float dB);
    float getBandGain(size_t band) const;

    const std::array<float, NUM_BANDS>& getBandGains() const { return m_bandGains; }

    void setStereoPan(float pan);
    float getStereoPan() const { return m_stereoPan; }

    void setStereoWidth(float width); // 0.0 = Mono (DSP_DownmixMono), 1.0 = Normal Stereo, 2.0 = Enhanced (DSP_StereoEnhancer)
    float getStereoWidth() const { return m_stereoWidth; }

    void loadPreset(const std::string& presetName);
    std::vector<std::string> getPresetNames() const;
    std::string getCurrentPresetName() const { return m_currentPreset; }
    void setCurrentPresetName(const std::string& name) { m_currentPreset = name; }

    void loadSdePresetsFromFolder(const std::string& folderPath);
    bool loadSdeFile(const std::string& filePath, const std::string& presetName);
    bool saveCurrentPresetToSde(const std::string& filePath, const std::string& presetName);

    // In-place processing of interleaved stereo float samples
    void process(float* samples, size_t frameCount, size_t channels);
    void resetState();

private:
    void updateFilter(size_t band);
    void updateAllFilters();

    bool m_enabled = true;
    float m_sampleRate = 44100.0f;
    float m_preampDB = 0.0f;
    float m_preampLinear = 1.0f;
    float m_stereoPan = 0.0f; // -1.0 (Left) to +1.0 (Right)
    float m_stereoWidth = 1.0f; // 0.0 (Mono) to 2.0 (Wide Stereo)
    std::string m_currentPreset = "Flat";

    std::array<float, NUM_BANDS> m_bandGains;
    std::array<BiquadCoeffs, NUM_BANDS> m_coeffs;
    std::vector<std::array<BiquadState, NUM_BANDS>> m_channelStates; // per channel
    std::map<std::string, std::pair<float, std::array<float, NUM_BANDS>>> m_presetsMap;

    mutable std::mutex m_mutex;
};
