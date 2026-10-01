#include "SettingsManager.h"
#include "Platform.h"
#include "../../third_party/json.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

SettingsManager& SettingsManager::instance() {
    static SettingsManager s_inst;
    return s_inst;
}

std::string SettingsManager::getSettingsFilePath() const {
    std::string appData = Platform::getAppDataDir();
    if (!appData.empty() && appData != ".") {
        return (fs::path(appData) / "slothplayer_settings.json").string();
    }
    return "slothplayer_settings.json";
}

bool SettingsManager::load() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string primaryPath = getSettingsFilePath();
    std::string localPath = "slothplayer_settings.json";

    std::string targetPath;
    if (fs::exists(primaryPath)) {
        targetPath = primaryPath;
    } else if (fs::exists(localPath)) {
        targetPath = localPath;
    } else {
        m_loaded = true;
        return false;
    }

    try {
        std::ifstream f(targetPath);
        if (!f.is_open()) {
            m_loaded = true;
            return false;
        }

        nlohmann::json j;
        f >> j;

        // Window
        if (j.contains("window")) {
            const auto& w = j["window"];
            if (w.contains("x")) m_settings.windowX = w["x"].get<int>();
            if (w.contains("y")) m_settings.windowY = w["y"].get<int>();
            if (w.contains("width")) m_settings.windowWidth = w["width"].get<int>();
            if (w.contains("height")) m_settings.windowHeight = w["height"].get<int>();
            if (w.contains("maximized")) m_settings.windowMaximized = w["maximized"].get<bool>();
        }

        // Theme
        if (j.contains("theme")) m_settings.theme = j["theme"].get<int>();
        if (j.contains("useLightExpandedCard")) m_settings.useLightExpandedCard = j["useLightExpandedCard"].get<bool>();

        // Audio
        if (j.contains("audio")) {
            const auto& a = j["audio"];
            if (a.contains("driver")) m_settings.audioDriver = a["driver"].get<int>();
            if (a.contains("deviceIndex")) m_settings.audioDeviceIndex = a["deviceIndex"].get<int>();
            if (a.contains("deviceName")) m_settings.audioDeviceName = a["deviceName"].get<std::string>();
            if (a.contains("bufferLatencyMs")) m_settings.bufferLatencyMs = a["bufferLatencyMs"].get<int>();
            if (a.contains("volume")) m_settings.volume = std::clamp(a["volume"].get<float>(), 0.0f, 1.0f);
            if (a.contains("isMuted")) m_settings.isMuted = a["isMuted"].get<bool>();
            if (a.contains("repeatMode")) m_settings.repeatMode = a["repeatMode"].get<int>();
            if (a.contains("shuffle")) m_settings.shuffle = a["shuffle"].get<bool>();
            if (a.contains("playbackSpeed")) m_settings.playbackSpeed = a["playbackSpeed"].get<float>();
            if (a.contains("stereoBalance")) m_settings.stereoBalance = a["stereoBalance"].get<float>();
            if (a.contains("crossfadeDuration")) m_settings.crossfadeDuration = a["crossfadeDuration"].get<float>();
            if (a.contains("silenceSkipping")) m_settings.silenceSkipping = a["silenceSkipping"].get<bool>();
            if (a.contains("replayGainPreamp")) m_settings.replayGainPreamp = a["replayGainPreamp"].get<float>();
            if (a.contains("releaseDriverWhenPaused")) m_settings.releaseDriverWhenPaused = a["releaseDriverWhenPaused"].get<bool>();
        }

        // Equalizer
        if (j.contains("equalizer")) {
            const auto& eq = j["equalizer"];
            if (eq.contains("enabled")) m_settings.eqEnabled = eq["enabled"].get<bool>();
            if (eq.contains("preamp")) m_settings.eqPreamp = eq["preamp"].get<float>();
            if (eq.contains("stereoPan")) m_settings.eqStereoPan = eq["stereoPan"].get<float>();
            if (eq.contains("stereoWidth")) m_settings.eqStereoWidth = eq["stereoWidth"].get<float>();
            if (eq.contains("currentPreset")) m_settings.eqCurrentPreset = eq["currentPreset"].get<std::string>();
            if (eq.contains("bandGains") && eq["bandGains"].is_array()) {
                m_settings.eqBandGains.clear();
                for (const auto& g : eq["bandGains"]) {
                    m_settings.eqBandGains.push_back(g.get<float>());
                }
                while (m_settings.eqBandGains.size() < 10) m_settings.eqBandGains.push_back(0.0f);
            }
        }

        // Playback
        if (j.contains("playback")) {
            const auto& p = j["playback"];
            if (p.contains("lastTrackId")) m_settings.lastTrackId = p["lastTrackId"].get<uint64_t>();
            if (p.contains("lastTrackPath")) m_settings.lastTrackPath = p["lastTrackPath"].get<std::string>();
            if (p.contains("lastPositionSeconds")) m_settings.lastPositionSeconds = p["lastPositionSeconds"].get<double>();
            if (p.contains("queueIndex")) m_settings.queueIndex = p["queueIndex"].get<size_t>();
            if (p.contains("stopAfterCurrent")) m_settings.stopAfterCurrent = p["stopAfterCurrent"].get<bool>();
            if (p.contains("showRemainingTime")) m_settings.showRemainingTime = p["showRemainingTime"].get<bool>();
            if (p.contains("skipDislikedOnAutoplay")) m_settings.skipDislikedOnAutoplay = p["skipDislikedOnAutoplay"].get<bool>();
            if (p.contains("autoSkipDislikedOnMark")) m_settings.autoSkipDislikedOnMark = p["autoSkipDislikedOnMark"].get<bool>();
            if (p.contains("queue") && p["queue"].is_array()) {
                m_settings.queue.clear();
                for (const auto& qId : p["queue"]) {
                    m_settings.queue.push_back(qId.get<uint64_t>());
                }
            }
        }

        // UI
        if (j.contains("ui")) {
            const auto& u = j["ui"];
            if (u.contains("viewMode")) m_settings.viewMode = u["viewMode"].get<int>();
            if (u.contains("navSource")) m_settings.navSource = u["navSource"].get<int>();
            if (u.contains("leftPanelWidth")) m_settings.leftPanelWidth = u["leftPanelWidth"].get<float>();
            if (u.contains("rightPanelWidth")) m_settings.rightPanelWidth = u["rightPanelWidth"].get<float>();
            if (u.contains("rightQueueSplitRatio")) m_settings.rightQueueSplitRatio = u["rightQueueSplitRatio"].get<float>();
            if (u.contains("bottomBarHeight")) m_settings.bottomBarHeight = u["bottomBarHeight"].get<float>();
            if (u.contains("albumCardSize")) m_settings.albumCardSize = u["albumCardSize"].get<float>();
            if (u.contains("rightPanelArtSize")) m_settings.rightPanelArtSize = u["rightPanelArtSize"].get<float>();
            if (u.contains("rightPanelTab")) m_settings.rightPanelTab = u["rightPanelTab"].get<int>();
            if (u.contains("lyricsScale")) m_settings.lyricsScale = u["lyricsScale"].get<float>();
            if (u.contains("useWavebar")) m_settings.useWavebar = u["useWavebar"].get<bool>();
            if (u.contains("visualizerMode")) m_settings.visualizerMode = u["visualizerMode"].get<int>();
            if (u.contains("showLeftNavigator")) m_settings.showLeftNavigator = u["showLeftNavigator"].get<bool>();
            if (u.contains("showRightSidebar")) m_settings.showRightSidebar = u["showRightSidebar"].get<bool>();
            if (u.contains("showRightTrackInfo")) m_settings.showRightTrackInfo = u["showRightTrackInfo"].get<bool>();
            if (u.contains("lockPanels")) m_settings.lockPanels = u["lockPanels"].get<bool>();
            if (u.contains("rightBottomView")) m_settings.rightBottomView = u["rightBottomView"].get<int>();
            if (u.contains("showJumpbar")) m_settings.showJumpbar = u["showJumpbar"].get<bool>();
            if (u.contains("showVisualizer")) m_settings.showVisualizer = u["showVisualizer"].get<bool>();
            if (u.contains("showStatusBar")) m_settings.showStatusBar = u["showStatusBar"].get<bool>();
            if (u.contains("showColumnBrowser")) m_settings.showColumnBrowser = u["showColumnBrowser"].get<bool>();
            if (u.contains("columnBrowserHeight")) m_settings.columnBrowserHeight = u["columnBrowserHeight"].get<float>();
            if (u.contains("cbSelectedGenre")) m_settings.cbSelectedGenre = u["cbSelectedGenre"].get<std::string>();
            if (u.contains("cbSelectedArtist")) m_settings.cbSelectedArtist = u["cbSelectedArtist"].get<std::string>();
            if (u.contains("cbSelectedAlbum")) m_settings.cbSelectedAlbum = u["cbSelectedAlbum"].get<std::string>();
            if (u.contains("miniPlayerMode")) m_settings.miniPlayerMode = u["miniPlayerMode"].get<int>();
            if (u.contains("miniPlayerAlwaysOnTop")) m_settings.miniPlayerAlwaysOnTop = u["miniPlayerAlwaysOnTop"].get<bool>();
            if (u.contains("miniPlayerOpacity")) m_settings.miniPlayerOpacity = u["miniPlayerOpacity"].get<float>();
            if (u.contains("miniPlayerShowQueue")) m_settings.miniPlayerShowQueue = u["miniPlayerShowQueue"].get<bool>();
            if (u.contains("miniPlayerShowLyrics")) m_settings.miniPlayerShowLyrics = u["miniPlayerShowLyrics"].get<bool>();
            if (u.contains("miniPlayerVinylStyle")) m_settings.miniPlayerVinylStyle = u["miniPlayerVinylStyle"].get<bool>();
        }

        // Auto-DJ & Sleep Timer
        if (j.contains("autoDJ")) {
            const auto& adj = j["autoDJ"];
            if (adj.contains("enabled")) m_settings.autoDJEnabled = adj["enabled"].get<bool>();
            if (adj.contains("trackSeparation")) m_settings.autoDJTrackSeparation = adj["trackSeparation"].get<int>();
            if (adj.contains("artistSeparation")) m_settings.autoDJArtistSeparation = adj["artistSeparation"].get<int>();
            if (adj.contains("favoritesOnly")) m_settings.autoDJFavoritesOnly = adj["favoritesOnly"].get<bool>();
            if (adj.contains("minRating")) m_settings.autoDJMinRating = adj["minRating"].get<int>();
        }
        if (j.contains("sleepTimer")) {
            const auto& st = j["sleepTimer"];
            if (st.contains("durationMinutes")) m_settings.sleepTimerDurationMinutes = st["durationMinutes"].get<int>();
            if (st.contains("fadeOutVolume")) m_settings.sleepTimerFadeOut = st["fadeOutVolume"].get<bool>();
        }

        m_loaded = true;
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[SettingsManager] Error loading settings: " << ex.what() << std::endl;
        m_loaded = true;
        return false;
    }
}

