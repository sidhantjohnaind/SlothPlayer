#include "EqualizerDSP.h"
#include "../platform/Platform.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

namespace fs = std::filesystem;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

constexpr std::array<float, EqualizerDSP::NUM_BANDS> EqualizerDSP::BAND_FREQUENCIES;

EqualizerDSP::EqualizerDSP() {
    m_bandGains.fill(0.0f);
    m_channelStates.resize(2); // default stereo
    updateAllFilters();

    // Auto-load presets from platform config directory
    std::string appDataDir = Platform::getAppDataDir();
    fs::path presetDir = fs::path(appDataDir) / "Equaliser";
    if (fs::exists(presetDir)) {
        loadSdePresetsFromFolder(presetDir.string());
    }
#if defined(_WIN32) || defined(_WIN64)
    const char* winAppData = std::getenv("APPDATA");
    if (winAppData) {
        fs::path eqPath = fs::path(winAppData) / "SlothPlayer" / "Equaliser";
        if (fs::exists(eqPath)) {
            loadSdePresetsFromFolder(eqPath.string());
        }
    }
#endif
}

void EqualizerDSP::setSampleRate(float sampleRate) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (sampleRate > 1000.0f && sampleRate != m_sampleRate) {
        m_sampleRate = sampleRate;
        updateAllFilters();
        resetState();
    }
}

void EqualizerDSP::setEnabled(bool enabled) {
    m_enabled = enabled;
}

void EqualizerDSP::setPreamp(float dB) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_preampDB = std::clamp(dB, -12.0f, 12.0f);
    m_preampLinear = std::pow(10.0f, m_preampDB / 20.0f);
}

void EqualizerDSP::setBandGain(size_t band, float dB) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (band < NUM_BANDS) {
        m_bandGains[band] = std::clamp(dB, -12.0f, 12.0f);
        m_currentPreset = "Custom";
        updateFilter(band);
    }
}

float EqualizerDSP::getBandGain(size_t band) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (band < NUM_BANDS) {
        return m_bandGains[band];
    }
    return 0.0f;
}

void EqualizerDSP::updateFilter(size_t band) {
    float f0 = BAND_FREQUENCIES[band];
    float gainDB = m_bandGains[band];

    // If frequency is near or above Nyquist, set pass-through
    if (f0 >= m_sampleRate * 0.48f) {
        m_coeffs[band] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        return;
    }

    if (std::abs(gainDB) < 0.01f) {
        m_coeffs[band] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        return;
    }

    float A = std::pow(10.0f, gainDB / 40.0f);
    float w0 = static_cast<float>(2.0 * M_PI) * f0 / m_sampleRate;
    float cos_w0 = std::cos(w0);
    float sin_w0 = std::sin(w0);
    float Q = 1.414f;
    float alpha = sin_w0 / (2.0f * Q);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cos_w0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cos_w0;
    float a2 = 1.0f - alpha / A;

    m_coeffs[band].b0 = b0 / a0;
    m_coeffs[band].b1 = b1 / a0;
    m_coeffs[band].b2 = b2 / a0;
    m_coeffs[band].a1 = a1 / a0;
    m_coeffs[band].a2 = a2 / a0;
}

void EqualizerDSP::updateAllFilters() {
    for (size_t i = 0; i < NUM_BANDS; ++i) {
        updateFilter(i);
    }
}

void EqualizerDSP::resetState() {
    for (auto& ch : m_channelStates) {
        for (auto& s : ch) {
            s.s1 = 0.0f;
            s.s2 = 0.0f;
        }
    }
}

void EqualizerDSP::setStereoPan(float pan) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stereoPan = std::clamp(pan, -1.0f, 1.0f);
}

void EqualizerDSP::loadSdePresetsFromFolder(const std::string& folderPath) {
    try {
        if (!fs::exists(folderPath)) return;
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".sde") {
                std::string pName = entry.path().stem().string();
                loadSdeFile(entry.path().string(), pName);
            }
        }
    } catch (...) {}
}

bool EqualizerDSP::loadSdeFile(const std::string& filePath, const std::string& presetName) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    float preamp = 0.0f;
    std::array<float, NUM_BANDS> gains{};
    gains.fill(0.0f);

    std::string line;
    while (std::getline(file, line)) {
        // Strip carriage return
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string valStr = line.substr(eqPos + 1);

        try {
            float val = std::stof(valStr);
            if (key == "LeftPreamp" || key == "Leftreamp") {
                preamp = std::clamp(val, -12.0f, 12.0f);
            } else if (key.rfind("Left", 0) == 0 && key.length() > 4) {
                int bIdx = std::stoi(key.substr(4)) - 1;
                if (bIdx >= 0 && static_cast<size_t>(bIdx) < NUM_BANDS) {
                    gains[bIdx] = std::clamp(val, -12.0f, 12.0f);
                }
            }
        } catch (...) {}
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_presetsMap[presetName] = { preamp, gains };
    return true;
}