bool SettingsManager::save() {
    std::lock_guard<std::mutex> lock(m_mutex);

    try {
        nlohmann::json j;

        // Window
        j["window"] = {
            {"x", m_settings.windowX},
            {"y", m_settings.windowY},
            {"width", m_settings.windowWidth},
            {"height", m_settings.windowHeight},
            {"maximized", m_settings.windowMaximized}
        };

        // Theme
        j["theme"] = m_settings.theme;
        j["useLightExpandedCard"] = m_settings.useLightExpandedCard;

        // Audio
        j["audio"] = {
            {"driver", m_settings.audioDriver},
            {"deviceIndex", m_settings.audioDeviceIndex},
            {"deviceName", m_settings.audioDeviceName},
            {"bufferLatencyMs", m_settings.bufferLatencyMs},
            {"volume", m_settings.volume},
            {"isMuted", m_settings.isMuted},
            {"repeatMode", m_settings.repeatMode},
            {"shuffle", m_settings.shuffle},
            {"playbackSpeed", m_settings.playbackSpeed},
            {"stereoBalance", m_settings.stereoBalance},
            {"crossfadeDuration", m_settings.crossfadeDuration},
            {"silenceSkipping", m_settings.silenceSkipping},
            {"replayGainPreamp", m_settings.replayGainPreamp},
            {"releaseDriverWhenPaused", m_settings.releaseDriverWhenPaused}
        };

        // Equalizer
        j["equalizer"] = {
            {"enabled", m_settings.eqEnabled},
            {"preamp", m_settings.eqPreamp},
            {"stereoPan", m_settings.eqStereoPan},
            {"stereoWidth", m_settings.eqStereoWidth},
            {"currentPreset", m_settings.eqCurrentPreset},
            {"bandGains", m_settings.eqBandGains}
        };

        // Playback
        j["playback"] = {
            {"lastTrackId", m_settings.lastTrackId},
            {"lastTrackPath", m_settings.lastTrackPath},
            {"lastPositionSeconds", m_settings.lastPositionSeconds},
            {"queueIndex", m_settings.queueIndex},
            {"stopAfterCurrent", m_settings.stopAfterCurrent},
            {"showRemainingTime", m_settings.showRemainingTime},
            {"skipDislikedOnAutoplay", m_settings.skipDislikedOnAutoplay},
            {"autoSkipDislikedOnMark", m_settings.autoSkipDislikedOnMark},
            {"queue", m_settings.queue}
        };

        // UI
        j["ui"] = {
            {"viewMode", m_settings.viewMode},
            {"navSource", m_settings.navSource},
            {"leftPanelWidth", m_settings.leftPanelWidth},
            {"rightPanelWidth", m_settings.rightPanelWidth},
            {"rightQueueSplitRatio", m_settings.rightQueueSplitRatio},
            {"bottomBarHeight", m_settings.bottomBarHeight},
            {"albumCardSize", m_settings.albumCardSize},
            {"rightPanelArtSize", m_settings.rightPanelArtSize},
            {"rightPanelTab", m_settings.rightPanelTab},
            {"lyricsScale", m_settings.lyricsScale},
            {"useWavebar", m_settings.useWavebar},
            {"visualizerMode", m_settings.visualizerMode},
            {"showLeftNavigator", m_settings.showLeftNavigator},
            {"showRightSidebar", m_settings.showRightSidebar},
            {"showRightTrackInfo", m_settings.showRightTrackInfo},
            {"lockPanels", m_settings.lockPanels},
            {"rightBottomView", m_settings.rightBottomView},
            {"showJumpbar", m_settings.showJumpbar},
            {"showVisualizer", m_settings.showVisualizer},
            {"showStatusBar", m_settings.showStatusBar},
            {"showColumnBrowser", m_settings.showColumnBrowser},
            {"columnBrowserHeight", m_settings.columnBrowserHeight},
            {"cbSelectedGenre", m_settings.cbSelectedGenre},
            {"cbSelectedArtist", m_settings.cbSelectedArtist},
            {"cbSelectedAlbum", m_settings.cbSelectedAlbum},
            {"miniPlayerMode", m_settings.miniPlayerMode},
            {"miniPlayerAlwaysOnTop", m_settings.miniPlayerAlwaysOnTop},
            {"miniPlayerOpacity", m_settings.miniPlayerOpacity},
            {"miniPlayerShowQueue", m_settings.miniPlayerShowQueue},
            {"miniPlayerShowLyrics", m_settings.miniPlayerShowLyrics},
            {"miniPlayerVinylStyle", m_settings.miniPlayerVinylStyle}
        };

        // Auto-DJ & Sleep Timer
        j["autoDJ"] = {
            {"enabled", m_settings.autoDJEnabled},
            {"trackSeparation", m_settings.autoDJTrackSeparation},
            {"artistSeparation", m_settings.autoDJArtistSeparation},
            {"favoritesOnly", m_settings.autoDJFavoritesOnly},
            {"minRating", m_settings.autoDJMinRating}
        };
        j["sleepTimer"] = {
            {"durationMinutes", m_settings.sleepTimerDurationMinutes},
            {"fadeOutVolume", m_settings.sleepTimerFadeOut}
        };

        std::string primaryPath = getSettingsFilePath();
        std::ofstream f(primaryPath);
        if (f.is_open()) {
            f << j.dump(4);
        }

        // Also sync to local working directory if running portable or if local file exists
        if (primaryPath != "slothplayer_settings.json") {
            std::ofstream fLocal("slothplayer_settings.json");
            if (fLocal.is_open()) {
                fLocal << j.dump(4);
            }
        }

        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[SettingsManager] Error saving settings: " << ex.what() << std::endl;
        return false;
    }
}