bool EqualizerDSP::saveCurrentPresetToSde(const std::string& filePath, const std::string& presetName) {
    std::ofstream file(filePath);
    if (!file.is_open()) return false;

    file << "[Equalizer]\n";
    file << "LeftPreamp=" << m_preampDB << "\n";
    file << "RightPreamp=" << m_preampDB << "\n";
    for (size_t i = 0; i < NUM_BANDS; ++i) {
        file << "Left" << (i + 1) << "=" << m_bandGains[i] << "\n";
        file << "Right" << (i + 1) << "=" << m_bandGains[i] << "\n";
    }
    file << "Bands=" << NUM_BANDS << "\n";

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_presetsMap[presetName] = { m_preampDB, m_bandGains };
        m_currentPreset = presetName;
    }
    return true;
}

void EqualizerDSP::loadPreset(const std::string& presetName) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_presetsMap.find(presetName);
    if (it != m_presetsMap.end()) {
        m_preampDB = it->second.first;
        m_preampLinear = std::pow(10.0f, m_preampDB / 20.0f);
        m_bandGains = it->second.second;
        m_currentPreset = presetName;
        updateAllFilters();
        return;
    }

    if (presetName == "Flat") {
        m_bandGains = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        m_preampDB = 0.0f;
        m_preampLinear = 1.0f;
    } else if (presetName == "Rock") {
        m_bandGains = {4.5f, 3.0f, -1.5f, -2.5f, -1.0f, 1.0f, 2.5f, 3.5f, 4.0f, 4.5f};
    } else if (presetName == "Pop") {
        m_bandGains = {-1.5f, -1.0f, 0.5f, 2.0f, 4.0f, 4.0f, 2.0f, 0.5f, -1.0f, -1.5f};
    } else if (presetName == "Jazz") {
        m_bandGains = {3.0f, 2.0f, 1.0f, 1.5f, -1.5f, -1.5f, 0.0f, 1.5f, 2.5f, 3.5f};
    } else if (presetName == "Classical") {
        m_bandGains = {4.0f, 3.0f, 2.5f, 2.0f, -1.5f, -1.5f, 0.0f, 2.0f, 3.0f, 3.5f};
    } else if (presetName == "Bass Boost") {
        m_bandGains = {6.5f, 5.5f, 4.0f, 2.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    } else if (presetName == "Treble Boost") {
        m_bandGains = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.5f, 4.5f, 6.0f, 7.5f};
    } else if (presetName == "Vocal Boost") {
        m_bandGains = {-2.0f, -3.0f, -2.0f, 1.5f, 3.5f, 3.5f, 2.5f, 1.0f, 0.0f, -1.5f};
    } else if (presetName == "Electronic") {
        m_bandGains = {4.0f, 3.5f, 1.5f, 0.0f, -1.5f, 2.0f, 1.0f, 2.0f, 4.0f, 4.5f};
    } else if (presetName == "Acoustic") {
        m_bandGains = {3.5f, 2.5f, 1.5f, 1.0f, 1.5f, 1.5f, 2.5f, 3.0f, 3.0f, 2.5f};
    }
    m_currentPreset = presetName;
    updateAllFilters();
}

std::vector<std::string> EqualizerDSP::getPresetNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::set<std::string> names = {
        "Flat", "Rock", "Pop", "Jazz", "Classical",
        "Bass Boost", "Treble Boost", "Vocal Boost", "Electronic", "Acoustic"
    };
    for (const auto& pair : m_presetsMap) {
        names.insert(pair.first);
    }
    return std::vector<std::string>(names.begin(), names.end());
}

void EqualizerDSP::setStereoWidth(float width) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stereoWidth = std::clamp(width, 0.0f, 2.0f);
}

void EqualizerDSP::process(float* samples, size_t frameCount, size_t channels) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_channelStates.size() < channels) {
        m_channelStates.resize(channels);
    }

    float panL = (m_stereoPan <= 0.0f) ? 1.0f : (1.0f - m_stereoPan);
    float panR = (m_stereoPan >= 0.0f) ? 1.0f : (1.0f + m_stereoPan);

    for (size_t f = 0; f < frameCount; ++f) {
        for (size_t c = 0; c < channels; ++c) {
            float x = samples[f * channels + c] * m_preampLinear;

            for (size_t b = 0; b < NUM_BANDS; ++b) {
                const auto& coeff = m_coeffs[b];
                auto& state = m_channelStates[c][b];

                // Direct Form II Transposed
                float y = coeff.b0 * x + state.s1;
                state.s1 = coeff.b1 * x - coeff.a1 * y + state.s2;
                state.s2 = coeff.b2 * x - coeff.a2 * y;

                x = y;
            }

            samples[f * channels + c] = x;
        }

        // Stereo Enhancement & Panning (DSP_DownmixMono, DSP_StereoEnhancer, DSP_Pan_SetPan)
        if (channels >= 2) {
            float l = samples[f * channels];
            float r = samples[f * channels + 1];

            float monoMid = (l + r) * 0.5f;
            float stereoSide = (l - r) * 0.5f * m_stereoWidth;

            float outL = (monoMid + stereoSide) * panL;
            float outR = (monoMid - stereoSide) * panR;

            samples[f * channels] = std::clamp(outL, -1.0f, 1.0f);
            samples[f * channels + 1] = std::clamp(outR, -1.0f, 1.0f);
        } else {
            samples[f * channels] = std::clamp(samples[f * channels], -1.0f, 1.0f);
        }
    }
}
