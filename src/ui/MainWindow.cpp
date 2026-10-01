#include "MainWindow.h"
#include "Theme.h"
#include "SkinManager.h"
#include "../library/TagReader.h"
#include "../platform/Platform.h"
#include "../platform/SMTCManager.h"
#include "../platform/TaskbarManager.h"
#include "../platform/SettingsManager.h"
#include "../../third_party/imgui/imgui.h"
#include "../../third_party/imgui/imgui_internal.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <set>
#include <random>
#include <fstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fs = std::filesystem;

static void drawVectorSpinner(ImDrawList* dl, ImVec2 center, float radius, float thickness, ImU32 color) {
    float time = static_cast<float>(ImGui::GetTime());
    float startAngle = time * 6.0f;
    float arcLength = 1.8f + std::sin(time * 3.0f) * 0.6f;
    dl->PathClear();
    int numSegments = 24;
    for (int i = 0; i <= numSegments; ++i) {
        float a = startAngle + (arcLength * static_cast<float>(i) / static_cast<float>(numSegments));
        dl->PathLineTo(ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius));
    }
    dl->PathStroke(color, false, thickness);
}

static void drawResizableArrowBadge(ImDrawList* dl, ImVec2 center, bool isVertical, ImU32 accentCol) {
    const float radius = 4.0f;
    ImU32 shadowCol = IM_COL32(0, 0, 0, 160);
    ImU32 bgCol = IM_COL32(18, 22, 30, 245);
    ImU32 arrowCol = IM_COL32(255, 255, 255, 245);
    ImU32 hairlineCol = IM_COL32(80, 90, 110, 180);

    if (isVertical) {
        // Vertical splitter -> Horizontal resize arrows ◀ ▶
        ImVec2 half(10.0f, 16.0f);
        ImVec2 pMin(center.x - half.x, center.y - half.y);
        ImVec2 pMax(center.x + half.x, center.y + half.y);

        // Soft drop shadow
        dl->AddRectFilled(ImVec2(pMin.x + 1.0f, pMin.y + 1.5f), ImVec2(pMax.x + 1.0f, pMax.y + 1.5f), shadowCol, radius);
        // Pill background & border
        dl->AddRectFilled(pMin, pMax, bgCol, radius);
        dl->AddRect(pMin, pMax, accentCol, radius, 0, 1.0f);

        // Center hairline
        dl->AddLine(ImVec2(center.x, center.y - 8.0f), ImVec2(center.x, center.y + 8.0f), hairlineCol, 1.0f);

        // Left triangle ◀
        dl->AddTriangleFilled(
            ImVec2(center.x - 7.0f, center.y),
            ImVec2(center.x - 2.5f, center.y - 4.0f),
            ImVec2(center.x - 2.5f, center.y + 4.0f),
            arrowCol
        );

        // Right triangle ▶
        dl->AddTriangleFilled(
            ImVec2(center.x + 7.0f, center.y),
            ImVec2(center.x + 2.5f, center.y - 4.0f),
            ImVec2(center.x + 2.5f, center.y + 4.0f),
            arrowCol
        );
    } else {
        // Horizontal splitter -> Vertical resize arrows ▲ ▼
        ImVec2 half(16.0f, 10.0f);
        ImVec2 pMin(center.x - half.x, center.y - half.y);
        ImVec2 pMax(center.x + half.x, center.y + half.y);

        // Soft drop shadow
        dl->AddRectFilled(ImVec2(pMin.x + 1.0f, pMin.y + 1.5f), ImVec2(pMax.x + 1.0f, pMax.y + 1.5f), shadowCol, radius);
        // Pill background & border
        dl->AddRectFilled(pMin, pMax, bgCol, radius);
        dl->AddRect(pMin, pMax, accentCol, radius, 0, 1.0f);

        // Center hairline
        dl->AddLine(ImVec2(center.x - 8.0f, center.y), ImVec2(center.x + 8.0f, center.y), hairlineCol, 1.0f);

        // Top triangle ▲
        dl->AddTriangleFilled(
            ImVec2(center.x, center.y - 7.0f),
            ImVec2(center.x - 4.0f, center.y - 2.5f),
            ImVec2(center.x + 4.0f, center.y - 2.5f),
            arrowCol
        );

        // Bottom triangle ▼
        dl->AddTriangleFilled(
            ImVec2(center.x, center.y + 7.0f),
            ImVec2(center.x - 4.0f, center.y + 2.5f),
            ImVec2(center.x + 4.0f, center.y + 2.5f),
            arrowCol
        );
    }
}

MainWindow::MainWindow(AudioEngine& audio, LibraryManager& library, TextureManager& textures)
    : m_audio(audio), m_library(library), m_textures(textures) {
    
    // Connect track ended callback (SlothPlayer PlaybackStopAfterCurrent & Sleep Timer support)
    m_audio.setTrackEndedCallback([this]() {
        m_pendingTrackEnded.store(true);
    });

    initDefaultTabs();
    loadPreferences();

    if (m_currentTrackId == 0 && !m_library.getTracks().empty()) {
        m_selectedTrackId = m_library.getTracks()[0].id;
    }

    if (m_currentTrackId != 0) {
        Track* curTr = m_library.getTrackById(m_currentTrackId);
        if (curTr) {
            requestWaveform(curTr->filePath);
        }
    }
}

MainWindow::~MainWindow() {
    savePreferences();
    m_audio.setTrackEndedCallback(nullptr);
    if (AudioConverter::isConverting()) {
        AudioConverter::cancelBatch();
    }
}

void MainWindow::loadPreferences() {
    SettingsManager::instance().load();
    const auto& s = SettingsManager::instance().settings();

    // View & navigation
    if (s.viewMode >= 0 && s.viewMode <= static_cast<int>(ViewMode::TheaterMode)) {
        m_viewMode = static_cast<ViewMode>(s.viewMode);
    }
    if (s.navSource >= 0 && s.navSource <= static_cast<int>(NavSource::History)) {
        m_navSource = static_cast<NavSource>(s.navSource);
    }

    // Panel dimensions
    if (s.leftPanelWidth >= 100.0f && s.leftPanelWidth <= 600.0f) {
        m_leftPanelWidth = s.leftPanelWidth;
    }
    if (s.rightPanelWidth >= 150.0f && s.rightPanelWidth <= 700.0f) {
        m_rightPanelWidth = s.rightPanelWidth;
    }
    if (s.rightQueueSplitRatio >= 0.1f && s.rightQueueSplitRatio <= 0.9f) {
        m_rightQueueSplitRatio = s.rightQueueSplitRatio;
    }
    if (s.bottomBarHeight >= 30.0f && s.bottomBarHeight <= 80.0f) {
        m_bottomBarHeight = s.bottomBarHeight;
    }
    if (s.albumCardSize >= 60.0f && s.albumCardSize <= 300.0f) {
        m_albumCardSize = s.albumCardSize;
    }
    if (s.rightPanelArtSize >= 60.0f && s.rightPanelArtSize <= 400.0f) {
        m_rightPanelArtSize = s.rightPanelArtSize;
    }
    m_skipDislikedOnAutoplay = s.skipDislikedOnAutoplay;
    m_autoSkipDislikedOnMark = s.autoSkipDislikedOnMark;

    // Right panel tab & lyrics
    if (s.rightPanelTab >= 0 && s.rightPanelTab <= static_cast<int>(RightPanelTab::Lyrics)) {
        m_rightPanelTab = static_cast<RightPanelTab>(s.rightPanelTab);
    }
    if (s.lyricsScale >= 0.5f && s.lyricsScale <= 3.0f) {
        m_lyricsScale = s.lyricsScale;
    }

    // UI Features
    m_useWavebar = s.useWavebar;
    if (s.visualizerMode >= 0 && s.visualizerMode <= static_cast<int>(VisualizerMode::Oscilloscope)) {
        m_visualizerMode = static_cast<VisualizerMode>(s.visualizerMode);
    }
    m_showLeftNavigator = s.showLeftNavigator;
    m_showRightSidebar = s.showRightSidebar;
    m_showRightTrackInfo = s.showRightTrackInfo;
    m_lockPanels = s.lockPanels;
    if (s.rightBottomView >= 0 && s.rightBottomView <= 2) {
        m_rightBottomView = static_cast<RightBottomView>(s.rightBottomView);
    }
    m_showJumpbar = s.showJumpbar;
    m_showVisualizer = s.showVisualizer;
    m_showStatusBar = s.showStatusBar;
    m_showColumnBrowser = s.showColumnBrowser;
    if (s.columnBrowserHeight >= 60.0f && s.columnBrowserHeight <= 300.0f) {
        m_columnBrowserHeight = s.columnBrowserHeight;
    }
    m_cbSelectedGenre = s.cbSelectedGenre;
    m_cbSelectedArtist = s.cbSelectedArtist;
    m_cbSelectedAlbum = s.cbSelectedAlbum;

    m_stopAfterCurrent = s.stopAfterCurrent;
    m_showRemainingTime = s.showRemainingTime;

    // Mini Player Settings
    if (s.miniPlayerMode >= 0 && s.miniPlayerMode <= static_cast<int>(MiniPlayerMode::PiP)) {
        m_miniPlayerMode = static_cast<MiniPlayerMode>(s.miniPlayerMode);
    }
    m_miniPlayerAlwaysOnTop = s.miniPlayerAlwaysOnTop;
    m_miniPlayerOpacity = (s.miniPlayerOpacity >= 0.2f && s.miniPlayerOpacity <= 1.0f) ? s.miniPlayerOpacity : 1.0f;
    m_miniPlayerShowQueue = s.miniPlayerShowQueue;
    m_miniPlayerShowLyrics = s.miniPlayerShowLyrics;
    m_miniPlayerVinylStyle = s.miniPlayerVinylStyle;

    // Theme & Expanded Card
    Theme::setUseLightExpandedCard(s.useLightExpandedCard);

    // Queue persistence: validate saved queue tracks against active library
    if (!s.queue.empty()) {
        std::vector<uint64_t> validQueue;
        for (uint64_t tid : s.queue) {
            if (m_library.getTrackById(tid)) {
                validQueue.push_back(tid);
            }
        }
        if (!validQueue.empty()) {
            m_queue = validQueue;
            m_queueIndex = (s.queueIndex < m_queue.size()) ? s.queueIndex : 0;
            if (!m_queue.empty()) {
                m_currentTrackId = m_queue[m_queueIndex];
                m_selectedTrackId = m_currentTrackId;
            }
        }
    }

    // Last track & position
    if (s.lastTrackId > 0 && m_library.getTrackById(s.lastTrackId)) {
        m_currentTrackId = s.lastTrackId;
        m_selectedTrackId = s.lastTrackId;
    } else if (!s.lastTrackPath.empty()) {
        for (const auto& t : m_library.getTracks()) {
            if (t.filePath == s.lastTrackPath) {
                m_currentTrackId = t.id;
                m_selectedTrackId = t.id;
                break;
            }
        }
    }
}

void MainWindow::savePreferences() {
    auto& s = SettingsManager::instance().settings();

#if defined(_WIN32)
    if (m_hWnd) {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(static_cast<HWND>(m_hWnd), &wp)) {
            s.windowMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
            if (!s.windowMaximized) {
                s.windowX = wp.rcNormalPosition.left;
                s.windowY = wp.rcNormalPosition.top;
                s.windowWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
                s.windowHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            }
        }
    }
#endif

    s.viewMode = static_cast<int>(m_viewMode);
    s.navSource = static_cast<int>(m_navSource);
    s.leftPanelWidth = m_leftPanelWidth;
    s.rightPanelWidth = m_rightPanelWidth;
    s.rightQueueSplitRatio = m_rightQueueSplitRatio;
    s.bottomBarHeight = m_bottomBarHeight;
    s.albumCardSize = m_albumCardSize;
    s.rightPanelArtSize = m_rightPanelArtSize;
    s.skipDislikedOnAutoplay = m_skipDislikedOnAutoplay;
    s.autoSkipDislikedOnMark = m_autoSkipDislikedOnMark;
    s.rightPanelTab = static_cast<int>(m_rightPanelTab);
    s.lyricsScale = m_lyricsScale;
    s.useWavebar = m_useWavebar;
    s.visualizerMode = static_cast<int>(m_visualizerMode);
    s.showLeftNavigator = m_showLeftNavigator;
    s.showRightSidebar = m_showRightSidebar;
    s.showRightTrackInfo = m_showRightTrackInfo;
    s.lockPanels = m_lockPanels;
    s.rightBottomView = static_cast<int>(m_rightBottomView);
    s.showJumpbar = m_showJumpbar;
    s.showVisualizer = m_showVisualizer;
    s.showStatusBar = m_showStatusBar;
    s.showColumnBrowser = m_showColumnBrowser;
    s.columnBrowserHeight = m_columnBrowserHeight;
    s.cbSelectedGenre = m_cbSelectedGenre;
    s.cbSelectedArtist = m_cbSelectedArtist;
    s.cbSelectedAlbum = m_cbSelectedAlbum;
    s.miniPlayerMode = static_cast<int>(m_miniPlayerMode);
    s.miniPlayerAlwaysOnTop = m_miniPlayerAlwaysOnTop;
    s.miniPlayerOpacity = m_miniPlayerOpacity;
    s.miniPlayerShowQueue = m_miniPlayerShowQueue;
    s.miniPlayerShowLyrics = m_miniPlayerShowLyrics;
    s.miniPlayerVinylStyle = m_miniPlayerVinylStyle;

    s.stopAfterCurrent = m_stopAfterCurrent;
    s.showRemainingTime = m_showRemainingTime;
    s.lastTrackId = m_currentTrackId;
    const Track* curTr = m_library.getTrackById(m_currentTrackId);
    if (curTr) {
        s.lastTrackPath = curTr->filePath;
    }
    s.lastPositionSeconds = m_audio.getCurrentTime();
    s.queue = m_queue;
    s.queueIndex = m_queueIndex;

    s.theme = static_cast<int>(Theme::getCurrentTheme());
    s.useLightExpandedCard = Theme::useLightExpandedCard();

    // Sync audio engine & EQ
    m_audio.saveAudioConfig();
    SettingsManager::instance().save();
}

void MainWindow::playQueueIndex(size_t index) {
    if (index >= m_queue.size()) return;
    m_queueIndex = index;
    uint64_t trackId = m_queue[index];
    Track* t = m_library.getTrackById(trackId);
    if (!t) return;

    if (m_audio.isShuffle()) {
        rebuildShuffleOrder(true);
    }

    m_currentTrackId = trackId;
    m_selectedTrackId = trackId;

    if (m_audio.loadAndPlay(t->filePath)) {
        m_library.recordTrackPlay(trackId);
        m_library.addHistoryEntry(trackId);
        m_scrobbler.onTrackStarted(*t);
        requestWaveform(t->filePath);

        // Extract album artwork for SMTC thumbnail
        std::string coverArtFile;
        if (!t->albumArtPath.empty() && fs::exists(t->albumArtPath)) {
            coverArtFile = t->albumArtPath;
        } else {
            std::vector<uint8_t> artBytes;
            if (TagReader::extractAlbumArt(*t, artBytes) && !artBytes.empty()) {
                std::error_code ec;
                std::string tempPath = (fs::temp_directory_path(ec) / "sloth_current_art.jpg").string();
                if (!ec) {
                    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                    if (out.is_open()) {
                        out.write(reinterpret_cast<const char*>(artBytes.data()), artBytes.size());
                        out.close();
                        coverArtFile = tempPath;
                    }
                }
            }
        }

        SMTCManager::instance().updateTrack(t->getDisplayTitle(), t->getDisplayArtist(), t->getDisplayAlbum(), coverArtFile);
        SMTCManager::instance().setPlaybackState(true, false);

        double bookmarkTime = m_library.getTrackBookmark(trackId);
        if (bookmarkTime > 1.0) {
            m_audio.seekTo(bookmarkTime);
        }
        savePreferences();
    }
}

void MainWindow::removeQueueIndex(size_t index) {
    if (index >= m_queue.size()) return;
    m_queue.erase(m_queue.begin() + index);
    if (m_queue.empty()) {
        m_queueIndex = 0;
        m_selectedQueueIndex = -1;
    } else if (index < m_queueIndex) {
        m_queueIndex--;
    } else if (m_queueIndex >= m_queue.size()) {
        m_queueIndex = m_queue.size() - 1;
    }

    if (m_selectedQueueIndex >= 0) {
        if (static_cast<size_t>(m_selectedQueueIndex) == index) {
            if (m_selectedQueueIndex >= static_cast<int>(m_queue.size())) {
                m_selectedQueueIndex = static_cast<int>(m_queue.size()) - 1;
            }
        } else if (static_cast<size_t>(m_selectedQueueIndex) > index) {
            m_selectedQueueIndex--;
        }
    }

    if (m_audio.isShuffle()) {
        rebuildShuffleOrder(true);
    }
    savePreferences();
}

void MainWindow::removeQueueIndices(const std::set<size_t>& indices) {
    if (indices.empty() || m_queue.empty()) return;

    size_t removedBeforeCurrent = 0;

    // Erase in reverse index order to preserve validity of preceding indices
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        size_t idx = *it;
        if (idx < m_queue.size()) {
            if (idx < m_queueIndex) {
                removedBeforeCurrent++;
            }
            m_queue.erase(m_queue.begin() + idx);
        }
    }

    if (m_queue.empty()) {
        m_queueIndex = 0;
        m_selectedQueueIndex = -1;
    } else {
        if (m_queueIndex >= removedBeforeCurrent) {
            m_queueIndex -= removedBeforeCurrent;
        } else {
            m_queueIndex = 0;
        }
        if (m_queueIndex >= m_queue.size()) {
            m_queueIndex = m_queue.size() - 1;
        }
    }

    m_selectedQueueIndices.clear();
    m_selectedQueueIndex = -1;

    if (m_audio.isShuffle()) {
        rebuildShuffleOrder(true);
    }
    savePreferences();
}

void MainWindow::playTrack(uint64_t trackId, const std::vector<uint64_t>& contextQueue) {
    Track* t = m_library.getTrackById(trackId);
    if (!t) return;

    if (!contextQueue.empty()) {
        m_queue = contextQueue;
        for (size_t i = 0; i < m_queue.size(); ++i) {
            if (m_queue[i] == trackId) {
                m_queueIndex = i;
                break;
            }
        }
    } else {
        if (m_queueIndex < m_queue.size() && m_queue[m_queueIndex] == trackId) {
            // Already matched active queue position
        } else {
            bool found = false;
            for (size_t i = 0; i < m_queue.size(); ++i) {
                if (m_queue[i] == trackId) {
                    m_queueIndex = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_queue.push_back(trackId);
                m_queueIndex = m_queue.size() - 1;
            }
        }
    }

    if (m_audio.isShuffle()) {
        rebuildShuffleOrder(true);
    }

    m_currentTrackId = trackId;
    m_selectedTrackId = trackId;

    if (m_audio.loadAndPlay(t->filePath)) {
        m_library.recordTrackPlay(trackId);
        m_library.addHistoryEntry(trackId);
        m_scrobbler.onTrackStarted(*t);
        requestWaveform(t->filePath);

        // Extract album artwork for SMTC thumbnail
        std::string coverArtFile;
        if (!t->albumArtPath.empty() && fs::exists(t->albumArtPath)) {
            coverArtFile = t->albumArtPath;
        } else {
            std::vector<uint8_t> artBytes;
            if (TagReader::extractAlbumArt(*t, artBytes) && !artBytes.empty()) {
                std::error_code ec;
                std::string tempPath = (fs::temp_directory_path(ec) / "sloth_current_art.jpg").string();
                if (!ec) {
                    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                    if (out.is_open()) {
                        out.write(reinterpret_cast<const char*>(artBytes.data()), artBytes.size());
                        out.close();
                        coverArtFile = tempPath;
                    }
                }
            }
        }

        SMTCManager::instance().updateTrack(t->getDisplayTitle(), t->getDisplayArtist(), t->getDisplayAlbum(), coverArtFile);
        SMTCManager::instance().setPlaybackState(true, false);

        double bookmarkTime = m_library.getTrackBookmark(trackId);
        if (bookmarkTime > 1.0) {
            m_audio.seekTo(bookmarkTime);
        }
        savePreferences();
    }
}

void MainWindow::togglePlayPause() {
    if (m_audio.isPlaying()) {
        m_audio.togglePlayPause();
        SMTCManager::instance().setPlaybackState(m_audio.isPlaying(), m_audio.isPaused());
    } else {
        play();
    }
}

void MainWindow::play() {
    if (m_audio.isPaused()) {
        m_audio.resume();
        SMTCManager::instance().setPlaybackState(true, false);
    } else if (!m_audio.isPlaying()) {
        uint64_t targetId = m_currentTrackId;
        if (targetId == 0 && m_selectedTrackId != 0) targetId = m_selectedTrackId;
        if (targetId == 0 && !m_queue.empty()) {
            for (uint64_t qId : m_queue) {
                const Track* trk = m_library.getTrackById(qId);
                if (trk && (!m_skipDislikedOnAutoplay || !trk->isDisliked)) {
                    targetId = qId;
                    break;
                }
            }
            if (targetId == 0) targetId = m_queue[0];
        }
        if (targetId == 0 && !m_library.getTracks().empty()) {
            for (const auto& trk : m_library.getTracks()) {
                if (!m_skipDislikedOnAutoplay || !trk.isDisliked) {
                    targetId = trk.id;
                    break;
                }
            }
            if (targetId == 0) targetId = m_library.getTracks()[0].id;
        }
        if (targetId != 0) {
            playTrack(targetId);
        }
    }
}

void MainWindow::pause() {
    if (m_audio.isPlaying()) {
        m_audio.pause();
        SMTCManager::instance().setPlaybackState(false, true);
    }
}

void MainWindow::stop() {
    m_audio.stop();
    SMTCManager::instance().setPlaybackState(false, false);
}

void MainWindow::triggerMediaAction(MediaAction action) {
    m_pendingMediaAction.store(static_cast<uint32_t>(action));
#if defined(_WIN32)
    if (m_hWnd) {
        PostMessage(static_cast<HWND>(m_hWnd), WM_NULL, 0, 0);
    }
#endif
}

bool MainWindow::isShuffleOrderValid() const {
    if (m_shuffleOrder.empty() || m_queue.empty()) return false;
    for (size_t idx : m_shuffleOrder) {
        if (idx >= m_queue.size()) return false;
        const Track* cand = m_library.getTrackById(m_queue[idx]);
        if (!cand || (m_skipDislikedOnAutoplay && cand->isDisliked)) return false;
    }
    return true;
}

void MainWindow::rebuildShuffleOrder(bool keepCurrentFirst) {
    m_shuffleOrder.clear();
    m_shuffleOrderPos = 0;
    if (m_queue.empty()) return;

    bool skipDisliked = m_skipDislikedOnAutoplay;
    std::vector<size_t> validIndices;
    validIndices.reserve(m_queue.size());

    for (size_t i = 0; i < m_queue.size(); ++i) {
        const Track* cand = m_library.getTrackById(m_queue[i]);
        if (cand && (!skipDisliked || !cand->isDisliked)) {
            validIndices.push_back(i);
        }
    }

    if (validIndices.empty()) return;

    std::random_device rd;
    std::mt19937 g(rd());

    if (keepCurrentFirst && m_queueIndex < m_queue.size()) {
        size_t curIdx = m_queueIndex;
        std::vector<size_t> others;
        others.reserve(validIndices.size());
        for (size_t idx : validIndices) {
            if (idx != curIdx) {
                others.push_back(idx);
            }
        }
        std::shuffle(others.begin(), others.end(), g);

        m_shuffleOrder.push_back(curIdx);
        for (size_t idx : others) {
            m_shuffleOrder.push_back(idx);
        }

        // Guarantee no back-to-back same song (even if duplicated in queue)
        uint64_t curTrackId = m_queue[curIdx];
        if (m_shuffleOrder.size() > 2 && m_queue[m_shuffleOrder[1]] == curTrackId) {
            for (size_t k = 2; k < m_shuffleOrder.size(); ++k) {
                if (m_queue[m_shuffleOrder[k]] != curTrackId) {
                    std::swap(m_shuffleOrder[1], m_shuffleOrder[k]);
                    break;
                }
            }
        }
        m_shuffleOrderPos = 0;
    } else {
        std::shuffle(validIndices.begin(), validIndices.end(), g);
        m_shuffleOrder = std::move(validIndices);

        // Disperse duplicate tracks so they don't appear back-to-back
        for (size_t i = 1; i < m_shuffleOrder.size(); ++i) {
            if (m_queue[m_shuffleOrder[i]] == m_queue[m_shuffleOrder[i - 1]]) {
                for (size_t j = i + 1; j < m_shuffleOrder.size(); ++j) {
                    if (m_queue[m_shuffleOrder[j]] != m_queue[m_shuffleOrder[i - 1]]) {
                        std::swap(m_shuffleOrder[i], m_shuffleOrder[j]);
                        break;
                    }
                }
            }
        }
        m_shuffleOrderPos = 0;
    }
}

void MainWindow::playNext() {
    if (m_library.getAutoDJConfig().enabled) {
        // Auto-DJ: If near end of queue, append next smart track
        if (m_queueIndex + 1 >= m_queue.size() || m_queue.empty()) {
            uint64_t djId = m_library.selectAutoDJTrack(m_queue);
            if (djId > 0) {
                m_queue.push_back(djId);
                if (m_audio.isShuffle()) {
                    m_shuffleOrder.push_back(m_queue.size() - 1);
                }
            }
        }
    }

    if (m_queue.empty()) {
        stop();
        return;
    }

    bool skipDisliked = m_skipDislikedOnAutoplay;

    if (m_audio.isShuffle()) {
        // Build or validate shuffle deck
        if (!isShuffleOrderValid()) {
            rebuildShuffleOrder(true);
        }

        if (m_shuffleOrder.empty()) {
            stop();
            return;
        }

        bool found = false;
        while (m_shuffleOrderPos + 1 < m_shuffleOrder.size()) {
            m_shuffleOrderPos++;
            size_t qIdx = m_shuffleOrder[m_shuffleOrderPos];
            if (qIdx < m_queue.size()) {
                const Track* cand = m_library.getTrackById(m_queue[qIdx]);
                if (cand && (!skipDisliked || !cand->isDisliked)) {
                    m_queueIndex = qIdx;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            // Reached the end of the shuffled deck
            if (m_audio.getRepeatMode() == RepeatMode::All) {
                // Reshuffle for the next complete round
                size_t lastQIdx = m_shuffleOrder.back();
                uint64_t lastTrackId = (lastQIdx < m_queue.size()) ? m_queue[lastQIdx] : 0;

                rebuildShuffleOrder(false);

                // Ensure the first track of the new round is NOT the same song as the last track
                if (m_shuffleOrder.size() > 1 && m_queue[m_shuffleOrder[0]] == lastTrackId) {
                    for (size_t k = 1; k < m_shuffleOrder.size(); ++k) {
                        if (m_queue[m_shuffleOrder[k]] != lastTrackId) {
                            std::swap(m_shuffleOrder[0], m_shuffleOrder[k]);
                            break;
                        }
                    }
                }
                m_shuffleOrderPos = 0;
                for (size_t k = 0; k < m_shuffleOrder.size(); ++k) {
                    size_t qIdx = m_shuffleOrder[k];
                    if (qIdx < m_queue.size()) {
                        const Track* cand = m_library.getTrackById(m_queue[qIdx]);
                        if (cand && (!skipDisliked || !cand->isDisliked)) {
                            m_shuffleOrderPos = k;
                            m_queueIndex = qIdx;
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    stop();
                    return;
                }
            } else {
                // End of queue in Shuffle mode without Repeat All
                stop();
                return;
            }
        }
    } else {
        size_t nextIdx = m_queueIndex + 1;
        bool found = false;
        size_t searchCount = 0;
        size_t qSize = m_queue.size();

        while (searchCount < qSize) {
            if (nextIdx >= qSize) {
                if (m_audio.getRepeatMode() == RepeatMode::All) {
                    nextIdx = 0;
                } else {
                    break;
                }
            }
            if (nextIdx == m_queueIndex && searchCount > 0) {
                break;
            }

            const Track* cand = m_library.getTrackById(m_queue[nextIdx]);
            if (cand && (!skipDisliked || !cand->isDisliked)) {
                m_queueIndex = nextIdx;
                found = true;
                break;
            }
            nextIdx++;
            searchCount++;
        }

        if (!found) {
            stop();
            return;
        }
    }

    if (m_queueIndex >= m_queue.size()) {
        m_queueIndex = 0;
    }

    uint64_t nextId = m_queue[m_queueIndex];
    Track* t = m_library.getTrackById(nextId);
    if (t) {
        m_currentTrackId = nextId;
        m_selectedTrackId = nextId;
        if (m_audio.loadAndPlay(t->filePath)) {
            m_library.recordTrackPlay(nextId);
            m_library.addHistoryEntry(nextId);
            m_scrobbler.onTrackStarted(*t);
            requestWaveform(t->filePath);

            std::string coverArtFile;
            if (!t->albumArtPath.empty() && fs::exists(t->albumArtPath)) {
                coverArtFile = t->albumArtPath;
            } else {
                std::vector<uint8_t> artBytes;
                if (TagReader::extractAlbumArt(*t, artBytes) && !artBytes.empty()) {
                    std::error_code ec;
                    std::string tempPath = (fs::temp_directory_path(ec) / "sloth_current_art.jpg").string();
                    if (!ec) {
                        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                        if (out.is_open()) {
                            out.write(reinterpret_cast<const char*>(artBytes.data()), artBytes.size());
                            out.close();
                            coverArtFile = tempPath;
                        }
                    }
                }
            }

            SMTCManager::instance().updateTrack(t->getDisplayTitle(), t->getDisplayArtist(), t->getDisplayAlbum(), coverArtFile);
            SMTCManager::instance().setPlaybackState(true, false);

            double bookmarkTime = m_library.getTrackBookmark(nextId);
            if (bookmarkTime > 1.0) {
                m_audio.seekTo(bookmarkTime);
            }
            savePreferences();
        }
    }
}

void MainWindow::playPrevious() {
    if (m_queue.empty()) return;

    if (m_audio.getCurrentTime() > 3.0) {
        m_audio.seekTo(0.0);
        return;
    }

    bool skipDisliked = m_skipDislikedOnAutoplay;

    if (m_audio.isShuffle()) {
        if (!isShuffleOrderValid()) {
            rebuildShuffleOrder(true);
        }

        if (m_shuffleOrderPos > 0 && m_shuffleOrderPos < m_shuffleOrder.size()) {
            m_shuffleOrderPos--;
            m_queueIndex = m_shuffleOrder[m_shuffleOrderPos];
        } else if (m_audio.getRepeatMode() == RepeatMode::All && !m_shuffleOrder.empty()) {
            m_shuffleOrderPos = m_shuffleOrder.size() - 1;
            m_queueIndex = m_shuffleOrder[m_shuffleOrderPos];
        } else {
            m_audio.seekTo(0.0);
            return;
        }
    } else {
        size_t qSize = m_queue.size();
        size_t prevIdx = (m_queueIndex > 0) ? (m_queueIndex - 1) : (m_audio.getRepeatMode() == RepeatMode::All ? (qSize - 1) : 0);
        bool found = false;
        size_t searchCount = 0;

        while (searchCount < qSize) {
            const Track* cand = m_library.getTrackById(m_queue[prevIdx]);
            if (cand && (!skipDisliked || !cand->isDisliked)) {
                m_queueIndex = prevIdx;
                found = true;
                break;
            }
            if (prevIdx == 0) {
                if (m_audio.getRepeatMode() == RepeatMode::All) {
                    prevIdx = qSize - 1;
                } else {
                    break;
                }
            } else {
                prevIdx--;
            }
            searchCount++;
        }

        if (!found) {
            m_queueIndex = (m_queueIndex > 0 && m_queueIndex < m_queue.size()) ? (m_queueIndex - 1) : 0;
        }
    }

    if (m_queueIndex >= m_queue.size()) {
        m_queueIndex = m_queue.size() - 1;
    }

    uint64_t prevId = m_queue[m_queueIndex];
    Track* t = m_library.getTrackById(prevId);
    if (t) {
        m_currentTrackId = prevId;
        m_selectedTrackId = prevId;
        m_audio.loadAndPlay(t->filePath);
        m_library.recordTrackPlay(prevId);
        m_library.addHistoryEntry(prevId);
        m_scrobbler.onTrackStarted(*t);
        requestWaveform(t->filePath);
        std::string coverArtFile = t->albumArtPath;
        SMTCManager::instance().updateTrack(t->getDisplayTitle(), t->getDisplayArtist(), t->getDisplayAlbum(), coverArtFile);
        SMTCManager::instance().setPlaybackState(true, false);
        savePreferences();
    }
}

void MainWindow::openSystemFolderDialog() {
    std::string folder = Platform::openFolderDialog("Select Music Directory to Add to SlothPlayer");
    if (!folder.empty()) {
        m_library.addMonitoredFolder(folder);
        m_library.scanDirectories({folder});
    }
}

void MainWindow::openSystemFileDialog() {
    std::string file = Platform::openFileDialog("Audio Files (*.mp3;*.flac;*.wav;*.ogg;*.m4a;*.aac)", "*.mp3;*.flac;*.wav;*.ogg;*.m4a;*.aac");
    if (!file.empty()) {
        handleDroppedFiles({file});
    }
}

void MainWindow::loadDemoAudio() {
    std::string sample = "D:\\Programming\\VIDPLAYER\\test_51_ident.flac";
    if (fs::exists(sample)) {
        handleDroppedFiles({sample});
    }
}

void MainWindow::handleDroppedFiles(const std::vector<std::string>& files) {
    for (const auto& path : files) {
        if (fs::is_directory(path)) {
            m_library.addMonitoredFolder(path);
            m_library.scanDirectories({path});
        } else if (fs::is_regular_file(path)) {
            uint64_t foundId = 0;
            for (const auto& t : m_library.getTracks()) {
                if (t.filePath == path) {
                    foundId = t.id;
                    break;
                }
            }
            if (foundId == 0) {
                Track t;
                if (TagReader::readMetadata(path, t)) {
                    static uint64_t manualId = 10000;
                    t.id = manualId++;
                    playTrack(t.id);
                    m_library.addMonitoredFolder(fs::path(path).parent_path().string());
                    m_library.scanDirectories({fs::path(path).parent_path().string()});
                    return;
                }
            } else {
                playTrack(foundId);
                return;
            }
        }
    }
}

// ----------------- Monochromatic Silver Vector Controls (SlothPlayer Style) -----------------

void MainWindow::drawVectorPlayPause(ImDrawList* dl, ImVec2 center, float radius, bool isPlaying, bool isHovered, bool isActive) {
    (void)isHovered;
    (void)isActive;
    // High contrast crisp dark glyph inside the vibrant accent circle
    ImU32 iconCol = IM_COL32(14, 16, 20, 255);

    if (!isPlaying) {
        float halfW = radius * 0.42f;
        float halfH = radius * 0.52f;
        float nudge = radius * 0.09f;
        ImVec2 p1 = ImVec2(center.x - halfW + nudge, center.y - halfH);
        ImVec2 p2 = ImVec2(center.x + halfW + nudge, center.y);
        ImVec2 p3 = ImVec2(center.x - halfW + nudge, center.y + halfH);
        dl->AddTriangleFilled(p1, p2, p3, iconCol);
    } else {
        float barW = radius * 0.22f;
        float barH = radius * 0.52f;
        float gap = radius * 0.14f;
        dl->AddRectFilled(ImVec2(center.x - gap - barW, center.y - barH), ImVec2(center.x - gap, center.y + barH), iconCol, 1.2f);
        dl->AddRectFilled(ImVec2(center.x + gap, center.y - barH), ImVec2(center.x + gap + barW, center.y + barH), iconCol, 1.2f);
    }
}

void MainWindow::drawVectorPrevNext(ImDrawList* dl, ImVec2 center, float size, bool isNext, bool isHovered, bool isActive) {
    if (isHovered || isActive) {
        dl->AddCircleFilled(center, 14.0f, IM_COL32(255, 255, 255, isActive ? 35 : 18));
    }
    ImU32 iconCol = isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(175, 180, 192, 255);
    float s = size * 0.58f;

    if (isNext) {
        ImVec2 p1 = ImVec2(center.x - s * 0.65f, center.y - s * 0.85f);
        ImVec2 p2 = ImVec2(center.x + s * 0.25f, center.y);
        ImVec2 p3 = ImVec2(center.x - s * 0.65f, center.y + s * 0.85f);
        dl->AddTriangleFilled(p1, p2, p3, iconCol);
        dl->AddRectFilled(ImVec2(center.x + s * 0.38f, center.y - s * 0.85f), ImVec2(center.x + s * 0.68f, center.y + s * 0.85f), iconCol, 1.0f);
    } else {
        dl->AddRectFilled(ImVec2(center.x - s * 0.68f, center.y - s * 0.85f), ImVec2(center.x - s * 0.38f, center.y + s * 0.85f), iconCol, 1.0f);
        ImVec2 p1 = ImVec2(center.x + s * 0.65f, center.y - s * 0.85f);
        ImVec2 p2 = ImVec2(center.x - s * 0.25f, center.y);
        ImVec2 p3 = ImVec2(center.x + s * 0.65f, center.y + s * 0.85f);
        dl->AddTriangleFilled(p1, p2, p3, iconCol);
    }
}

static inline ImU32 getVectorActiveBg(bool isHovered) {
    ImVec4 col = Theme::AccentColor();
    col.w = isHovered ? 0.28f : 0.16f;
    return ImGui::GetColorU32(col);
}

void MainWindow::drawVectorStop(ImDrawList* dl, ImVec2 center, float size, bool isHovered, bool isActive) {
    if (isHovered || isActive) {
        dl->AddCircleFilled(center, 13.0f, isActive ? getVectorActiveBg(isHovered) : IM_COL32(255, 255, 255, 18));
    }
    ImU32 iconCol = isActive ? ImGui::GetColorU32(Theme::AccentColor())
                             : (isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(155, 162, 175, 255));
    float s = size * 0.36f;
    dl->AddRectFilled(ImVec2(center.x - s, center.y - s), ImVec2(center.x + s, center.y + s), iconCol, 1.8f);
}

void MainWindow::drawVectorShuffle(ImDrawList* dl, ImVec2 center, float size, bool active, bool isHovered, bool isActive) {
    (void)isActive;
    if (isHovered || active) {
        dl->AddCircleFilled(center, 13.0f, active ? getVectorActiveBg(isHovered) : IM_COL32(255, 255, 255, 18));
    }
    ImU32 iconCol = active ? ImGui::GetColorU32(Theme::AccentColor())
                           : (isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(148, 155, 168, 255));
    float s = size * 0.44f;
    float yTop = center.y - s * 0.52f;
    float yBot = center.y + s * 0.52f;
    float xL = center.x - s;
    float xR = center.x + s * 0.42f;
    float xTip = center.x + s * 0.95f;

    // Top-left curving down to bottom-right
    dl->AddBezierCubic(ImVec2(xL, yTop), ImVec2(center.x - s * 0.22f, yTop),
                       ImVec2(center.x + s * 0.18f, yBot), ImVec2(xR, yBot), iconCol, 1.7f);
    dl->AddTriangleFilled(ImVec2(xTip, yBot), ImVec2(xR - 0.5f, yBot - 3.2f), ImVec2(xR - 0.5f, yBot + 3.2f), iconCol);

    // Bottom-left curving up with depth gap
    dl->AddBezierCubic(ImVec2(xL, yBot), ImVec2(center.x - s * 0.35f, yBot),
                       ImVec2(center.x - s * 0.12f, center.y + s * 0.16f), ImVec2(center.x - s * 0.04f, center.y + s * 0.08f), iconCol, 1.7f);
    dl->AddBezierCubic(ImVec2(center.x + s * 0.08f, center.y - s * 0.10f), ImVec2(center.x + s * 0.20f, yTop),
                       ImVec2(center.x + s * 0.30f, yTop), ImVec2(xR, yTop), iconCol, 1.7f);
    dl->AddTriangleFilled(ImVec2(xTip, yTop), ImVec2(xR - 0.5f, yTop - 3.2f), ImVec2(xR - 0.5f, yTop + 3.2f), iconCol);
}

void MainWindow::drawVectorRepeat(ImDrawList* dl, ImVec2 center, float size, RepeatMode mode, bool isHovered, bool isActive) {
    (void)isActive;
    bool active = (mode != RepeatMode::Off);
    if (isHovered || active) {
        dl->AddCircleFilled(center, 13.0f, active ? getVectorActiveBg(isHovered) : IM_COL32(255, 255, 255, 18));
    }
    ImU32 iconCol = active ? ImGui::GetColorU32(Theme::AccentColor())
                           : (isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(148, 155, 168, 255));
    float r = size * 0.37f;

    // Smooth circular arc (~285 degrees)
    dl->PathArcTo(center, r, -2.5f, 1.85f, 24);
    dl->PathStroke(iconCol, 0, 1.7f);

    // Arrow head at arc end pointing along tangent
    float tipAngle = 1.85f;
    ImVec2 tip(center.x + std::cos(tipAngle) * r, center.y + std::sin(tipAngle) * r);
    ImVec2 a1(tip.x + 4.2f, tip.y - 3.4f);
    ImVec2 a2(tip.x - 1.2f, tip.y - 4.2f);
    dl->AddTriangleFilled(tip, a1, a2, iconCol);

    if (mode == RepeatMode::One) {
        ImVec2 textSz = ImGui::CalcTextSize("1");
        dl->AddText(ImVec2(center.x - textSz.x * 0.5f, center.y - textSz.y * 0.5f - 0.5f), iconCol, "1");
    }
}

void MainWindow::drawVectorSpeaker(ImDrawList* dl, ImVec2 center, float size, float volume, bool muted, bool isHovered) {
    ImU32 iconCol = isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(175, 182, 195, 255);
    float s = size * 0.45f;

    // Speaker box & cone
    ImVec2 b0(center.x - s * 0.90f, center.y - s * 0.35f);
    ImVec2 b1(center.x - s * 0.35f, center.y + s * 0.35f);
    dl->AddRectFilled(b0, b1, iconCol, 1.0f);

    ImVec2 p1(center.x - s * 0.35f, center.y - s * 0.35f);
    ImVec2 p2(center.x + s * 0.20f, center.y - s * 0.85f);
    ImVec2 p3(center.x + s * 0.20f, center.y + s * 0.85f);
    ImVec2 p4(center.x - s * 0.35f, center.y + s * 0.35f);
    dl->AddQuadFilled(p1, p2, p3, p4, iconCol);

    if (muted || volume <= 0.01f) {
        ImU32 muteCol = IM_COL32(245, 60, 80, 255);
        dl->AddLine(ImVec2(center.x + s * 0.40f, center.y - s * 0.55f), ImVec2(center.x + s * 0.95f, center.y + s * 0.55f), muteCol, 2.0f);
        dl->AddLine(ImVec2(center.x + s * 0.95f, center.y - s * 0.55f), ImVec2(center.x + s * 0.40f, center.y + s * 0.55f), muteCol, 2.0f);
    } else {
        float arcX = center.x + s * 0.25f;
        dl->PathArcTo(ImVec2(arcX, center.y), s * 0.35f, -0.7f, 0.7f, 6);
        dl->PathStroke(iconCol, 0, 1.4f);
        if (volume > 0.30f) {
            dl->PathArcTo(ImVec2(arcX, center.y), s * 0.65f, -0.75f, 0.75f, 8);
            dl->PathStroke(iconCol, 0, 1.4f);
        }
        if (volume > 0.70f) {
            dl->PathArcTo(ImVec2(arcX, center.y), s * 0.95f, -0.80f, 0.80f, 10);
            dl->PathStroke(iconCol, 0, 1.4f);
        }
    }
}

void MainWindow::drawVectorEQIcon(ImDrawList* dl, ImVec2 center, float size, bool active, bool isHovered) {
    if (isHovered || active) {
        dl->AddCircleFilled(center, 13.0f, active ? getVectorActiveBg(isHovered) : IM_COL32(255, 255, 255, 18));
    }
    ImU32 iconCol = active ? ImGui::GetColorU32(Theme::AccentColor())
                           : (isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(148, 155, 168, 255));
    float s = size * 0.44f;
    float x1 = center.x - s * 0.68f;
    float x2 = center.x;
    float x3 = center.x + s * 0.68f;

    dl->AddLine(ImVec2(x1, center.y - s), ImVec2(x1, center.y + s), iconCol, 1.4f);
    dl->AddLine(ImVec2(x2, center.y - s), ImVec2(x2, center.y + s), iconCol, 1.4f);
    dl->AddLine(ImVec2(x3, center.y - s), ImVec2(x3, center.y + s), iconCol, 1.4f);

    float kw = 3.2f, kh = 2.0f;
    float yKnob1 = center.y + s * 0.30f;
    float yKnob2 = center.y - s * 0.35f;
    float yKnob3 = center.y + s * 0.45f;

    dl->AddRectFilled(ImVec2(x1 - kw, yKnob1 - kh), ImVec2(x1 + kw, yKnob1 + kh), iconCol, 1.2f);
    dl->AddRectFilled(ImVec2(x2 - kw, yKnob2 - kh), ImVec2(x2 + kw, yKnob2 + kh), iconCol, 1.2f);
    dl->AddRectFilled(ImVec2(x3 - kw, yKnob3 - kh), ImVec2(x3 + kw, yKnob3 + kh), iconCol, 1.2f);
}

void MainWindow::drawVectorWavebar(ImDrawList* dl, ImVec2 center, float size, bool active, bool isHovered) {
    if (isHovered || active) {
        dl->AddCircleFilled(center, 13.0f, active ? getVectorActiveBg(isHovered) : IM_COL32(255, 255, 255, 18));
    }
    ImU32 iconCol = active ? ImGui::GetColorU32(Theme::AccentColor())
                           : (isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(148, 155, 168, 255));
    float s = size * 0.44f;
    const float heights[5] = { 0.35f, 0.70f, 1.00f, 0.65f, 0.40f };
    const float xOffsets[5] = { -s * 0.80f, -s * 0.40f, 0.0f, s * 0.40f, s * 0.80f };
    float barHalfW = 1.2f;

    for (int i = 0; i < 5; ++i) {
        float h = s * heights[i];
        float bx = center.x + xOffsets[i];
        dl->AddRectFilled(ImVec2(bx - barHalfW, center.y - h), ImVec2(bx + barHalfW, center.y + h), iconCol, barHalfW);
    }
}

void MainWindow::drawVectorMiniPlayer(ImDrawList* dl, ImVec2 center, float size, bool isHovered) {
    if (isHovered) {
        dl->AddCircleFilled(center, 13.0f, IM_COL32(255, 255, 255, 18));
    }
    ImU32 iconCol = isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(148, 155, 168, 255);
    float s = size * 0.44f;

    dl->AddRect(ImVec2(center.x - s, center.y - s * 0.75f),
                ImVec2(center.x + s, center.y + s * 0.75f),
                iconCol, 2.0f, 0, 1.4f);

    dl->AddLine(ImVec2(center.x - s, center.y - s * 0.28f),
                ImVec2(center.x + s, center.y - s * 0.28f),
                iconCol, 1.0f);

    dl->AddRectFilled(ImVec2(center.x + s * 0.18f, center.y + s * 0.05f),
                      ImVec2(center.x + s * 0.82f, center.y + s * 0.65f),
                      iconCol, 1.0f);
}

void MainWindow::drawVectorAudioDriver(ImDrawList* dl, ImVec2 center, float size, bool isExclusive, bool isHovered) {
    if (isHovered) {
        dl->AddCircleFilled(center, 13.0f, IM_COL32(255, 255, 255, 18));
    }

    float w = size * 0.75f;
    float h = size * 0.54f;
    ImVec2 bMin(center.x - w, center.y - h);
    ImVec2 bMax(center.x + w, center.y + h);

    ImU32 chipBg = isExclusive ? (isHovered ? IM_COL32(22, 50, 32, 255) : IM_COL32(15, 36, 24, 230))
                               : (isHovered ? IM_COL32(36, 44, 56, 255) : IM_COL32(22, 28, 38, 220));
    ImU32 chipBorder = isExclusive ? (isHovered ? IM_COL32(85, 235, 135, 255) : IM_COL32(55, 185, 95, 230))
                                   : (isHovered ? IM_COL32(115, 195, 255, 255) : IM_COL32(72, 125, 175, 200));

    // Pins on left and right
    ImU32 pinCol = isHovered ? IM_COL32(220, 230, 245, 255) : IM_COL32(140, 150, 168, 200);
    float pinLen = 2.4f;
    float pinSpacing = h * 0.55f;
    for (int i = -1; i <= 1; ++i) {
        float py = center.y + i * pinSpacing;
        dl->AddLine(ImVec2(bMin.x - pinLen, py), ImVec2(bMin.x, py), pinCol, 1.2f);
        dl->AddLine(ImVec2(bMax.x, py), ImVec2(bMax.x + pinLen, py), pinCol, 1.2f);
    }

    // Chip package
    dl->AddRectFilled(bMin, bMax, chipBg, 2.5f);
    dl->AddRect(bMin, bMax, chipBorder, 2.5f, 0, 1.2f);

    // Audio waveform indicator bars inside chip
    ImU32 waveCol = isExclusive ? IM_COL32(120, 245, 160, 240) : IM_COL32(160, 210, 255, 240);
    float cx = center.x - 2.8f;
    float barW = 1.3f;
    dl->AddRectFilled(ImVec2(cx - 3.4f, center.y - 2.6f), ImVec2(cx - 3.4f + barW, center.y + 2.6f), waveCol, 0.5f);
    dl->AddRectFilled(ImVec2(cx, center.y - 4.6f), ImVec2(cx + barW, center.y + 4.6f), waveCol, 0.5f);
    dl->AddRectFilled(ImVec2(cx + 3.4f, center.y - 2.0f), ImVec2(cx + 3.4f + barW, center.y + 2.0f), waveCol, 0.5f);

    // Micro LED indicator
    ImVec2 ledPos(bMax.x - 3.0f, bMin.y + 3.0f);
    ImU32 ledCol = isExclusive ? IM_COL32(75, 245, 130, 255) : IM_COL32(65, 195, 255, 255);
    ImU32 ledGlow = isExclusive ? IM_COL32(75, 245, 130, 90) : IM_COL32(65, 195, 255, 90);
    dl->AddCircleFilled(ledPos, 3.0f, ledGlow);
    dl->AddCircleFilled(ledPos, 1.6f, ledCol);
}

void MainWindow::drawVectorStar(ImDrawList* dl, ImVec2 center, float r, bool filled, unsigned int col) {
    ImVec2 pts[10];
    float rInner = r * 0.42f;
    for (int i = 0; i < 10; ++i) {
        float angle = static_cast<float>(i * (3.1415926535f / 5.0f) - 3.1415926535f * 0.5f);
        float cr = (i % 2 == 0) ? r : rInner;
        pts[i] = ImVec2(center.x + std::cos(angle) * cr, center.y + std::sin(angle) * cr);
    }
    if (filled) {
        for (int i = 0; i < 10; ++i) {
            dl->AddTriangleFilled(center, pts[i], pts[(i + 1) % 10], col);
        }
    } else {
        dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, 1.0f);
    }
}

void MainWindow::drawMicroEqualizer(ImDrawList* dl, ImVec2 center, unsigned int col, bool isPlaying) {
    float barW = 2.4f;
    float gap = 1.6f;
    float totalW = barW * 3.0f + gap * 2.0f;
    float startX = center.x - totalW * 0.5f;
    float baseH = 12.0f;

    float t = static_cast<float>(ImGui::GetTime());
    float h0 = isPlaying ? (3.0f + 8.0f * (0.5f + 0.5f * std::sin(t * 8.2f))) : 4.0f;
    float h1 = isPlaying ? (4.0f + 8.0f * (0.5f + 0.5f * std::sin(t * 12.0f + 1.2f))) : 8.0f;
    float h2 = isPlaying ? (3.0f + 8.0f * (0.5f + 0.5f * std::sin(t * 9.5f + 2.5f))) : 5.0f;

    float bottomY = center.y + baseH * 0.5f;

    dl->AddRectFilled(ImVec2(startX, bottomY - h0), ImVec2(startX + barW, bottomY), col, 1.0f);
    dl->AddRectFilled(ImVec2(startX + barW + gap, bottomY - h1), ImVec2(startX + barW * 2.0f + gap, bottomY), col, 1.0f);
    dl->AddRectFilled(ImVec2(startX + (barW + gap) * 2.0f, bottomY - h2), ImVec2(startX + (barW + gap) * 2.0f + barW, bottomY), col, 1.0f);
}

void MainWindow::drawMagnifyingGlass(ImDrawList* dl, ImVec2 center, float radius, unsigned int col) {
    dl->AddCircle(center, radius, col, 16, 1.5f);
    float cos45 = 0.7071f;
    ImVec2 handleStart(center.x + radius * cos45, center.y + radius * cos45);
    ImVec2 handleEnd(center.x + radius * 1.85f, center.y + radius * 1.85f);
    dl->AddLine(handleStart, handleEnd, col, 1.8f);
}

// ----------------- Background Logic & Main Render Loop (SlothPlayer Authentic) -----------------

void MainWindow::update() {
    uint32_t actionVal = m_pendingMediaAction.exchange(0);
    if (actionVal != 0) {
        MediaAction act = static_cast<MediaAction>(actionVal);
        switch (act) {
            case MediaAction::Play: play(); break;
            case MediaAction::Pause: pause(); break;
            case MediaAction::TogglePlayPause: togglePlayPause(); break;
            case MediaAction::Next: playNext(); break;
            case MediaAction::Previous: playPrevious(); break;
            case MediaAction::Stop: stop(); break;
            default: break;
        }
    }

    if (m_pendingTrackEnded.exchange(false)) {
        if (m_stopAfterCurrent) {
            m_stopAfterCurrent = false;
            m_audio.stop();
        } else if (m_sleepTimer.active && m_sleepTimer.stopAfterCurrent) {
            m_sleepTimer.active = false;
            m_audio.stop();
        } else if (m_sleepTimer.active && m_sleepTimer.remainingTracks > 0) {
            m_sleepTimer.remainingTracks--;
            if (m_sleepTimer.remainingTracks <= 0) {
                m_sleepTimer.active = false;
                m_audio.stop();
            } else {
                playNext();
            }
        } else {
            playNext();
        }
    }

    checkSleepTimer();

    double curPlayTime = m_audio.getCurrentTime();
    double totPlayDur = m_audio.getTotalDuration();
    m_scrobbler.onTrackProgress(curPlayTime, totPlayDur);

    const Track* currTrack = m_library.getTrackById(m_currentTrackId);
    std::string trackTitle = currTrack ? currTrack->getDisplayTitle() : "";
    std::string trackArtist = currTrack ? currTrack->getDisplayArtist() : "";
    if (currTrack && totPlayDur <= 0.0) totPlayDur = currTrack->duration;
    TaskbarManager::instance().update(m_hWnd, m_audio.isPlaying(), m_audio.isPaused(), curPlayTime, totPlayDur, trackTitle, trackArtist);
}

void MainWindow::render() {
    update();

    if (m_isMiniPlayer) {
        renderMiniPlayer();
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoNavFocus |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("SlothPlayerMain", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    // Lock root window scrolling so the full viewport never shifts or scrolls
    ImGui::SetScrollX(0.0f);
    ImGui::SetScrollY(0.0f);

    renderUnifiedTopBar();

    float totalW = viewport->Size.x;
    float availH = ImGui::GetContentRegionAvail().y;
    float splitterThickness = 1.0f;
    float subHeaderH = 30.0f;

    // Spacious premium Bottom Player Bar
    float bottomBarH = 72.0f;
    float middleH = availH - subHeaderH - bottomBarH;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_isDraggingLeftSplitter = false;
        m_isDraggingRightSplitter = false;
        m_isDraggingRightQueueSplitter = false;
    }
    m_isNearLeftSplitter = false;
    m_isNearRightSplitter = false;
    m_isNearRightQueueSplitter = false;

    // Dynamic Resizable Panel Widths (Flexible Splitter System)
    float leftW = 0.0f;
    float rightW = 0.0f;
    float centerW = totalW;

    if (totalW >= 620.0f) {
        float maxLeft = std::max(140.0f, totalW * 0.35f);
        float maxRight = std::max(160.0f, totalW * 0.40f);
        leftW = m_showLeftNavigator ? std::clamp(m_leftPanelWidth, 110.0f, maxLeft) : 0.0f;
        rightW = m_showRightSidebar ? std::clamp(m_rightPanelWidth, 150.0f, maxRight) : 0.0f;
        float leftSplit = (leftW > 0.0f) ? splitterThickness : 0.0f;
        float rightSplit = (rightW > 0.0f) ? splitterThickness : 0.0f;
        centerW = totalW - leftW - rightW - leftSplit - rightSplit;
        if (centerW < 240.0f) {
            float deficit = 240.0f - centerW;
            if (rightW > 160.0f) {
                float shrinkR = std::min(deficit * 0.6f, rightW - 160.0f);
                rightW -= shrinkR;
                deficit -= shrinkR;
            }
            if (leftW > 120.0f && deficit > 0.0f) {
                float shrinkL = std::min(deficit, leftW - 120.0f);
                leftW -= shrinkL;
            }
            centerW = totalW - leftW - rightW - leftSplit - rightSplit;
        }
    } else if (totalW >= 440.0f) {
        leftW = m_showLeftNavigator ? std::clamp(m_leftPanelWidth, 110.0f, totalW * 0.45f) : 0.0f;
        rightW = 0.0f;
        float leftSplit = (leftW > 0.0f) ? splitterThickness : 0.0f;
        centerW = totalW - leftW - leftSplit;
    } else {
        leftW = 0.0f;
        rightW = 0.0f;
        centerW = totalW;
    }

    // 0. SlothPlayer Sub-Header Toolbar (Panel Headers & Inline A-Z Jump Bar)
    renderSubHeaderToolbar(leftW, centerW, rightW);

    if (m_viewMode == ViewMode::TheaterMode) {
        renderTheaterMode();
        ImGui::SetCursorPos(ImVec2(0.0f, viewport->WorkSize.y - bottomBarH));
        renderBottomPlayerBar(bottomBarH);
        renderEqualizerModal();
        renderShortcutsModal();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
            toggleTheaterMode();
        }
        ImGui::End();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 1. Left Panel (if visible)
    if (leftW > 0.0f) {
        renderLeftPanel(leftW, middleH);
        ImGui::SameLine(0.0f, 0.0f);

        // Interactive Left-Center Vertical Splitter (1px thin hairline + proximity resize arrows)
        ImGui::PushID("LeftSplitter");
        ImVec2 spPos = ImGui::GetCursorScreenPos();
        float hitMargin = 14.0f; // 28px total grab hit zone (+-14px from the 1px line)
        ImGui::SetCursorScreenPos(ImVec2(spPos.x - hitMargin, spPos.y));
        ImGui::InvisibleButton("##split", ImVec2(hitMargin * 2.0f, middleH));
        bool isHovered = ImGui::IsItemHovered();
        bool isActive = ImGui::IsItemActive();

        ImVec2 mousePos = ImGui::GetIO().MousePos;
        bool isNear = !m_lockPanels && (isHovered || m_isDraggingLeftSplitter ||
            (mousePos.x >= spPos.x - hitMargin && mousePos.x <= spPos.x + hitMargin &&
             mousePos.y >= spPos.y && mousePos.y <= spPos.y + middleH));
        m_isNearLeftSplitter = isNear;

        if (!m_lockPanels) {
            if (isNear && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_isDraggingLeftSplitter = true;
            }
            if (m_isDraggingLeftSplitter) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    float deltaX = ImGui::GetIO().MouseDelta.x;
                    if (deltaX != 0.0f) {
                        m_leftPanelWidth += deltaX;
                        m_leftPanelWidth = std::clamp(m_leftPanelWidth, 120.0f, totalW * 0.35f);
                    }
                } else {
                    m_isDraggingLeftSplitter = false;
                    savePreferences();
                }
            }
        }

        if (isNear || m_isDraggingLeftSplitter) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        ImU32 accentCol = ImGui::GetColorU32(Theme::AccentColor());
        ImU32 splitCol = (m_isDraggingLeftSplitter || isActive) ? accentCol
                       : (isNear ? IM_COL32(110, 140, 180, 255) : IM_COL32(38, 42, 52, 255));
        dl->AddLine(ImVec2(spPos.x, spPos.y), ImVec2(spPos.x, spPos.y + middleH), splitCol, 1.0f);

        if (isNear || m_isDraggingLeftSplitter) {
            float badgeY = std::clamp(mousePos.y, spPos.y + 24.0f, spPos.y + middleH - 24.0f);
            drawResizableArrowBadge(ImGui::GetForegroundDrawList(), ImVec2(spPos.x, badgeY), true, accentCol);
        }

        ImGui::PopID();

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorScreenPos(ImVec2(spPos.x + splitterThickness, spPos.y));
    }

    // 2. Center Panel (Scales dynamically)
    renderCenterPanel(centerW, middleH);

    // 3. Right Panel (if visible)
    if (rightW > 0.0f) {
        ImGui::SameLine(0.0f, 0.0f);

        // Interactive Center-Right Vertical Splitter (1px thin hairline + proximity resize arrows)
        ImGui::PushID("RightSplitter");
        ImVec2 spPos = ImGui::GetCursorScreenPos();
        float hitMargin = 14.0f;
        ImGui::SetCursorScreenPos(ImVec2(spPos.x - hitMargin, spPos.y));
        ImGui::InvisibleButton("##split", ImVec2(hitMargin * 2.0f, middleH));
        bool isHovered = ImGui::IsItemHovered();
        bool isActive = ImGui::IsItemActive();

        ImVec2 mousePos = ImGui::GetIO().MousePos;
        bool isNear = !m_lockPanels && (isHovered || m_isDraggingRightSplitter ||
            (mousePos.x >= spPos.x - hitMargin && mousePos.x <= spPos.x + hitMargin &&
             mousePos.y >= spPos.y && mousePos.y <= spPos.y + middleH));
        m_isNearRightSplitter = isNear;

        if (!m_lockPanels) {
            if (isNear && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_isDraggingRightSplitter = true;
            }
            if (m_isDraggingRightSplitter) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    float deltaX = ImGui::GetIO().MouseDelta.x;
                    if (deltaX != 0.0f) {
                        m_rightPanelWidth -= deltaX;
                        m_rightPanelWidth = std::clamp(m_rightPanelWidth, 160.0f, totalW * 0.40f);
                    }
                } else {
                    m_isDraggingRightSplitter = false;
                    savePreferences();
                }
            }
        }

        if (isNear || m_isDraggingRightSplitter) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        ImU32 accentCol = ImGui::GetColorU32(Theme::AccentColor());
        ImU32 splitCol = (m_isDraggingRightSplitter || isActive) ? accentCol
                       : (isNear ? IM_COL32(110, 140, 180, 255) : IM_COL32(38, 42, 52, 255));
        dl->AddLine(ImVec2(spPos.x, spPos.y), ImVec2(spPos.x, spPos.y + middleH), splitCol, 1.0f);

        if (isNear || m_isDraggingRightSplitter) {
            float badgeY = std::clamp(mousePos.y, spPos.y + 24.0f, spPos.y + middleH - 24.0f);
            drawResizableArrowBadge(ImGui::GetForegroundDrawList(), ImVec2(spPos.x, badgeY), true, accentCol);
        }

        ImGui::PopID();

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorScreenPos(ImVec2(spPos.x + splitterThickness, spPos.y));
        renderRightPanel(rightW, middleH);
    }

    // Ensure resize mouse cursor stays active across the entire frame when near any splitter or dragging
    if (m_isDraggingLeftSplitter || m_isDraggingRightSplitter || m_isNearLeftSplitter || m_isNearRightSplitter) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (m_isDraggingRightQueueSplitter || m_isNearRightQueueSplitter) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }

    // Clean 1px separator line above bottom player bar (SlothPlayer style)
    dl->AddLine(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - bottomBarH),
                ImVec2(viewport->WorkPos.x + totalW, viewport->WorkPos.y + viewport->WorkSize.y - bottomBarH),
                IM_COL32(36, 39, 46, 255), 1.0f);

    // 4. Bottom Player Bar (anchored precisely to bottom edge of window)
    ImGui::SetCursorPos(ImVec2(0.0f, viewport->WorkSize.y - bottomBarH));
    renderBottomPlayerBar(bottomBarH);

    // Modals
    renderEqualizerModal();
    renderAddFolderModal();
    renderCreatePlaylistModal();
    renderTrackPropertiesModal();
    renderArtworkSearchModal();
    renderAboutModal();
    renderAutoDJModal();
    renderSleepTimerModal();
    renderBatchTagModal();
    renderLibraryStatsModal();
    renderDuplicatesModal();
    renderShortcutsModal();
    renderAddRadioStreamModal();
    renderFileOrganizerModal();
    renderAudioConverterModal();
    renderSmartPlaylistBuilderModal();
    renderSaveQueueAsPlaylistModal();
    renderTrackInfoModal();
    renderVolumeScannerModal();
    renderScrobblerModal();
    renderPanelsConfigModal();
    renderAudioDriverModal();
    renderPreferencesModal();
    renderDeleteTrackModal();
    renderDeleteAlbumModal();
    renderScanIndicatorOverlay();

    // Poll asynchronous waveform generator
    if (m_waveformLoading && m_waveformFuture.valid()) {
        if (m_waveformFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            m_cachedWaveform = m_waveformFuture.get();
            m_waveformLoading = false;
        }
    }

    // Global SlothPlayer Hotkeys
    if (!ImGui::GetIO().WantTextInput) {
        // F1: Keyboard Shortcuts (HelpShortcuts)
        if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
            m_showShortcutsModal = true;
        }
        // F11: Theater Mode (ViewTheaterMode)
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
            toggleTheaterMode();
        }
        // Ctrl+Shift+M: Compact / Mini Player Toggle (ViewCompactPlayer)
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_M, false)) {
            toggleMiniPlayer();
        }
        // Ctrl+D: Auto-DJ Toggle (PlaybackAutoDJToggle)
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            toggleAutoDJ();
        }
        // Ctrl+Shift+S: Sleep Timer (ToolsSleepTimer)
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            m_showSleepTimerModal = true;
        }
        // Ctrl+T: Tag Editor (EditTags)
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
            showBatchTagModal();
        }
        // Ctrl+Shift+R: Auto-Organize Files (ToolsAutoOrganize)
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            showFileOrganizerModal();
        }
        // Ctrl+Shift+C: Audio Format Converter (ToolsFormatConverter)
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            showAudioConverterModal();
        }
        // Ctrl+Shift+A: Audio Drivers & Hardware Output Preferences
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            openAudioDriverModal();
        }
        // Ctrl+Shift+P: Smart Auto-Playlist Builder
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
            showSmartPlaylistBuilderModal();
        }
        // Ctrl+P: Preferences & Display Settings
        if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
            showPreferencesModal(0);
        }
        // Ctrl+Shift+V: Volume Scanner (ToolsAnalyzeVolume)
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            showVolumeScannerModal();
        }
        // Ctrl+Shift+L: Last.fm Scrobbler Settings
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
            showScrobblerModal();
        }
        // Shift+Enter: Track Information (EditTrackInformation)
        if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            showTrackInfoModal(m_selectedTrackId > 0 ? m_selectedTrackId : m_currentTrackId);
        }
        // Ctrl+W: Close Active Tab
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            if (m_tabs.size() > 1 && !m_tabs[m_activeTabIndex].isPinned) {
                m_tabs.erase(m_tabs.begin() + m_activeTabIndex);
                if (m_activeTabIndex >= m_tabs.size()) m_activeTabIndex = m_tabs.size() - 1;
                m_viewMode = m_tabs[m_activeTabIndex].viewMode;
                m_navSource = static_cast<NavSource>(m_tabs[m_activeTabIndex].navSource);
            }
        }
        // Delete or Shift+Delete: If focused/selected in Queue, remove from queue; else delete selected track
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (m_rightPanelTab == RightPanelTab::Queue) {
                if (!m_selectedQueueIndices.empty()) {
                    removeQueueIndices(m_selectedQueueIndices);
                } else if (m_selectedQueueIndex >= 0 && static_cast<size_t>(m_selectedQueueIndex) < m_queue.size()) {
                    removeQueueIndex(static_cast<size_t>(m_selectedQueueIndex));
                }
            } else if (m_selectedTrackId > 0) {
                bool alsoFromDisk = ImGui::GetIO().KeyShift;
                showDeleteTrackModal(m_selectedTrackId, alsoFromDisk);
            }
        }
        // Ctrl+I: Library Statistics (ToolsLibraryStats)
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) {
            showLibraryStatsModal();
        }

        // F5: Rescan Library
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && !m_library.isScanning()) {
            m_library.scanDirectories(m_library.getMonitoredFolders());
        }

        // SlothPlayer Standard Playback Hotkeys (when not in search / text field)
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) playPrevious();
            if (ImGui::IsKeyPressed(ImGuiKey_X, false)) stop();
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) pause();
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) m_stopAfterCurrent = !m_stopAfterCurrent;
            if (ImGui::IsKeyPressed(ImGuiKey_B, false)) playNext();
        }

        // Space: Play/Pause (PlaybackPlayPause)
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            togglePlayPause();
        }
        // Left/Right: Skip 5s / 30s / Prev / Next
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
            if (ImGui::GetIO().KeyCtrl) {
                playPrevious();
            } else if (ImGui::GetIO().KeyShift) {
                m_audio.seekTo(std::max(0.0, m_audio.getCurrentTime() - 30.0));
            } else {
                m_audio.seekTo(std::max(0.0, m_audio.getCurrentTime() - 5.0));
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
            if (ImGui::GetIO().KeyCtrl) {
                playNext();
            } else if (ImGui::GetIO().KeyShift) {
                m_audio.seekTo(std::min(m_audio.getTotalDuration(), m_audio.getCurrentTime() + 30.0));
            } else {
                m_audio.seekTo(std::min(m_audio.getTotalDuration(), m_audio.getCurrentTime() + 5.0));
            }
        }
        // Ctrl+Up / Ctrl+Down: Volume (PlaybackVolumeUp / PlaybackVolumeDown)
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            m_audio.setVolume(std::min(1.0f, m_audio.getVolume() + 0.05f));
            if (m_audio.isMuted()) m_audio.setMuted(false);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            m_audio.setVolume(std::max(0.0f, m_audio.getVolume() - 0.05f));
        }
        // Ctrl+M: Mute (PlaybackVolumeMute)
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_M, false)) {
            m_audio.setMuted(!m_audio.isMuted());
        }
        // Ctrl+L: Loved Toggle (RatingNowPlayingToggleLove)
        if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
            if (m_currentTrackId > 0) m_library.toggleFavorite(m_currentTrackId);
        }
        // Ctrl+Shift+L: Dislike Current Track & Suddenly Skip
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
            if (m_currentTrackId > 0) {
                m_library.toggleDislike(m_currentTrackId);
                const Track* trk = m_library.getTrackById(m_currentTrackId);
                if (trk && trk->isDisliked) {
                    if (m_audio.isShuffle()) {
                        rebuildShuffleOrder(false);
                    }
                    playNext();
                }
            }
        }
        // Ctrl+E: Equalizer (ViewEqualiser)
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            m_showEqualizer = !m_showEqualizer;
        }
        // Alt+Enter: Properties of selected track (EditProperties)
        if (ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            if (m_selectedTrackId > 0) openPropertiesForTrack(m_selectedTrackId);
            else if (m_currentTrackId > 0) openPropertiesForTrack(m_currentTrackId);
        }
        // Ctrl+0..Ctrl+5: Star ratings (RatingNowPlaying0..5)
        if (ImGui::GetIO().KeyCtrl) {
            for (int k = 0; k <= 5; ++k) {
                ImGuiKey key = static_cast<ImGuiKey>(ImGuiKey_0 + k);
                if (ImGui::IsKeyPressed(key, false) && m_currentTrackId > 0) {
                    m_library.setRating(m_currentTrackId, k);
                }
            }
        }
    }

    if (m_isDraggingLeftSplitter || m_isDraggingRightSplitter || m_isNearLeftSplitter || m_isNearRightSplitter) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (m_isDraggingRightQueueSplitter || m_isNearRightQueueSplitter) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }

    ImGui::End();
}

void MainWindow::renderTopMenuBar() {
    // Replaced by renderUnifiedTopBar() for SlothPlayer authentic layout
}

void MainWindow::renderUnifiedTopBar() {
    float totalW = ImGui::GetWindowWidth();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.070f, 0.075f, 0.086f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));

    ImGui::BeginChild("UnifiedTopHeader", ImVec2(totalW, 38.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 headerP0 = ImGui::GetWindowPos();

    // 1. App Icon & SlothPlayer Dropdown Menu Button
    float startX = 14.0f;
    ImTextureID appIconTex = m_textures.getArtworkFromFile("resources/app_icon.png");
    if (appIconTex) {
        ImGui::SetCursorPos(ImVec2(startX, 9.0f));
        ImGui::Image(appIconTex, ImVec2(20.0f, 20.0f));
        startX += 26.0f;
    } else {
        ImVec2 iconCenter(headerP0.x + startX + 10.0f, headerP0.y + 19.0f);
        dl->AddCircleFilled(iconCenter, 9.0f, IM_COL32(235, 140, 30, 255));
        dl->AddCircleFilled(iconCenter, 6.0f, IM_COL32(18, 20, 24, 255));
        dl->AddCircleFilled(iconCenter, 2.5f, IM_COL32(235, 140, 30, 255));
        startX += 26.0f;
    }

    ImGui::SetCursorPos(ImVec2(startX, 7.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(235, 238, 245, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));

    if (ImGui::Button("SlothPlayer \xE2\x96\xBE##AppMenuBtn", ImVec2(0.0f, 26.0f))) {
        ImGui::OpenPopup("SlothPlayerMainMenuPopup");
    }
    ImVec2 menuBtnMin = ImGui::GetItemRectMin();
    ImVec2 menuBtnMax = ImGui::GetItemRectMax();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.13f, 0.16f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.28f, 0.34f, 1.0f));

    ImGui::SetNextWindowPos(ImVec2(menuBtnMin.x, menuBtnMax.y + 2.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(160.0f, 0.0f), ImVec2(400.0f, FLT_MAX));
    if (ImGui::BeginPopup("SlothPlayerMainMenuPopup")) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Audio File...", "Ctrl+O")) openSystemFileDialog();
            if (ImGui::MenuItem("Add Music Folder...", "Ctrl+Shift+O")) openSystemFolderDialog();
            if (ImGui::MenuItem("Load Demo Audio")) loadDemoAudio();
            if (m_library.isScanning()) {
                ImGui::MenuItem("Rescanning Library...", "F5", false, false);
            } else {
                if (ImGui::MenuItem("Rescan Library", "F5")) m_library.scanDirectories(m_library.getMonitoredFolders());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("New Playlist...")) m_showCreatePlaylistModal = true;
            if (ImGui::MenuItem("Compact / Mini Player", "Ctrl+Shift+M")) toggleMiniPlayer();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) Platform::requestQuit();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Batch Tag Editor...", "Ctrl+T")) showBatchTagModal();
            if (ImGui::MenuItem("Organize Audio Files...", "Ctrl+Shift+R")) showFileOrganizerModal();
            if (ImGui::MenuItem("Track Information...", "Shift+Enter")) showTrackInfoModal(m_selectedTrackId > 0 ? m_selectedTrackId : m_currentTrackId);
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...", "Ctrl+P")) showPreferencesModal(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Now Playing")) m_viewMode = ViewMode::NowPlaying;
            if (ImGui::MenuItem("Tracks")) m_viewMode = ViewMode::Tracks;
            if (ImGui::MenuItem("Albums")) m_viewMode = ViewMode::Albums;
            if (ImGui::MenuItem("Music Explorer")) m_viewMode = ViewMode::MusicExplorer;
            if (ImGui::MenuItem("Playlists")) m_viewMode = ViewMode::Playlists;
            if (ImGui::MenuItem("Artists")) m_viewMode = ViewMode::Artists;
            if (ImGui::MenuItem("Folders")) m_viewMode = ViewMode::Folders;
            if (ImGui::MenuItem("Web Radio Streams")) m_navSource = NavSource::RadioStreams;
            if (ImGui::MenuItem("Podcasts")) m_navSource = NavSource::Podcasts;
            if (ImGui::MenuItem("Playback History")) m_navSource = NavSource::History;
            ImGui::Separator();
            if (ImGui::BeginMenu("Mini Player Modes")) {
                if (ImGui::MenuItem("Compact Player (Classic)", nullptr, m_isMiniPlayer && m_miniPlayerMode == MiniPlayerMode::Compact)) {
                    openMiniPlayerInMode(MiniPlayerMode::Compact);
                }
                if (ImGui::MenuItem("Album Art / Vinyl Card", nullptr, m_isMiniPlayer && m_miniPlayerMode == MiniPlayerMode::AlbumArt)) {
                    openMiniPlayerInMode(MiniPlayerMode::AlbumArt);
                }
                if (ImGui::MenuItem("DeskBand / Taskbar Strip", nullptr, m_isMiniPlayer && m_miniPlayerMode == MiniPlayerMode::Taskbar)) {
                    openMiniPlayerInMode(MiniPlayerMode::Taskbar);
                }
                if (ImGui::MenuItem("Minimalist HUD Ticker", nullptr, m_isMiniPlayer && m_miniPlayerMode == MiniPlayerMode::MinimalHUD)) {
                    openMiniPlayerInMode(MiniPlayerMode::MinimalHUD);
                }
                if (ImGui::MenuItem("Floating Synced Lyrics", nullptr, m_isMiniPlayer && m_miniPlayerMode == MiniPlayerMode::Lyrics)) {
                    openMiniPlayerInMode(MiniPlayerMode::Lyrics);
                }
                if (ImGui::MenuItem("Picture-in-Picture / Video Card", nullptr, m_isMiniPlayer && m_miniPlayerMode == MiniPlayerMode::PiP)) {
                    openMiniPlayerInMode(MiniPlayerMode::PiP);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Arrange Panels...", nullptr)) showPanelsConfigModal();
            if (ImGui::MenuItem("Show Left Navigator", nullptr, m_showLeftNavigator)) { m_showLeftNavigator = !m_showLeftNavigator; savePreferences(); }
            if (ImGui::MenuItem("Show Right Sidebar", nullptr, m_showRightSidebar)) { m_showRightSidebar = !m_showRightSidebar; savePreferences(); }
            if (ImGui::MenuItem("Waveform Progress Bar", nullptr, m_useWavebar)) { m_useWavebar = !m_useWavebar; savePreferences(); }
            ImGui::Separator();
            if (ImGui::BeginMenu("Theme / Skin")) {
                AppTheme curTheme = Theme::getCurrentTheme();
                if (ImGui::MenuItem("Dark Slate (Classic Default)", nullptr, curTheme == AppTheme::ClassicDark)) { Theme::applyTheme(AppTheme::ClassicDark); savePreferences(); }
                if (ImGui::MenuItem("Modern Obsidian", nullptr, curTheme == AppTheme::ModernObsidian)) { Theme::applyTheme(AppTheme::ModernObsidian); savePreferences(); }
                if (ImGui::MenuItem("Midnight Navy", nullptr, curTheme == AppTheme::MidnightNavy)) { Theme::applyTheme(AppTheme::MidnightNavy); savePreferences(); }
                if (ImGui::MenuItem("Forest Emerald", nullptr, curTheme == AppTheme::ForestEmerald)) { Theme::applyTheme(AppTheme::ForestEmerald); savePreferences(); }
                if (ImGui::MenuItem("Cyberpunk Neon", nullptr, curTheme == AppTheme::CyberpunkNeon)) { Theme::applyTheme(AppTheme::CyberpunkNeon); savePreferences(); }
                if (ImGui::MenuItem("Nordic Frost", nullptr, curTheme == AppTheme::NordicFrost)) { Theme::applyTheme(AppTheme::NordicFrost); savePreferences(); }
                if (ImGui::MenuItem("Amber Sunset", nullptr, curTheme == AppTheme::AmberSunset)) { Theme::applyTheme(AppTheme::AmberSunset); savePreferences(); }
                if (ImGui::MenuItem("Dracula", nullptr, curTheme == AppTheme::Dracula)) { Theme::applyTheme(AppTheme::Dracula); savePreferences(); }
                if (ImGui::MenuItem("Tokyo Night", nullptr, curTheme == AppTheme::TokyoNight)) { Theme::applyTheme(AppTheme::TokyoNight); savePreferences(); }
                if (ImGui::MenuItem("Metro Light", nullptr, curTheme == AppTheme::MetroLight)) { Theme::applyTheme(AppTheme::MetroLight); savePreferences(); }
                ImGui::Separator();
                bool useLightCard = Theme::useLightExpandedCard();
                if (ImGui::MenuItem("Light Expanded Album Card", nullptr, useLightCard)) {
                    Theme::setUseLightExpandedCard(!useLightCard);
                    savePreferences();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Control")) {
            if (ImGui::MenuItem("Play / Pause", "Space")) togglePlayPause();
            if (ImGui::MenuItem("Next Track", "Ctrl+Right")) playNext();
            if (ImGui::MenuItem("Previous Track", "Ctrl+Left")) playPrevious();
            if (ImGui::MenuItem("Stop", "Ctrl+S")) stop();
            ImGui::Separator();
            bool isShuf = m_audio.isShuffle();
            if (ImGui::MenuItem("Shuffle", "Ctrl+H", isShuf)) {
                m_audio.setShuffle(!isShuf);
                if (!isShuf) rebuildShuffleOrder(true);
            }
            bool isDJ = m_library.getAutoDJConfig().enabled;
            if (ImGui::MenuItem("Auto-DJ", "Ctrl+D", isDJ)) toggleAutoDJ();
            if (ImGui::MenuItem("Sleep Timer...", "Ctrl+Shift+S")) m_showSleepTimerModal = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Audio Output & Drivers...", "Ctrl+Shift+A")) openAudioDriverModal();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Track Information...", "Shift+Enter")) showTrackInfoModal(m_selectedTrackId > 0 ? m_selectedTrackId : m_currentTrackId);
            if (ImGui::MenuItem("Analyze Volume (ReplayGain)...", "Ctrl+Shift+V")) showVolumeScannerModal();
            if (ImGui::MenuItem("Last.fm Scrobbler Settings...", "Ctrl+Shift+L")) showScrobblerModal();
            ImGui::Separator();
            if (ImGui::MenuItem("Auto-DJ Settings...", "Ctrl+D")) m_showAutoDJModal = true;
            if (ImGui::MenuItem("Sleep Timer Settings...", "Ctrl+Shift+S")) m_showSleepTimerModal = true;
            if (ImGui::MenuItem("Batch Tag Editor...", "Ctrl+T")) showBatchTagModal();
            if (ImGui::MenuItem("Organize Audio Files...", "Ctrl+Shift+R")) showFileOrganizerModal();
            if (ImGui::MenuItem("Audio Format Converter...", "Ctrl+Shift+C")) showAudioConverterModal();
            if (ImGui::MenuItem("New Smart Auto-Playlist...", "Ctrl+Shift+P")) showSmartPlaylistBuilderModal();
            ImGui::Separator();
            if (ImGui::MenuItem("Library Statistics...", "Ctrl+I")) showLibraryStatsModal();
            if (ImGui::MenuItem("Find Duplicate Tracks...")) showDuplicatesModal();
            if (ImGui::MenuItem("Graphic Equalizer...", "Ctrl+E")) m_showEqualizer = true;
            if (ImGui::MenuItem("Audio Drivers & Hardware Output...", "Ctrl+Shift+A")) openAudioDriverModal();
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences & Settings...", "Ctrl+P")) showPreferencesModal(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Keyboard Shortcuts", "F1")) m_showShortcutsModal = true;
            if (ImGui::MenuItem("About SlothPlayer")) m_showAboutModal = true;
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);

    ImGui::SameLine(0.0f, 6.0f);

    // 2. Navigation History Arrows (< >)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 168, 180, 255));

    if (ImGui::Button("<##NavBack", ImVec2(20.0f, 26.0f))) {}
    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::Button(">##NavFwd", ImVec2(20.0f, 26.0f))) {}
    ImGui::PopStyleColor(4);

    ImGui::SameLine(0.0f, 8.0f);

    float cursorX = ImGui::GetCursorPosX();

    // 3. Flat SlothPlayer Navigation Tabs
    struct NavTabDef {
        const char* label;
        ViewMode mode;
        NavSource nav;
    };
    static const NavTabDef kTabs[] = {
        { "NOW PLAYING", ViewMode::NowPlaying, NavSource::AllTracks },
        { "TRACKS", ViewMode::Tracks, NavSource::AllTracks },
        { "ALBUMS", ViewMode::Albums, NavSource::AllTracks },
        { "MUSIC EXPLORER", ViewMode::MusicExplorer, NavSource::AllTracks },
        { "PLAYLISTS", ViewMode::Playlists, NavSource::Playlist },
        { "PODCASTS", ViewMode::Tracks, NavSource::Podcasts }
    };

    float capBtnW = 46.0f;
    float capBtnH = 38.0f;
    float capTotalW = m_hWnd ? (capBtnW * 3.0f) : 0.0f;
    float rightReserved = capTotalW + 210.0f;

    for (const auto& tab : kTabs) {
        if (cursorX + 110.0f > totalW - rightReserved) break;

        bool active = (tab.nav == NavSource::Podcasts) ? (m_navSource == NavSource::Podcasts)
                                                       : (m_viewMode == tab.mode && m_navSource != NavSource::Podcasts);

        ImVec4 tabTextCol = active ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.62f, 0.66f, 0.74f, 1.0f);
        ImVec4 tabBgCol = active ? ImVec4(0.16f, 0.19f, 0.24f, 1.0f) : ImVec4(0, 0, 0, 0);

        ImGui::PushStyleColor(ImGuiCol_Button, tabBgCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.09f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_Text, tabTextCol);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));

        std::string btnId = std::string(tab.label) + "##NavTab";
        if (ImGui::Button(btnId.c_str(), ImVec2(0.0f, 26.0f))) {
            m_viewMode = tab.mode;
            m_navSource = tab.nav;
        }

        if (active) {
            ImVec2 minP = ImGui::GetItemRectMin();
            ImVec2 maxP = ImGui::GetItemRectMax();
            // Subtle accent bottom indicator pill
            dl->AddRectFilled(ImVec2(minP.x + 8.0f, maxP.y - 2.5f), ImVec2(maxP.x - 8.0f, maxP.y), ImGui::GetColorU32(Theme::AccentColor()), 1.5f);
            // Subtle glowing border around active capsule
            dl->AddRect(minP, maxP, ImGui::GetColorU32(ImVec4(Theme::AccentColor().x, Theme::AccentColor().y, Theme::AccentColor().z, 0.40f)), 6.0f, 0, 1.0f);
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        ImGui::SameLine(0.0f, 3.0f);
        cursorX = ImGui::GetCursorPosX();
    }

    // 4. Right side: Panel Arrange icon and Search Pill
    float searchW = 160.0f;
    float searchX = totalW - capTotalW - searchW - 12.0f;

    // Arrange panels button
    float arrangeX = searchX - 28.0f;
    if (arrangeX > cursorX + 10.0f) {
        ImGui::SetCursorPos(ImVec2(arrangeX, 7.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(165, 172, 185, 255));
        if (ImGui::Button("\xE2\x98\xB0##ArrangePanels", ImVec2(24.0f, 26.0f))) {
            showPanelsConfigModal();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Arrange Panels & Layout");
        }
        ImGui::PopStyleColor(4);
    }

    // Search Box Pill with Vector Magnifying Glass and 1-Click Clear Button
    if (searchX > cursorX + 20.0f) {
        ImGui::SetCursorPos(ImVec2(searchX, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(26.0f, 3.0f)); // Left space for magnifying glass
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.13f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.16f, 0.18f, 0.22f, 1.0f));
        ImGui::SetNextItemWidth(searchW);
        ImGui::InputTextWithHint("##SearchTopPill", "Search... (Ctrl+F)", m_searchBuffer, sizeof(m_searchBuffer));
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        ImVec2 sMin = ImGui::GetItemRectMin();
        ImVec2 sMax = ImGui::GetItemRectMax();

        // Draw Magnifying Glass Icon on the left inside pill
        ImVec2 glassCenter(sMin.x + 13.0f, (sMin.y + sMax.y) * 0.5f);
        drawMagnifyingGlass(dl, glassCenter, 4.2f, IM_COL32(150, 158, 172, 255));

        // Draw Clear '✕' Button on the right inside pill when query is present
        if (m_searchBuffer[0] != '\0') {
            ImVec2 clrCenter(sMax.x - 14.0f, (sMin.y + sMax.y) * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(clrCenter.x - 10.0f, clrCenter.y - 10.0f));
            if (ImGui::InvisibleButton("##TopSearchClearBtn", ImVec2(20.0f, 20.0f))) {
                m_searchBuffer[0] = '\0';
                ImGui::ClearActiveID();
            }
            bool isClrHov = ImGui::IsItemHovered();
            if (isClrHov) {
                dl->AddCircleFilled(clrCenter, 8.0f, IM_COL32(255, 255, 255, 45));
            }
            ImU32 clrIconCol = isClrHov ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 168, 180, 255);
            dl->AddLine(ImVec2(clrCenter.x - 3.5f, clrCenter.y - 3.5f), ImVec2(clrCenter.x + 3.5f, clrCenter.y + 3.5f), clrIconCol, 1.4f);
            dl->AddLine(ImVec2(clrCenter.x + 3.5f, clrCenter.y - 3.5f), ImVec2(clrCenter.x - 3.5f, clrCenter.y + 3.5f), clrIconCol, 1.4f);
        }
    }

    // 5. Windows 11 Style Caption Buttons (Minimize, Maximize / Restore, Close)
#if defined(_WIN32)
    if (m_hWnd) {
        HWND hWnd = static_cast<HWND>(m_hWnd);
        bool isMaximized = IsZoomed(hWnd);
        float capStartX = totalW - capTotalW;

        ImU32 normCapTextCol = IM_COL32(200, 205, 215, 255);
        ImU32 hoverCapBgCol = IM_COL32(255, 255, 255, 25);
        ImU32 activeCapBgCol = IM_COL32(255, 255, 255, 45);

        // --- Minimize Button ---
        ImGui::SetCursorPos(ImVec2(capStartX, 0.0f));
        if (ImGui::InvisibleButton("##CapMinimize", ImVec2(capBtnW, capBtnH))) {
            ShowWindow(hWnd, SW_MINIMIZE);
        }
        bool minHov = ImGui::IsItemHovered();
        bool minAct = ImGui::IsItemActive();
        ImVec2 minB0 = ImGui::GetItemRectMin();
        ImVec2 minB1 = ImGui::GetItemRectMax();
        if (minAct) {
            dl->AddRectFilled(minB0, minB1, activeCapBgCol);
        } else if (minHov) {
            dl->AddRectFilled(minB0, minB1, hoverCapBgCol);
        }
        ImVec2 minCenter((minB0.x + minB1.x) * 0.5f, (minB0.y + minB1.y) * 0.5f);
        dl->AddLine(ImVec2(minCenter.x - 5.0f, minCenter.y), ImVec2(minCenter.x + 5.0f, minCenter.y), normCapTextCol, 1.0f);

        // --- Maximize / Restore Button ---
        ImGui::SetCursorPos(ImVec2(capStartX + capBtnW, 0.0f));
        if (ImGui::InvisibleButton("##CapMaximize", ImVec2(capBtnW, capBtnH))) {
            ShowWindow(hWnd, isMaximized ? SW_RESTORE : SW_MAXIMIZE);
        }
        bool maxHov = ImGui::IsItemHovered();
        bool maxAct = ImGui::IsItemActive();
        ImVec2 maxB0 = ImGui::GetItemRectMin();
        ImVec2 maxB1 = ImGui::GetItemRectMax();
        if (maxAct) {
            dl->AddRectFilled(maxB0, maxB1, activeCapBgCol);
        } else if (maxHov) {
            dl->AddRectFilled(maxB0, maxB1, hoverCapBgCol);
        }
        ImVec2 maxCenter((maxB0.x + maxB1.x) * 0.5f, (maxB0.y + maxB1.y) * 0.5f);
        if (!isMaximized) {
            dl->AddRect(ImVec2(maxCenter.x - 5.0f, maxCenter.y - 5.0f), ImVec2(maxCenter.x + 5.0f, maxCenter.y + 5.0f), normCapTextCol, 0.0f, 0, 1.0f);
        } else {
            dl->AddLine(ImVec2(maxCenter.x - 2.0f, maxCenter.y - 5.0f), ImVec2(maxCenter.x + 5.0f, maxCenter.y - 5.0f), normCapTextCol, 1.0f);
            dl->AddLine(ImVec2(maxCenter.x + 5.0f, maxCenter.y - 5.0f), ImVec2(maxCenter.x + 5.0f, maxCenter.y + 2.0f), normCapTextCol, 1.0f);
            dl->AddLine(ImVec2(maxCenter.x + 2.0f, maxCenter.y + 2.0f), ImVec2(maxCenter.x + 5.0f, maxCenter.y + 2.0f), normCapTextCol, 1.0f);
            dl->AddLine(ImVec2(maxCenter.x - 2.0f, maxCenter.y - 5.0f), ImVec2(maxCenter.x - 2.0f, maxCenter.y - 2.0f), normCapTextCol, 1.0f);
            dl->AddRect(ImVec2(maxCenter.x - 5.0f, maxCenter.y - 2.0f), ImVec2(maxCenter.x + 2.0f, maxCenter.y + 5.0f), normCapTextCol, 0.0f, 0, 1.0f);
        }

        // --- Close Button ---
        ImGui::SetCursorPos(ImVec2(capStartX + capBtnW * 2.0f, 0.0f));
        if (ImGui::InvisibleButton("##CapClose", ImVec2(capBtnW, capBtnH))) {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        bool clsHov = ImGui::IsItemHovered();
        bool clsAct = ImGui::IsItemActive();
        ImVec2 clsB0 = ImGui::GetItemRectMin();
        ImVec2 clsB1 = ImGui::GetItemRectMax();
        if (clsAct) {
            dl->AddRectFilled(clsB0, clsB1, IM_COL32(200, 15, 30, 255));
        } else if (clsHov) {
            dl->AddRectFilled(clsB0, clsB1, IM_COL32(232, 17, 35, 255));
        }
        ImU32 clsIconCol = (clsHov || clsAct) ? IM_COL32(255, 255, 255, 255) : normCapTextCol;
        ImVec2 clsCenter((clsB0.x + clsB1.x) * 0.5f, (clsB0.y + clsB1.y) * 0.5f);
        dl->AddLine(ImVec2(clsCenter.x - 4.5f, clsCenter.y - 4.5f), ImVec2(clsCenter.x + 4.5f, clsCenter.y + 4.5f), clsIconCol, 1.2f);
        dl->AddLine(ImVec2(clsCenter.x + 4.5f, clsCenter.y - 4.5f), ImVec2(clsCenter.x - 4.5f, clsCenter.y + 4.5f), clsIconCol, 1.2f);
    }

    // 6. Titlebar Dragging, Double-click Maximize, and Right-click System Menu on empty area ONLY within the 38px top bar
    ImVec2 mPos = ImGui::GetIO().MousePos;
    bool isOverTopBar = (mPos.x >= headerP0.x && mPos.x <= headerP0.x + totalW &&
                         mPos.y >= headerP0.y && mPos.y <= headerP0.y + 38.0f);
    if (m_hWnd && isOverTopBar && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
        HWND hWnd = static_cast<HWND>(m_hWnd);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ReleaseCapture();
            SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE);
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = GetSystemMenu(hWnd, FALSE);
            if (hMenu) {
                bool isMax = IsZoomed(hWnd);
                EnableMenuItem(hMenu, SC_RESTORE, isMax ? MF_ENABLED : MF_GRAYED);
                EnableMenuItem(hMenu, SC_MOVE, isMax ? MF_GRAYED : MF_ENABLED);
                EnableMenuItem(hMenu, SC_SIZE, isMax ? MF_GRAYED : MF_ENABLED);
                EnableMenuItem(hMenu, SC_MAXIMIZE, isMax ? MF_GRAYED : MF_ENABLED);
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                if (cmd > 0) {
                    SendMessage(hWnd, WM_SYSCOMMAND, cmd, 0);
                }
            }
        }
    }

    // Top 1px hairline border when not maximized
    if (m_hWnd && !IsZoomed(static_cast<HWND>(m_hWnd))) {
        dl->AddLine(headerP0, ImVec2(headerP0.x + totalW, headerP0.y), IM_COL32(48, 52, 60, 255), 1.0f);
    }
#endif

    // Bottom subtle 1px divider for top bar
    dl->AddLine(ImVec2(headerP0.x, headerP0.y + 37.0f), ImVec2(headerP0.x + totalW, headerP0.y + 37.0f), IM_COL32(36, 40, 48, 255), 1.0f);

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void MainWindow::renderSubHeaderToolbar(float leftW, float centerW, float rightW) {
    float totalW = ImGui::GetWindowWidth();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.088f, 0.095f, 0.108f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    ImGui::BeginChild("SubHeaderToolbar", ImVec2(totalW, 30.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 subP0 = ImGui::GetWindowPos();

    float curX = 0.0f;

    // --- LEFT COLUMN HEADER (Album ▾) ---
    if (leftW > 0.0f) {
        ImGui::SetCursorPos(ImVec2(curX + 12.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(190, 195, 205, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.15f));

        const char* leftTitle = (m_viewMode == ViewMode::Albums) ? "Album \xE2\x96\xBE" : "Library \xE2\x96\xBE";
        bool openLeftMenu = ImGui::Button(leftTitle, ImVec2(0.0f, 24.0f)) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if (openLeftMenu) {
            ImGui::OpenPopup("LeftNavDropdownPopup");
        }
        ImVec2 leftBtnMin = ImGui::GetItemRectMin();
        ImVec2 leftBtnMax = ImGui::GetItemRectMax();
        ImGui::PopStyleColor(4);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.13f, 0.16f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.28f, 0.34f, 1.0f));
        ImGui::SetNextWindowPos(ImVec2(leftBtnMin.x, leftBtnMax.y + 2.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("LeftNavDropdownPopup")) {
            if (ImGui::MenuItem("Album Covers", nullptr, m_viewMode == ViewMode::Albums)) m_viewMode = ViewMode::Albums;
            if (ImGui::MenuItem("Tracks", nullptr, m_viewMode == ViewMode::Tracks)) m_viewMode = ViewMode::Tracks;
            if (ImGui::MenuItem("Artists", nullptr, m_viewMode == ViewMode::Artists)) m_viewMode = ViewMode::Artists;
            if (ImGui::MenuItem("Playlists", nullptr, m_viewMode == ViewMode::Playlists)) m_viewMode = ViewMode::Playlists;
            if (ImGui::MenuItem("Folders", nullptr, m_viewMode == ViewMode::Folders)) m_viewMode = ViewMode::Folders;
            ImGui::Separator();
            if (ImGui::MenuItem("Show Settings...")) {
                m_showPanelsConfigModal = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(m_lockPanels ? "Unlock Panels" : "Lock Panel", nullptr, m_lockPanels)) {
                m_lockPanels = !m_lockPanels;
                savePreferences();
            }
            if (ImGui::MenuItem("Close Panel")) {
                m_showLeftNavigator = false;
                savePreferences();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Arrange Panels...")) {
                m_showPanelsConfigModal = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);

        curX += leftW;
        dl->AddLine(ImVec2(subP0.x + curX, subP0.y), ImVec2(subP0.x + curX, subP0.y + 30.0f), IM_COL32(38, 42, 50, 255), 1.0f);
        curX += 1.0f;
    }

    // --- CENTER COLUMN HEADER (Albums ▾ + # A B C ... Z jump bar) ---
    {
        ImGui::SetCursorPos(ImVec2(curX + 14.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 235, 245, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.15f));

        const char* centerTitle = "Albums \xE2\x96\xBE";
        if (m_viewMode == ViewMode::Tracks) centerTitle = "Tracks \xE2\x96\xBE";
        else if (m_viewMode == ViewMode::Artists) centerTitle = "Artists \xE2\x96\xBE";
        else if (m_viewMode == ViewMode::Playlists) centerTitle = "Playlists \xE2\x96\xBE";
        else if (m_viewMode == ViewMode::Folders) centerTitle = "Folders \xE2\x96\xBE";
        else if (m_viewMode == ViewMode::MusicExplorer) centerTitle = "Music Explorer \xE2\x96\xBE";

        bool openCenterMenu = ImGui::Button(centerTitle, ImVec2(0.0f, 24.0f)) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if (openCenterMenu) {
            ImGui::OpenPopup("CenterViewDropdownPopup");
        }
        ImVec2 centerBtnMin = ImGui::GetItemRectMin();
        ImVec2 centerBtnMax = ImGui::GetItemRectMax();
        ImGui::PopStyleColor(4);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.13f, 0.16f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.28f, 0.34f, 1.0f));
        ImGui::SetNextWindowPos(ImVec2(centerBtnMin.x, centerBtnMax.y + 2.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("CenterViewDropdownPopup")) {
            if (ImGui::MenuItem("Albums View", nullptr, m_viewMode == ViewMode::Albums)) m_viewMode = ViewMode::Albums;
            if (ImGui::MenuItem("Tracks View", nullptr, m_viewMode == ViewMode::Tracks)) m_viewMode = ViewMode::Tracks;
            if (ImGui::MenuItem("Artists View", nullptr, m_viewMode == ViewMode::Artists)) m_viewMode = ViewMode::Artists;
            if (ImGui::MenuItem("Music Explorer", nullptr, m_viewMode == ViewMode::MusicExplorer)) m_viewMode = ViewMode::MusicExplorer;
            ImGui::Separator();
            if (m_viewMode == ViewMode::Albums) {
                ImGui::TextDisabled("Artwork Size");
                if (ImGui::MenuItem("  Small (100px)", nullptr, m_albumCardSize <= 110.0f)) { m_albumCardSize = 100.0f; savePreferences(); }
                if (ImGui::MenuItem("  Medium (130px)", nullptr, m_albumCardSize > 110.0f && m_albumCardSize <= 145.0f)) { m_albumCardSize = 130.0f; savePreferences(); }
                if (ImGui::MenuItem("  Large (160px)", nullptr, m_albumCardSize > 145.0f && m_albumCardSize <= 180.0f)) { m_albumCardSize = 160.0f; savePreferences(); }
                if (ImGui::MenuItem("  Extra Large (200px)", nullptr, m_albumCardSize > 180.0f)) { m_albumCardSize = 200.0f; savePreferences(); }
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Show Settings...")) {
                m_showPanelsConfigModal = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(m_lockPanels ? "Unlock Panels" : "Lock Panel", nullptr, m_lockPanels)) {
                m_lockPanels = !m_lockPanels;
                savePreferences();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Arrange Panels...")) {
                m_showPanelsConfigModal = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);

        // Inline SlothPlayer continuous A-Z Strip on same toolbar line
        if (m_viewMode == ViewMode::Albums) {
            // Quick Sort dropdown button
            ImGui::SameLine(0.0f, 8.0f);
            const char* sortLabel = "Sort: Artist \xE2\x96\xBE";
            if (m_albumSortMode == AlbumSortMode::Title) sortLabel = "Sort: Title \xE2\x96\xBE";
            else if (m_albumSortMode == AlbumSortMode::Year) sortLabel = "Sort: Year \xE2\x96\xBE";
            else if (m_albumSortMode == AlbumSortMode::TrackCount) sortLabel = "Sort: Tracks \xE2\x96\xBE";

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.16f));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(165, 175, 190, 255));
            if (ImGui::Button(sortLabel, ImVec2(0.0f, 24.0f))) {
                ImGui::OpenPopup("AlbumSortDropdownPopup");
            }
            ImGui::PopStyleColor(4);
            if (ImGui::BeginPopup("AlbumSortDropdownPopup")) {
                if (ImGui::MenuItem("Sort by Artist", nullptr, m_albumSortMode == AlbumSortMode::Artist)) m_albumSortMode = AlbumSortMode::Artist;
                if (ImGui::MenuItem("Sort by Album Title", nullptr, m_albumSortMode == AlbumSortMode::Title)) m_albumSortMode = AlbumSortMode::Title;
                if (ImGui::MenuItem("Sort by Year", nullptr, m_albumSortMode == AlbumSortMode::Year)) m_albumSortMode = AlbumSortMode::Year;
                if (ImGui::MenuItem("Sort by Track Count", nullptr, m_albumSortMode == AlbumSortMode::TrackCount)) m_albumSortMode = AlbumSortMode::TrackCount;
                ImGui::Separator();
                if (ImGui::MenuItem("Ascending (A-Z / 0-9)", nullptr, m_albumSortAscending)) m_albumSortAscending = true;
                if (ImGui::MenuItem("Descending (Z-A / 9-0)", nullptr, !m_albumSortAscending)) m_albumSortAscending = false;
                ImGui::EndPopup();
            }

            const auto& albums = m_library.getAlbums();
            std::set<char> availableLetters;
            for (const auto& alb : albums) {
                if (!alb.name.empty()) {
                    char c = static_cast<char>(toupper(static_cast<unsigned char>(alb.name[0])));
                    if (c >= 'A' && c <= 'Z') availableLetters.insert(c);
                }
            }

            // Divider between Sort dropdown and Alphabet jump strip
            ImVec2 sortBtnMax = ImGui::GetItemRectMax();
            ImVec2 sortBtnMin = ImGui::GetItemRectMin();
            float divX = sortBtnMax.x + 8.0f;
            float divMidY = (sortBtnMin.y + sortBtnMax.y) * 0.5f;
            dl->AddLine(ImVec2(divX, divMidY - 7.0f), ImVec2(divX, divMidY + 7.0f), IM_COL32(255, 255, 255, 28), 1.0f);

            ImGui::SameLine(0.0f, 17.0f);
            float stripStart = ImGui::GetCursorPosX();
            float maxStripW = centerW - (stripStart - curX) - 10.0f;

            if (maxStripW >= 240.0f) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));

                auto renderLetterBtn = [&](const char* lbl, bool isAct, bool hasItems) -> bool {
                    float btnW = 18.0f;
                    float btnH = 20.0f;
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    std::string btnId = std::string("##AZ_") + lbl;
                    bool clicked = ImGui::InvisibleButton(btnId.c_str(), ImVec2(btnW, btnH));
                    bool hov = ImGui::IsItemHovered();
                    ImVec2 b0 = ImGui::GetItemRectMin();
                    ImVec2 b1 = ImGui::GetItemRectMax();
                    ImVec2 ctr((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
                    ImVec2 tSz = ImGui::CalcTextSize(lbl);

                    if (isAct) {
                        dl->AddCircleFilled(ctr, 8.5f, ImGui::GetColorU32(Theme::AccentColor()));
                        dl->AddText(ImVec2(ctr.x - tSz.x * 0.5f, ctr.y - tSz.y * 0.5f), IM_COL32(10, 14, 20, 255), lbl);
                    } else if (hov) {
                        dl->AddCircleFilled(ctr, 8.5f, IM_COL32(255, 255, 255, 28));
                        dl->AddText(ImVec2(ctr.x - tSz.x * 0.5f, ctr.y - tSz.y * 0.5f), IM_COL32(255, 255, 255, 255), lbl);
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    } else {
                        ImU32 col = hasItems ? IM_COL32(175, 184, 198, 220) : IM_COL32(65, 72, 85, 180);
                        dl->AddText(ImVec2(ctr.x - tSz.x * 0.5f, ctr.y - tSz.y * 0.5f), col, lbl);
                    }
                    return clicked;
                };

                // '#' button
                bool isHash = (m_albumAlphabetFilter == '#');
                if (renderLetterBtn("#", isHash, true)) {
                    m_albumAlphabetFilter = isHash ? '\0' : '#';
                }
                ImGui::SameLine();

                for (char ch = 'A'; ch <= 'Z'; ++ch) {
                    bool hasL = (availableLetters.find(ch) != availableLetters.end());
                    bool isAct = (m_albumAlphabetFilter == ch);
                    char lbl[2] = { ch, '\0' };

                    if (renderLetterBtn(lbl, isAct, hasL)) {
                        m_albumAlphabetFilter = isAct ? '\0' : ch;
                    }
                    if (ch < 'Z') ImGui::SameLine();
                }

                ImGui::PopStyleVar();
            }
        }

        curX += centerW;
    }

    // --- RIGHT COLUMN HEADER (Playing Tracks ▾) ---
    if (rightW > 0.0f) {
        dl->AddLine(ImVec2(subP0.x + curX, subP0.y), ImVec2(subP0.x + curX, subP0.y + 30.0f), IM_COL32(38, 42, 50, 255), 1.0f);
        curX += 1.0f;

        ImGui::SetCursorPos(ImVec2(curX + 8.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(190, 195, 205, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.15f));

        const char* ptHeaderLabel = (m_queueViewMode == QueueViewMode::UpcomingTracks)
            ? "Upcoming Tracks \xE2\x96\xBE" : "Playing Tracks \xE2\x96\xBE";
        bool openRightMenu = ImGui::Button(ptHeaderLabel, ImVec2(0.0f, 24.0f)) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if (openRightMenu) {
            ImGui::OpenPopup("PlayingTracksHeaderPopup");
        }
        ImVec2 ptBtnMin = ImGui::GetItemRectMin();
        ImVec2 ptBtnMax = ImGui::GetItemRectMax();
        ImGui::PopStyleColor(4);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.13f, 0.16f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.28f, 0.34f, 1.0f));
        ImGui::SetNextWindowPos(ImVec2(ptBtnMin.x, ptBtnMax.y + 2.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("PlayingTracksHeaderPopup")) {
            if (ImGui::MenuItem("Hide Header Menu")) {
                m_showRightHeaderMenu = false;
                savePreferences();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(m_queueViewMode == QueueViewMode::PlayingTracks ? "\xE2\x97\x86 Playing Tracks" : "   Playing Tracks")) {
                m_queueViewMode = QueueViewMode::PlayingTracks;
            }
            if (ImGui::MenuItem(m_queueViewMode == QueueViewMode::UpcomingTracks ? "\xE2\x97\x86 Upcoming Tracks" : "   Upcoming Tracks")) {
                m_queueViewMode = QueueViewMode::UpcomingTracks;
            }
            ImGui::Separator();
            ImGui::TextColored(Theme::HeaderMuted(), "Playing Tracks Layout:");
            if (ImGui::MenuItem(m_queueLayoutMode == QueueLayoutMode::TrackDetails ? "\xE2\x97\x86 - Track Details" : "   - Track Details")) {
                m_queueLayoutMode = QueueLayoutMode::TrackDetails;
            }
            if (ImGui::MenuItem(m_queueLayoutMode == QueueLayoutMode::AlbumAndTracks ? "\xE2\x97\x86 - Album and Tracks" : "   - Album and Tracks")) {
                m_queueLayoutMode = QueueLayoutMode::AlbumAndTracks;
            }
            if (ImGui::MenuItem(m_queueLayoutMode == QueueLayoutMode::TrackWithThumbnail ? "\xE2\x97\x86 - Track with Thumbnail" : "   - Track with Thumbnail")) {
                m_queueLayoutMode = QueueLayoutMode::TrackWithThumbnail;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Search...")) {
                // Focus search
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Customise Panel...")) {
                m_showPanelsConfigModal = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }

    // Bottom subtle 1px divider for sub-header toolbar
    dl->AddLine(ImVec2(subP0.x, subP0.y + 29.0f), ImVec2(subP0.x + totalW, subP0.y + 29.0f), IM_COL32(34, 38, 46, 255), 1.0f);

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void MainWindow::renderLeftPanel(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::PanelBackground());
    ImGuiWindowFlags leftFlags = ImGuiWindowFlags_None;
    if (m_viewMode == ViewMode::Albums) {
        leftFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }
    ImGui::BeginChild("LeftNavigationPanel", ImVec2(width, height), false, leftFlags);

    if (m_viewMode == ViewMode::MusicExplorer) {
        renderExplorerLeftPanel(width, height);
    } else if (m_viewMode == ViewMode::Playlists) {
        renderPlaylistExplorer(width, height);
    } else if (m_viewMode == ViewMode::Albums) {
        // SlothPlayer Left Albums Thumbnail Browser (Matching Screenshot)
        const auto& albums = m_library.getAlbums();
        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::TextColored(Theme::HeaderMuted(), "Album");
        ImGui::Spacing();

        // 1. "All Albums" item with Vinyl Record Icon (40x40) and Full-Row Click
        bool isAllAlbums = (m_albumAlphabetFilter == '\0' && m_searchBuffer[0] == '\0' && m_expandedAlbumRepId == 0 && m_expandedAlbumName.empty());
        ImGui::PushID("AllAlbumsRow");

        float padLeft = 8.0f;
        float allRowH = 46.0f;
        float allRowW = ImGui::GetContentRegionAvail().x - (padLeft + 4.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padLeft);
        ImVec2 allP0 = ImGui::GetCursorScreenPos();
        ImVec2 allP1 = ImVec2(allP0.x + allRowW, allP0.y + allRowH);

        if (ImGui::InvisibleButton("##AllAlbumsBtn", ImVec2(allRowW, allRowH))) {
            m_albumAlphabetFilter = '\0';
            m_searchBuffer[0] = '\0';
            m_expandedAlbumRepId = 0;
            m_expandedAlbumName.clear();
        }
        bool isAllHovered = ImGui::IsItemHovered();

        if (ImGui::BeginPopupContextItem("AllAlbumsContextMenu")) {
            renderAllAlbumsContextMenu(albums);
            ImGui::EndPopup();
        }

        ImDrawList* leftDl = ImGui::GetWindowDrawList();
        if (isAllAlbums) {
            leftDl->AddRectFilled(allP0, allP1, ImGui::GetColorU32(ImGuiCol_HeaderActive), 4.0f);
        } else if (isAllHovered) {
            leftDl->AddRectFilled(allP0, allP1, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 4.0f);
        }

        // Vinyl Art Icon
        ImTextureID vinylArt = m_textures.getDefaultArtwork();
        if (vinylArt) {
            leftDl->AddImageRounded(vinylArt, ImVec2(allP0.x + 3.0f, allP0.y + 3.0f), ImVec2(allP0.x + 43.0f, allP0.y + 43.0f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
        }

        float allTextX = allP0.x + 43.0f + 10.0f;
        ImU32 allTitleCol = isAllAlbums ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(250, 250, 255, 255);
        leftDl->AddText(ImVec2(allTextX, allP0.y + 4.0f), allTitleCol, "All Albums");

        std::string albCountStr = std::to_string(albums.size()) + " albums";
        leftDl->AddText(ImVec2(allTextX, allP0.y + 24.0f), IM_COL32(185, 195, 210, 255), albCountStr.c_str());

        ImGui::PopID();

        // Right-clicking empty space in the left panel opens the panel customization menu
        if (ImGui::BeginPopupContextWindow("LeftPanelBodyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Album Covers", nullptr, m_viewMode == ViewMode::Albums)) m_viewMode = ViewMode::Albums;
            if (ImGui::MenuItem("Tracks", nullptr, m_viewMode == ViewMode::Tracks)) m_viewMode = ViewMode::Tracks;
            if (ImGui::MenuItem("Artists", nullptr, m_viewMode == ViewMode::Artists)) m_viewMode = ViewMode::Artists;
            if (ImGui::MenuItem("Playlists", nullptr, m_viewMode == ViewMode::Playlists)) m_viewMode = ViewMode::Playlists;
            if (ImGui::MenuItem("Folders", nullptr, m_viewMode == ViewMode::Folders)) m_viewMode = ViewMode::Folders;
            ImGui::Separator();
            if (ImGui::MenuItem("Show Settings...")) {
                m_showPanelsConfigModal = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(m_lockPanels ? "Unlock Panels" : "Lock Panel", nullptr, m_lockPanels)) {
                m_lockPanels = !m_lockPanels;
                savePreferences();
            }
            if (ImGui::MenuItem("Close Panel")) {
                m_showLeftNavigator = false;
                savePreferences();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Arrange Panels...")) {
                m_showPanelsConfigModal = true;
            }
            ImGui::EndPopup();
        }
        ImGui::Spacing();

        // 2. Reactive Album List with 40x40 Thumbnails & Direct Expansion
        ImGui::BeginChild("AlbumsListLeft", ImVec2(0, 0), false);
        for (size_t i = 0; i < albums.size(); ++i) {
            const auto& alb = albums[i];
            ImGui::PushID(static_cast<int>(i));

            const Track* repTrack = m_library.getTrackById(alb.representativeTrackId);
            ImTextureID thumbTex = repTrack ? m_textures.getTrackArtwork(*repTrack) : m_textures.getDefaultArtwork();

            float padLeft = 8.0f;
            float rowH = 46.0f;
            float rowW = ImGui::GetContentRegionAvail().x - (padLeft + 4.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padLeft);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1 = ImVec2(p0.x + rowW, p0.y + rowH);
            bool isSelected = (m_expandedAlbumRepId != 0 && m_expandedAlbumRepId == alb.representativeTrackId);

            // 1. InvisibleButton reserves exact layout space and captures all mouse events
            std::string btnId = "##alb_row_" + std::to_string(alb.representativeTrackId) + "_" + std::to_string(i);
            if (ImGui::InvisibleButton(btnId.c_str(), ImVec2(rowW, rowH))) {
                if (m_expandedAlbumRepId == alb.representativeTrackId) {
                    m_expandedAlbumRepId = 0;
                    m_expandedAlbumName.clear();
                } else {
                    m_expandedAlbumRepId = alb.representativeTrackId;
                    m_expandedAlbumName = alb.name;
                    m_searchBuffer[0] = '\0';
                }
            }

            bool isHovered = ImGui::IsItemHovered();

            renderAlbumContextMenu(alb);

            if (isHovered) {
                ImGui::SetTooltip("%s\n%s (%zu tracks)", alb.name.c_str(), alb.artist.c_str(), alb.trackIds.size());
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (!alb.trackIds.empty()) {
                        playTrack(alb.trackIds[0], alb.trackIds);
                    }
                }
            }

            // 2. Render background highlight directly with DrawList
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (isSelected) {
                dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_HeaderActive), 4.0f);
            } else if (isHovered) {
                dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 4.0f);
            }

            // 3. Render Artwork Thumbnail (42x42) with Aspect-Fill Cropping & rounded corners
            if (thumbTex) {
                int tw = 0, th = 0;
                m_textures.getDimensions(thumbTex, tw, th);
                ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
                TextureManager::getAspectFillUV(tw, th, 1.0f, uv0, uv1);
                dl->AddImageRounded(thumbTex,
                             ImVec2(p0.x + 2.0f, p0.y + 2.0f),
                             ImVec2(p0.x + 44.0f, p0.y + 44.0f),
                             uv0, uv1, IM_COL32_WHITE, 4.0f);
            }

            // 4. Truncate text pixel-accurately
            float textX = p0.x + 44.0f + 8.0f;
            float availTextW = rowW - (44.0f + 14.0f);

            std::string albTitle = alb.name.empty() ? "Unknown Album" : alb.name;
            std::string dispTitle = albTitle;
            if (availTextW > 20.0f && ImGui::CalcTextSize(dispTitle.c_str()).x > availTextW) {
                while (dispTitle.length() > 3 && ImGui::CalcTextSize((dispTitle + "...").c_str()).x > availTextW) {
                    dispTitle.pop_back();
                }
                dispTitle += "...";
            }

            std::string subText = alb.artist;
            if (alb.year > 0) subText += ", " + std::to_string(alb.year);
            std::string dispSub = subText;
            if (availTextW > 20.0f && ImGui::CalcTextSize(dispSub.c_str()).x > availTextW) {
                while (dispSub.length() > 3 && ImGui::CalcTextSize((dispSub + "...").c_str()).x > availTextW) {
                    dispSub.pop_back();
                }
                dispSub += "...";
            }

            // Draw Album Title
            ImU32 titleCol = isSelected ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(250, 250, 255, 255);
            dl->AddText(ImVec2(textX, p0.y + 4.0f), titleCol, dispTitle.c_str());

            // Draw Subtitle: Artist, Year
            ImU32 subCol = IM_COL32(185, 195, 210, 255);
            dl->AddText(ImVec2(textX, p0.y + 24.0f), subCol, dispSub.c_str());

            ImGui::PopID();
        }
        ImGui::EndChild();
    } else {
        // Standard Library Navigation (Collapsible headers with count badges)
        float navPadLeft = 8.0f;
        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::HeaderMuted());
        std::string libHeader = std::string(m_navSectionLibraryOpen ? " \xE2\x96\xBE " : " \xE2\x96\xB8 ") + "LIBRARY";
        if (ImGui::Selectable(libHeader.c_str(), false, ImGuiSelectableFlags_None, ImVec2(100.0f, 0.0f))) {
            m_navSectionLibraryOpen = !m_navSectionLibraryOpen;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, 6.0f);
        if (m_library.isScanning()) {
            ImVec2 spinPos = ImGui::GetCursorScreenPos();
            spinPos.x += 6.0f;
            spinPos.y += 8.0f;
            drawVectorSpinner(ImGui::GetWindowDrawList(), spinPos, 5.0f, 1.5f, ImGui::GetColorU32(Theme::AccentColor()));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);
            ImGui::TextColored(Theme::AccentColor(), "%.0f%%", m_library.getScanProgress() * 100.0f);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140, 150, 165, 200));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.22f));
            if (ImGui::SmallButton("Rescan")) {
                m_library.scanDirectories(m_library.getMonitoredFolders());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Rescan Music Folders (F5)");
            }
            ImGui::PopStyleColor(4);
        }
        ImGui::Separator();

        auto drawNavCountBadge = [](const std::string& countStr, ImVec2 itemMin, ImVec2 itemMax, bool isSelected) {
            if (countStr.empty()) return;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 txtSz = ImGui::CalcTextSize(countStr.c_str());
            float badgeW = txtSz.x + 10.0f;
            float badgeH = 14.0f;
            float badgeX = itemMax.x - badgeW - 6.0f;
            float badgeY = itemMin.y + (itemMax.y - itemMin.y - badgeH) * 0.5f;

            ImU32 bgCol = isSelected ? IM_COL32(255, 255, 255, 26) : IM_COL32(255, 255, 255, 12);
            dl->AddRectFilled(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH), bgCol, 7.0f);

            ImU32 txtCol = isSelected ? IM_COL32(240, 244, 255, 240) : IM_COL32(135, 142, 155, 200);
            dl->AddText(ImVec2(badgeX + 5.0f, badgeY + 0.5f), txtCol, countStr.c_str());
        };

        if (m_navSectionLibraryOpen) {
            float itemW = ImGui::GetContentRegionAvail().x - (navPadLeft + 4.0f);

            bool isAllSelected = (m_navSource == NavSource::AllTracks);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavAllTracks", "All Tracks", NavVectorIcon::AllTracks, m_library.getTracks().size(), isAllSelected, itemW, 26.0f)) {
                m_navSource = NavSource::AllTracks;
            }
            if (ImGui::BeginPopupContextItem("AllTracksCtx")) {
                if (ImGui::MenuItem("Play All")) {
                    std::vector<uint64_t> ids;
                    for (const auto& t : m_library.getTracks()) ids.push_back(t.id);
                    if (!ids.empty()) playTrack(ids[0], ids);
                }
                if (ImGui::MenuItem("Queue All")) {
                    for (const auto& t : m_library.getTracks()) m_queue.push_back(t.id);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Rescan Library", "F5")) {
                    m_library.scanDirectories(m_library.getMonitoredFolders());
                }
                ImGui::EndPopup();
            }

            size_t favCount = 0;
            size_t topRatedCount = 0;
            size_t neverPlayedCount = 0;
            size_t dislikedCount = 0;
            for (const auto& t : m_library.getTracks()) {
                if (t.isFavorite) favCount++;
                if (t.isDisliked) dislikedCount++;
                if (t.rating >= 4) topRatedCount++;
                if (t.playCount == 0) neverPlayedCount++;
            }

            bool isFavSelected = (m_navSource == NavSource::Favorites);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavFavorites", "Favorites", NavVectorIcon::Favorites, favCount, isFavSelected, itemW, 26.0f)) {
                m_navSource = NavSource::Favorites;
            }
            if (ImGui::BeginPopupContextItem("FavoritesCtx")) {
                if (ImGui::MenuItem("Play All Favorites")) {
                    std::vector<uint64_t> ids;
                    for (const auto& t : m_library.getTracks()) if (t.isFavorite) ids.push_back(t.id);
                    if (!ids.empty()) playTrack(ids[0], ids);
                }
                if (ImGui::MenuItem("Queue All Favorites")) {
                    for (const auto& t : m_library.getTracks()) if (t.isFavorite) m_queue.push_back(t.id);
                }
                ImGui::EndPopup();
            }

            bool isTopRatedSelected = (m_navSource == NavSource::TopRated);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavTopRated", "Top Rated", NavVectorIcon::TopRated, topRatedCount, isTopRatedSelected, itemW, 26.0f)) {
                m_navSource = NavSource::TopRated;
            }
            if (ImGui::BeginPopupContextItem("TopRatedCtx")) {
                if (ImGui::MenuItem("Play All Top Rated")) {
                    std::vector<uint64_t> ids;
                    for (const auto& t : m_library.getTracks()) if (t.rating >= 4) ids.push_back(t.id);
                    if (!ids.empty()) playTrack(ids[0], ids);
                }
                if (ImGui::MenuItem("Queue All Top Rated")) {
                    for (const auto& t : m_library.getTracks()) if (t.rating >= 4) m_queue.push_back(t.id);
                }
                ImGui::EndPopup();
            }

            bool isTop25Selected = (m_navSource == NavSource::Top25MostPlayed);
            size_t mostPlayedCount = std::min<size_t>(25, m_library.getTracks().size());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavMostPlayed", "Most Played", NavVectorIcon::MostPlayed, mostPlayedCount, isTop25Selected, itemW, 26.0f)) {
                m_navSource = NavSource::Top25MostPlayed;
            }
            if (ImGui::BeginPopupContextItem("MostPlayedCtx")) {
                if (ImGui::MenuItem("Play Most Played")) {
                    std::vector<const Track*> sorted;
                    for (const auto& t : m_library.getTracks()) sorted.push_back(&t);
                    std::sort(sorted.begin(), sorted.end(), [](const Track* a, const Track* b) { return a->playCount > b->playCount; });
                    std::vector<uint64_t> ids;
                    size_t count = std::min<size_t>(25, sorted.size());
                    for (size_t i = 0; i < count; ++i) ids.push_back(sorted[i]->id);
                    if (!ids.empty()) playTrack(ids[0], ids);
                }
                if (ImGui::MenuItem("Queue Most Played")) {
                    std::vector<const Track*> sorted;
                    for (const auto& t : m_library.getTracks()) sorted.push_back(&t);
                    std::sort(sorted.begin(), sorted.end(), [](const Track* a, const Track* b) { return a->playCount > b->playCount; });
                    size_t count = std::min<size_t>(25, sorted.size());
                    for (size_t i = 0; i < count; ++i) m_queue.push_back(sorted[i]->id);
                }
                ImGui::EndPopup();
            }

            bool isRecentSelected = (m_navSource == NavSource::RecentlyAdded);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavRecentlyAdded", "Recently Added", NavVectorIcon::RecentlyAdded, m_library.getTracks().size(), isRecentSelected, itemW, 26.0f)) {
                m_navSource = NavSource::RecentlyAdded;
            }
            if (ImGui::BeginPopupContextItem("RecentCtx")) {
                if (ImGui::MenuItem("Play Recently Added")) {
                    std::vector<const Track*> sorted;
                    for (const auto& t : m_library.getTracks()) sorted.push_back(&t);
                    std::sort(sorted.begin(), sorted.end(), [](const Track* a, const Track* b) { return a->dateAdded > b->dateAdded; });
                    std::vector<uint64_t> ids;
                    for (const auto* t : sorted) ids.push_back(t->id);
                    if (!ids.empty()) playTrack(ids[0], ids);
                }
                if (ImGui::MenuItem("Queue Recently Added")) {
                    std::vector<const Track*> sorted;
                    for (const auto& t : m_library.getTracks()) sorted.push_back(&t);
                    std::sort(sorted.begin(), sorted.end(), [](const Track* a, const Track* b) { return a->dateAdded > b->dateAdded; });
                    for (const auto* t : sorted) m_queue.push_back(t->id);
                }
                ImGui::EndPopup();
            }

            bool isNeverSelected = (m_navSource == NavSource::NeverPlayed);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavNeverPlayed", "Never Played", NavVectorIcon::NeverPlayed, neverPlayedCount, isNeverSelected, itemW, 26.0f)) {
                m_navSource = NavSource::NeverPlayed;
            }
            if (ImGui::BeginPopupContextItem("NeverPlayedCtx")) {
                if (ImGui::MenuItem("Play Never Played")) {
                    std::vector<uint64_t> ids;
                    for (const auto& t : m_library.getTracks()) if (t.playCount == 0) ids.push_back(t.id);
                    if (!ids.empty()) playTrack(ids[0], ids);
                }
                if (ImGui::MenuItem("Queue Never Played")) {
                    for (const auto& t : m_library.getTracks()) if (t.playCount == 0) m_queue.push_back(t.id);
                }
                ImGui::EndPopup();
            }

            bool isDislikedSelected = (m_navSource == NavSource::Disliked);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavDisliked", "Disliked", NavVectorIcon::Disliked, dislikedCount, isDislikedSelected, itemW, 26.0f)) {
                m_navSource = NavSource::Disliked;
            }
            if (ImGui::BeginPopupContextItem("DislikedNavCtx")) {
                if (ImGui::MenuItem("Play Disliked Tracks (Manual)")) {
                    std::vector<uint64_t> ids;
                    for (const auto& t : m_library.getTracks()) if (t.isDisliked) ids.push_back(t.id);
                    if (!ids.empty()) playTrack(ids[0], ids);
                }
                if (ImGui::MenuItem("Queue Disliked Tracks")) {
                    for (const auto& t : m_library.getTracks()) if (t.isDisliked) m_queue.push_back(t.id);
                }
                if (ImGui::MenuItem("Clear All Dislikes")) {
                    for (const auto& t : m_library.getTracks()) {
                        if (t.isDisliked) m_library.setDisliked(t.id, false);
                    }
                }
                ImGui::EndPopup();
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        const auto& playlists = m_library.getPlaylists();
        const auto& customSpls = m_library.getCustomSmartPlaylists();
        size_t totalPlaylists = playlists.size() + customSpls.size();

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::HeaderMuted());
        std::string plHeader = std::string(m_navSectionPlaylistsOpen ? " \xE2\x96\xBE " : " \xE2\x96\xB8 ") + "PLAYLISTS";
        if (ImGui::Selectable(plHeader.c_str(), false, ImGuiSelectableFlags_None, ImVec2(100.0f, 0.0f))) {
            m_navSectionPlaylistsOpen = !m_navSectionPlaylistsOpen;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        float plBadgeX = ImGui::GetWindowWidth() - 36.0f;
        if (plBadgeX > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(plBadgeX);
        ImGui::TextDisabled("%zu", totalPlaylists);

        ImGui::Separator();

        if (m_navSectionPlaylistsOpen) {
            float itemW = ImGui::GetContentRegionAvail().x - (navPadLeft + 4.0f);
            if (playlists.empty() && customSpls.empty()) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
                ImGui::TextDisabled("   No playlists");
            } else {
                for (const auto& pair : playlists) {
                    bool isPlSelected = (m_navSource == NavSource::Playlist && m_selectedPlaylistName == pair.first);
                    std::string label = "  " + pair.first;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
                    if (ImGui::Selectable(label.c_str(), isPlSelected, 0, ImVec2(itemW, 22.0f))) {
                        m_navSource = NavSource::Playlist;
                        m_selectedPlaylistName = pair.first;
                    }
                    drawNavCountBadge(std::to_string(pair.second.size()), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), isPlSelected);
                    std::string plCtxId = "PlCtx_" + pair.first;
                    if (ImGui::BeginPopupContextItem(plCtxId.c_str())) {
                        if (ImGui::MenuItem("Play Playlist")) {
                            if (!pair.second.empty()) playTrack(pair.second[0], pair.second);
                        }
                        if (ImGui::MenuItem("Queue Playlist")) {
                            m_queue.insert(m_queue.end(), pair.second.begin(), pair.second.end());
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Delete Playlist")) {
                            m_library.deletePlaylist(pair.first);
                            if (m_selectedPlaylistName == pair.first) m_selectedPlaylistName.clear();
                        }
                        ImGui::EndPopup();
                    }
                    if (isPlSelected) {
                        ImVec2 selMin = ImGui::GetItemRectMin();
                        ImVec2 selMax = ImGui::GetItemRectMax();
                        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(selMin.x, selMin.y + 2.0f), ImVec2(selMin.x + 3.0f, selMax.y - 2.0f), ImGui::GetColorU32(Theme::AccentColor()), 2.0f);
                    }
                }

                for (const auto& spl : customSpls) {
                    bool isSplSel = (m_navSource == NavSource::Playlist && m_selectedPlaylistName == spl.name);
                    std::string label = "  " + spl.name;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
                    if (ImGui::Selectable(label.c_str(), isSplSel, 0, ImVec2(itemW, 22.0f))) {
                        m_navSource = NavSource::Playlist;
                        m_selectedPlaylistName = spl.name;
                    }
                    drawNavCountBadge(std::to_string(m_library.evaluateSmartPlaylist(spl).size()), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), isSplSel);
                    std::string splCtxId = "SplCtx_" + spl.name;
                    if (ImGui::BeginPopupContextItem(splCtxId.c_str())) {
                        if (ImGui::MenuItem("Play Smart Playlist")) {
                            auto ids = m_library.evaluateSmartPlaylist(spl);
                            if (!ids.empty()) playTrack(ids[0], ids);
                        }
                        if (ImGui::MenuItem("Queue Smart Playlist")) {
                            auto ids = m_library.evaluateSmartPlaylist(spl);
                            m_queue.insert(m_queue.end(), ids.begin(), ids.end());
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Delete Smart Playlist")) {
                            m_library.deleteCustomSmartPlaylist(spl.name);
                            if (m_selectedPlaylistName == spl.name) m_selectedPlaylistName.clear();
                        }
                        ImGui::EndPopup();
                    }
                    if (isSplSel) {
                        ImVec2 selMin = ImGui::GetItemRectMin();
                        ImVec2 selMax = ImGui::GetItemRectMax();
                        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(selMin.x, selMin.y + 2.0f), ImVec2(selMin.x + 3.0f, selMax.y - 2.0f), ImGui::GetColorU32(Theme::AccentColor()), 2.0f);
                    }
                }
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        const auto& folders = m_library.getMonitoredFolders();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::HeaderMuted());
        std::string foldHeader = std::string(m_navSectionFoldersOpen ? " \xE2\x96\xBE " : " \xE2\x96\xB8 ") + "FOLDERS";
        if (ImGui::Selectable(foldHeader.c_str(), false, ImGuiSelectableFlags_None, ImVec2(100.0f, 0.0f))) {
            m_navSectionFoldersOpen = !m_navSectionFoldersOpen;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        float foldBadgeX = ImGui::GetWindowWidth() - 36.0f;
        if (foldBadgeX > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(foldBadgeX);
        ImGui::TextDisabled("%zu", folders.size());

        ImGui::Separator();

        if (m_navSectionFoldersOpen) {
            float itemW = ImGui::GetContentRegionAvail().x - (navPadLeft + 4.0f);
            for (const auto& f : folders) {
                fs::path p(f);
                std::string folderName = p.filename().string();
                if (folderName.empty()) folderName = f;
                bool isFoldSelected = (m_navSource == NavSource::Folder && m_selectedFolderPath == f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
                if (ImGui::Selectable(("  " + folderName).c_str(), isFoldSelected, 0, ImVec2(itemW, 22.0f))) {
                    m_navSource = NavSource::Folder;
                    m_selectedFolderPath = f;
                }
                std::string fCtxId = "FCtx_" + f;
                if (ImGui::BeginPopupContextItem(fCtxId.c_str())) {
                    if (ImGui::MenuItem("Play Folder")) {
                        std::vector<uint64_t> fIds;
                        for (const auto& t : m_library.getTracks()) {
                            if (t.filePath.rfind(f, 0) == 0) fIds.push_back(t.id);
                        }
                        if (!fIds.empty()) playTrack(fIds[0], fIds);
                    }
                    if (ImGui::MenuItem("Queue Folder")) {
                        for (const auto& t : m_library.getTracks()) {
                            if (t.filePath.rfind(f, 0) == 0) m_queue.push_back(t.id);
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Open in File Explorer")) {
                        Platform::openDirectory(f);
                    }
                    if (ImGui::MenuItem("Rescan This Folder")) {
                        m_library.scanDirectories({f});
                    }
                    ImGui::EndPopup();
                }
                if (isFoldSelected) {
                    ImVec2 selMin = ImGui::GetItemRectMin();
                    ImVec2 selMax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(selMin.x, selMin.y + 2.0f), ImVec2(selMin.x + 3.0f, selMax.y - 2.0f), ImGui::GetColorU32(Theme::AccentColor()), 2.0f);
                }
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::HeaderMuted());
        std::string streamHeader = std::string(m_navSectionStreamingOpen ? " \xE2\x96\xBE " : " \xE2\x96\xB8 ") + "STREAMING & SHOWS";
        if (ImGui::Selectable(streamHeader.c_str(), false, ImGuiSelectableFlags_None, ImVec2(170.0f, 0.0f))) {
            m_navSectionStreamingOpen = !m_navSectionStreamingOpen;
        }
        ImGui::PopStyleColor();

        ImGui::Separator();

        if (m_navSectionStreamingOpen) {
            float itemW = ImGui::GetContentRegionAvail().x - (navPadLeft + 4.0f);

            bool isRadioSelected = (m_navSource == NavSource::RadioStreams);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavRadio", "Web Radio", NavVectorIcon::Radio, m_library.getRadioStreams().size(), isRadioSelected, itemW, 26.0f)) {
                m_navSource = NavSource::RadioStreams;
            }

            bool isPodcastSelected = (m_navSource == NavSource::Podcasts);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavPodcasts", "Podcasts", NavVectorIcon::Podcasts, m_podcasts.getChannels().size(), isPodcastSelected, itemW, 26.0f)) {
                m_navSource = NavSource::Podcasts;
            }

            bool isHistorySelected = (m_navSource == NavSource::History);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + navPadLeft);
            if (drawModernNavRow("##NavHistory", "Playback History", NavVectorIcon::History, m_library.getHistory().size(), isHistorySelected, itemW, 26.0f)) {
                m_navSource = NavSource::History;
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderCenterPanel(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::CardBackground());
    ImGui::BeginChild("CenterMainPanel", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (m_navSource == NavSource::RadioStreams) {
        renderRadioStreamsView();
    } else if (m_navSource == NavSource::Podcasts) {
        renderPodcastsView();
    } else if (m_navSource == NavSource::History) {
        renderHistoryView();
    } else {
        switch (m_viewMode) {
            case ViewMode::NowPlaying:
                renderNowPlayingView();
                break;
            case ViewMode::Tracks:
                renderTracksView();
                break;
            case ViewMode::Albums:
                renderAlbumsView();
                break;
            case ViewMode::Artists:
                renderArtistsView();
                break;
            case ViewMode::Folders:
                renderFoldersView();
                break;
            case ViewMode::Playlists:
                renderPlaylistsView();
                break;
            case ViewMode::MusicExplorer:
                renderMusicExplorerView();
                break;
            case ViewMode::TheaterMode:
                renderTheaterMode();
                break;
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderEmptyDropzone() {
    float availW = ImGui::GetContentRegionAvail().x;
    float availH = ImGui::GetContentRegionAvail().y;

    ImVec2 center = ImVec2(ImGui::GetCursorScreenPos().x + availW * 0.5f, ImGui::GetCursorScreenPos().y + availH * 0.40f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float cardW = std::min(420.0f, availW - 30.0f);
    float cardH = 220.0f;
    ImVec2 p0 = ImVec2(center.x - cardW * 0.5f, center.y - cardH * 0.5f);
    ImVec2 p1 = ImVec2(center.x + cardW * 0.5f, center.y + cardH * 0.5f);

    dl->AddRectFilled(p0, p1, IM_COL32(18, 20, 25, 255), 8.0f);
    dl->AddRect(p0, p1, IM_COL32(40, 44, 54, 255), 8.0f, 0, 1.5f);

    ImGui::SetCursorScreenPos(ImVec2(center.x - 130.0f, center.y - 65.0f));
    ImGui::TextColored(Theme::AccentColor(), "  DRAG & DROP AUDIO FILES HERE");

    ImGui::SetCursorScreenPos(ImVec2(center.x - 145.0f, center.y - 35.0f));
    ImGui::TextDisabled("Supports MP3, FLAC, WAV, OGG, AAC & M4A");

    ImGui::SetCursorScreenPos(ImVec2(center.x - 140.0f, center.y));
    if (ImGui::Button("Open File (Ctrl+O)", ImVec2(135.0f, 28.0f))) {
        openSystemFileDialog();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Folder", ImVec2(135.0f, 28.0f))) {
        openSystemFolderDialog();
    }

    ImGui::SetCursorScreenPos(ImVec2(center.x - 90.0f, center.y + 40.0f));
    if (ImGui::Button("Play Demo FLAC", ImVec2(180.0f, 28.0f))) {
        loadDemoAudio();
    }
}

void MainWindow::renderTrackContextMenu(uint64_t trackId, bool isFromQueue, size_t queueIdx) {
    if (ImGui::BeginPopupContextItem()) {
        const Track* t = m_library.getTrackById(trackId);
        if (t) {
            m_selectedTrackId = trackId;

            bool isMultiQueue = isFromQueue && m_selectedQueueIndices.size() > 1 && m_selectedQueueIndices.count(queueIdx);

            // Title Header Preview
            if (isMultiQueue) {
                std::string header = std::to_string(m_selectedQueueIndices.size()) + " Tracks Selected";
                ImGui::TextColored(Theme::AccentColor(), "%s", header.c_str());
                ImGui::TextDisabled("Now Playing Queue Selection");
            } else {
                std::string titleHeader = t->getDisplayTitle();
                if (titleHeader.length() > 32) titleHeader = titleHeader.substr(0, 30) + "...";
                ImGui::TextColored(Theme::AccentColor(), "%s", titleHeader.c_str());
                ImGui::TextDisabled("%s", t->getDisplayArtist().c_str());
            }
            ImGui::Separator();

            // 1. Play Now
            if (ImGui::MenuItem("Play Now", "Enter")) {
                if (isFromQueue) {
                    playQueueIndex(queueIdx);
                } else {
                    playTrack(trackId);
                }
            }

            // 2. Play Next
            if (ImGui::MenuItem("Play Next")) {
                if (isMultiQueue) {
                    std::vector<uint64_t> ids;
                    for (size_t qIdx : m_selectedQueueIndices) {
                        if (qIdx < m_queue.size()) ids.push_back(m_queue[qIdx]);
                    }
                    if (!ids.empty()) {
                        size_t insertPos = (m_queueIndex + 1 <= m_queue.size()) ? (m_queueIndex + 1) : m_queue.size();
                        m_queue.insert(m_queue.begin() + insertPos, ids.begin(), ids.end());
                    }
                } else {
                    if (m_queue.empty()) {
                        m_queue.push_back(trackId);
                        m_queueIndex = 0;
                    } else {
                        size_t insertPos = (m_queueIndex + 1 <= m_queue.size()) ? (m_queueIndex + 1) : m_queue.size();
                        m_queue.insert(m_queue.begin() + insertPos, trackId);
                    }
                }
                if (m_audio.isShuffle()) {
                    rebuildShuffleOrder(true);
                }
                savePreferences();
            }

            // 3. Queue Last
            if (ImGui::MenuItem("Queue Last")) {
                if (isMultiQueue) {
                    for (size_t qIdx : m_selectedQueueIndices) {
                        if (qIdx < m_queue.size()) m_queue.push_back(m_queue[qIdx]);
                    }
                } else {
                    m_queue.push_back(trackId);
                }
                if (m_audio.isShuffle()) {
                    rebuildShuffleOrder(true);
                }
                savePreferences();
            }

            ImGui::Separator();

            // 4. Favorite Toggle
            if (!isMultiQueue) {
                bool isFav = t->isFavorite;
                if (ImGui::MenuItem(isFav ? "Remove Favorite" : "Add to Favorites", isFav ? "★" : "☆")) {
                    m_library.toggleFavorite(trackId);
                }

                // Dislike Toggle
                bool isDis = t->isDisliked;
                std::string disLabel = isDis ? "Remove Dislike (Allow Autoplay)" : ((m_currentTrackId == trackId) ? "Dislike Track (Skip to next song)" : "Dislike Track (Never Autoplay)");
                if (ImGui::MenuItem(disLabel.c_str(), isDis ? "Disliked" : nullptr)) {
                    m_library.toggleDislike(trackId);
                    const Track* trk = m_library.getTrackById(trackId);
                    if (trk && trk->isDisliked && m_currentTrackId == trackId) {
                        if (m_audio.isShuffle()) {
                            rebuildShuffleOrder(false);
                        }
                        playNext();
                    }
                }
            }

            // 5. Add to Playlist ‣
            if (ImGui::BeginMenu("Add to Playlist")) {
                auto addSelectedToPlaylist = [&](const std::string& plName) {
                    if (isMultiQueue) {
                        for (size_t qIdx : m_selectedQueueIndices) {
                            if (qIdx < m_queue.size()) m_library.addToPlaylist(plName, m_queue[qIdx]);
                        }
                    } else {
                        m_library.addToPlaylist(plName, trackId);
                    }
                };

                if (ImGui::MenuItem("Favorites")) {
                    addSelectedToPlaylist("Favorites");
                }
                const auto& playlists = m_library.getPlaylists();
                for (const auto& pl : playlists) {
                    if (pl.first == "Favorites") continue;
                    if (ImGui::MenuItem(pl.first.c_str())) {
                        addSelectedToPlaylist(pl.first);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("+ New Playlist...")) {
                    m_showCreatePlaylistModal = true;
                }
                ImGui::EndMenu();
            }

            // 6. If inside queue: "Remove from Queue"
            if (isFromQueue) {
                ImGui::Separator();
                std::string removeLabel = isMultiQueue ? ("Remove Selected (" + std::to_string(m_selectedQueueIndices.size()) + ") from Queue") : "Remove from Queue";
                if (ImGui::MenuItem(removeLabel.c_str(), "Del")) {
                    if (isMultiQueue) {
                        removeQueueIndices(m_selectedQueueIndices);
                    } else {
                        removeQueueIndex(queueIdx);
                    }
                }
            }

            ImGui::Separator();

            // 7. Locate in File Manager
            if (ImGui::MenuItem("Locate in File Manager")) {
                Platform::openInFileManager(t->filePath);
            }

            // 8. Copy Track Details (Title - Artist)
            if (ImGui::MenuItem("Copy Title & Artist", "Ctrl+C")) {
                std::string clip = t->getDisplayTitle() + " - " + t->getDisplayArtist();
                ImGui::SetClipboardText(clip.c_str());
            }
            if (ImGui::MenuItem("Copy Album Name")) {
                ImGui::SetClipboardText(t->getDisplayAlbum().c_str());
            }

            // 9. Reset Play Count (SlothPlayer ToolsTagResetPlayCount)
            if (t->playCount > 0) {
                if (ImGui::MenuItem("Reset Play Count")) {
                    Track* mutT = m_library.getTrackById(trackId);
                    if (mutT) mutT->playCount = 0;
                }
            }

            // 10. Properties / Details...
            ImGui::Separator();
            if (ImGui::MenuItem("Track Information...", "Shift+Enter")) {
                showTrackInfoModal(trackId);
            }
            if (ImGui::MenuItem("Properties & Tag Inspector...", "Alt+Enter")) {
                openPropertiesForTrack(trackId);
            }
            if (ImGui::MenuItem("Search Artwork Online...")) {
                openArtworkSearch(t->getDisplayArtist(), t->getDisplayAlbum());
            }

            // 11. Delete Actions
            ImGui::Separator();
            if (m_navSource == NavSource::Playlist && !m_selectedPlaylistName.empty()) {
                if (ImGui::MenuItem("Delete from Playlist", "Del")) {
                    const auto& pls = m_library.getPlaylists();
                    auto it = pls.find(m_selectedPlaylistName);
                    if (it != pls.end()) {
                        for (size_t pIdx = 0; pIdx < it->second.size(); ++pIdx) {
                            if (it->second[pIdx] == trackId) {
                                m_library.removeFromPlaylist(m_selectedPlaylistName, pIdx);
                                break;
                            }
                        }
                    }
                }
            }
            if (ImGui::MenuItem("Delete Track from Library...", "Del")) {
                showDeleteTrackModal(trackId, false);
            }
            if (ImGui::MenuItem("Delete Track from Hard Disk...", "Shift+Del")) {
                showDeleteTrackModal(trackId, true);
            }
        }
        ImGui::EndPopup();
    }
}

void MainWindow::renderAllAlbumsContextMenu(const std::vector<AlbumInfo>& albums) {
    ImGui::TextColored(Theme::AccentColor(), "All Albums");
    ImGui::TextDisabled("%zu albums, %zu tracks", albums.size(), m_library.getTracks().size());
    ImGui::Separator();

    if (ImGui::MenuItem("Play Now", "Alt+Enter")) {
        std::vector<uint64_t> ids;
        for (const auto& t : m_library.getTracks()) ids.push_back(t.id);
        if (!ids.empty()) playTrack(ids[0], ids);
    }
    if (ImGui::MenuItem("Play Shuffled")) {
        std::vector<uint64_t> ids;
        for (const auto& t : m_library.getTracks()) ids.push_back(t.id);
        if (!ids.empty()) {
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(ids.begin(), ids.end(), g);
            playTrack(ids[0], ids);
        }
    }
    if (ImGui::MenuItem("Queue Next", "Ctrl+Shift+Enter")) {
        std::vector<uint64_t> ids;
        for (const auto& t : m_library.getTracks()) ids.push_back(t.id);
        size_t insertPos = (m_queueIndex + 1 <= m_queue.size()) ? (m_queueIndex + 1) : m_queue.size();
        m_queue.insert(m_queue.begin() + insertPos, ids.begin(), ids.end());
    }
    if (ImGui::MenuItem("Queue Random", "Ctrl+Enter")) {
        std::vector<uint64_t> ids;
        for (const auto& t : m_library.getTracks()) ids.push_back(t.id);
        if (!ids.empty()) {
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(ids.begin(), ids.end(), g);
            m_queue.insert(m_queue.end(), ids.begin(), ids.end());
        }
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Add Music Folder...")) {
        m_showAddFolderModal = true;
    }
    if (ImGui::MenuItem("Rescan Library", "F5")) {
        m_library.scanDirectories(m_library.getMonitoredFolders());
    }
    if (ImGui::MenuItem("Library Statistics...")) {
        m_showLibraryStatsModal = true;
    }

    ImGui::Separator();
    if (ImGui::MenuItem(m_lockPanels ? "Unlock Panels" : "Lock Panel", nullptr, m_lockPanels)) {
        m_lockPanels = !m_lockPanels;
        savePreferences();
    }
    if (ImGui::MenuItem("Arrange Panels...")) {
        m_showPanelsConfigModal = true;
    }
}

void MainWindow::renderAlbumContextMenu(const AlbumInfo& alb) {
    if (ImGui::BeginPopupContextItem()) {
        std::string albHeader = alb.name;
        if (albHeader.length() > 32) albHeader = albHeader.substr(0, 30) + "...";
        ImGui::TextColored(Theme::AccentColor(), "%s", albHeader.c_str());
        ImGui::TextDisabled("%s (%zu tracks)", alb.artist.c_str(), alb.trackIds.size());
        ImGui::Separator();

        // 1. Play Album
        if (ImGui::MenuItem("Play Album", "Enter")) {
            if (!alb.trackIds.empty()) {
                playTrack(alb.trackIds[0], alb.trackIds);
            }
        }
        if (ImGui::MenuItem("Play Shuffled")) {
            if (!alb.trackIds.empty()) {
                std::vector<uint64_t> shuffled = alb.trackIds;
                std::random_device rd;
                std::mt19937 g(rd());
                std::shuffle(shuffled.begin(), shuffled.end(), g);
                playTrack(shuffled[0], shuffled);
            }
        }

        // 2. Play Next
        if (ImGui::MenuItem("Play Next")) {
            if (!alb.trackIds.empty()) {
                size_t insertPos = (m_queueIndex + 1 <= m_queue.size()) ? (m_queueIndex + 1) : m_queue.size();
                m_queue.insert(m_queue.begin() + insertPos, alb.trackIds.begin(), alb.trackIds.end());
                if (m_audio.isShuffle()) {
                    rebuildShuffleOrder(true);
                }
                savePreferences();
            }
        }

        // 3. Queue Last
        if (ImGui::MenuItem("Queue Last")) {
            m_queue.insert(m_queue.end(), alb.trackIds.begin(), alb.trackIds.end());
            if (m_audio.isShuffle()) {
                rebuildShuffleOrder(true);
            }
            savePreferences();
        }

        ImGui::Separator();

        // 4. Toggle Expanded Tracklist
        bool isExp = (m_expandedAlbumRepId != 0 && m_expandedAlbumRepId == alb.representativeTrackId);
        if (ImGui::MenuItem(isExp ? "Collapse Tracklist" : "Expand Tracklist")) {
            if (isExp) {
                m_expandedAlbumRepId = 0;
                m_expandedAlbumName.clear();
            } else {
                m_expandedAlbumRepId = alb.representativeTrackId;
                m_expandedAlbumName = alb.name;
            }
        }
        if (ImGui::MenuItem("Search Album Artwork Online...")) {
            openArtworkSearch(alb.artist, alb.name);
        }

        ImGui::Separator();

        // 5. Open Containing Folder in File Manager
        if (ImGui::MenuItem("Open Album Folder in File Manager")) {
            if (!alb.trackIds.empty()) {
                const Track* t = m_library.getTrackById(alb.trackIds[0]);
                if (t) {
                    fs::path p(t->filePath);
                    std::string dir = p.parent_path().string();
                    Platform::openDirectory(dir);
                }
            }
        }

        // 6. Copy Album Info
        if (ImGui::MenuItem("Copy Album Info")) {
            std::string clip = alb.artist + " - " + alb.name;
            if (alb.year > 0) clip += " (" + std::to_string(alb.year) + ")";
            ImGui::SetClipboardText(clip.c_str());
        }

        // 7. Delete Album
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Album from Library...")) {
            showDeleteAlbumModal(alb.name, alb.artist, alb.trackIds, false);
        }
        if (ImGui::MenuItem("Delete Album from Hard Disk...")) {
            showDeleteAlbumModal(alb.name, alb.artist, alb.trackIds, true);
        }

        ImGui::EndPopup();
    }
}

void MainWindow::renderNowPlayingView() {
    float origAvailW = ImGui::GetContentRegionAvail().x;
    float origAvailH = ImGui::GetContentRegionAvail().y;
    float horizPad = 12.0f;
    float topPad = 10.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);
    float availW = origAvailW - (horizPad * 2.0f);
    float availH = origAvailH - (topPad * 2.0f);

    const Track* curr = m_library.getTrackById(m_currentTrackId);
    if (!curr) {
        // Sleek vinyl record placeholder when idle
        ImVec2 center = ImVec2(ImGui::GetCursorScreenPos().x + availW * 0.5f, ImGui::GetCursorScreenPos().y + availH * 0.38f);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        float vinylR = std::min(150.0f, std::min(availW, availH) * 0.30f);
        dl->AddCircleFilled(center, vinylR + 3.0f, IM_COL32(0, 0, 0, 40), 64);
        dl->AddCircleFilled(center, vinylR, IM_COL32(16, 18, 22, 255), 64);
        for (float r = vinylR * 0.35f; r < vinylR * 0.95f; r += 6.0f) {
            dl->AddCircle(center, r, IM_COL32(28, 32, 40, 160), 48, 1.0f);
        }
        dl->AddCircleFilled(center, vinylR * 0.32f, IM_COL32(229, 160, 13, 255), 32);
        dl->AddCircleFilled(center, 6.0f, IM_COL32(12, 14, 18, 255), 16);

        ImGui::SetCursorScreenPos(ImVec2(center.x - 100.0f, center.y + vinylR + 24.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.94f, 1.0f));
        ImGui::Text("No track currently playing");
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(ImVec2(center.x - 80.0f, center.y + vinylR + 56.0f));
        if (ImGui::Button("Play First Track", ImVec2(160.0f, 32.0f))) {
            const auto& allTr = m_library.getTracks();
            if (!allTr.empty()) {
                playTrack(allTr[0].id);
            }
        }
        return;
    }

    // Two-column Theater / Now Playing layout
    float leftW;
    if (availW >= 720.0f) {
        leftW = std::clamp(availW * 0.45f, 300.0f, 500.0f);
    } else {
        leftW = std::floor((availW - 14.0f) * 0.50f);
    }
    float rightW = availW - leftW - 14.0f;

    // --- LEFT COLUMN: Artwork, Metadata, Specs, FFT Spectrum ---
    ImGui::BeginChild("NPLeftPane", ImVec2(leftW, availH), false);

    int artW = 0, artH = 0;
    ImTextureID artTex = m_textures.getTrackArtwork(*curr, &artW, &artH);
    if (!artTex) artTex = m_textures.getDefaultArtwork(&artW, &artH);

    float maxArtSize = std::clamp(leftW - 32.0f, 80.0f, 380.0f);

    ImVec2 fitSize = TextureManager::getAspectFitSize(artW, artH, maxArtSize, maxArtSize);

    ImGui::Spacing();
    ImGui::SetCursorPosX((leftW - fitSize.x) * 0.5f);
    if (artTex) {
        ImVec2 artP0 = ImGui::GetCursorScreenPos();
        // Drop shadow behind artwork
        ImDrawList* npDl = ImGui::GetWindowDrawList();
        npDl->AddRectFilled(ImVec2(artP0.x + 3.0f, artP0.y + 3.0f), ImVec2(artP0.x + fitSize.x + 3.0f, artP0.y + fitSize.y + 3.0f), IM_COL32(0, 0, 0, 50), 8.0f);
        npDl->AddImageRounded(artTex, artP0, ImVec2(artP0.x + fitSize.x, artP0.y + fitSize.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f);
        ImGui::Dummy(fitSize);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Track Title (Large, bold)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextWrapped("%s", curr->getDisplayTitle().c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Artist & Album
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::TextWrapped("%s", curr->getDisplayArtist().c_str());
    ImGui::PopStyleColor();

    std::string albYear = curr->getDisplayAlbum();
    if (curr->year > 0) albYear += " (" + std::to_string(curr->year) + ")";
    ImGui::TextDisabled("%s", albYear.c_str());

    ImGui::Spacing();

    // Loved & Star Ratings
    ImVec2 heartPos = ImGui::GetCursorScreenPos();
    heartPos.x += 10.0f; heartPos.y += 10.0f;
    std::string npHId = "##np_fav_" + std::to_string(curr->id);
    ImGui::SetCursorScreenPos(ImVec2(heartPos.x - 10.0f, heartPos.y - 10.0f));
    if (ImGui::InvisibleButton(npHId.c_str(), ImVec2(20.0f, 20.0f))) {
        m_library.toggleFavorite(curr->id);
    }
    drawVectorHeart(ImGui::GetWindowDrawList(), heartPos, 7.0f, curr->isFavorite, ImGui::IsItemHovered());

    ImGui::SameLine(0.0f, 8.0f);
    ImVec2 npDisPos = ImGui::GetCursorScreenPos();
    npDisPos.x += 10.0f; npDisPos.y += 10.0f;
    std::string npDisId = "##np_dis_" + std::to_string(curr->id);
    ImGui::SetCursorScreenPos(ImVec2(npDisPos.x - 10.0f, npDisPos.y - 10.0f));
    if (ImGui::InvisibleButton(npDisId.c_str(), ImVec2(20.0f, 20.0f))) {
        m_library.toggleDislike(curr->id);
        const Track* trk = m_library.getTrackById(curr->id);
        if (trk && trk->isDisliked) {
            if (m_audio.isShuffle()) {
                rebuildShuffleOrder(false);
            }
            playNext();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(curr->isDisliked ? "Remove Dislike (Song can autoplay)" : "Dislike Song (Suddenly skip to next song)");
    }
    drawVectorDislike(ImGui::GetWindowDrawList(), npDisPos, 6.5f, curr->isDisliked, ImGui::IsItemHovered());

    ImGui::SameLine(0.0f, 18.0f);
    ImVec2 starPos = ImGui::GetCursorScreenPos();
    starPos.y += 2.0f;
    int currRating = curr->rating;
    drawInteractiveStarRating(ImGui::GetWindowDrawList(), starPos, currRating, curr->id);
    ImGui::NewLine();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Technical Specs (Bitrate, Sample Rate, Format)
    if (curr->bitrate > 0 || curr->sampleRate > 0) {
        ImGui::TextDisabled("AUDIO SPECS: %s | %d kbps | %d Hz | %s",
            curr->filePath.substr(curr->filePath.find_last_of(".") + 1).c_str(),
            curr->bitrate, curr->sampleRate,
            (curr->channels == 2 ? "Stereo" : (curr->channels == 1 ? "Mono" : "Multi-channel")));
    }

    ImGui::Spacing();

    // 32-Band Full-Width Visualizer
    std::vector<float> spectrumBars;
    m_audio.getSpectrum(spectrumBars, 32);
    if (!spectrumBars.empty()) {
        float specW = std::max(20.0f, leftW - 32.0f);
        float specH = 42.0f;
        float stepW = specW / static_cast<float>(spectrumBars.size());
        ImVec2 spPos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        for (size_t b = 0; b < spectrumBars.size(); ++b) {
            float val = std::clamp(spectrumBars[b], 0.04f, 1.0f);
            float h = val * specH;
            ImVec2 b0(spPos.x + b * stepW, spPos.y + (specH - h));
            ImVec2 b1(b0.x + std::max(1.0f, stepW - 2.0f), spPos.y + specH);
            ImU32 bCol = (val > 0.70f) ? IM_COL32(255, 195, 25, 255) : IM_COL32(220, 150, 15, 220);
            dl->AddRectFilled(b0, b1, bCol, 2.0f);
        }
        ImGui::Dummy(ImVec2(specW, specH + 8.0f));
    }

    ImGui::EndChild();

    // --- RIGHT COLUMN: Synchronized Karaoke Lyrics or Rich Stage ---
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::BeginChild("NPRightPane", ImVec2(rightW, availH), false);
    const LyricsData& lyrics = getCachedLyrics(*curr);
    if (lyrics.hasLyrics) {
        renderLyricsPanel(rightW, availH);
    } else {
        renderNowPlayingStage(curr, rightW, availH);
    }
    ImGui::EndChild();
}

void MainWindow::renderTracksView() {
    const auto& allTracks = m_library.getTracks();
    if (allTracks.empty()) {
        renderEmptyDropzone();
        return;
    }

    std::vector<const Track*> visibleTracks;
    std::string searchLower = m_searchBuffer;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    if (m_navSource == NavSource::AllTracks) {
        for (const auto& t : allTracks) visibleTracks.push_back(&t);
    } else if (m_navSource == NavSource::Favorites) {
        for (const auto& t : allTracks) {
            if (t.isFavorite) visibleTracks.push_back(&t);
        }
    } else if (m_navSource == NavSource::TopRated) {
        for (const auto& t : allTracks) {
            if (t.rating >= 4) visibleTracks.push_back(&t);
        }
    } else if (m_navSource == NavSource::RecentlyAdded) {
        for (const auto& t : allTracks) visibleTracks.push_back(&t);
        std::sort(visibleTracks.begin(), visibleTracks.end(), [](const Track* a, const Track* b) {
            return a->dateAdded > b->dateAdded;
        });
    } else if (m_navSource == NavSource::Top25MostPlayed) {
        std::vector<const Track*> sorted;
        for (const auto& t : allTracks) sorted.push_back(&t);
        std::sort(sorted.begin(), sorted.end(), [](const Track* a, const Track* b) {
            return a->playCount > b->playCount;
        });
        size_t count = std::min<size_t>(25, sorted.size());
        for (size_t i = 0; i < count; ++i) visibleTracks.push_back(sorted[i]);
    } else if (m_navSource == NavSource::NeverPlayed) {
        for (const auto& t : allTracks) {
            if (t.playCount == 0) visibleTracks.push_back(&t);
        }
    } else if (m_navSource == NavSource::Disliked) {
        for (const auto& t : allTracks) {
            if (t.isDisliked) visibleTracks.push_back(&t);
        }
    } else if (m_navSource == NavSource::Playlist) {
        const auto& playlists = m_library.getPlaylists();
        auto it = playlists.find(m_selectedPlaylistName);
        if (it != playlists.end()) {
            for (uint64_t tid : it->second) {
                const Track* t = m_library.getTrackById(tid);
                if (t) visibleTracks.push_back(t);
            }
        } else {
            for (const auto& spl : m_library.getCustomSmartPlaylists()) {
                if (spl.name == m_selectedPlaylistName) {
                    std::vector<uint64_t> ids = m_library.evaluateSmartPlaylist(spl);
                    for (uint64_t tid : ids) {
                        const Track* t = m_library.getTrackById(tid);
                        if (t) visibleTracks.push_back(t);
                    }
                    break;
                }
            }
        }
    } else if (m_navSource == NavSource::Folder) {
        for (const auto& t : allTracks) {
            if (t.filePath.rfind(m_selectedFolderPath, 0) == 0) {
                visibleTracks.push_back(&t);
            }
        }
    }

    if (m_showJumpbar) {
        renderAZJumpBar();
    }

    // Column Browser: Genre > Artist > Album multi-tier filter
    if (m_showColumnBrowser) {
        float cbHorizPad = 14.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cbHorizPad);
        float cbW = ImGui::GetContentRegionAvail().x - (cbHorizPad * 2.0f);
        std::vector<const Track*> cbFiltered;
        renderColumnBrowser(cbW, m_columnBrowserHeight, visibleTracks, cbFiltered);
        visibleTracks = std::move(cbFiltered);
    }

    if (m_activeJumpLetter != '\0') {
        std::vector<const Track*> jumpFiltered;
        for (const Track* t : visibleTracks) {
            std::string title = t->getDisplayTitle();
            if (title.empty()) continue;
            char first = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
            if (m_activeJumpLetter == '#') {
                if (!std::isalpha(static_cast<unsigned char>(first))) jumpFiltered.push_back(t);
            } else if (first == m_activeJumpLetter) {
                jumpFiltered.push_back(t);
            }
        }
        visibleTracks = std::move(jumpFiltered);
    }

    if (!searchLower.empty()) {
        std::vector<const Track*> filtered;
        for (const Track* t : visibleTracks) {
            std::string title = t->getDisplayTitle();
            std::string artist = t->getDisplayArtist();
            std::string album = t->getDisplayAlbum();
            std::transform(title.begin(), title.end(), title.begin(), ::tolower);
            std::transform(artist.begin(), artist.end(), artist.begin(), ::tolower);
            std::transform(album.begin(), album.end(), album.begin(), ::tolower);

            if (title.find(searchLower) != std::string::npos ||
                artist.find(searchLower) != std::string::npos ||
                album.find(searchLower) != std::string::npos) {
                filtered.push_back(t);
            }
        }
        visibleTracks = std::move(filtered);
    }

    if (visibleTracks.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("   No tracks match current filters.");
        return;
    }

    std::sort(visibleTracks.begin(), visibleTracks.end(), [this](const Track* a, const Track* b) {
        bool res = false;
        switch (m_sortColumn) {
            case 1: res = a->trackNumber < b->trackNumber; break;
            case 2: res = a->getDisplayTitle() < b->getDisplayTitle(); break;
            case 3: res = a->getDisplayArtist() < b->getDisplayArtist(); break;
            case 4: res = a->getDisplayAlbum() < b->getDisplayAlbum(); break;
            case 5: res = a->duration < b->duration; break;
            case 6: res = a->rating < b->rating; break;
            case 7: res = a->genre < b->genre; break;
            case 8: res = a->year < b->year; break;
            default: res = a->id < b->id; break;
        }
        return m_sortAscending ? res : !res;
    });

    float horizPad = 14.0f;
    float topPad = 8.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);

    float availW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f);

    // Column Browser toggle button
    if (m_showColumnBrowser) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(Theme::AccentColor().x, Theme::AccentColor().y, Theme::AccentColor().z, 0.35f));
    if (ImGui::SmallButton("Columns")) {
        m_showColumnBrowser = !m_showColumnBrowser;
    }
    if (m_showColumnBrowser) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Column Browser (Genre / Artist / Album)");

    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextDisabled("%zu tracks", visibleTracks.size());
    if (m_selectedTrackId > 0) {
        ImGui::SameLine(0.0f, 16.0f);
        if (m_navSource == NavSource::Playlist && !m_selectedPlaylistName.empty()) {
            if (ImGui::SmallButton("Delete from Playlist")) {
                const auto& pls = m_library.getPlaylists();
                auto it = pls.find(m_selectedPlaylistName);
                if (it != pls.end()) {
                    for (size_t pIdx = 0; pIdx < it->second.size(); ++pIdx) {
                        if (it->second[pIdx] == m_selectedTrackId) {
                            m_library.removeFromPlaylist(m_selectedPlaylistName, pIdx);
                            break;
                        }
                    }
                }
            }
            ImGui::SameLine(0.0f, 8.0f);
        }
        if (ImGui::SmallButton("Delete Track...")) {
            showDeleteTrackModal(m_selectedTrackId, false);
        }
    }

    int numCols = 10;
    if (availW < 520.0f) {
        numCols = 5;
    } else if (availW < 780.0f) {
        numCols = 7;
    }

    ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                 ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                 ImGuiTableFlags_Sortable | ImGuiTableFlags_Hideable;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    if (ImGui::BeginTable("TracksTable", numCols, tableFlags, ImVec2(availW, -1.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##Status", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 20.0f);
        ImGui::TableSetupColumn("##FavDis", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 38.0f);

        if (numCols >= 7) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f, 1);
        }

        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 0.35f, 2);
        ImGui::TableSetupColumn("Artist", ImGuiTableColumnFlags_WidthStretch, 0.25f, 3);

        if (numCols >= 7) {
            ImGui::TableSetupColumn("Album", ImGuiTableColumnFlags_WidthStretch, 0.22f, 4);
        }

        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 48.0f, 5);

        if (numCols >= 10) {
            ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthFixed, 78.0f, 6);
            ImGui::TableSetupColumn("Genre", ImGuiTableColumnFlags_WidthStretch, 0.12f, 7);
            ImGui::TableSetupColumn("Year", ImGuiTableColumnFlags_WidthFixed, 42.0f, 8);
        }

        ImGui::TableHeadersRow();

        ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
        if (sortSpecs && sortSpecs->SpecsDirty) {
            if (sortSpecs->SpecsCount > 0) {
                m_sortColumn = sortSpecs->Specs[0].ColumnUserID;
                m_sortAscending = (sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
            }
            sortSpecs->SpecsDirty = false;
        }

        std::vector<uint64_t> currentTrackIds;
        for (const Track* t : visibleTracks) currentTrackIds.push_back(t->id);

        for (const Track* t : visibleTracks) {
            ImGui::PushID(static_cast<int>(t->id));
            ImGui::TableNextRow(0, 28.0f);
            bool isCurrent = (t->id == m_currentTrackId);
            bool isSelected = (t->id == m_selectedTrackId);
            int colIdx = 0;

            // Col 0: Status (Play/Pause indicator)
            ImGui::TableSetColumnIndex(colIdx++);
            if (isCurrent) {
                ImVec2 statusCenter = ImGui::GetCursorScreenPos();
                statusCenter.x += 8.0f;
                statusCenter.y += 10.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 statCol = ImGui::GetColorU32(Theme::AccentColor());
                if (m_audio.isPlaying()) {
                    drawMicroEqualizer(dl, statusCenter, statCol, true);
                } else {
                    dl->AddRectFilled(ImVec2(statusCenter.x - 3.5f, statusCenter.y - 4.0f), ImVec2(statusCenter.x - 1.0f, statusCenter.y + 4.0f), statCol, 0.5f);
                    dl->AddRectFilled(ImVec2(statusCenter.x + 1.0f, statusCenter.y - 4.0f), ImVec2(statusCenter.x + 3.5f, statusCenter.y + 4.0f), statCol, 0.5f);
                }
            }

            // Col 1: Loved Heart & Dislike Toggles
            ImGui::TableSetColumnIndex(colIdx++);
            ImVec2 basePos = ImGui::GetCursorScreenPos();
            ImDrawList* rowDl = ImGui::GetWindowDrawList();

            ImVec2 mousePos = ImGui::GetIO().MousePos;
            bool isRowHovered = (mousePos.y >= basePos.y - 2.0f && mousePos.y < basePos.y + 26.0f && ImGui::IsWindowHovered());

            // Heart button
            ImVec2 heartCenter(basePos.x + 8.0f, basePos.y + 10.0f);
            ImGui::SetCursorScreenPos(ImVec2(heartCenter.x - 8.0f, heartCenter.y - 8.0f));
            std::string hId = "##fav_btn_" + std::to_string(t->id);
            if (ImGui::InvisibleButton(hId.c_str(), ImVec2(16.0f, 16.0f))) {
                m_library.toggleFavorite(t->id);
            }
            bool isHeartHovered = ImGui::IsItemHovered();
            if (isHeartHovered) ImGui::SetTooltip(t->isFavorite ? "Unmark Favorite" : "Mark Favorite");
            if (t->isFavorite || isHeartHovered || isRowHovered) {
                drawVectorHeart(rowDl, heartCenter, 5.5f, t->isFavorite, isHeartHovered);
            }

            // Dislike button
            ImVec2 disCenter(basePos.x + 26.0f, basePos.y + 10.0f);
            ImGui::SetCursorScreenPos(ImVec2(disCenter.x - 8.0f, disCenter.y - 8.0f));
            std::string dId = "##dis_btn_" + std::to_string(t->id);
            if (ImGui::InvisibleButton(dId.c_str(), ImVec2(16.0f, 16.0f))) {
                m_library.toggleDislike(t->id);
                const Track* trk = m_library.getTrackById(t->id);
                if (trk && trk->isDisliked && m_currentTrackId == t->id) {
                    if (m_audio.isShuffle()) {
                        rebuildShuffleOrder(false);
                    }
                    playNext();
                }
            }
            bool isDisHovered = ImGui::IsItemHovered();
            if (isDisHovered) ImGui::SetTooltip(t->isDisliked ? "Remove Dislike (Allow Autoplay)" : (m_currentTrackId == t->id ? "Dislike Track (Suddenly skip to next song)" : "Dislike Track (Never Autoplay)"));
            if (t->isDisliked || isDisHovered || isRowHovered) {
                drawVectorDislike(rowDl, disCenter, 5.0f, t->isDisliked, isDisHovered);
            }

            // Col 2: Track #
            if (numCols >= 7) {
                ImGui::TableSetColumnIndex(colIdx++);
                if (t->trackNumber > 0) {
                    ImGui::TextDisabled("%d", t->trackNumber);
                } else {
                    ImGui::TextDisabled("-");
                }
            }

            // Col 3: Title
            ImGui::TableSetColumnIndex(colIdx++);
            if (isCurrent) {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
            } else if (t->isDisliked) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.48f, 0.50f, 1.0f));
            }

            ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick;
            std::string titleStr = t->isDisliked ? (t->getDisplayTitle() + " [Disliked]") : t->getDisplayTitle();
            if (ImGui::Selectable(titleStr.c_str(), isSelected, selFlags)) {
                m_selectedTrackId = t->id;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    playTrack(t->id, currentTrackIds);
                }
            }
            if (isCurrent || t->isDisliked) ImGui::PopStyleColor();

            // Context Menu
            renderTrackContextMenu(t->id, false, 0);

            // Col 4: Artist
            ImGui::TableSetColumnIndex(colIdx++);
            ImGui::TextUnformatted(t->getDisplayArtist().c_str());

            // Col 5: Album
            if (numCols >= 7) {
                ImGui::TableSetColumnIndex(colIdx++);
                ImGui::TextUnformatted(t->getDisplayAlbum().c_str());
            }

            // Col 6: Time
            ImGui::TableSetColumnIndex(colIdx++);
            ImGui::TextDisabled("%s", t->formatDuration().c_str());

            // Col 7, 8, 9: Rating, Genre & Year
            if (numCols >= 10) {
                ImGui::TableSetColumnIndex(colIdx++);
                ImVec2 rPos = ImGui::GetCursorScreenPos();
                rPos.y += 4.0f;
                int trRating = t->rating;
                drawInteractiveStarRating(ImGui::GetWindowDrawList(), rPos, trRating, t->id);

                ImGui::TableSetColumnIndex(colIdx++);
                ImGui::TextUnformatted(t->genre.empty() ? "-" : t->genre.c_str());

                ImGui::TableSetColumnIndex(colIdx++);
                if (t->year > 0) ImGui::TextDisabled("%d", t->year);
                else ImGui::TextDisabled("-");
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}
static std::string truncateTextToWidth(const std::string& text, float maxWidth) {
    if (text.empty() || maxWidth <= 0.0f) return "";
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;

    const std::string ellipsis = "...";
    float ellipsisW = ImGui::CalcTextSize(ellipsis.c_str()).x;
    if (ellipsisW >= maxWidth) return "";

    float targetW = maxWidth - ellipsisW;
    size_t lastValidByte = 0;
    size_t i = 0;
    while (i < text.size()) {
        size_t nextI = i + 1;
        while (nextI < text.size() && (static_cast<unsigned char>(text[nextI]) & 0xC0) == 0x80) {
            nextI++;
        }
        float curW = ImGui::CalcTextSize(text.data(), text.data() + nextI).x;
        if (curW > targetW) {
            break;
        }
        lastValidByte = nextI;
        i = nextI;
    }

    if (lastValidByte == 0) return ellipsisW <= maxWidth ? ellipsis : "";
    return text.substr(0, lastValidByte) + ellipsis;
}

void MainWindow::renderAlbumsView() {
    const auto& albums = m_library.getAlbums();
    if (albums.empty()) {
        renderEmptyDropzone();
        return;
    }

    std::vector<const AlbumInfo*> filteredAlbums;
    std::string searchLower = m_searchBuffer;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    for (const auto& alb : albums) {
        if (!searchLower.empty()) {
            std::string nameLower = alb.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find(searchLower) == std::string::npos) continue;
        }
        if (m_albumAlphabetFilter != '\0') {
            char firstCh = alb.name.empty() ? '#' : static_cast<char>(toupper(alb.name[0]));
            if (m_albumAlphabetFilter == '#') {
                if (firstCh >= 'A' && firstCh <= 'Z') continue;
            } else {
                if (firstCh != m_albumAlphabetFilter) continue;
            }
        }
        filteredAlbums.push_back(&alb);
    }

    // Apply AlbumSortMode sorting
    std::sort(filteredAlbums.begin(), filteredAlbums.end(), [this](const AlbumInfo* a, const AlbumInfo* b) {
        bool res = false;
        switch (m_albumSortMode) {
            case AlbumSortMode::Artist:
                if (a->artist != b->artist) res = a->artist < b->artist;
                else res = a->name < b->name;
                break;
            case AlbumSortMode::Title:
                res = a->name < b->name;
                break;
            case AlbumSortMode::Year:
                if (a->year != b->year) res = a->year < b->year;
                else res = a->name < b->name;
                break;
            case AlbumSortMode::TrackCount:
                if (a->trackIds.size() != b->trackIds.size()) res = a->trackIds.size() < b->trackIds.size();
                else res = a->name < b->name;
                break;
        }
        return m_albumSortAscending ? res : !res;
    });

    // Dense Responsive Album Grid (Symmetric layout matching SlothPlayer signature presentation)
    ImGui::BeginChild("AlbumsGridDense", ImVec2(0, 0), false);
    float horizPad = 14.0f;
    float topPad = 12.0f;
    float gridAvailW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f) - 4.0f;
    float targetCardW = m_albumCardSize; // 150px target size
    float minGap = 16.0f;

    int cols = static_cast<int>((gridAvailW + minGap) / (targetCardW + minGap));
    if (cols < 1) cols = 1;

    // Distribute available width evenly and symmetrically across all columns
    float cardW = std::floor((gridAvailW - (cols - 1) * minGap) / static_cast<float>(cols));
    if (cardW < 40.0f) cardW = 40.0f;
    float remainingSpace = gridAvailW - (cols * cardW);
    float cardGap = (cols > 1) ? (remainingSpace / static_cast<float>(cols - 1)) : 0.0f;

    float gridStartX = ImGui::GetCursorPosX() + horizPad;
    float rowStartY = ImGui::GetCursorPosY() + topPad;
    float rowGap = 20.0f;
    float cardTotalH = cardW + ImGui::GetStyle().ItemSpacing.y * 2.0f + ImGui::GetTextLineHeight() * 2.0f;

    const AlbumInfo* expandedAlbumInRow = nullptr;
    float arrowCenterXInRow = 0.0f;

    for (size_t i = 0; i < filteredAlbums.size(); ++i) {
        int colIdx = static_cast<int>(i % cols);

        if (colIdx == 0) {
            // Check if any album in this row is currently expanded
            bool rowContainsExpanded = false;
            if (m_expandedAlbumRepId != 0) {
                for (size_t checkI = i; checkI < std::min(i + cols, filteredAlbums.size()); ++checkI) {
                    if (filteredAlbums[checkI]->representativeTrackId == m_expandedAlbumRepId) {
                        rowContainsExpanded = true;
                        break;
                    }
                }
            }

            // Viewport culling: if this entire row is well off-screen and not expanded, skip drawing it
            float scrollY = ImGui::GetScrollY();
            float winH = ImGui::GetWindowHeight();
            bool isRowVisible = (rowStartY + cardTotalH >= scrollY - 60.0f) && (rowStartY <= scrollY + winH + 60.0f);

            if (!isRowVisible && !rowContainsExpanded) {
                size_t remainingInRow = std::min(static_cast<size_t>(cols), filteredAlbums.size() - i);
                i += remainingInRow - 1; // loop increment does the +1
                rowStartY += cardTotalH + rowGap;
                continue;
            }
        }

        float cardPosX = gridStartX + colIdx * (cardW + cardGap);

        // Deterministic positioning prevents any column shift or asymmetrical drift
        ImGui::SetCursorPos(ImVec2(cardPosX, rowStartY));

        const AlbumInfo* alb = filteredAlbums[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginGroup();

        int artW = 0, artH = 0;
        const Track* repTrack = m_library.getTrackById(alb->representativeTrackId);
        ImTextureID artTex = repTrack ? m_textures.getTrackArtwork(*repTrack, &artW, &artH) : m_textures.getDefaultArtwork(&artW, &artH);
        ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
        TextureManager::getAspectFillUV(artW, artH, 1.0f, uv0, uv1);

        bool isSelected = (m_selectedAlbumRepId != 0 && m_selectedAlbumRepId == alb->representativeTrackId)
                       || (m_expandedAlbumRepId != 0 && m_expandedAlbumRepId == alb->representativeTrackId);

        ImVec2 artMin = ImGui::GetCursorScreenPos();
        if (artTex) {
            bool cardClicked = ImGui::InvisibleButton("##art", ImVec2(cardW, cardW));
            bool isCardHovered = ImGui::IsItemHovered();

            ImDrawList* dl = ImGui::GetWindowDrawList();

            ImVec2 cMin(artMin.x, artMin.y);
            ImVec2 cMax(artMin.x + cardW, artMin.y + cardW);

            // 1. Drop shadow behind card (drawn BEFORE artwork so it never overlays image)
            float shadowOffset = isCardHovered ? 3.0f : 2.0f;
            dl->AddRectFilled(ImVec2(cMin.x + 1.0f, cMin.y + shadowOffset),
                              ImVec2(cMax.x + 1.0f, cMax.y + shadowOffset + 1.0f),
                              IM_COL32(0, 0, 0, isCardHovered ? 70 : 45), 6.0f);

            // 2. Artwork with crisp 6.0f rounded corners (never pokes out of border!)
            dl->AddImageRounded(artTex, cMin, cMax, uv0, uv1, IM_COL32_WHITE, 6.0f);

            // 3. Selection / hover border with matching 6.0f rounded corners (flush to card, never buffed/puffed out)
            if (isSelected) {
                ImVec4 acc = Theme::AccentColor();
                dl->AddRect(cMin, cMax, ImGui::GetColorU32(acc), 6.0f, 0, 1.2f);
            } else if (isCardHovered) {
                // Clean, subtle hover border — elegant highlight without jarring neon pop
                dl->AddRect(cMin, cMax, IM_COL32(255, 255, 255, 60), 6.0f, 0, 1.0f);
            } else {
                // Subtle clean hairline border to frame card edge against dark background
                dl->AddRect(cMin, cMax, IM_COL32(255, 255, 255, 24), 6.0f, 0, 1.0f);
            }

            if (cardClicked) {
                m_selectedAlbumRepId = alb->representativeTrackId;
                m_selectedAlbumName = alb->name;
                if (m_expandedAlbumRepId == alb->representativeTrackId) {
                    m_expandedAlbumRepId = 0;
                    m_expandedAlbumName.clear();
                } else {
                    m_expandedAlbumRepId = alb->representativeTrackId;
                    m_expandedAlbumName = alb->name;
                }
            }

            if (isCardHovered) {
                ImGui::SetTooltip("%s\n%s (%zu tracks)\nClick to expand tracks \xE2\x80\xA2 Double-click to listen", alb->name.c_str(), alb->artist.c_str(), alb->trackIds.size());
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (!alb->trackIds.empty()) {
                        playTrack(alb->trackIds[0], alb->trackIds);
                    }
                }
            }
        }

        renderAlbumContextMenu(*alb);

        if (m_expandedAlbumRepId != 0 && m_expandedAlbumRepId == alb->representativeTrackId) {
            expandedAlbumInRow = alb;
            arrowCenterXInRow = artMin.x + cardW * 0.5f;
        }

        // Center Album Title & Artist clamped strictly to cardW (SlothPlayer signature styling)
        std::string albName = truncateTextToWidth(alb->name, cardW);
        ImVec2 titleSz = ImGui::CalcTextSize(albName.c_str());
        float titleOffX = std::max(0.0f, (cardW - titleSz.x) * 0.5f);
        ImGui::SetCursorPosX(cardPosX + titleOffX);
        ImGui::TextUnformatted(albName.c_str());

        std::string fullArtName = alb->artist;
        if (alb->year > 0) fullArtName += ", " + std::to_string(alb->year);
        std::string artName = truncateTextToWidth(fullArtName, cardW);
        ImVec2 artSz = ImGui::CalcTextSize(artName.c_str());
        float artOffX = std::max(0.0f, (cardW - artSz.x) * 0.5f);
        ImGui::SetCursorPosX(cardPosX + artOffX);
        ImGui::TextDisabled("%s", artName.c_str());

        ImGui::EndGroup();
        ImGui::PopID();

        bool isRowEnd = (colIdx == cols - 1 || (i + 1) == filteredAlbums.size());
        if (isRowEnd) {
            rowStartY += cardTotalH + rowGap;
            if (expandedAlbumInRow) {
                ImGui::SetCursorPos(ImVec2(gridStartX, rowStartY));
                renderExpandedAlbumPanel(*expandedAlbumInRow, gridAvailW, arrowCenterXInRow);
                expandedAlbumInRow = nullptr;
                rowStartY = ImGui::GetCursorPosY() + rowGap;
            }
        }
    }

    // Inform ImGui of the total scrollable height
    ImGui::SetCursorPos(ImVec2(gridStartX, rowStartY));
    ImGui::Dummy(ImVec2(gridAvailW, 10.0f));

    ImGui::EndChild();
}

void MainWindow::renderExpandedAlbumPanel(const AlbumInfo& alb, float width, float arrowCenterX) {
    // 1. Sequential sort by disc number then track number
    std::vector<uint64_t> sortedTrackIds = alb.trackIds;
    std::sort(sortedTrackIds.begin(), sortedTrackIds.end(), [this](uint64_t aId, uint64_t bId) {
        const Track* a = m_library.getTrackById(aId);
        const Track* b = m_library.getTrackById(bId);
        if (!a || !b) return aId < bId;
        if (a->discNumber != b->discNumber && a->discNumber > 0 && b->discNumber > 0) {
            return a->discNumber < b->discNumber;
        }
        if (a->trackNumber != b->trackNumber && a->trackNumber > 0 && b->trackNumber > 0) {
            return a->trackNumber < b->trackNumber;
        }
        return a->filePath < b->filePath;
    });

    size_t totalTracks = sortedTrackIds.size();

    // 2. Cover art size and responsive columns
    float artBoxSize = (totalTracks <= 3) ? 140.0f : ((width >= 620.0f) ? 200.0f : 160.0f);
    float headX = 16.0f + artBoxSize + 22.0f;
    float tracklistW = width - headX - 20.0f;

    // Authentic SlothPlayer layout: 2 balanced columns (tracks 1..half on left, half+1..N on right)
    int numCols = 1;
    if (tracklistW >= 360.0f && totalTracks > 4) {
        numCols = 2;
    }

    float colW = tracklistW / static_cast<float>(numCols);
    size_t rowsPerCol = (totalTracks + numCols - 1) / numCols;

    float rowH = 22.0f; // Clean, compact row height matching SlothPlayer reference
    float tracklistH = static_cast<float>(rowsPerCol) * rowH;
    float panelH = std::max(artBoxSize + 32.0f, tracklistH + 68.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 curPos = ImGui::GetCursorScreenPos();

    // 3. Theme Colors (SlothPlayer Authentic Light Card)
    ImU32 cardBgCol = ImGui::GetColorU32(Theme::ExpandedCardBackground());
    ImU32 cardBdrCol = ImGui::GetColorU32(Theme::ExpandedCardBorder());
    ImU32 titleTextCol = ImGui::GetColorU32(Theme::ExpandedCardTextPrimary());
    ImU32 subTextCol = ImGui::GetColorU32(Theme::ExpandedCardTextSecondary());
    ImU32 rowHovCol = ImGui::GetColorU32(Theme::ExpandedCardRowHover());

    // 4. Upward pointer arrow pointing seamlessly to the clicked album card
    float arrowHalfW = 11.0f;
    float arrowH = 9.0f;
    if (arrowCenterX > curPos.x + 12.0f && arrowCenterX < curPos.x + width - 12.0f) {
        dl->AddTriangleFilled(
            ImVec2(arrowCenterX - arrowHalfW, curPos.y + 1.0f),
            ImVec2(arrowCenterX, curPos.y - arrowH),
            ImVec2(arrowCenterX + arrowHalfW, curPos.y + 1.0f),
            cardBgCol
        );
        dl->AddLine(ImVec2(arrowCenterX - arrowHalfW, curPos.y + 1.0f), ImVec2(arrowCenterX, curPos.y - arrowH), cardBdrCol, 1.2f);
        dl->AddLine(ImVec2(arrowCenterX, curPos.y - arrowH), ImVec2(arrowCenterX + arrowHalfW, curPos.y + 1.0f), cardBdrCol, 1.2f);
    }

    // 5. Card Soft Drop Shadow
    dl->AddRectFilled(
        ImVec2(curPos.x + 2.0f, curPos.y + 3.0f),
        ImVec2(curPos.x + width + 2.0f, curPos.y + panelH + 3.0f),
        IM_COL32(0, 0, 0, 45),
        6.0f
    );

    // 6. Child Window Card (Never scroll internally; grid handles vertical scrolling)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ExpandedCardBackground());
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::ExpandedCardBorder());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.2f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

    std::string childId = "ExpAlbCard_" + std::to_string(alb.representativeTrackId);
    ImGui::BeginChild(childId.c_str(), ImVec2(width, panelH), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* cardDl = ImGui::GetWindowDrawList();
    ImVec2 cP0 = ImGui::GetWindowPos();

    // 7. Album Cover Artwork with Soft Drop Shadow & Clean Border
    int artW = 0, artH = 0;
    const Track* repTrack = m_library.getTrackById(alb.representativeTrackId);
    ImTextureID artTex = repTrack ? m_textures.getTrackArtwork(*repTrack, &artW, &artH) : m_textures.getDefaultArtwork(&artW, &artH);

    if (artTex) {
        ImVec2 fitSize = TextureManager::getAspectFitSize(artW, artH, artBoxSize, artBoxSize);
        float padX = (artBoxSize - fitSize.x) * 0.5f;
        float padY = (artBoxSize - fitSize.y) * 0.5f;

        ImGui::SetCursorPos(ImVec2(16.0f + padX, 16.0f + padY));
        ImVec2 artScreenPos = ImGui::GetCursorScreenPos();

        // Artwork Drop Shadow
        cardDl->AddRectFilled(
            ImVec2(artScreenPos.x + 2.0f, artScreenPos.y + 2.0f),
            ImVec2(artScreenPos.x + fitSize.x + 3.0f, artScreenPos.y + fitSize.y + 3.0f),
            IM_COL32(0, 0, 0, 45),
            3.0f
        );

        ImGui::Image(artTex, fitSize);

        // Thin elegant artwork border
        cardDl->AddRect(
            artScreenPos,
            ImVec2(artScreenPos.x + fitSize.x, artScreenPos.y + fitSize.y),
            IM_COL32(0, 0, 0, 35),
            2.0f,
            0,
            1.0f
        );
    }

    // 8. Header: Title, Circular Play Button, Artist/Year Subtitle, Close Button
    float headY = 14.0f;
    float closeX = width - 36.0f;

    // Premium Vector Close Button at top right
    float btnSize = 24.0f;
    ImGui::SetCursorPos(ImVec2(closeX, headY - 2.0f));
    ImVec2 closeScreenPos = ImGui::GetCursorScreenPos();
    ImVec2 closeCenter(closeScreenPos.x + btnSize * 0.5f, closeScreenPos.y + btnSize * 0.5f);

    if (ImGui::InvisibleButton("##CloseExpAlb", ImVec2(btnSize, btnSize))) {
        m_expandedAlbumRepId = 0;
        m_expandedAlbumName.clear();
    }
    bool isClsHov = ImGui::IsItemHovered();
    bool isClsAct = ImGui::IsItemActive();

    if (isClsHov) {
        ImGui::SetTooltip("Close");
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        cardDl->AddCircleFilled(closeCenter, 11.5f, isClsAct ? IM_COL32(0, 0, 0, 40) : IM_COL32(0, 0, 0, 20));
        cardDl->AddCircle(closeCenter, 11.5f, isClsAct ? IM_COL32(0, 0, 0, 60) : IM_COL32(0, 0, 0, 35), 0, 1.0f);
    }

    // High precision anti-aliased vector cross '✕'
    float crossArm = 4.2f;
    ImU32 iconCol = isClsAct ? IM_COL32(20, 24, 30, 255)
                             : (isClsHov ? IM_COL32(32, 36, 44, 255) : subTextCol);
    cardDl->AddLine(ImVec2(closeCenter.x - crossArm, closeCenter.y - crossArm),
                    ImVec2(closeCenter.x + crossArm, closeCenter.y + crossArm),
                    iconCol, 1.4f);
    cardDl->AddLine(ImVec2(closeCenter.x + crossArm, closeCenter.y - crossArm),
                    ImVec2(closeCenter.x - crossArm, closeCenter.y + crossArm),
                    iconCol, 1.4f);

    // Album Title (Crisp, bold, dark text)
    float maxTitleW = closeX - headX - 36.0f;
    std::string dispTitle = truncateTextToWidth(alb.name, maxTitleW);

    ImGui::SetCursorPos(ImVec2(headX, headY));
    ImGui::PushStyleColor(ImGuiCol_Text, titleTextCol);
    ImGui::TextUnformatted(dispTitle.c_str());
    ImGui::PopStyleColor();

    // Inline Circular Play Button ▷
    ImGui::SameLine(0.0f, 10.0f);
    float playR = 9.5f;
    ImVec2 playCenter = ImVec2(ImGui::GetCursorScreenPos().x + playR, ImGui::GetCursorScreenPos().y + playR);
    ImGui::SetCursorScreenPos(ImVec2(playCenter.x - playR, playCenter.y - playR));
    if (ImGui::InvisibleButton("##ExpAlbPlayBtn", ImVec2(playR * 2.0f, playR * 2.0f))) {
        if (!sortedTrackIds.empty()) {
            playTrack(sortedTrackIds[0], sortedTrackIds);
        }
    }
    bool hov = ImGui::IsItemHovered();
    ImU32 playCol = hov ? IM_COL32(255, 205, 65, 255) : IM_COL32(235, 168, 15, 255);
    cardDl->AddCircleFilled(playCenter, playR, playCol);
    cardDl->AddTriangleFilled(
        ImVec2(playCenter.x - 2.5f, playCenter.y - 5.0f),
        ImVec2(playCenter.x + 5.0f, playCenter.y),
        ImVec2(playCenter.x - 2.5f, playCenter.y + 5.0f),
        IM_COL32(18, 20, 24, 255)
    );
    if (hov) {
        ImGui::SetTooltip("Play Album");
    }

    // Subtitle: Artist (Year)
    std::string subStr = alb.artist;
    if (alb.year > 0) subStr += " (" + std::to_string(alb.year) + ")";
    ImGui::SetCursorPos(ImVec2(headX, headY + 22.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, subTextCol);
    ImGui::TextUnformatted(subStr.c_str());
    ImGui::PopStyleColor();

    // 9. Multi-column Tracklist (SlothPlayer Authentic Column Split)
    float trackStartY = headY + 44.0f;

    for (int c = 0; c < numCols; ++c) {
        float curColX = headX + c * colW;
        for (size_t r = 0; r < rowsPerCol; ++r) {
            size_t idx = c * rowsPerCol + r;
            if (idx >= totalTracks) break;

            uint64_t tid = sortedTrackIds[idx];
            const Track* t = m_library.getTrackById(tid);
            if (!t) continue;

            float rowY = trackStartY + r * rowH;
            ImGui::SetCursorPos(ImVec2(curColX, rowY));
            ImGui::PushID(static_cast<int>(idx));

            bool isCurrent = (t->id == m_currentTrackId);

            int trkNum = t->trackNumber > 0 ? t->trackNumber : static_cast<int>(idx + 1);
            char numBuf[16];
            if (alb.discCount > 1 && t->discNumber > 0) {
                snprintf(numBuf, sizeof(numBuf), "%d.%02d", t->discNumber, trkNum);
            } else {
                snprintf(numBuf, sizeof(numBuf), "%2d", trkNum);
            }

            std::string durStr = t->formatDuration();
            float durW = ImGui::CalcTextSize(durStr.c_str()).x;

            float availTitleW = colW - 32.0f - durW - 16.0f;
            std::string dispTrackTitle = truncateTextToWidth(t->getDisplayTitle(), availTitleW);

            // Invisible row button to capture hover & clicks across the entire row
            ImVec2 rowMin = ImVec2(cP0.x + curColX, cP0.y + rowY);
            ImVec2 rowMax = ImVec2(rowMin.x + colW - 8.0f, rowMin.y + rowH);
            ImGui::SetCursorScreenPos(rowMin);
            std::string rowBtnId = "##tkRow_" + std::to_string(t->id);
            if (ImGui::InvisibleButton(rowBtnId.c_str(), ImVec2(colW - 8.0f, rowH))) {
                m_selectedTrackId = t->id;
            }
            bool isRowHovered = ImGui::IsItemHovered();
            bool isRowActive = ImGui::IsItemActive();

            if (isRowHovered) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    playTrack(t->id, sortedTrackIds);
                }
            }

            renderTrackContextMenu(t->id, false, 0);

            // Row background highlight
            if (isCurrent) {
                ImU32 activeBg = Theme::useLightExpandedCard() ? IM_COL32(255, 235, 175, 180) : IM_COL32(70, 55, 20, 200);
                cardDl->AddRectFilled(rowMin, rowMax, activeBg, 3.0f);
            } else if (isRowActive) {
                cardDl->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(Theme::ExpandedCardRowActive()), 3.0f);
            } else if (isRowHovered) {
                cardDl->AddRectFilled(rowMin, rowMax, rowHovCol, 3.0f);
            }

            // Track Number (clean muted grey)
            ImU32 numCol = isCurrent ? ImGui::GetColorU32(Theme::AccentColor()) : subTextCol;
            cardDl->AddText(ImVec2(rowMin.x + 4.0f, rowMin.y + 3.0f), numCol, numBuf);

            // Track Title
            ImU32 titleCol = isCurrent ? ImGui::GetColorU32(Theme::AccentColor()) : titleTextCol;
            cardDl->AddText(ImVec2(rowMin.x + 28.0f, rowMin.y + 3.0f), titleCol, dispTrackTitle.c_str());

            // Right-aligned duration
            cardDl->AddText(ImVec2(rowMax.x - durW - 4.0f, rowMin.y + 3.0f), subTextCol, durStr.c_str());

            ImGui::PopID();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void MainWindow::renderArtistsView() {
    const auto& artists = m_library.getArtists();
    if (artists.empty()) {
        renderEmptyDropzone();
        return;
    }

    float horizPad = 14.0f;
    float topPad = 12.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);

    float availW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f);
    float availH = ImGui::GetContentRegionAvail().y - (topPad * 2.0f);
    float splitW = std::clamp(availW * 0.35f, 160.0f, 260.0f);

    ImGui::BeginChild("ArtistsList", ImVec2(splitW, availH), false);
    float padLeft = 8.0f;
    float itemW = splitW - (padLeft + 4.0f);
    for (const auto& art : artists) {
        bool isSelected = (m_selectedArtistName == art.name);
        std::string label = art.name;
        if (label.length() > 22) label = label.substr(0, 20) + "...";
        std::string artId = "Art_" + art.name;
        ImGui::PushID(artId.c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padLeft);
        if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(itemW, 22.0f))) {
            m_selectedArtistName = art.name;
        }
        if (isSelected) {
            ImVec2 selMin = ImGui::GetItemRectMin();
            ImVec2 selMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(selMin.x, selMin.y + 2.0f), ImVec2(selMin.x + 3.0f, selMax.y - 2.0f), ImGui::GetColorU32(Theme::AccentColor()), 2.0f);
        }
        if (ImGui::BeginPopupContextItem("ArtistCtx")) {
            if (ImGui::MenuItem("Play All by Artist")) {
                if (!art.trackIds.empty()) playTrack(art.trackIds[0], art.trackIds);
            }
            if (ImGui::MenuItem("Queue All by Artist")) {
                m_queue.insert(m_queue.end(), art.trackIds.begin(), art.trackIds.end());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy Artist Name")) {
                ImGui::SetClipboardText(art.name.c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 14.0f);

    ImGui::BeginChild("ArtistTracks", ImVec2(availW - splitW - 14.0f, availH), false);
    if (m_selectedArtistName.empty() && !artists.empty()) {
        m_selectedArtistName = artists[0].name;
    }

    ImGui::TextColored(Theme::AccentColor(), "%s", m_selectedArtistName.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    for (const auto& art : artists) {
        if (art.name == m_selectedArtistName) {
            for (uint64_t tid : art.trackIds) {
                const Track* t = m_library.getTrackById(tid);
                if (t) {
                    ImGui::PushID(static_cast<int>(t->id));
                    if (ImGui::Selectable((t->getDisplayTitle() + " - " + t->getDisplayAlbum()).c_str(), t->id == m_currentTrackId, 0, ImVec2(0.0f, 22.0f))) {
                        playTrack(t->id, art.trackIds);
                    }
                    renderTrackContextMenu(t->id, false, 0);
                    ImGui::PopID();
                }
            }
            break;
        }
    }

    ImGui::EndChild();
}

void MainWindow::renderFoldersView() {
    float horizPad = 14.0f;
    float topPad = 12.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);
    float availW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f);
    float availH = ImGui::GetContentRegionAvail().y - (topPad * 2.0f);

    ImGui::BeginChild("FoldersExplorer", ImVec2(availW, availH), false);
    ImGui::TextColored(Theme::HeaderMuted(), "Local Music Directories");
    ImGui::Separator();
    ImGui::Spacing();

    const auto& folders = m_library.getMonitoredFolders();
    for (const auto& f : folders) {
        if (ImGui::TreeNode(f.c_str())) {
            try {
                if (fs::exists(f)) {
                    for (const auto& entry : fs::directory_iterator(f)) {
                        if (entry.is_directory()) {
                            ImGui::BulletText("%s/", entry.path().filename().string().c_str());
                        } else {
                            std::string ext = entry.path().extension().string();
                            if (ext == ".mp3" || ext == ".flac" || ext == ".wav" || ext == ".ogg") {
                                if (ImGui::Selectable(entry.path().filename().string().c_str())) {
                                    Track* found = nullptr;
                                    for (const auto& t : m_library.getTracks()) {
                                        if (t.filePath == entry.path().string()) {
                                            found = const_cast<Track*>(&t);
                                            break;
                                        }
                                    }
                                    if (found) {
                                        playTrack(found->id);
                                    } else {
                                        handleDroppedFiles({entry.path().string()});
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (...) {}
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
}

void MainWindow::renderPlaylistExplorer(float width, float height) {
    (void)width;
    (void)height;
    float padLeft = 8.0f;
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.98f, 1.0f));
    ImGui::Text("Playlist Explorer \xE2\x96\xBE");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    auto renderPlaylistItem = [this, padLeft](const char* name, SmartPlaylistType type, const char* customName = nullptr) {
        bool selected = (customName == nullptr) ? (m_smartPlaylist == type) : (m_smartPlaylist == SmartPlaylistType::Custom && m_activeCustomPlaylist == customName);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.98f, 1.0f));
        }

        float itemW = ImGui::GetContentRegionAvail().x - (padLeft + 4.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padLeft);
        std::string label = std::string("  ") + name;
        if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(itemW, 22.0f))) {
            m_smartPlaylist = type;
            if (customName) m_activeCustomPlaylist = customName;
            m_viewMode = ViewMode::Playlists;
        }
        ImGui::PopStyleColor();

        if (selected) {
            ImVec2 selMin = ImGui::GetItemRectMin();
            ImVec2 selMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(selMin.x, selMin.y + 2.0f), ImVec2(selMin.x + 3.0f, selMax.y - 2.0f), ImGui::GetColorU32(Theme::AccentColor()), 2.0f);
        }

        if (customName) {
            std::string popupId = std::string("##PlCtx_") + customName;
            if (ImGui::BeginPopupContextItem(popupId.c_str())) {
                ImGui::TextColored(Theme::AccentColor(), "%s", customName);
                ImGui::Separator();
                if (ImGui::MenuItem("Play Playlist")) {
                    const auto& pls = m_library.getPlaylists();
                    auto it = pls.find(customName);
                    if (it != pls.end() && !it->second.empty()) {
                        playTrack(it->second[0], it->second);
                    }
                }
                if (ImGui::MenuItem("Delete Playlist")) {
                    m_library.deletePlaylist(customName);
                    if (m_activeCustomPlaylist == customName) {
                        m_activeCustomPlaylist.clear();
                        m_smartPlaylist = SmartPlaylistType::RecentlyAdded;
                    }
                }
                ImGui::EndPopup();
            }
        }
    };

    renderPlaylistItem("Recently Added", SmartPlaylistType::RecentlyAdded);
    renderPlaylistItem("Top 25 Most Played", SmartPlaylistType::Top25MostPlayed);
    renderPlaylistItem("Favorites", SmartPlaylistType::Favorites);

    const auto& customPlaylists = m_library.getPlaylists();
    if (!customPlaylists.empty()) {
        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::TextColored(Theme::HeaderMuted(), "CUSTOM");
        ImGui::Separator();
        for (const auto& pair : customPlaylists) {
            renderPlaylistItem(pair.first.c_str(), SmartPlaylistType::Custom, pair.first.c_str());
        }
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padLeft);
    if (ImGui::Button("  + New Playlist...  ")) {
        m_showCreatePlaylistModal = true;
    }
}

void MainWindow::renderPlaylistBanner(const std::string& title, size_t trackCount, size_t albumCount, double totalDuration, ImTextureID coverArt, const std::vector<uint64_t>& trackQueue) {
    float horizPad = 14.0f;
    float topPad = 10.0f;
    float availW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f);
    float bannerH = 74.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);
    ImVec2 bPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 bMin = bPos;
    ImVec2 bMax = ImVec2(bPos.x + availW, bPos.y + bannerH);

    // Subtle drop shadow behind banner
    dl->AddRectFilled(ImVec2(bMin.x + 1.0f, bMin.y + 3.0f), ImVec2(bMax.x + 1.0f, bMax.y + 3.0f), IM_COL32(0, 0, 0, 45), 8.0f);

    // Authentic SlothPlayer Teal Gradient Banner (#216a75 to #297e88) with rounded corners
    dl->AddRectFilledMultiColor(
        bMin, bMax,
        IM_COL32(33, 106, 117, 255),
        IM_COL32(41, 126, 136, 255),
        IM_COL32(37, 118, 128, 255),
        IM_COL32(29, 95, 105, 255)
    );
    dl->AddRect(bMin, bMax, IM_COL32(38, 92, 102, 200), 8.0f, 0, 1.0f);

    float curX = bPos.x + 14.0f;
    float artY = bPos.y + (bannerH - 56.0f) * 0.5f;

    // 1. 56x56 Album Artwork Thumbnail (Aspect-Fill cropped) with rounded corners
    ImTextureID displayArt = coverArt ? coverArt : m_textures.getDefaultArtwork();
    if (displayArt) {
        int artW = 0, artH = 0;
        m_textures.getDimensions(displayArt, artW, artH);
        ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
        TextureManager::getAspectFillUV(artW, artH, 1.0f, uv0, uv1);
        ImVec2 aMin(curX, artY);
        ImVec2 aMax(curX + 56.0f, artY + 56.0f);
        dl->AddRectFilled(ImVec2(aMin.x + 1.0f, aMin.y + 2.0f), ImVec2(aMax.x + 1.0f, aMax.y + 2.0f), IM_COL32(0, 0, 0, 50), 6.0f);
        dl->AddImageRounded(displayArt, aMin, aMax, uv0, uv1, IM_COL32_WHITE, 6.0f);
        dl->AddRect(aMin, aMax, IM_COL32(255, 255, 255, 60), 6.0f, 0, 1.0f);
    }
    curX += 68.0f;

    // 2. Title & Play Button (▷)
    float textY = bPos.y + 14.0f;
    ImGui::SetCursorScreenPos(ImVec2(curX, textY));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 10.0f);
    float playRadius = 10.0f;
    ImVec2 playCenter = ImVec2(ImGui::GetCursorScreenPos().x + playRadius, textY + 9.0f);
    ImGui::SetCursorScreenPos(ImVec2(playCenter.x - playRadius, playCenter.y - playRadius));
    if (ImGui::InvisibleButton("##BannerPlayBtn", ImVec2(playRadius * 2.0f, playRadius * 2.0f))) {
        if (!trackQueue.empty()) {
            playTrack(trackQueue[0], trackQueue);
        }
    }
    bool playHov = ImGui::IsItemHovered();
    bool playAct = ImGui::IsItemActive();
    ImU32 playCol = playAct ? IM_COL32(255, 255, 255, 255) : (playHov ? IM_COL32(220, 240, 245, 255) : IM_COL32(180, 215, 222, 220));
    dl->AddCircle(playCenter, playRadius, playCol, 24, 1.5f);
    dl->AddTriangleFilled(
        ImVec2(playCenter.x - 3.0f, playCenter.y - 5.0f),
        ImVec2(playCenter.x + 5.0f, playCenter.y),
        ImVec2(playCenter.x - 3.0f, playCenter.y + 5.0f),
        playCol
    );

    // 3. Subtitle (e.g. "2,298 tracks / 107 albums, duration: 3d 6:50")
    int totalSec = static_cast<int>(totalDuration);
    int days = totalSec / 86400;
    int hours = (totalSec % 86400) / 3600;
    int mins = (totalSec % 3600) / 60;
    char durStr[64];
    if (days > 0) {
        snprintf(durStr, sizeof(durStr), "%dd %d:%02d", days, hours, mins);
    } else {
        snprintf(durStr, sizeof(durStr), "%d:%02d", hours, mins);
    }

    char metaStr[128];
    if (albumCount > 0) {
        snprintf(metaStr, sizeof(metaStr), "%zu tracks / %zu albums, duration: %s", trackCount, albumCount, durStr);
    } else {
        snprintf(metaStr, sizeof(metaStr), "%zu tracks, duration: %s", trackCount, durStr);
    }
    ImGui::SetCursorScreenPos(ImVec2(curX, textY + 24.0f));
    ImGui::TextColored(ImVec4(0.80f, 0.92f, 0.95f, 1.0f), "%s", metaStr);

    // 4. Right side actions: Delete Playlist / Edit / Sort
    if (availW >= 420.0f) {
        bool isCustom = (m_library.getPlaylists().count(title) > 0);
        float rightX = bPos.x + availW - (isCustom ? 210.0f : 90.0f);
        ImGui::SetCursorScreenPos(ImVec2(rightX, textY + 18.0f));
        if (isCustom) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.15f, 0.15f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
            if (ImGui::SmallButton("Delete Playlist")) {
                m_library.deletePlaylist(title);
                m_navSource = NavSource::AllTracks;
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0.0f, 10.0f);
        }
        ImGui::TextColored(ImVec4(0.85f, 0.94f, 0.97f, 1.0f), "Edit ▾  Sort");
    }

    ImGui::SetCursorScreenPos(ImVec2(bPos.x, bPos.y + bannerH + 12.0f));
}

void MainWindow::renderSectionedAlbumsView(const std::vector<std::pair<std::string, std::vector<AlbumInfo>>>& sections) {
    ImGui::BeginChild("SectionedAlbumsScroll", ImVec2(0, 0), false);
    float horizPad = 14.0f;
    float topPad = 8.0f;
    float gridAvailW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f) - 4.0f;
    float targetCardW = m_albumCardSize;
    float minGap = 16.0f;

    int cols = static_cast<int>((gridAvailW + minGap) / (targetCardW + minGap));
    if (cols < 1) cols = 1;

    float cardW = std::floor((gridAvailW - (cols - 1) * minGap) / static_cast<float>(cols));
    if (cardW < 40.0f) cardW = 40.0f;
    float remainingSpace = gridAvailW - (cols * cardW);
    float cardGap = (cols > 1) ? (remainingSpace / static_cast<float>(cols - 1)) : 0.0f;

    float gridStartX = ImGui::GetCursorPosX() + horizPad;
    float rowGap = 16.0f;
    float cardTotalH = cardW + ImGui::GetStyle().ItemSpacing.y * 2.0f + ImGui::GetTextLineHeight() * 2.0f;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);

    for (size_t secIdx = 0; secIdx < sections.size(); ++secIdx) {
        const auto& sec = sections[secIdx];
        if (sec.second.empty()) continue;

        ImGui::Spacing();
        ImGui::SetCursorPosX(gridStartX);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.97f, 1.0f, 1.0f));
        ImGui::Text("%s", sec.first.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();

        float rowStartY = ImGui::GetCursorPosY();

        for (size_t i = 0; i < sec.second.size(); ++i) {
            int colIdx = static_cast<int>(i % cols);
            float cardPosX = gridStartX + colIdx * (cardW + cardGap);

            ImGui::SetCursorPos(ImVec2(cardPosX, rowStartY));

            const auto& alb = sec.second[i];
            ImGui::PushID(static_cast<int>(secIdx * 1000 + i));
            ImGui::BeginGroup();

            int artW = 0, artH = 0;
            const Track* repTrack = m_library.getTrackById(alb.representativeTrackId);
            ImTextureID artTex = repTrack ? m_textures.getTrackArtwork(*repTrack, &artW, &artH) : m_textures.getDefaultArtwork(&artW, &artH);
            ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
            TextureManager::getAspectFillUV(artW, artH, 1.0f, uv0, uv1);

            if (artTex) {
                ImVec2 artP0 = ImGui::GetCursorScreenPos();
                ImVec2 artP1 = ImVec2(artP0.x + cardW, artP0.y + cardW);

                std::string btnId = "##sArtBtn_" + std::to_string(alb.representativeTrackId) + "_" + std::to_string(i);
                if (ImGui::InvisibleButton(btnId.c_str(), ImVec2(cardW, cardW))) {
                    if (!alb.trackIds.empty()) {
                        playTrack(alb.trackIds[0], alb.trackIds);
                    }
                }

                bool isHovered = ImGui::IsItemHovered();
                bool isActive = ImGui::IsItemActive();
                ImDrawList* dl = ImGui::GetWindowDrawList();

                // Drop shadow BEHIND artwork
                dl->AddRectFilled(ImVec2(artP0.x + 1.0f, artP0.y + 3.0f), ImVec2(artP1.x + 1.0f, artP1.y + 3.0f), IM_COL32(0, 0, 0, 45), 6.0f);

                // Rounded artwork with zero corner bleed
                dl->AddImageRounded(artTex, artP0, artP1, uv0, uv1, IM_COL32_WHITE, 6.0f);

                if (isHovered || isActive) {
                    ImGui::SetTooltip("%s\n%s (%zu tracks)", alb.name.c_str(), alb.artist.c_str(), alb.trackIds.size());

                    // Soft outer glow + single accent border with matching 6.0f radius
                    ImVec4 acc = Theme::AccentColor();
                    dl->AddRect(artP0, artP1, ImGui::GetColorU32(acc), 6.0f, 0, 1.8f);
                    dl->AddRect(ImVec2(artP0.x - 1.0f, artP0.y - 1.0f), ImVec2(artP1.x + 1.0f, artP1.y + 1.0f),
                                IM_COL32(static_cast<int>(acc.x * 255), static_cast<int>(acc.y * 255), static_cast<int>(acc.z * 255), 80), 7.0f, 0, 1.2f);

                    // Centered Quick-Play play circle button
                    ImVec2 center = ImVec2(artP0.x + cardW * 0.5f, artP0.y + cardW * 0.5f);
                    float btnRadius = 20.0f;
                    ImVec2 mousePos = ImGui::GetIO().MousePos;
                    float distSq = (mousePos.x - center.x) * (mousePos.x - center.x) + (mousePos.y - center.y) * (mousePos.y - center.y);
                    bool isCircleHovered = distSq <= (btnRadius * btnRadius);

                    ImU32 circleBg = isCircleHovered ? IM_COL32(20, 24, 30, 240) : IM_COL32(20, 24, 30, 200);
                    ImU32 circleBorder = isCircleHovered ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(255, 255, 255, 180);
                    ImU32 iconColor = isCircleHovered ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(255, 255, 255, 240);

                    dl->AddCircleFilled(center, btnRadius, circleBg, 32);
                    dl->AddCircle(center, btnRadius, circleBorder, 32, 1.5f);
                    dl->AddTriangleFilled(
                        ImVec2(center.x - 5.0f, center.y - 8.0f),
                        ImVec2(center.x + 8.0f, center.y),
                        ImVec2(center.x - 5.0f, center.y + 8.0f),
                        iconColor
                    );
                }
            }

            std::string albName = truncateTextToWidth(alb.name, cardW);
            ImVec2 titleSz = ImGui::CalcTextSize(albName.c_str());
            float titleOffX = std::max(0.0f, (cardW - titleSz.x) * 0.5f);
            ImGui::SetCursorPosX(cardPosX + titleOffX);
            ImGui::TextUnformatted(albName.c_str());

            std::string fullArtName = alb.artist;
            if (alb.year > 0) fullArtName += ", " + std::to_string(alb.year);
            std::string artName = truncateTextToWidth(fullArtName, cardW);
            ImVec2 artSz = ImGui::CalcTextSize(artName.c_str());
            float artOffX = std::max(0.0f, (cardW - artSz.x) * 0.5f);
            ImGui::SetCursorPosX(cardPosX + artOffX);
            ImGui::TextColored(ImVec4(0.70f, 0.74f, 0.80f, 1.0f), "%s", artName.c_str());

            ImGui::EndGroup();
            ImGui::PopID();

            bool isRowEnd = (colIdx == cols - 1 || (i + 1) == sec.second.size());
            if (isRowEnd) {
                rowStartY += cardTotalH + rowGap;
            }
        }

        ImGui::SetCursorPosY(rowStartY + 8.0f);
    }

    ImGui::Dummy(ImVec2(gridAvailW, 10.0f));
    ImGui::EndChild();
}

void MainWindow::renderPlaylistsView() {
    if (m_smartPlaylist == SmartPlaylistType::RecentlyAdded) {
        auto tracks = m_library.getRecentlyAddedTrackIds();
        auto sections = m_library.getRecentlyAddedAlbumsCategorized();

        size_t totalAlbums = 0;
        for (const auto& sec : sections) totalAlbums += sec.second.size();

        double totalDur = 0.0;
        ImTextureID repArt = 0;
        for (uint64_t tid : tracks) {
            const Track* t = m_library.getTrackById(tid);
            if (t) {
                totalDur += t->duration;
                if (!repArt) repArt = m_textures.getTrackArtwork(*t);
            }
        }

        renderPlaylistBanner("Recently Added", tracks.size(), totalAlbums, totalDur, repArt, tracks);
        renderSectionedAlbumsView(sections);
    } else if (m_smartPlaylist == SmartPlaylistType::Top25MostPlayed) {
        auto tracks = m_library.getTop25MostPlayedTrackIds();
        double totalDur = 0.0;
        ImTextureID repArt = 0;
        for (uint64_t tid : tracks) {
            const Track* t = m_library.getTrackById(tid);
            if (t) {
                totalDur += t->duration;
                if (!repArt) repArt = m_textures.getTrackArtwork(*t);
            }
        }
        renderPlaylistBanner("Top 25 Most Played", tracks.size(), 0, totalDur, repArt, tracks);
        renderTracksView();
    } else if (m_smartPlaylist == SmartPlaylistType::Favorites) {
        auto tracks = m_library.getFavoritesTrackIds();
        double totalDur = 0.0;
        ImTextureID repArt = 0;
        for (uint64_t tid : tracks) {
            const Track* t = m_library.getTrackById(tid);
            if (t) {
                totalDur += t->duration;
                if (!repArt) repArt = m_textures.getTrackArtwork(*t);
            }
        }
        renderPlaylistBanner("Favorites", tracks.size(), 0, totalDur, repArt, tracks);
        renderTracksView();
    } else if (m_smartPlaylist == SmartPlaylistType::Custom) {
        const auto& playlists = m_library.getPlaylists();
        auto it = playlists.find(m_activeCustomPlaylist);
        std::vector<uint64_t> tracks = (it != playlists.end()) ? it->second : std::vector<uint64_t>{};

        double totalDur = 0.0;
        ImTextureID repArt = 0;
        for (uint64_t tid : tracks) {
            const Track* t = m_library.getTrackById(tid);
            if (t) {
                totalDur += t->duration;
                if (!repArt) repArt = m_textures.getTrackArtwork(*t);
            }
        }
        renderPlaylistBanner(m_activeCustomPlaylist.empty() ? "Playlist" : m_activeCustomPlaylist, tracks.size(), 0, totalDur, repArt, tracks);
        renderTracksView();
    }
}

void MainWindow::renderExplorerLeftPanel(float width, float height) {
    (void)width;
    (void)height;
    const auto& artists = m_library.getArtists();
    if (artists.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No artists found");
        return;
    }

    if (m_selectedExplorerArtist.empty()) {
        m_selectedExplorerArtist = artists[0].name;
    }

    // Header: "Album Artist ▾" (SlothPlayer style)
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.98f, 1.0f));
    ImGui::Text("Album Artist \xE2\x96\xBE");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Available letters only A-Z bar
    std::set<char> availableLetters;
    bool hasNonAlpha = false;
    for (const auto& art : artists) {
        if (art.name.empty()) {
            hasNonAlpha = true;
            continue;
        }
        char c = static_cast<char>(toupper(static_cast<unsigned char>(art.name[0])));
        if (c >= 'A' && c <= 'Z') availableLetters.insert(c);
        else hasNonAlpha = true;
    }

    std::string letters;
    if (hasNonAlpha) letters += '#';
    for (char c = 'A'; c <= 'Z'; ++c) {
        if (availableLetters.find(c) != availableLetters.end()) letters += c;
    }

    float padLeft = 8.0f;
    if (!letters.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padLeft);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.5f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.28f, 0.35f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.38f, 0.45f, 0.8f));

        bool isAll = (m_artistAlphabetFilter == '\0');
        if (isAll) ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
        else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.68f, 0.72f, 0.78f, 1.0f));
        if (ImGui::Button("All##ArtAll", ImVec2(24.0f, 18.0f))) {
            m_artistAlphabetFilter = '\0';
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        for (size_t i = 0; i < letters.size(); ++i) {
            char ch = letters[i];
            bool isActive = (m_artistAlphabetFilter == ch);
            if (isActive) ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
            else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.84f, 0.90f, 1.0f));

            char lbl[4] = { ch, '\0' };
            if (ImGui::Button(lbl, ImVec2(18.0f, 18.0f))) {
                if (m_artistAlphabetFilter == ch) m_artistAlphabetFilter = '\0';
                else m_artistAlphabetFilter = ch;
            }
            ImGui::PopStyleColor();
            if (i + 1 < letters.size()) ImGui::SameLine();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::NewLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padLeft);
        ImGui::Separator();
        ImGui::Spacing();
    }

    // Artist list
    ImGui::BeginChild("ExplorerArtistsScroll", ImVec2(0, 0), false);
    for (size_t i = 0; i < artists.size(); ++i) {
        const auto& art = artists[i];
        if (m_artistAlphabetFilter != '\0') {
            char firstCh = art.name.empty() ? '#' : static_cast<char>(toupper(static_cast<unsigned char>(art.name[0])));
            if (m_artistAlphabetFilter == '#') {
                if (firstCh >= 'A' && firstCh <= 'Z') continue;
            } else {
                if (firstCh != m_artistAlphabetFilter) continue;
            }
        }

        ImGui::PushID(static_cast<int>(i));

        float rowH = 46.0f;
        float rowW = ImGui::GetContentRegionAvail().x - (padLeft + 4.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padLeft);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + rowW, p0.y + rowH);
        bool isSelected = (m_selectedExplorerArtist == art.name);

        std::string btnId = "##art_sel_row_" + std::to_string(i);
        if (ImGui::InvisibleButton(btnId.c_str(), ImVec2(rowW, rowH))) {
            m_selectedExplorerArtist = art.name;
        }

        bool isHovered = ImGui::IsItemHovered();
        if (ImGui::BeginPopupContextItem("ExpArtCtx")) {
            if (ImGui::MenuItem("Play All by Artist")) {
                if (!art.trackIds.empty()) playTrack(art.trackIds[0], art.trackIds);
            }
            if (ImGui::MenuItem("Queue All by Artist")) {
                m_queue.insert(m_queue.end(), art.trackIds.begin(), art.trackIds.end());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy Artist Name")) {
                ImGui::SetClipboardText(art.name.c_str());
            }
            ImGui::EndPopup();
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (isSelected) {
            dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_HeaderActive), 4.0f);
        } else if (isHovered) {
            dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 4.0f);
        }

        const Track* repTrack = art.trackIds.empty() ? nullptr : m_library.getTrackById(art.trackIds[0]);
        ImTextureID artTex = repTrack ? m_textures.getTrackArtwork(*repTrack) : m_textures.getDefaultArtwork();
        if (artTex) {
            int tw = 0, th = 0;
            m_textures.getDimensions(artTex, tw, th);
            ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
            TextureManager::getAspectFillUV(tw, th, 1.0f, uv0, uv1);
            dl->AddImageRounded(artTex,
                                ImVec2(p0.x + 3.0f, p0.y + 3.0f),
                                ImVec2(p0.x + 43.0f, p0.y + 43.0f),
                                uv0, uv1, IM_COL32_WHITE, 4.0f);
        }

        float textX = p0.x + 43.0f + 10.0f;
        float availW = rowW - (43.0f + 14.0f);
        std::string nameDisp = art.name;
        if (availW > 20.0f && ImGui::CalcTextSize(nameDisp.c_str()).x > availW) {
            while (nameDisp.length() > 3 && ImGui::CalcTextSize((nameDisp + "...").c_str()).x > availW) {
                nameDisp.pop_back();
            }
            nameDisp += "...";
        }

        ImU32 titleCol = isSelected ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(245, 248, 255, 255);
        dl->AddText(ImVec2(textX, p0.y + 4.0f), titleCol, nameDisp.c_str());

        int albCount = art.albumCount;
        std::string sub = std::to_string(albCount) + (albCount == 1 ? " album" : " albums");
        ImU32 subCol = IM_COL32(175, 185, 200, 255);
        dl->AddText(ImVec2(textX, p0.y + 24.0f), subCol, sub.c_str());

        ImGui::PopID();
    }
    ImGui::EndChild();
}

void MainWindow::renderArtistHeroBanner(const std::string& artistName, uint32_t totalPlays, size_t albumCount, size_t trackCount, const std::string& genreTag, ImTextureID avatarArt, const std::vector<uint64_t>& allTracks) {
    float horizPad = 14.0f;
    float availW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f);
    float bannerH = 74.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImVec2 bPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Subtle drop shadow behind banner
    dl->AddRectFilled(ImVec2(bPos.x + 1.0f, bPos.y + 3.0f), ImVec2(bPos.x + availW + 1.0f, bPos.y + bannerH + 3.0f), IM_COL32(0, 0, 0, 45), 8.0f);
    dl->AddRectFilled(bPos, ImVec2(bPos.x + availW, bPos.y + bannerH), IM_COL32(18, 20, 25, 255), 8.0f);
    dl->AddRect(bPos, ImVec2(bPos.x + availW, bPos.y + bannerH), IM_COL32(38, 42, 52, 200), 8.0f, 0, 1.0f);

    float avatarRadius = 26.0f;
    ImVec2 avatarCenter = ImVec2(bPos.x + 16.0f + avatarRadius, bPos.y + bannerH * 0.5f);

    if (avatarArt) {
        ImVec2 pMin = ImVec2(avatarCenter.x - avatarRadius, avatarCenter.y - avatarRadius);
        ImVec2 pMax = ImVec2(avatarCenter.x + avatarRadius, avatarCenter.y + avatarRadius);
        dl->AddImageRounded(avatarArt, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, avatarRadius);
        dl->AddCircle(avatarCenter, avatarRadius, IM_COL32(80, 90, 105, 180), 32, 1.5f);
    } else {
        dl->AddCircleFilled(avatarCenter, avatarRadius, IM_COL32(45, 50, 60, 255), 32);
    }

    float textX = avatarCenter.x + avatarRadius + 16.0f;
    float textY = bPos.y + 12.0f;

    ImGui::SetCursorScreenPos(ImVec2(textX, textY));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::TextUnformatted(artistName.c_str());
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 10.0f);
    float playRadius = 10.0f;
    ImVec2 playCenter = ImVec2(ImGui::GetCursorScreenPos().x + playRadius, textY + 9.0f);
    ImGui::SetCursorScreenPos(ImVec2(playCenter.x - playRadius, playCenter.y - playRadius));
    if (ImGui::InvisibleButton("##ArtistHeroPlay", ImVec2(playRadius * 2.0f, playRadius * 2.0f))) {
        if (!allTracks.empty()) {
            playTrack(allTracks[0], allTracks);
        }
    }
    bool playHov = ImGui::IsItemHovered();
    bool playAct = ImGui::IsItemActive();
    ImU32 playCol = playAct ? IM_COL32(255, 255, 255, 255) : (playHov ? IM_COL32(230, 240, 250, 255) : IM_COL32(180, 190, 205, 220));
    dl->AddCircle(playCenter, playRadius, playCol, 24, 1.5f);
    dl->AddTriangleFilled(
        ImVec2(playCenter.x - 3.0f, playCenter.y - 5.0f),
        ImVec2(playCenter.x + 5.0f, playCenter.y),
        ImVec2(playCenter.x - 3.0f, playCenter.y + 5.0f),
        playCol
    );

    char statsBuf[128];
    snprintf(statsBuf, sizeof(statsBuf), "%u plays  •  %zu %s / %zu %s",
             totalPlays,
             albumCount, (albumCount == 1 ? "album" : "albums"),
             trackCount, (trackCount == 1 ? "track" : "tracks"));
    ImGui::SetCursorScreenPos(ImVec2(textX, textY + 24.0f));
    ImGui::TextColored(ImVec4(0.70f, 0.74f, 0.82f, 1.0f), "%s", statsBuf);

    if (!genreTag.empty() && availW >= 550.0f) {
        std::string tagStr = genreTag;
        float tagW = ImGui::CalcTextSize(tagStr.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(bPos.x + availW - tagW - 20.0f, textY + 24.0f));
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "%s", tagStr.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(bPos.x, bPos.y + bannerH + 4.0f));
}

void MainWindow::renderArtistAlbumCards(const std::vector<AlbumInfo>& albums, float width, float height) {
    ImGui::BeginChild("ArtistAlbumsColumn", ImVec2(width, height), false);

    if (albums.empty()) {
        ImGui::TextDisabled("No albums found for this artist");
        ImGui::EndChild();
        return;
    }

    float targetCardW = m_albumCardSize; // 150px default, matching main Albums grid!
    float minGap = 16.0f;
    float availW = width > 0.0f ? (width - 4.0f) : (ImGui::GetContentRegionAvail().x - 4.0f);
    int cols = std::max(1, static_cast<int>((availW + minGap) / (targetCardW + minGap)));
    float cardW = std::floor((availW - (cols - 1) * minGap) / static_cast<float>(cols));
    if (cardW < 40.0f) cardW = 40.0f;
    float remainingSpace = availW - (cols * cardW);
    float cardGap = (cols > 1) ? (remainingSpace / static_cast<float>(cols - 1)) : 0.0f;

    float gridStartX = ImGui::GetCursorPosX();
    float rowStartY = ImGui::GetCursorPosY();
    float rowGap = 20.0f;
    float cardTotalH = cardW + ImGui::GetStyle().ItemSpacing.y * 2.0f + ImGui::GetTextLineHeight() * 2.0f;

    for (size_t i = 0; i < albums.size(); ++i) {
        int colIdx = static_cast<int>(i % cols);
        float cardPosX = gridStartX + colIdx * (cardW + cardGap);

        ImGui::SetCursorPos(ImVec2(cardPosX, rowStartY));

        const auto& alb = albums[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginGroup();

        int artW = 0, artH = 0;
        const Track* repTrack = m_library.getTrackById(alb.representativeTrackId);
        ImTextureID artTex = repTrack ? m_textures.getTrackArtwork(*repTrack, &artW, &artH) : m_textures.getDefaultArtwork(&artW, &artH);
        ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
        TextureManager::getAspectFillUV(artW, artH, 1.0f, uv0, uv1);

        ImVec2 artMin = ImGui::GetCursorScreenPos();
        if (artTex) {
            bool cardClicked = ImGui::InvisibleButton("##artAlbArt", ImVec2(cardW, cardW));
            bool isCardHovered = ImGui::IsItemHovered();

            ImDrawList* dl = ImGui::GetWindowDrawList();

            ImVec2 cMin(artMin.x, artMin.y);
            ImVec2 cMax(artMin.x + cardW, artMin.y + cardW);

            // 1. Drop shadow behind card (drawn BEFORE artwork)
            float shadowOffset = isCardHovered ? 3.0f : 2.0f;
            dl->AddRectFilled(ImVec2(cMin.x + 1.0f, cMin.y + shadowOffset),
                              ImVec2(cMax.x + 1.0f, cMax.y + shadowOffset + 1.0f),
                              IM_COL32(0, 0, 0, isCardHovered ? 70 : 45), 6.0f);

            // 2. Artwork with crisp 6.0f rounded corners (never pokes out!)
            dl->AddImageRounded(artTex, cMin, cMax, uv0, uv1, IM_COL32_WHITE, 6.0f);

            // 3. Selection / hover border with matching 6.0f rounded corners
            if (isCardHovered) {
                // Clean, subtle hover border — elegant highlight without jarring neon pop
                dl->AddRect(cMin, cMax, IM_COL32(255, 255, 255, 60), 6.0f, 0, 1.2f);
            } else {
                dl->AddRect(cMin, cMax, IM_COL32(255, 255, 255, 24), 6.0f, 0, 1.0f);
            }

            if (cardClicked) {
                if (!alb.trackIds.empty()) {
                    playTrack(alb.trackIds[0], alb.trackIds);
                }
            }

            if (isCardHovered) {
                ImGui::SetTooltip("%s (%zu tracks)\nClick to play album", alb.name.c_str(), alb.trackIds.size());
            }
        }

        renderAlbumContextMenu(alb);

        std::string albName = truncateTextToWidth(alb.name, cardW);
        ImVec2 titleSz = ImGui::CalcTextSize(albName.c_str());
        float titleOffX = std::max(0.0f, (cardW - titleSz.x) * 0.5f);
        ImGui::SetCursorPosX(cardPosX + titleOffX);
        ImGui::TextUnformatted(albName.c_str());

        if (alb.year > 0) {
            std::string yrStr = std::to_string(alb.year);
            ImVec2 yrSz = ImGui::CalcTextSize(yrStr.c_str());
            float yrOffX = std::max(0.0f, (cardW - yrSz.x) * 0.5f);
            ImGui::SetCursorPosX(cardPosX + yrOffX);
            ImGui::TextColored(ImVec4(0.70f, 0.74f, 0.80f, 1.0f), "%d", alb.year);
        }

        ImGui::EndGroup();
        ImGui::PopID();

        bool isRowEnd = (colIdx == cols - 1 || (i + 1) == albums.size());
        if (isRowEnd) {
            rowStartY += cardTotalH + rowGap;
        }
    }

    ImGui::SetCursorPos(ImVec2(gridStartX, rowStartY));
    ImGui::Dummy(ImVec2(availW, 10.0f));
    ImGui::EndChild();
}

void MainWindow::renderArtistTopTracks(const std::vector<uint64_t>& topTrackIds, float width, float height) {
    ImGui::BeginChild("ArtistTopTracksColumn", ImVec2(width, height), false);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.94f, 1.0f));
    ImGui::Text("TOP TRACKS");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (topTrackIds.empty()) {
        ImGui::TextDisabled("No tracks");
        ImGui::EndChild();
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoBordersInBody;
    if (ImGui::BeginTable("TopTracksTable", 2, flags, ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 32.0f);

        for (size_t i = 0; i < topTrackIds.size(); ++i) {
            const Track* t = m_library.getTrackById(topTrackIds[i]);
            if (!t) continue;

            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow(0, 22.0f);

            bool isCurrent = (t->id == m_currentTrackId);

            ImGui::TableSetColumnIndex(0);
            if (isCurrent) {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.96f, 0.98f, 1.0f));
            }

            std::string title = t->getDisplayTitle();
            if (ImGui::Selectable(title.c_str(), isCurrent, ImGuiSelectableFlags_SpanAllColumns)) {
                playTrack(t->id, topTrackIds);
            }
            ImGui::PopStyleColor();

            renderTrackContextMenu(t->id, false, 0);

            ImGui::TableSetColumnIndex(1);
            std::string countStr = std::to_string(t->playCount);
            float cW = ImGui::CalcTextSize(countStr.c_str()).x;
            float cAvail = ImGui::GetContentRegionAvail().x;
            if (cAvail > cW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cAvail - cW));
            ImGui::TextColored(ImVec4(0.70f, 0.74f, 0.80f, 1.0f), "%s", countStr.c_str());

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

void MainWindow::renderMusicExplorerView() {
    if (m_selectedExplorerArtist.empty()) {
        const auto& artists = m_library.getArtists();
        if (!artists.empty()) m_selectedExplorerArtist = artists[0].name;
    }

    if (m_selectedExplorerArtist.empty()) {
        renderEmptyDropzone();
        return;
    }

    std::string artistName = m_selectedExplorerArtist;
    uint32_t totalPlays = 0;
    size_t albumCount = 0;
    size_t trackCount = 0;
    std::string genreTag;
    m_library.getArtistStats(artistName, totalPlays, albumCount, trackCount, genreTag);

    auto topTrackIds = m_library.getArtistTopTrackIds(artistName);
    auto albums = m_library.getAlbumsByArtist(artistName);

    ImTextureID avatarArt = 0;
    if (!topTrackIds.empty()) {
        const Track* repTrack = m_library.getTrackById(topTrackIds[0]);
        if (repTrack) avatarArt = m_textures.getTrackArtwork(*repTrack);
    }
    if (!avatarArt) avatarArt = m_textures.getDefaultArtwork();

    float horizPad = 14.0f;
    float topPad = 12.0f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);
    ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.68f, 1.0f), "Artist Information");
    ImGui::Spacing();

    renderArtistHeroBanner(artistName, totalPlays, albumCount, trackCount, genreTag, avatarArt, topTrackIds);

    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    auto renderSubTab = [this](const char* label, ExplorerSubTab tab) {
        bool active = (m_explorerSubTab == tab);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
        }
        if (ImGui::Button(label)) {
            m_explorerSubTab = tab;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 12.0f);
    };

    renderSubTab("Albums & Stats", ExplorerSubTab::AlbumsAndStats);
    renderSubTab("More Albums", ExplorerSubTab::MoreAlbums);
    renderSubTab("Profile", ExplorerSubTab::Profile);
    renderSubTab("Similar Artists", ExplorerSubTab::SimilarArtists);
    ImGui::NewLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::Separator();
    ImGui::Spacing();

    float availW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f);
    float availH = ImGui::GetContentRegionAvail().y - 8.0f;

    float topTracksW = std::clamp(availW * 0.38f, 240.0f, 360.0f);
    float albumsW = availW - topTracksW - 14.0f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    renderArtistAlbumCards(albums, albumsW, availH);
    ImGui::SameLine(0.0f, 14.0f);
    renderArtistTopTracks(topTrackIds, topTracksW, availH);
}

void MainWindow::renderLyricsPanel(float width, float height) {
    const Track* curr = m_library.getTrackById(m_currentTrackId);
    if (!curr) {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextDisabled("  No track currently playing");
        return;
    }

    const LyricsData& lyrics = getCachedLyrics(*curr);

    // Track Title & Artist Preview
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "  %s", curr->getDisplayTitle().c_str());
    ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.84f, 1.0f), "  %s", curr->getDisplayArtist().c_str());

    // Badges & Action bar
    ImGui::Spacing();
    if (lyrics.hasLyrics) {
        if (lyrics.isSynced) {
            ImGui::TextColored(Theme::AccentColor(), "  ● SYNCED LRC (%zu lines)", lyrics.lines.size());
        } else {
            ImGui::TextColored(ImVec4(0.60f, 0.65f, 0.72f, 1.0f), "  ● TEXT LYRICS");
        }
    } else {
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "  ○ Instrumental / No Embedded Lyrics");
    }

    ImGui::SameLine();
    float btnW = 165.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW > btnW + 10.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - btnW - 6.0f));
    }
    if (ImGui::SmallButton(" - ")) {
        m_lyricsScale = std::max(0.7f, m_lyricsScale - 0.1f);
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::SmallButton(" + ")) {
        m_lyricsScale = std::min(1.8f, m_lyricsScale + 0.1f);
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::SmallButton("Load .LRC...")) {
        std::string lrcFile = Platform::openFileDialog("Lyrics Files (*.lrc;*.txt)", "*.lrc;*.txt");
        if (!lrcFile.empty()) {
            m_lyrics.setCustomLyricsFile(curr->id, lrcFile);
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (!lyrics.hasLyrics) {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.78f, 1.0f), "  Place a '.lrc' file next to your audio file\n  or click 'Load .LRC...' above to link lyrics.");
        return;
    }

    // Scrollable lyrics container
    float scrollH = height - ImGui::GetCursorPosY() - 10.0f;
    if (scrollH < 100.0f) scrollH = 100.0f;
    ImGui::BeginChild("LyricsScrollArea", ImVec2(0.0f, scrollH), false, ImGuiWindowFlags_None);
    ImGui::SetWindowFontScale(m_lyricsScale);

    if (lyrics.isSynced) {
        double currentPos = m_audio.getCurrentTime();
        int activeIdx = LyricsManager::findActiveLineIndex(lyrics.lines, currentPos);

        static int lastActiveIdx = -1;
        static uint64_t lastTrackId = 0;
        bool needAutoScroll = (activeIdx != lastActiveIdx || curr->id != lastTrackId);
        lastActiveIdx = activeIdx;
        lastTrackId = curr->id;

        for (int i = 0; i < static_cast<int>(lyrics.lines.size()); ++i) {
            const auto& line = lyrics.lines[i];
            bool isActive = (i == activeIdx);

            if (isActive) {
                // Auto-center currently active line in the view!
                if (needAutoScroll) {
                    ImGui::SetScrollHereY(0.40f);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
            } else if (i < activeIdx) {
                // Past lines (soft silver)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.76f, 0.84f, 1.0f));
            } else {
                // Upcoming lines (darker silver)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.55f, 1.0f));
            }

            std::string lineDisplay;
            if (isActive) {
                lineDisplay = " > " + (line.text.empty() ? "..." : line.text);
            } else {
                lineDisplay = "   " + (line.text.empty() ? "..." : line.text);
            }

            std::string id = lineDisplay + "###lyric_" + std::to_string(i);
            if (ImGui::Selectable(id.c_str(), isActive, ImGuiSelectableFlags_SpanAllColumns)) {
                // Interactive karaoke seeking: Click to jump!
                m_audio.seekTo(line.timeSeconds);
            }
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered()) {
                int mm = static_cast<int>(line.timeSeconds) / 60;
                int ss = static_cast<int>(line.timeSeconds) % 60;
                ImGui::SetTooltip("Jump to %02d:%02d", mm, ss);
            }
        }
    } else {
        // Plain text lyrics
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.92f, 0.96f, 1.0f));
        ImGui::TextWrapped("%s", lyrics.plainText.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();
}

void MainWindow::renderRightPanel(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::PanelBackground());
    ImGuiWindowFlags rightFlags = ImGuiWindowFlags_None;
    if (m_rightPanelTab == RightPanelTab::Queue || m_rightPanelTab == RightPanelTab::Lyrics) {
        rightFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }
    ImGui::BeginChild("RightInfoPanel", ImVec2(width, height), false, rightFlags);

    // Segmented Pill Capsule Control [ Queue (N) | Details | Lyrics ]
    ImGui::Spacing();
    std::string qTabLabel = (width >= 240.0f ? "Queue (" : "Q (") + std::to_string(m_queue.size()) + ")";
    const char* tabLabels[3] = { qTabLabel.c_str(), "Details", "Lyrics" };
    RightPanelTab tabValues[3] = { RightPanelTab::Queue, RightPanelTab::TrackInfo, RightPanelTab::Lyrics };

    float segTotalW = (std::min)(width - 48.0f, 240.0f);
    if (segTotalW < 140.0f) segTotalW = width - 40.0f;
    float segH = 26.0f;
    float segItemW = segTotalW / 3.0f;

    ImVec2 segP0 = ImGui::GetCursorScreenPos();
    segP0.x += 8.0f;
    ImVec2 segP1 = ImVec2(segP0.x + segTotalW, segP0.y + segH);
    ImDrawList* rDl = ImGui::GetWindowDrawList();

    // Segmented Outer Capsule Background
    rDl->AddRectFilled(segP0, segP1, IM_COL32(18, 21, 27, 240), 13.0f);
    rDl->AddRect(segP0, segP1, IM_COL32(36, 42, 54, 255), 13.0f, 0, 1.0f);

    for (int t = 0; t < 3; ++t) {
        ImVec2 tP0(segP0.x + t * segItemW, segP0.y);
        ImVec2 tP1(tP0.x + segItemW, segP0.y + segH);
        bool isAct = (m_rightPanelTab == tabValues[t]);

        std::string bId = "##RP_Tab_" + std::to_string(t);
        ImGui::SetCursorScreenPos(tP0);
        if (ImGui::InvisibleButton(bId.c_str(), ImVec2(segItemW, segH))) {
            m_rightPanelTab = tabValues[t];
        }
        bool isHov = ImGui::IsItemHovered();

        if (isAct) {
            // Selected Pill with subtle glow & theme accent tint
            ImVec4 acc = Theme::AccentColor();
            ImU32 pillBg = IM_COL32(static_cast<int>(acc.x * 255.0f * 0.25f + 40),
                                    static_cast<int>(acc.y * 255.0f * 0.25f + 48),
                                    static_cast<int>(acc.z * 255.0f * 0.25f + 60), 255);
            rDl->AddRectFilled(ImVec2(tP0.x + 2.0f, tP0.y + 2.0f), ImVec2(tP1.x - 2.0f, tP1.y - 2.0f), pillBg, 11.0f);
            rDl->AddRect(ImVec2(tP0.x + 2.0f, tP0.y + 2.0f), ImVec2(tP1.x - 2.0f, tP1.y - 2.0f), ImGui::GetColorU32(Theme::AccentColor()), 11.0f, 0, 1.0f);
        } else if (isHov) {
            rDl->AddRectFilled(ImVec2(tP0.x + 2.0f, tP0.y + 2.0f), ImVec2(tP1.x - 2.0f, tP1.y - 2.0f), IM_COL32(255, 255, 255, 14), 11.0f);
        }

        ImVec2 txtSz = ImGui::CalcTextSize(tabLabels[t]);
        ImVec2 txtPos(tP0.x + (segItemW - txtSz.x) * 0.5f, tP0.y + (segH - txtSz.y) * 0.5f);
        ImU32 txtCol = isAct ? IM_COL32(255, 255, 255, 255) : (isHov ? IM_COL32(220, 226, 238, 255) : IM_COL32(145, 154, 168, 220));
        rDl->AddText(txtPos, txtCol, tabLabels[t]);
    }

    if (m_rightPanelTab == RightPanelTab::Queue) {
        float moreBtnW = 26.0f;
        float moreX = width - moreBtnW - 10.0f;
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + moreX, segP0.y));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.16f, 0.20f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.25f, 0.32f, 1.0f));
        if (ImGui::Button("•••##QueueHeaderMore")) {
            ImGui::OpenPopup("QueueHeaderMorePopup");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Queue Actions");
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (ImGui::BeginPopup("QueueHeaderMorePopup")) {
            if (ImGui::MenuItem("Shuffle Queue", nullptr, false, m_queue.size() > 1)) {
                if (m_queue.size() > 1) {
                    uint64_t curId = m_queueIndex < m_queue.size() ? m_queue[m_queueIndex] : 0;
                    std::random_device rd;
                    std::mt19937 g(rd());
                    std::shuffle(m_queue.begin(), m_queue.end(), g);
                    if (curId > 0) {
                        auto it = std::find(m_queue.begin(), m_queue.end(), curId);
                        if (it != m_queue.end()) {
                            m_queue.erase(it);
                            m_queue.insert(m_queue.begin(), curId);
                            m_queueIndex = 0;
                        }
                    }
                    m_selectedQueueIndex = -1;
                    m_selectedQueueIndices.clear();
                    rebuildShuffleOrder(true);
                    savePreferences();
                }
            }
            if (ImGui::MenuItem("Reverse Queue Order", nullptr, false, m_queue.size() > 1)) {
                std::reverse(m_queue.begin(), m_queue.end());
                m_queueIndex = m_queue.empty() ? 0 : (m_queue.size() - 1 - m_queueIndex);
                m_selectedQueueIndex = -1;
                m_selectedQueueIndices.clear();
                rebuildShuffleOrder(true);
                savePreferences();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Queue as Playlist...", nullptr, false, !m_queue.empty())) {
                m_showSaveQueueAsPlaylistModal = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Queue", nullptr, false, !m_queue.empty())) {
                m_queue.clear();
                m_queueIndex = 0;
                m_selectedQueueIndex = -1;
                m_selectedQueueIndices.clear();
                m_shuffleOrder.clear();
                m_shuffleOrderPos = 0;
                savePreferences();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::NewLine();
    ImGui::Separator();

    const Track* curr = m_library.getTrackById(m_currentTrackId);

    if (m_rightPanelTab == RightPanelTab::Lyrics) {
        renderLyricsPanel(width, height);
    } else if (m_rightPanelTab == RightPanelTab::TrackInfo) {
        ImGui::Spacing();
        if (curr) {
            float availW = ImGui::GetContentRegionAvail().x;
            float artSide = std::min(m_rightPanelArtSize, std::max(64.0f, availW - 8.0f));
            int artW = 0, artH = 0;
            ImTextureID bigArt = m_textures.getTrackArtwork(*curr, &artW, &artH);
            if (!bigArt) bigArt = m_textures.getDefaultArtwork(&artW, &artH);
            if (bigArt) {
                ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
                TextureManager::getAspectFillUV(artW, artH, 1.0f, uv0, uv1);
                float padX = (availW - artSide) * 0.5f;
                if (padX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
                ImVec2 imgPos = ImGui::GetCursorScreenPos();
                ImDrawList* dlDetails = ImGui::GetWindowDrawList();
                dlDetails->AddRectFilled(ImVec2(imgPos.x + 2.0f, imgPos.y + 2.0f), ImVec2(imgPos.x + artSide + 2.0f, imgPos.y + artSide + 2.0f), IM_COL32(0, 0, 0, 45), 6.0f);
                dlDetails->AddImageRounded(bigArt, imgPos, ImVec2(imgPos.x + artSide, imgPos.y + artSide), uv0, uv1, IM_COL32_WHITE, 6.0f);
                dlDetails->AddRect(imgPos, ImVec2(imgPos.x + artSide, imgPos.y + artSide), IM_COL32(45, 50, 62, 200), 6.0f, 0, 1.0f);
                ImGui::Dummy(ImVec2(artSide, artSide));
                ImGui::Spacing();
            }

            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", curr->getDisplayTitle().c_str());
            ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.94f, 1.0f), "%s", curr->getDisplayArtist().c_str());
            ImGui::TextColored(ImVec4(0.74f, 0.78f, 0.84f, 1.0f), "%s", curr->getDisplayAlbum().c_str());
            if (curr->year > 0) ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.74f, 1.0f), "Year: %d", curr->year);
            if (!curr->genre.empty()) ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.74f, 1.0f), "Genre: %s", curr->genre.c_str());
            if (curr->playCount > 0) ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.74f, 1.0f), "Plays: %d", curr->playCount);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(Theme::AccentColor(), "TECHNICAL SPECS");
            if (curr->bitrate > 0) ImGui::Text("Bitrate: %d kbps", curr->bitrate);
            if (curr->sampleRate > 0) ImGui::Text("Sample Rate: %d Hz", curr->sampleRate);
            ImGui::Text("Channels: %d", curr->channels);
            if (curr->fileSizeBytes > 0) ImGui::Text("Size: %.2f MB", curr->fileSizeBytes / (1024.0 * 1024.0));
            ImGui::TextWrapped("File: %s", curr->filePath.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(Theme::AccentColor(), "HARDWARE AUDIO OUTPUT");
            bool isExcl = m_audio.isExclusiveActive();
            if (isExcl) {
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.50f, 1.0f), "Driver: WASAPI Exclusive (Bit-Perfect)");
            } else if (m_audio.getAudioDriverType() == AudioDriverType::DirectSound) {
                ImGui::Text("Driver: DirectSound");
            } else {
                ImGui::Text("Driver: WASAPI Shared");
            }
            ImGui::TextWrapped("Device: %s", m_audio.getActiveDeviceName().c_str());
            ImGui::Text("Buffer: %d ms", m_audio.getBufferLatencyMs());
            if (ImGui::SmallButton("Audio Driver Preferences...")) {
                openAudioDriverModal();
            }
        } else {
            ImGui::TextDisabled("No track playing");
        }
    } else {
        // SlothPlayer Two-Tier Right Panel: Top Queue + (Optional) Bottom Track Info & Large Album Art
        auto renderQueueTableHelper = [&](float childH) {
            // Next Up preview card if there's a next track
            const Track* nextTrack = nullptr;
            if (m_audio.isShuffle()) {
                if (!isShuffleOrderValid()) {
                    rebuildShuffleOrder(true);
                }
                if (m_shuffleOrderPos + 1 < m_shuffleOrder.size()) {
                    size_t nextQIdx = m_shuffleOrder[m_shuffleOrderPos + 1];
                    if (nextQIdx < m_queue.size()) {
                        nextTrack = m_library.getTrackById(m_queue[nextQIdx]);
                    }
                } else if (m_audio.getRepeatMode() == RepeatMode::All && !m_shuffleOrder.empty()) {
                    size_t nextQIdx = m_shuffleOrder[0];
                    if (nextQIdx < m_queue.size()) {
                        nextTrack = m_library.getTrackById(m_queue[nextQIdx]);
                    }
                }
            } else {
                size_t nextIdx = m_queueIndex + 1;
                size_t qSize = m_queue.size();
                size_t searchCount = 0;
                while (searchCount < qSize && qSize > 0) {
                    if (nextIdx >= qSize) {
                        if (m_audio.getRepeatMode() == RepeatMode::All) {
                            nextIdx = 0;
                        } else {
                            break;
                        }
                    }
                    if (nextIdx == m_queueIndex && searchCount > 0) break;
                    const Track* cand = m_library.getTrackById(m_queue[nextIdx]);
                    if (cand && (!m_skipDislikedOnAutoplay || !cand->isDisliked)) {
                        nextTrack = cand;
                        break;
                    }
                    nextIdx++;
                    searchCount++;
                }
            }

            if (nextTrack) {
                ImVec2 cardP0 = ImGui::GetCursorScreenPos();
                float availW = ImGui::GetContentRegionAvail().x;
                float cardH = 34.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();

                // Background card
                dl->AddRectFilled(cardP0, ImVec2(cardP0.x + availW, cardP0.y + cardH), IM_COL32(22, 25, 32, 240), 6.0f);
                dl->AddRect(cardP0, ImVec2(cardP0.x + availW, cardP0.y + cardH), IM_COL32(40, 44, 56, 200), 6.0f, 0, 1.0f);

                // Next Artwork
                int nArtW = 0, nArtH = 0;
                ImTextureID nArt = m_textures.getTrackArtwork(*nextTrack, &nArtW, &nArtH);
                if (!nArt) nArt = m_textures.getDefaultArtwork(&nArtW, &nArtH);
                if (nArt) {
                    ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
                    TextureManager::getAspectFillUV(nArtW, nArtH, 1.0f, uv0, uv1);
                    dl->AddImageRounded(nArt, ImVec2(cardP0.x + 4.0f, cardP0.y + 4.0f), ImVec2(cardP0.x + 30.0f, cardP0.y + 30.0f), uv0, uv1, IM_COL32_WHITE, 4.0f);
                }

                // Next track text
                dl->AddText(ImVec2(cardP0.x + 36.0f, cardP0.y + 2.0f), ImGui::GetColorU32(Theme::AccentColor()), "NEXT UP \xE2\x96\xB8");
                std::string nextTitle = truncateTextToWidth(nextTrack->getDisplayTitle(), std::max(20.0f, availW - 46.0f));
                dl->AddText(ImVec2(cardP0.x + 36.0f, cardP0.y + 17.0f), IM_COL32(220, 225, 235, 255), nextTitle.c_str());

                ImGui::InvisibleButton("##NextUpCard", ImVec2(availW, cardH));
                if (ImGui::IsItemHovered()) {
                    dl->AddRect(cardP0, ImVec2(cardP0.x + availW, cardP0.y + cardH), ImGui::GetColorU32(Theme::AccentColor()), 6.0f, 0, 1.0f);
                    ImGui::SetTooltip("Next Up: %s - %s\nClick to play now", nextTrack->getDisplayTitle().c_str(), nextTrack->getDisplayArtist().c_str());
                }
                if (ImGui::IsItemClicked()) {
                    playNext();
                }
                ImGui::Spacing();
            }

            ImGui::BeginChild("QueueListSubChild", ImVec2(0.0f, childH), false, ImGuiWindowFlags_None);

            ImGuiTableFlags qTableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoBordersInBody;
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 4.0f));
            if (ImGui::BeginTable("QueueTable", 3, qTableFlags, ImVec2(0.0f, 0.0f))) {
                ImGui::TableSetupColumn("Thumb", ImGuiTableColumnFlags_WidthFixed, 46.0f);
                ImGui::TableSetupColumn("Meta", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 46.0f);

                const float rowH = 44.0f;
                const float thumbSide = 34.0f;

                size_t startIdx = (m_queueViewMode == QueueViewMode::UpcomingTracks)
                    ? ((m_queueIndex + 1 < m_queue.size()) ? (m_queueIndex + 1) : m_queue.size())
                    : 0;

                for (size_t i = startIdx; i < m_queue.size(); ++i) {
                    const Track* qt = m_library.getTrackById(m_queue[i]);
                    if (!qt) continue;

                    bool isCurrentQueue = (i == m_queueIndex);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);

                    // Col 0: Whole-row selectable spanning all columns
                    ImGui::TableSetColumnIndex(0);
                    ImVec2 cellPos = ImGui::GetCursorScreenPos();

                    std::string selId = "##qrow_" + std::to_string(i);
                    bool isRowSelected = (m_selectedQueueIndices.count(i) > 0) ||
                                         (m_selectedQueueIndices.empty() && m_selectedQueueIndex == static_cast<int>(i)) ||
                                         (m_selectedQueueIndices.empty() && m_selectedQueueIndex < 0 && isCurrentQueue);

                    bool rowClicked = ImGui::Selectable(selId.c_str(), isRowSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, rowH));
                    bool rowDoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

                    if (rowDoubleClicked) {
                        m_selectedQueueIndices.clear();
                        m_selectedQueueIndices.insert(i);
                        m_selectedQueueIndex = static_cast<int>(i);
                        m_queueAnchorIdx = i;
                        playQueueIndex(i);
                    } else if (rowClicked) {
                        bool shiftHeld = ImGui::GetIO().KeyShift;
                        bool ctrlHeld = ImGui::GetIO().KeyCtrl;

                        if (shiftHeld) {
                            m_selectedQueueIndices.clear();
                            size_t minIdx = std::min(m_queueAnchorIdx, i);
                            size_t maxIdx = std::max(m_queueAnchorIdx, i);
                            for (size_t k = minIdx; k <= maxIdx; ++k) {
                                m_selectedQueueIndices.insert(k);
                            }
                            m_selectedQueueIndex = static_cast<int>(i);
                        } else if (ctrlHeld) {
                            if (m_selectedQueueIndices.count(i)) {
                                m_selectedQueueIndices.erase(i);
                                if (m_selectedQueueIndex == static_cast<int>(i)) {
                                    m_selectedQueueIndex = m_selectedQueueIndices.empty() ? -1 : static_cast<int>(*m_selectedQueueIndices.rbegin());
                                }
                            } else {
                                m_selectedQueueIndices.insert(i);
                                m_selectedQueueIndex = static_cast<int>(i);
                                m_queueAnchorIdx = i;
                            }
                        } else {
                            m_selectedQueueIndices.clear();
                            m_selectedQueueIndices.insert(i);
                            m_selectedQueueIndex = static_cast<int>(i);
                            m_queueAnchorIdx = i;
                        }
                        m_selectedTrackId = qt->id;
                    }

                    // Native ImGui Drag & Drop Reordering
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        ImGui::SetDragDropPayload("DND_QUEUE_ROW", &i, sizeof(size_t));
                        ImGui::Text("Move \"%s\"", qt->getDisplayTitle().c_str());
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_QUEUE_ROW")) {
                            size_t srcIdx = *(const size_t*)payload->Data;
                            if (srcIdx < m_queue.size() && srcIdx != i) {
                                uint64_t movedId = m_queue[srcIdx];
                                m_queue.erase(m_queue.begin() + srcIdx);
                                m_queue.insert(m_queue.begin() + i, movedId);
                                if (m_queueIndex == srcIdx) {
                                    m_queueIndex = i;
                                } else if (srcIdx < m_queueIndex && i >= m_queueIndex) {
                                    m_queueIndex--;
                                } else if (srcIdx > m_queueIndex && i <= m_queueIndex) {
                                    m_queueIndex++;
                                }

                                if (m_selectedQueueIndex >= 0) {
                                    if (static_cast<size_t>(m_selectedQueueIndex) == srcIdx) {
                                        m_selectedQueueIndex = static_cast<int>(i);
                                    } else if (srcIdx < static_cast<size_t>(m_selectedQueueIndex) && i >= static_cast<size_t>(m_selectedQueueIndex)) {
                                        m_selectedQueueIndex--;
                                    } else if (srcIdx > static_cast<size_t>(m_selectedQueueIndex) && i <= static_cast<size_t>(m_selectedQueueIndex)) {
                                        m_selectedQueueIndex++;
                                    }
                                }

                                if (m_audio.isShuffle()) {
                                    rebuildShuffleOrder(true);
                                }
                                savePreferences();
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Accent left border strip for currently playing track
                    if (isCurrentQueue) {
                        ImDrawList* rowDl = ImGui::GetWindowDrawList();
                        rowDl->AddRectFilled(ImVec2(cellPos.x, cellPos.y), ImVec2(cellPos.x + 3.0f, cellPos.y + rowH), ImGui::GetColorU32(Theme::AccentColor()), 2.0f);
                    }

                    renderTrackContextMenu(qt->id, true, i);

                    // Col 0: Draw Artwork Thumbnail with breathing room
                    float thumbY = cellPos.y + (rowH - thumbSide) * 0.5f;
                    ImVec2 imgP0(cellPos.x + 5.0f, thumbY);
                    int artW = 0, artH = 0;
                    ImTextureID art = m_textures.getTrackArtwork(*qt, &artW, &artH);
                    if (!art) art = m_textures.getDefaultArtwork(&artW, &artH);
                    if (art) {
                        ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
                        TextureManager::getAspectFillUV(artW, artH, 1.0f, uv0, uv1);
                        ImDrawList* rowDl = ImGui::GetWindowDrawList();
                        rowDl->AddImageRounded(art, imgP0, ImVec2(imgP0.x + thumbSide, imgP0.y + thumbSide), uv0, uv1, IM_COL32_WHITE, 4.0f);
                        if (isCurrentQueue && m_audio.isPlaying()) {
                            rowDl->AddCircleFilled(ImVec2(imgP0.x + thumbSide * 0.5f, imgP0.y + thumbSide * 0.5f), 11.0f, IM_COL32(0, 0, 0, 160));
                            drawMicroEqualizer(rowDl, ImVec2(imgP0.x + thumbSide * 0.5f, imgP0.y + thumbSide * 0.5f), ImGui::GetColorU32(Theme::AccentColor()), true);
                        }
                    }

                    // Col 1: Title (Line 1) & Artist (Line 2) with clear padding, clipping, and ellipsis
                    ImGui::TableSetColumnIndex(1);
                    ImVec2 textPos = ImGui::GetCursorScreenPos();
                    float textStartX = textPos.x + 6.0f;
                    float availMetaW = ImGui::GetContentRegionAvail().x;
                    float maxTextW = std::max(20.0f, availMetaW - 10.0f);
                    ImDrawList* dl = ImGui::GetWindowDrawList();

                    ImU32 titleCol = isCurrentQueue ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(248, 250, 255, 255);
                    ImU32 artistCol = isCurrentQueue ? ImGui::GetColorU32(ImVec4(Theme::AccentColor().x * 0.85f, Theme::AccentColor().y * 0.85f, Theme::AccentColor().z * 0.85f, 1.0f)) : IM_COL32(165, 175, 192, 255);

                    dl->PushClipRect(ImVec2(textStartX, cellPos.y), ImVec2(textStartX + maxTextW, cellPos.y + rowH), true);

                    ImGui::PushStyleColor(ImGuiCol_Text, titleCol);
                    ImGui::RenderTextEllipsis(dl,
                        ImVec2(textStartX, cellPos.y + 5.0f),
                        ImVec2(textStartX + maxTextW, cellPos.y + 22.0f),
                        textStartX + maxTextW,
                        qt->getDisplayTitle().c_str(),
                        nullptr,
                        nullptr);
                    ImGui::PopStyleColor();

                    ImGui::PushStyleColor(ImGuiCol_Text, artistCol);
                    ImGui::RenderTextEllipsis(dl,
                        ImVec2(textStartX, cellPos.y + 23.0f),
                        ImVec2(textStartX + maxTextW, cellPos.y + 39.0f),
                        textStartX + maxTextW,
                        qt->getDisplayArtist().c_str(),
                        nullptr,
                        nullptr);
                    ImGui::PopStyleColor();

                    dl->PopClipRect();

                    // Col 2: Duration (Right-aligned, vertically centered)
                    ImGui::TableSetColumnIndex(2);
                    std::string dur = qt->formatDuration();
                    float durW = ImGui::CalcTextSize(dur.c_str()).x;
                    float availCellW = ImGui::GetContentRegionAvail().x;
                    float durX = ImGui::GetCursorScreenPos().x + std::max(0.0f, availCellW - durW - 6.0f);
                    float durY = cellPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                    dl->AddText(ImVec2(durX, durY), IM_COL32(170, 180, 195, 255), dur.c_str());

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
            ImGui::EndChild();
        };

        if (!m_showRightTrackInfo) {
            renderQueueTableHelper(0.0f);
        } else {
            float availContentH = std::max(120.0f, ImGui::GetContentRegionAvail().y);
            float splitterH = 1.0f;
            float topH = std::clamp((availContentH - splitterH) * m_rightQueueSplitRatio, 60.0f, availContentH - splitterH - 60.0f);
            float bottomH = availContentH - splitterH - topH;

            // Top Sub-Child: Playing Tracks Queue
            renderQueueTableHelper(topH);

            // Horizontal Splitter Bar (1px thin hairline + proximity resize arrows)
            ImVec2 splitterPos = ImGui::GetCursorScreenPos();
            float availW = ImGui::GetContentRegionAvail().x;
            float hitMarginH = 14.0f;
            ImGui::SetCursorScreenPos(ImVec2(splitterPos.x, splitterPos.y - hitMarginH));
            ImGui::InvisibleButton("##RightQueueSplitter", ImVec2(availW, hitMarginH * 2.0f));
            bool isSplitterHovered = ImGui::IsItemHovered();
            bool isSplitterActive = ImGui::IsItemActive();
            ImGui::SetCursorScreenPos(ImVec2(splitterPos.x, splitterPos.y + splitterH));

            ImVec2 mousePos = ImGui::GetIO().MousePos;
            bool isNearHoriz = !m_lockPanels && (isSplitterHovered || m_isDraggingRightQueueSplitter ||
                (mousePos.y >= splitterPos.y - hitMarginH && mousePos.y <= splitterPos.y + hitMarginH &&
                 mousePos.x >= splitterPos.x && mousePos.x <= splitterPos.x + availW));
            m_isNearRightQueueSplitter = isNearHoriz;

            if (!m_lockPanels) {
                if (isNearHoriz && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    m_isDraggingRightQueueSplitter = true;
                }
                if (m_isDraggingRightQueueSplitter) {
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        float deltaY = ImGui::GetIO().MouseDelta.y;
                        if (deltaY != 0.0f && availContentH > 100.0f) {
                            float newTopH = topH + deltaY;
                            m_rightQueueSplitRatio = std::clamp(newTopH / (availContentH - splitterH), 0.15f, 0.85f);
                        }
                    } else {
                        m_isDraggingRightQueueSplitter = false;
                        savePreferences();
                    }
                }
            }

            if (isNearHoriz || m_isDraggingRightQueueSplitter) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            }

            ImDrawList* dlSplit = ImGui::GetWindowDrawList();
            ImU32 accentCol = ImGui::GetColorU32(Theme::AccentColor());
            ImU32 splitLineCol = (m_isDraggingRightQueueSplitter || isSplitterActive) ? accentCol
                               : (isNearHoriz ? IM_COL32(110, 140, 180, 255) : IM_COL32(40, 44, 52, 255));
            dlSplit->AddLine(splitterPos, ImVec2(splitterPos.x + availW, splitterPos.y), splitLineCol, 1.0f);

            if (isNearHoriz || m_isDraggingRightQueueSplitter) {
                float badgeX = std::clamp(mousePos.x, splitterPos.x + 24.0f, splitterPos.x + availW - 24.0f);
                drawResizableArrowBadge(ImGui::GetForegroundDrawList(), ImVec2(badgeX, splitterPos.y), false, accentCol);
            }

            // Bottom Sub-Child: Track Information + Large Square Artwork
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
            ImGui::BeginChild("TrackInfoSubChild", ImVec2(0.0f, bottomH), false, ImGuiWindowFlags_None);

            // Header button with dropdown arrow (Matching SlothPlayer Screenshot)
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(165, 172, 185, 255));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.15f));

            const char* tiHeaderTitle = (m_rightBottomView == RightBottomView::ArtworkAndInfo)
                ? "Track Information \xE2\x96\xBE"
                : ((m_rightBottomView == RightBottomView::AlbumCover) ? "Album Covers \xE2\x96\xBE" : "Track Information \xE2\x96\xBE");
            bool openTiMenu = ImGui::Button(tiHeaderTitle, ImVec2(0.0f, 22.0f)) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
            if (openTiMenu) {
                ImGui::OpenPopup("TrackInfoPanelDropdown");
            }
            ImVec2 tiBtnMin = ImGui::GetItemRectMin();
            ImVec2 tiBtnMax = ImGui::GetItemRectMax();
            ImGui::PopStyleColor(4);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.13f, 0.16f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.28f, 0.34f, 1.0f));
            ImGui::SetNextWindowPos(ImVec2(tiBtnMin.x, tiBtnMax.y + 2.0f), ImGuiCond_Appearing);

            auto renderTiMenuItems = [&]() {
                if (ImGui::MenuItem("Track Information & Artwork", nullptr, m_rightBottomView == RightBottomView::ArtworkAndInfo)) {
                    m_rightBottomView = RightBottomView::ArtworkAndInfo;
                    savePreferences();
                }
                if (ImGui::MenuItem("Track Information Only", nullptr, m_rightBottomView == RightBottomView::TrackInfo)) {
                    m_rightBottomView = RightBottomView::TrackInfo;
                    savePreferences();
                }
                if (ImGui::MenuItem("Album Covers Only", nullptr, m_rightBottomView == RightBottomView::AlbumCover)) {
                    m_rightBottomView = RightBottomView::AlbumCover;
                    savePreferences();
                }
                if (ImGui::MenuItem("Preferences & Artwork Size...", "Ctrl+P")) {
                    showPreferencesModal(0);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(m_lockPanels ? "Unlock Panels" : "Lock Panel", nullptr, m_lockPanels)) {
                    m_lockPanels = !m_lockPanels;
                    savePreferences();
                }
                if (ImGui::MenuItem("Close Panel")) {
                    m_showRightTrackInfo = false;
                    savePreferences();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Arrange Panels...")) {
                    m_showPanelsConfigModal = true;
                }
            };

            if (ImGui::BeginPopup("TrackInfoPanelDropdown")) {
                renderTiMenuItems();
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);

            // Right-clicking empty space in the TrackInfoSubChild also brings up the SlothPlayer panel menu
            if (ImGui::BeginPopupContextWindow("TrackInfoBgContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                renderTiMenuItems();
                ImGui::EndPopup();
            }

            auto renderMetadataBlock = [&](const Track* curr, float maxW) {
                if (!curr) return;
                (void)maxW;

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));

                // 1. Title: Bold, prominent, bright white (MusicBee style)
                ImGui::SetWindowFontScale(1.05f);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
                ImGui::TextWrapped("%s", curr->getDisplayTitle().c_str());
                ImGui::PopStyleColor();
                ImGui::SetWindowFontScale(1.0f);

                // 2. Artist: Crisp off-white / silver
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(215, 220, 230, 255));
                ImGui::TextWrapped("%s", curr->getDisplayArtist().c_str());
                ImGui::PopStyleColor();

                // 3. Album: Muted light gray
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(175, 182, 195, 255));
                ImGui::TextWrapped("%s", curr->getDisplayAlbum().c_str());
                ImGui::PopStyleColor();

                // 4. Date / Year: On its own line, matching MusicBee
                std::string dateStr;
                auto itDate = curr->rawTags.find("DATE");
                if (itDate != curr->rawTags.end() && !itDate->second.empty()) {
                    dateStr = itDate->second;
                } else {
                    auto itYear = curr->rawTags.find("YEAR");
                    if (itYear != curr->rawTags.end() && !itYear->second.empty()) {
                        dateStr = itYear->second;
                    } else if (curr->year > 0) {
                        dateStr = std::to_string(curr->year);
                    }
                }
                if (!dateStr.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140, 146, 160, 255));
                    ImGui::TextUnformatted(dateStr.c_str());
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

                // 5. Clean comma-separated audio format specs line matching MusicBee
                // Format: "FLAC 24 bit, 96 kHz, 2289k, Stereo, 1:44"
                std::string codecName;
                if (!curr->codec.empty()) {
                    codecName = curr->codec;
                } else {
                    size_t dot = curr->filePath.find_last_of('.');
                    if (dot != std::string::npos) {
                        codecName = curr->filePath.substr(dot + 1);
                        for (auto& c : codecName) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                    } else {
                        codecName = "AUDIO";
                    }
                }

                std::string specsLine = codecName;
                if (curr->bitsPerSample > 0) {
                    specsLine += " " + std::to_string(curr->bitsPerSample) + " bit";
                }
                if (curr->sampleRate > 0) {
                    specsLine += ", ";
                    if (curr->sampleRate % 1000 == 0) {
                        specsLine += std::to_string(curr->sampleRate / 1000) + " kHz";
                    } else {
                        char srBuf[16];
                        snprintf(srBuf, sizeof(srBuf), "%.1f kHz", curr->sampleRate / 1000.0f);
                        specsLine += srBuf;
                    }
                }
                if (curr->bitrate > 0) {
                    specsLine += ", " + std::to_string(curr->bitrate) + "k";
                }
                if (curr->channels == 1) {
                    specsLine += ", Mono";
                } else if (curr->channels == 2) {
                    specsLine += ", Stereo";
                } else if (curr->channels > 2) {
                    specsLine += ", " + std::to_string(curr->channels) + " ch";
                }
                if (curr->duration > 0.0) {
                    int totalSec = static_cast<int>(curr->duration);
                    char durBuf[16];
                    snprintf(durBuf, sizeof(durBuf), ", %d:%02d", totalSec / 60, totalSec % 60);
                    specsLine += durBuf;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 144, 156, 255));
                ImGui::TextWrapped("%s", specsLine.c_str());
                ImGui::PopStyleColor();

                ImGui::PopStyleVar(); // ItemSpacing
            };

            if (curr) {
                float availW = ImGui::GetContentRegionAvail().x;
                float availH = ImGui::GetContentRegionAvail().y;

                if (m_rightBottomView == RightBottomView::AlbumCover) {
                    // Album Covers View: artwork fills available space with aspect ratio preserved
                    float artSide = std::max(48.0f, std::min(availW, availH - 10.0f));
                    int artW = 0, artH = 0;
                    ImTextureID bigArt = m_textures.getTrackArtwork(*curr, &artW, &artH);
                    if (!bigArt) bigArt = m_textures.getDefaultArtwork(&artW, &artH);
                    if (bigArt) {
                        ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
                        TextureManager::getAspectFillUV(artW, artH, 1.0f, uv0, uv1);
                        float padX = (availW - artSide) * 0.5f;
                        if (padX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
                        ImVec2 imgPos = ImGui::GetCursorScreenPos();
                        ImDrawList* dlChild = ImGui::GetWindowDrawList();
                        dlChild->AddRectFilled(ImVec2(imgPos.x + 2.0f, imgPos.y + 2.0f), ImVec2(imgPos.x + artSide + 2.0f, imgPos.y + artSide + 2.0f), IM_COL32(0, 0, 0, 45), 6.0f);
                        dlChild->AddImageRounded(bigArt, imgPos, ImVec2(imgPos.x + artSide, imgPos.y + artSide), uv0, uv1, IM_COL32_WHITE, 6.0f);
                        dlChild->AddRect(imgPos, ImVec2(imgPos.x + artSide, imgPos.y + artSide), IM_COL32(45, 50, 62, 200), 6.0f, 0, 1.0f);
                        ImGui::Dummy(ImVec2(artSide, artSide));

                        if (ImGui::BeginPopupContextItem("AlbumCoverArtworkCtx")) {
                            if (ImGui::MenuItem("Track Information...", "Shift+Enter")) {
                                showTrackInfoModal(curr->id);
                            }
                            if (ImGui::MenuItem("Preferences & Artwork Size...", "Ctrl+P")) {
                                showPreferencesModal(0);
                            }
                            if (ImGui::MenuItem("Play Next")) {
                                size_t insertPos = (m_queueIndex + 1 <= m_queue.size()) ? (m_queueIndex + 1) : m_queue.size();
                                m_queue.insert(m_queue.begin() + insertPos, curr->id);
                            }
                            if (ImGui::MenuItem("Show in Tracks View")) {
                                m_viewMode = ViewMode::Tracks;
                                m_selectedTrackId = curr->id;
                            }
                            if (ImGui::MenuItem("Open Containing Folder")) {
                                fs::path p(curr->filePath);
                                Platform::openDirectory(p.parent_path().string());
                            }
                            ImGui::EndPopup();
                        }
                    }
                } else if (m_rightBottomView == RightBottomView::ArtworkAndInfo) {
                    // Artwork and Information View: Track info on TOP, Artwork BELOW (Matching MusicBee)
                    renderMetadataBlock(curr, availW);

                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);

                    float remH = ImGui::GetContentRegionAvail().y - 6.0f;
                    float artSide = (remH >= 64.0f) ? std::min(availW, remH) : availW;
                    artSide = std::max(48.0f, artSide);

                    int artW = 0, artH = 0;
                    ImTextureID bigArt = m_textures.getTrackArtwork(*curr, &artW, &artH);
                    if (!bigArt) bigArt = m_textures.getDefaultArtwork(&artW, &artH);
                    if (bigArt) {
                        ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
                        TextureManager::getAspectFillUV(artW, artH, 1.0f, uv0, uv1);

                        float artPadX = (availW - artSide) * 0.5f;
                        if (artPadX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + artPadX);

                        ImVec2 imgPos = ImGui::GetCursorScreenPos();
                        ImDrawList* dlChild = ImGui::GetWindowDrawList();
                        dlChild->AddRectFilled(ImVec2(imgPos.x + 1.0f, imgPos.y + 1.0f), ImVec2(imgPos.x + artSide + 1.0f, imgPos.y + artSide + 1.0f), IM_COL32(0, 0, 0, 50), 3.0f);
                        dlChild->AddImageRounded(bigArt, imgPos, ImVec2(imgPos.x + artSide, imgPos.y + artSide), uv0, uv1, IM_COL32_WHITE, 3.0f);
                        dlChild->AddRect(imgPos, ImVec2(imgPos.x + artSide, imgPos.y + artSide), IM_COL32(40, 44, 54, 220), 3.0f, 0, 1.0f);
                        ImGui::Dummy(ImVec2(artSide, artSide));

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\n%s\nRight-click for options | Double-click for Now Playing", curr->getDisplayTitle().c_str(), curr->getDisplayArtist().c_str());
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                m_viewMode = ViewMode::NowPlaying;
                            }
                        }

                        if (ImGui::BeginPopupContextItem("ArtworkAndInfoArtCtx")) {
                            if (ImGui::MenuItem("Track Information...", "Shift+Enter")) showTrackInfoModal(curr->id);
                            if (ImGui::MenuItem("Preferences & Artwork Size...", "Ctrl+P")) showPreferencesModal(0);
                            if (ImGui::MenuItem("Play Next")) {
                                size_t insertPos = (m_queueIndex + 1 <= m_queue.size()) ? (m_queueIndex + 1) : m_queue.size();
                                m_queue.insert(m_queue.begin() + insertPos, curr->id);
                            }
                            if (ImGui::MenuItem("Show in Tracks View")) {
                                m_viewMode = ViewMode::Tracks;
                                m_selectedTrackId = curr->id;
                            }
                            if (ImGui::MenuItem("Open Containing Folder")) {
                                fs::path p(curr->filePath);
                                Platform::openDirectory(p.parent_path().string());
                            }
                            ImGui::EndPopup();
                        }
                    }
                } else {
                    // Track Information View: Typographic Metadata
                    renderMetadataBlock(curr, availW);
                }
            } else {
                ImGui::TextColored(ImVec4(0.60f, 0.64f, 0.70f, 1.0f), "No track playing");
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(); // WindowPadding
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ----------------- SlothPlayer Authentic Ultra-Slim Bottom Player Bar -----------------

void MainWindow::renderBottomPlayerBar(float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.065f, 0.070f, 0.082f, 1.0f));
    ImGui::BeginChild("BottomPlayerBar", ImVec2(0.0f, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const Track* curr = m_library.getTrackById(m_currentTrackId);
    float totalW = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 barPos = ImGui::GetWindowPos();

    double curTime = m_audio.getCurrentTime();
    double totalDur = m_audio.getTotalDuration();
    if (curr && totalDur <= 0.0 && curr->duration > 0.0) {
        totalDur = curr->duration;
    }

    // Subtle gradient shadow line at top of bottom bar for depth
    for (int g = 0; g < 4; ++g) {
        float alpha = 35.0f - g * 8.0f;
        dl->AddLine(ImVec2(barPos.x, barPos.y - g), ImVec2(barPos.x + totalW, barPos.y - g),
                    IM_COL32(0, 0, 0, static_cast<int>(alpha)), 1.0f);
    }

    // Top Seek Scrubber Rail across entire bottom bar
    if (m_useWavebar) {
        ImGui::SetCursorScreenPos(ImVec2(barPos.x, barPos.y));
        renderWaveformSeeker(totalW, 16.0f, curTime, totalDur);
    } else {
        float railH = 3.5f;
        float hitH = 14.0f;
        ImGui::SetCursorScreenPos(ImVec2(barPos.x, barPos.y));
        ImGui::InvisibleButton("##TopScrub", ImVec2(totalW, hitH));
        bool scrubHovered = ImGui::IsItemHovered();
        bool scrubActive = ImGui::IsItemActive();

        if (scrubHovered || scrubActive) railH = 5.0f;

        // Rounded dark rail
        float railY = barPos.y + (hitH - railH) * 0.5f;
        dl->AddRectFilled(ImVec2(barPos.x, railY), ImVec2(barPos.x + totalW, railY + railH), IM_COL32(30, 34, 42, 255), railH * 0.5f);

        float progress = (totalDur > 0.0) ? std::clamp(static_cast<float>(curTime / totalDur), 0.0f, 1.0f) : 0.0f;
        float fillW = totalW * progress;
        if (fillW > 0.0f) {
            dl->AddRectFilled(ImVec2(barPos.x, railY), ImVec2(barPos.x + fillW, railY + railH), ImGui::GetColorU32(Theme::AccentColor()), railH * 0.5f);
        }

        // Sleek playhead thumb on hover or active
        if (scrubHovered || scrubActive) {
            float mouseX = ImGui::GetIO().MousePos.x;
            float hovFrac = std::clamp((mouseX - barPos.x) / totalW, 0.0f, 1.0f);
            double hovTime = hovFrac * totalDur;
            int hovSec = static_cast<int>(hovTime);

            // Thumb glow
            dl->AddCircleFilled(ImVec2(barPos.x + fillW, railY + railH * 0.5f), 10.0f, IM_COL32(230, 160, 20, 30));
            // Thumb circle
            dl->AddCircleFilled(ImVec2(barPos.x + fillW, railY + railH * 0.5f), 6.0f, IM_COL32(255, 255, 255, 255));
            dl->AddCircle(ImVec2(barPos.x + fillW, railY + railH * 0.5f), 6.0f, ImGui::GetColorU32(Theme::AccentColor()), 0, 1.5f);

            char hovBuf[16];
            snprintf(hovBuf, sizeof(hovBuf), "%02d:%02d", hovSec / 60, hovSec % 60);

            ImVec2 badgeSize = ImGui::CalcTextSize(hovBuf);
            ImVec2 badgePos = ImVec2(mouseX - badgeSize.x * 0.5f - 8.0f, barPos.y - 28.0f);

            dl->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x + 16.0f, badgePos.y + badgeSize.y + 6.0f), IM_COL32(14, 16, 22, 245), 6.0f);
            dl->AddRect(badgePos, ImVec2(badgePos.x + badgeSize.x + 16.0f, badgePos.y + badgeSize.y + 6.0f), ImGui::GetColorU32(Theme::AccentColor()), 6.0f, 0, 1.0f);
            dl->AddText(ImVec2(badgePos.x + 8.0f, badgePos.y + 3.0f), IM_COL32(250, 250, 250, 255), hovBuf);

            if (scrubActive && totalDur > 0.0) {
                m_audio.seekTo(hovTime);
            }
        }
    }

    float contentY = barPos.y + height * 0.5f + (m_useWavebar ? 5.0f : 3.0f);

    // --- 1. LEFT CONTROLS: Prev, Play/Pause, Next, Stop, Volume Slider, Artwork, Loved Heart, Rating ---
    float curX = barPos.x + 14.0f;

    // Prev Button (larger hit target)
    ImVec2 prevPos(curX + 14.0f, contentY);
    ImGui::SetCursorScreenPos(ImVec2(prevPos.x - 14.0f, prevPos.y - 14.0f));
    if (ImGui::InvisibleButton("##BPrev", ImVec2(28.0f, 28.0f))) playPrevious();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous Track (Z)");
    drawVectorPrevNext(dl, prevPos, 11.0f, false, ImGui::IsItemHovered(), ImGui::IsItemActive());
    curX += 32.0f;

    // Play / Pause Button — Prominent accent circle (36x36)
    ImVec2 playPos(curX + 18.0f, contentY);
    ImGui::SetCursorScreenPos(ImVec2(playPos.x - 18.0f, playPos.y - 18.0f));
    if (ImGui::InvisibleButton("##BPlay", ImVec2(36.0f, 36.0f))) togglePlayPause();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_audio.isPlaying() ? "Pause (Space / C)" : "Play (Space)");
    // Filled accent circle background
    bool playHov = ImGui::IsItemHovered();
    bool playAct = ImGui::IsItemActive();
    ImVec4 accentCol = Theme::AccentColor();
    ImU32 playBgCol = playHov ? ImGui::GetColorU32(Theme::AccentHoverColor())
                              : (playAct ? ImGui::GetColorU32(Theme::AccentActiveColor())
                                         : ImGui::GetColorU32(accentCol));
    dl->AddCircleFilled(playPos, 16.0f, playBgCol);
    // Subtle glow ring when playing
    if (m_audio.isPlaying()) {
        float time = static_cast<float>(ImGui::GetTime());
        float pulse = 0.20f + 0.10f * std::sin(time * 3.0f);
        dl->AddCircle(playPos, 19.0f, IM_COL32(static_cast<int>(accentCol.x * 255), static_cast<int>(accentCol.y * 255), static_cast<int>(accentCol.z * 255), static_cast<int>(pulse * 255)), 0, 1.8f);
    }
    drawVectorPlayPause(dl, playPos, 11.0f, m_audio.isPlaying(), playHov, playAct);
    curX += 40.0f;

    // Next Button
    ImVec2 nextPos(curX + 14.0f, contentY);
    ImGui::SetCursorScreenPos(ImVec2(nextPos.x - 14.0f, nextPos.y - 14.0f));
    if (ImGui::InvisibleButton("##BNext", ImVec2(28.0f, 28.0f))) playNext();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next Track (B)");
    drawVectorPrevNext(dl, nextPos, 11.0f, true, ImGui::IsItemHovered(), ImGui::IsItemActive());
    curX += 32.0f;

    // Stop Button
    ImVec2 stopPos(curX + 14.0f, contentY);
    ImGui::SetCursorScreenPos(ImVec2(stopPos.x - 14.0f, stopPos.y - 14.0f));
    if (ImGui::InvisibleButton("##BStop", ImVec2(28.0f, 28.0f))) stop();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop Playback (X)");
    drawVectorStop(dl, stopPos, 11.0f, ImGui::IsItemHovered(), false);
    curX += 32.0f;

    // Speaker Icon
    if (totalW >= 550.0f) {
        ImVec2 spkPos(curX + 12.0f, contentY);
        ImGui::SetCursorScreenPos(ImVec2(spkPos.x - 12.0f, spkPos.y - 12.0f));
        if (ImGui::InvisibleButton("##BSpk", ImVec2(24.0f, 24.0f))) {
            m_audio.setMuted(!m_audio.isMuted());
            m_audio.saveAudioConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_audio.isMuted() ? "Unmute" : "Mute (Ctrl+M)");
        drawVectorSpeaker(dl, spkPos, 15.0f, m_audio.getVolume(), m_audio.isMuted(), ImGui::IsItemHovered());
        curX += 26.0f;

        // Volume Slider (90px wide)
        float vol = m_audio.getVolume();
        float sliderW = 90.0f;
        float sliderH = 18.0f;
        ImVec2 sMin(curX, contentY - sliderH * 0.5f);
        ImVec2 sMax(curX + sliderW, contentY + sliderH * 0.5f);

        ImGui::SetCursorScreenPos(sMin);
        if (ImGui::InvisibleButton("##BVol", ImVec2(sliderW, sliderH))) {}
        bool volHov = ImGui::IsItemHovered();
        bool volAct = ImGui::IsItemActive();

        if (volHov && ImGui::GetIO().MouseWheel != 0.0f) {
            float delta = ImGui::GetIO().MouseWheel * 0.03f;
            vol = std::clamp(vol + delta, 0.0f, 1.0f);
            m_audio.setVolume(vol);
            if (m_audio.isMuted()) m_audio.setMuted(false);
            m_audio.saveAudioConfig();
        }

        if (volAct) {
            float mouseX = ImGui::GetIO().MousePos.x;
            vol = std::clamp((mouseX - sMin.x) / sliderW, 0.0f, 1.0f);
            m_audio.setVolume(vol);
            if (m_audio.isMuted()) m_audio.setMuted(false);
        }
        if (ImGui::IsItemDeactivated()) {
            m_audio.saveAudioConfig();
        }
        if (volHov || volAct) {
            char vBuf[16];
            snprintf(vBuf, sizeof(vBuf), "%d%%", static_cast<int>(vol * 100.0f));
            ImGui::SetTooltip(m_audio.isMuted() ? "Volume: %s (Muted)" : "Volume: %s", vBuf);
        }

        // Rounded horizontal groove rail
        float railY = contentY;
        dl->AddLine(ImVec2(sMin.x, railY), ImVec2(sMax.x, railY), IM_COL32(42, 46, 56, 255), 4.0f);

        float fillW = sliderW * vol;
        if (fillW > 0.0f) {
            dl->AddLine(ImVec2(sMin.x, railY), ImVec2(sMin.x + fillW, railY), ImGui::GetColorU32(Theme::AccentColor()), 4.0f);
        }

        // Circular thumb knob
        float thumbX = sMin.x + fillW;
        float thumbR = (volHov || volAct) ? 6.5f : 5.0f;
        ImU32 thumbCol = (volAct || volHov) ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 225, 235, 255);
        dl->AddCircleFilled(ImVec2(thumbX, railY), thumbR, thumbCol);
        dl->AddCircle(ImVec2(thumbX, railY), thumbR, ImGui::GetColorU32(Theme::AccentColor()), 0, 1.2f);

        curX += sliderW + 12.0f;

        // Stereo LED VU Peak Meter
        float vuL = 0.0f, vuR = 0.0f;
        m_audio.getVUMeters(vuL, vuR);
        float vuW = 34.0f;
        float vuH = 4.5f;
        float vuY1 = contentY - vuH - 2.0f;
        float vuY2 = contentY + 2.0f;

        float barX = curX;

        dl->AddRectFilled(ImVec2(barX, vuY1), ImVec2(barX + vuW, vuY1 + vuH), IM_COL32(28, 32, 40, 255), 2.0f);
        dl->AddRectFilled(ImVec2(barX, vuY2), ImVec2(barX + vuW, vuY2 + vuH), IM_COL32(28, 32, 40, 255), 2.0f);

        float fillL = std::clamp(vuL, 0.0f, 1.0f) * vuW;
        float fillR = std::clamp(vuR, 0.0f, 1.0f) * vuW;

        if (fillL > 0.5f) {
            ImU32 colL = (vuL > 0.85f) ? IM_COL32(245, 75, 75, 255) : ((vuL > 0.6f) ? IM_COL32(245, 190, 45, 255) : IM_COL32(50, 205, 90, 255));
            dl->AddRectFilled(ImVec2(barX, vuY1), ImVec2(barX + fillL, vuY1 + vuH), colL, 2.0f);
        }
        if (fillR > 0.5f) {
            ImU32 colR = (vuR > 0.85f) ? IM_COL32(245, 75, 75, 255) : ((vuR > 0.6f) ? IM_COL32(245, 190, 45, 255) : IM_COL32(50, 205, 90, 255));
            dl->AddRectFilled(ImVec2(barX, vuY2), ImVec2(barX + fillR, vuY2 + vuH), colR, 2.0f);
        }

        // Sample-and-hold peak values so the readout updates calmly (every 1.25s) rather than flickering
        static float s_tooltipVuL = 0.0f;
        static float s_tooltipVuR = 0.0f;
        static float s_windowPeakL = 0.0f;
        static float s_windowPeakR = 0.0f;
        static double s_lastVuTooltipTime = 0.0;
        static bool s_wasVuHovered = false;

        if (vuL > s_windowPeakL) s_windowPeakL = vuL;
        if (vuR > s_windowPeakR) s_windowPeakR = vuR;

        double nowVuTime = ImGui::GetTime();
        if (nowVuTime - s_lastVuTooltipTime >= 1.25) {
            s_tooltipVuL = s_windowPeakL;
            s_tooltipVuR = s_windowPeakR;
            s_windowPeakL = vuL;
            s_windowPeakR = vuR;
            s_lastVuTooltipTime = nowVuTime;
        }

        ImGui::SetCursorScreenPos(ImVec2(curX, vuY1));
        if (ImGui::InvisibleButton("##StereoVUMeters", ImVec2(vuW, vuH * 2.0f + 4.0f))) {}
        bool isVuHovered = ImGui::IsItemHovered();
        if (isVuHovered && !s_wasVuHovered) {
            // First moment hovered: snapshot peak and grant a full 1.25s hold window so the user can read calmly
            s_tooltipVuL = (s_windowPeakL > 0.0f) ? s_windowPeakL : vuL;
            s_tooltipVuR = (s_windowPeakR > 0.0f) ? s_windowPeakR : vuR;
            s_lastVuTooltipTime = nowVuTime;
        }
        s_wasVuHovered = isVuHovered;

        if (isVuHovered) {
            ImGui::SetTooltip("Stereo Output Peak Meter\nTop (Left):     %.0f%%\nBottom (Right): %.0f%%", s_tooltipVuL * 100.0f, s_tooltipVuR * 100.0f);
        }

        curX += vuW + 16.0f;
    }

    // Album Artwork Thumbnail (48x48 rounded with shadow)
    if (curr) {
        int artW = 0, artH = 0;
        ImTextureID artTex = m_textures.getTrackArtwork(*curr, &artW, &artH);
        if (!artTex) artTex = m_textures.getDefaultArtwork(&artW, &artH);
        if (artTex) {
            float artSz = 48.0f;
            float artY = contentY - artSz * 0.5f;
            ImVec2 artMin(curX, artY);
            ImVec2 artMax(curX + artSz, artY + artSz);
            // Ambient glow when playing & subtle drop shadow
            if (m_audio.isPlaying()) {
                ImVec4 acc = Theme::AccentColor();
                dl->AddRectFilled(ImVec2(artMin.x - 3.0f, artMin.y - 3.0f), ImVec2(artMax.x + 3.0f, artMax.y + 3.0f),
                                  IM_COL32(static_cast<int>(acc.x * 255), static_cast<int>(acc.y * 255), static_cast<int>(acc.z * 255), 55), 8.0f);
            }
            dl->AddRectFilled(ImVec2(artMin.x + 2.0f, artMin.y + 2.0f), ImVec2(artMax.x + 2.0f, artMax.y + 2.0f), IM_COL32(0, 0, 0, 60), 6.0f);
            ImGui::SetCursorScreenPos(artMin);
            if (ImGui::InvisibleButton("##BArtThumb", ImVec2(artSz, artSz))) {
                m_viewMode = ViewMode::NowPlaying;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to switch to Now Playing view");
            dl->AddImageRounded(artTex, artMin, artMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
            dl->AddRect(artMin, artMax, ImGui::IsItemHovered() ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(55, 62, 75, 200), 6.0f, 0, 1.0f);
            curX += artSz + 12.0f;
        }
    }

    // Loved Heart Toggle & Dislike & 5 Stars Rating
    if (curr) {
        ImVec2 heartCenter(curX + 10.0f, contentY);
        ImGui::SetCursorScreenPos(ImVec2(heartCenter.x - 10.0f, heartCenter.y - 10.0f));
        if (ImGui::InvisibleButton("##BHeart", ImVec2(20.0f, 20.0f))) {
            m_library.toggleFavorite(curr->id);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(curr->isFavorite ? "Unmark Favorite (Ctrl+L)" : "Mark Favorite (Ctrl+L)");
        drawVectorHeart(dl, heartCenter, 6.5f, curr->isFavorite, ImGui::IsItemHovered());
        curX += 20.0f;

        // Dislike Button Toggle (Never autoplay unless explicitly clicked)
        ImVec2 dislikeCenter(curX + 10.0f, contentY);
        ImGui::SetCursorScreenPos(ImVec2(dislikeCenter.x - 10.0f, dislikeCenter.y - 10.0f));
        if (ImGui::InvisibleButton("##BDislike", ImVec2(20.0f, 20.0f))) {
            m_library.toggleDislike(curr->id);
            const Track* trk = m_library.getTrackById(curr->id);
            if (trk && trk->isDisliked) {
                if (m_audio.isShuffle()) {
                    rebuildShuffleOrder(false);
                }
                playNext();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(curr->isDisliked ? "Remove Dislike (Song can autoplay)" : "Dislike Song (Suddenly skip to next song)");
        }
        drawVectorDislike(dl, dislikeCenter, 6.0f, curr->isDisliked, ImGui::IsItemHovered());
        curX += 24.0f;

        // 5 Stars (Interactive rating)
        if (totalW >= 680.0f) {
            int r = curr->rating;
            ImVec2 rPos(curX + 4.0f, contentY - 6.0f);
            drawInteractiveStarRating(dl, rPos, r, curr->id);
            curX += 78.0f;
        }
    }

    float leftBoundary = curX + 14.0f;

    // --- 2. RIGHT SECTION: Status, Time, Driver, Icons (Built right-to-left) ---
    float rightBoundary = barPos.x + totalW - 16.0f;

    // Mini Player / Layout panel toggle icon
    rightBoundary -= 24.0f;
    ImVec2 miniPos(rightBoundary + 10.0f, contentY);
    ImGui::SetCursorScreenPos(ImVec2(miniPos.x - 10.0f, miniPos.y - 10.0f));
    if (ImGui::InvisibleButton("##BMiniToggle", ImVec2(22.0f, 22.0f))) {
        toggleMiniPlayer();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("MiniPlayerBottomPopup");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch to Mini Player (Ctrl+Shift+M)\nRight-click for widget modes");
    drawVectorMiniPlayer(dl, miniPos, 14.0f, ImGui::IsItemHovered());

    if (ImGui::BeginPopup("MiniPlayerBottomPopup")) {
        ImGui::TextColored(Theme::AccentColor(), "Mini Player Modes");
        ImGui::Separator();
        if (ImGui::MenuItem("Compact Player (Classic)", nullptr, m_miniPlayerMode == MiniPlayerMode::Compact)) openMiniPlayerInMode(MiniPlayerMode::Compact);
        if (ImGui::MenuItem("Album Art / Vinyl Card", nullptr, m_miniPlayerMode == MiniPlayerMode::AlbumArt)) openMiniPlayerInMode(MiniPlayerMode::AlbumArt);
        if (ImGui::MenuItem("DeskBand / Taskbar Strip", nullptr, m_miniPlayerMode == MiniPlayerMode::Taskbar)) openMiniPlayerInMode(MiniPlayerMode::Taskbar);
        if (ImGui::MenuItem("Minimalist HUD Ticker", nullptr, m_miniPlayerMode == MiniPlayerMode::MinimalHUD)) openMiniPlayerInMode(MiniPlayerMode::MinimalHUD);
        if (ImGui::MenuItem("Floating Synced Lyrics", nullptr, m_miniPlayerMode == MiniPlayerMode::Lyrics)) openMiniPlayerInMode(MiniPlayerMode::Lyrics);
        if (ImGui::MenuItem("Picture-in-Picture / Video Card", nullptr, m_miniPlayerMode == MiniPlayerMode::PiP)) openMiniPlayerInMode(MiniPlayerMode::PiP);
        ImGui::EndPopup();
    }

    // Equalizer Icon
    rightBoundary -= 26.0f;
    ImVec2 eqPos(rightBoundary + 10.0f, contentY);
    ImGui::SetCursorScreenPos(ImVec2(eqPos.x - 11.0f, eqPos.y - 11.0f));
    if (ImGui::InvisibleButton("##BEQ", ImVec2(22.0f, 22.0f))) m_showEqualizer = !m_showEqualizer;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Equalizer & Sound Effects (Ctrl+E)");
    drawVectorEQIcon(dl, eqPos, 15.0f, m_showEqualizer, ImGui::IsItemHovered());

    // Wavebar Toggle
    rightBoundary -= 26.0f;
    ImVec2 wbPos(rightBoundary + 10.0f, contentY);
    ImGui::SetCursorScreenPos(ImVec2(wbPos.x - 11.0f, wbPos.y - 11.0f));
    if (ImGui::InvisibleButton("##BWavebarToggle", ImVec2(22.0f, 22.0f))) {
        m_useWavebar = !m_useWavebar;
        savePreferences();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(m_useWavebar ? "Waveform Bar (Click for slim progress bar)" : "Slim Progress Bar (Click for Waveform)");
    }
    drawVectorWavebar(dl, wbPos, 14.0f, m_useWavebar, ImGui::IsItemHovered());

    // Stop After Current Track
    if (totalW >= 560.0f) {
        rightBoundary -= 26.0f;
        ImVec2 stopPos(rightBoundary + 10.0f, contentY);
        ImGui::SetCursorScreenPos(ImVec2(stopPos.x - 11.0f, stopPos.y - 11.0f));
        if (ImGui::InvisibleButton("##BStopAfter", ImVec2(22.0f, 22.0f))) {
            m_stopAfterCurrent = !m_stopAfterCurrent;
            savePreferences();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_stopAfterCurrent ? "Stop After Current Track (Active)" : "Stop After Current Track (V)");
        drawVectorStop(dl, stopPos, 12.0f, ImGui::IsItemHovered(), m_stopAfterCurrent);

        // Repeat
        rightBoundary -= 26.0f;
        ImVec2 repPos(rightBoundary + 10.0f, contentY);
        ImGui::SetCursorScreenPos(ImVec2(repPos.x - 11.0f, repPos.y - 11.0f));
        if (ImGui::InvisibleButton("##BRep", ImVec2(22.0f, 22.0f))) {
            m_audio.cycleRepeatMode();
            m_audio.saveAudioConfig();
        }
        if (ImGui::IsItemHovered()) {
            const char* repTips[] = { "Repeat: Off", "Repeat: All", "Repeat: One" };
            ImGui::SetTooltip("%s", repTips[static_cast<int>(m_audio.getRepeatMode())]);
        }
        drawVectorRepeat(dl, repPos, 15.0f, m_audio.getRepeatMode(), ImGui::IsItemHovered(), ImGui::IsItemActive());

        // Shuffle
        rightBoundary -= 26.0f;
        ImVec2 shufPos(rightBoundary + 10.0f, contentY);
        ImGui::SetCursorScreenPos(ImVec2(shufPos.x - 11.0f, shufPos.y - 11.0f));
        if (ImGui::InvisibleButton("##BShuf", ImVec2(22.0f, 22.0f))) {
            m_audio.toggleShuffle();
            m_audio.saveAudioConfig();
            if (m_audio.isShuffle()) {
                rebuildShuffleOrder(true);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_audio.isShuffle() ? "Shuffle: On" : "Shuffle: Off");
        drawVectorShuffle(dl, shufPos, 15.0f, m_audio.isShuffle(), ImGui::IsItemHovered(), ImGui::IsItemActive());
    }

    // Time Display
    if (totalW >= 420.0f) {
        int curSec = static_cast<int>(curTime);
        int totSec = static_cast<int>(totalDur);
        char timeStr[32];
        if (totalDur > 0.0) {
            if (m_showRemainingTime) {
                int remSec = std::max(0, totSec - curSec);
                snprintf(timeStr, sizeof(timeStr), "%d:%02d  -%d:%02d", curSec / 60, curSec % 60, remSec / 60, remSec % 60);
            } else {
                snprintf(timeStr, sizeof(timeStr), "%d:%02d / %d:%02d", curSec / 60, curSec % 60, totSec / 60, totSec % 60);
            }
        } else {
            snprintf(timeStr, sizeof(timeStr), "0:00 / 0:00");
        }

        ImVec2 timeSize = ImGui::CalcTextSize(timeStr);
        rightBoundary -= (timeSize.x + 14.0f);
        ImVec2 tPos(rightBoundary + 5.0f, contentY - timeSize.y * 0.5f);
        ImGui::SetCursorScreenPos(tPos);
        if (ImGui::InvisibleButton("##TimeRemainToggle", timeSize)) {
            m_showRemainingTime = !m_showRemainingTime;
            savePreferences();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to toggle remaining time display");
        ImU32 tCol = ImGui::IsItemHovered() ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(165, 172, 185, 255);
        dl->AddText(tPos, tCol, timeStr);
    }

    // Audio Output Hardware / DAC Icon
    if (totalW >= 640.0f) {
        bool isExcl = m_audio.isExclusiveActive();
        float iconBtnW = 28.0f;
        float iconBtnH = 22.0f;
        rightBoundary -= (iconBtnW + 8.0f);
        ImVec2 badgePos(rightBoundary + 2.0f, contentY - iconBtnH * 0.5f);
        ImGui::SetCursorScreenPos(badgePos);
        if (ImGui::InvisibleButton("##AudioDriverBadge", ImVec2(iconBtnW, iconBtnH))) {
            ImGui::OpenPopup("QuickOutputDeviceMenu");
        }
        if (ImGui::BeginPopup("QuickOutputDeviceMenu")) {
            ImGui::TextColored(Theme::AccentColor(), "Quick Audio Output Switcher");
            ImGui::Separator();
            auto devList = m_audio.getAudioDevices(m_audio.getAudioDriverType());
            int curIdx = m_audio.getSelectedDeviceIndex();
            std::string curDevName = m_audio.getActiveDeviceName();
            for (size_t di = 0; di < devList.size(); ++di) {
                bool isCur = (curIdx == static_cast<int>(di)) || (curDevName.find(devList[di].name) != std::string::npos);
                std::string icon = (devList[di].name.find("Headphone") != std::string::npos || devList[di].name.find("USB") != std::string::npos) ? "[H] " : "[S] ";
                std::string dLabel = icon + devList[di].name;
                if (devList[di].isDefault) dLabel += " (Windows Default)";
                if (ImGui::MenuItem(dLabel.c_str(), nullptr, isCur)) {
                    m_audio.reinitAudioDevice(m_audio.getAudioDriverType(), static_cast<int>(di), m_audio.getBufferLatencyMs(), devList[di].name);
                    m_audio.saveAudioConfig();
                    if (m_audio.isReleaseDriverWhenPaused() && (!m_audio.isPlaying() || m_audio.isPaused())) {
                        m_audio.releaseDevice();
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Sound Drivers & Hardware Preferences...")) {
                openAudioDriverModal();
            }
            ImGui::EndPopup();
        }
        bool bHov = ImGui::IsItemHovered();
        if (bHov) {
            const char* drvBadge = isExcl ? "WASAPI Exclusive (Bit-Perfect)" : (m_audio.getAudioDriverType() == AudioDriverType::DirectSound ? "DirectSound" : "WASAPI Shared");
            ImGui::SetTooltip("Audio Output Hardware: %s\nDevice: %s\nLatency: %dms\nMode: %s\nClick for Quick Output Switcher\nCtrl+Shift+A for sound preferences",
                drvBadge,
                m_audio.getActiveDeviceName().c_str(),
                m_audio.getBufferLatencyMs(),
                isExcl ? "Bit-Perfect Direct Hardware Control (bypasses Windows mixer)" : "Windows Shared Audio Session");
        }

        ImVec2 center(badgePos.x + iconBtnW * 0.5f, badgePos.y + iconBtnH * 0.5f);
        drawVectorAudioDriver(dl, center, 17.0f, isExcl, bHov);
    }

    // Library Status
    if (m_library.isScanning()) {
        float spinRadius = 6.0f;
        float prog = m_library.getScanProgress();
        char scanStr[48];
        snprintf(scanStr, sizeof(scanStr), "Scanning: %.0f%%", prog * 100.0f);
        ImVec2 scanSize = ImGui::CalcTextSize(scanStr);
        rightBoundary -= (scanSize.x + spinRadius * 2.0f + 18.0f);
        ImVec2 spinCenter(rightBoundary + spinRadius, contentY);
        drawVectorSpinner(dl, spinCenter, spinRadius, 1.4f, ImGui::GetColorU32(Theme::AccentColor()));
        dl->AddText(ImVec2(rightBoundary + spinRadius * 2.0f + 6.0f, contentY - scanSize.y * 0.5f), ImGui::GetColorU32(Theme::AccentColor()), scanStr);
    }

    // --- 3. CENTER TEXT (Title + Artist • Album) ---
    float centerSpace = rightBoundary - leftBoundary - 24.0f;
    if (centerSpace >= 60.0f) {
        dl->PushClipRect(ImVec2(leftBoundary, barPos.y), ImVec2(rightBoundary, barPos.y + height), true);
        if (curr) {
            std::string title = curr->getDisplayTitle();
            std::string subtitle = curr->getDisplayArtist();
            if (!curr->getDisplayAlbum().empty()) {
                subtitle += "  \xE2\x80\xA2  " + curr->getDisplayAlbum();
            }

            ImVec2 titleSz = ImGui::CalcTextSize(title.c_str());
            ImVec2 subSz = ImGui::CalcTextSize(subtitle.c_str());

            // Two-line layout with proper vertical centering
            float totalTextH = titleSz.y + subSz.y + 2.0f;
            float startY = barPos.y + (height - totalTextH) * 0.5f + (m_useWavebar ? 5.0f : 3.0f);

            float subX = leftBoundary + (centerSpace - std::min(subSz.x, centerSpace)) * 0.5f;

            // Title with slight emphasis and animated micro-equalizer
            ImGui::SetWindowFontScale(1.05f);
            ImVec2 scaledTitleSz = ImGui::CalcTextSize(title.c_str());
            float eqOffset = m_audio.isPlaying() ? 18.0f : 0.0f;
            float totalTitleW = scaledTitleSz.x + eqOffset;
            float scaledTitleX = leftBoundary + (centerSpace - std::min(totalTitleW, centerSpace)) * 0.5f;

            if (m_audio.isPlaying()) {
                drawMicroEqualizer(dl, ImVec2(scaledTitleX + 6.0f, startY + scaledTitleSz.y * 0.5f), ImGui::GetColorU32(Theme::AccentColor()), true);
                scaledTitleX += 16.0f;
            }
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.05f,
                        ImVec2(scaledTitleX, startY), IM_COL32(252, 253, 255, 255), title.c_str());
            ImGui::SetWindowFontScale(1.0f);

            dl->AddText(ImVec2(subX, startY + scaledTitleSz.y + 1.0f), IM_COL32(140, 148, 165, 255), subtitle.c_str());
        } else {
            const char* brandText = "SlothPlayer \xE2\x80\x94 Music Manager & Player";
            ImVec2 brandSz = ImGui::CalcTextSize(brandText);
            float brandX = leftBoundary + (centerSpace - brandSz.x) * 0.5f;
            dl->AddText(ImVec2(brandX, contentY - brandSz.y * 0.5f), IM_COL32(120, 125, 138, 255), brandText);
        }
        dl->PopClipRect();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderEqualizerModal() {
    if (!m_showEqualizer) return;

    ImGui::SetNextWindowSize(ImVec2(540.0f, 430.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Graphic DSP Equalizer & Sound Effects", &m_showEqualizer, ImGuiWindowFlags_NoCollapse)) {
        EqualizerDSP& eq = m_audio.getEqualizer();

        bool enabled = eq.isEnabled();
        if (ImGui::Checkbox("Enable Equalizer DSP", &enabled)) {
            eq.setEnabled(enabled);
        }

        ImGui::SameLine(180.0f);

        std::vector<std::string> presets = eq.getPresetNames();
        std::string currentPreset = eq.getCurrentPresetName();

        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::BeginCombo("Preset", currentPreset.c_str())) {
            for (const auto& p : presets) {
                bool isSel = (currentPreset == p);
                if (ImGui::Selectable(p.c_str(), isSel)) {
                    eq.loadPreset(p);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset Flat")) {
            eq.loadPreset("Flat");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Preamp, Stereo Balance & Stereo Widener (SlothPlayer DSP_Pan, DSP_StereoEnhancer & DSP_DownmixMono)
        ImGui::Columns(3, "EQControls", false);
        ImGui::SetColumnWidth(0, 180.0f);
        ImGui::SetColumnWidth(1, 180.0f);

        float preamp = eq.getPreamp();
        ImGui::Text("Preamp:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95.0f);
        if (ImGui::SliderFloat("##Preamp", &preamp, -12.0f, 12.0f, "%+.1f dB")) {
            eq.setPreamp(preamp);
        }

        ImGui::NextColumn();

        float pan = eq.getStereoPan();
        ImGui::Text("Pan:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::SliderFloat("##StereoPan", &pan, -1.0f, 1.0f, pan < -0.05f ? "L %.1f" : (pan > 0.05f ? "R %.1f" : "Center"))) {
            eq.setStereoPan(pan);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("C")) {
            eq.setStereoPan(0.0f);
        }

        ImGui::NextColumn();

        float width = eq.getStereoWidth();
        ImGui::Text("Stereo FX:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        const char* wFmt = "100%";
        if (width < 0.05f) wFmt = "Mono";
        else if (width < 0.95f) wFmt = "Narrow";
        else if (width > 1.05f) wFmt = "Wide";
        if (ImGui::SliderFloat("##StereoWidth", &width, 0.0f, 2.0f, wFmt)) {
            eq.setStereoWidth(width);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("1x")) {
            eq.setStereoWidth(1.0f);
        }

        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 10 Band sliders with real-time dB text
        const char* bandLabels[10] = {
            "31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
        };

        float sliderW = 34.0f;
        float sliderH = 135.0f;

        for (size_t b = 0; b < 10; ++b) {
            if (b > 0) ImGui::SameLine(0.0f, 14.0f);

            ImGui::BeginGroup();
            float gain = eq.getBandGain(b);
            char idBuf[16];
            snprintf(idBuf, sizeof(idBuf), "##b%zu", b);

            char valStr[16];
            snprintf(valStr, sizeof(valStr), "%+.0f", gain);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (sliderW - ImGui::CalcTextSize(valStr).x) * 0.5f);
            ImGui::TextColored(std::abs(gain) > 0.1f ? Theme::AccentColor() : ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "%s", valStr);

            if (ImGui::VSliderFloat(idBuf, ImVec2(sliderW, sliderH), &gain, -12.0f, 12.0f, "")) {
                eq.setBandGain(b, gain);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (sliderW - ImGui::CalcTextSize(bandLabels[b]).x) * 0.5f);
            ImGui::TextDisabled("%s", bandLabels[b]);
            ImGui::EndGroup();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // SlothPlayer .sde Preset Management
        ImGui::Text("Save Current EQ as .sde Preset:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("##SavePresetName", m_savePresetNameBuffer, sizeof(m_savePresetNameBuffer));
        ImGui::SameLine();
        if (ImGui::Button("Save Preset") && m_savePresetNameBuffer[0] != '\0') {
            fs::path saveDir = fs::path(Platform::getAppDataDir()) / "Equaliser";
            fs::create_directories(saveDir);
            fs::path outPath = saveDir / (std::string(m_savePresetNameBuffer) + ".sde");
            eq.saveCurrentPresetToSde(outPath.string(), m_savePresetNameBuffer);
            m_savePresetNameBuffer[0] = '\0';
        }

        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::Button("Import .sde File...")) {
            std::string sdeFile = Platform::openFileDialog("Equalizer Preset (*.sde)", "*.sde");
            if (!sdeFile.empty()) {
                std::string pName = fs::path(sdeFile).stem().string();
                eq.loadSdeFile(sdeFile, pName);
                eq.loadPreset(pName);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(Theme::AccentColor(), "Advanced Audio DSP Processing:");
        ImGui::Columns(3, "DSPCols", false);

        // 1. ReplayGain Preamp
        float replayGain = m_audio.getReplayGainPreamp();
        ImGui::Text("ReplayGain:");
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::SliderFloat("##RGPreamp", &replayGain, -12.0f, 12.0f, "%+.1f dB")) {
            m_audio.setReplayGainPreamp(replayGain);
        }

        ImGui::NextColumn();

        // 2. Crossfade Duration
        float xfade = m_audio.getCrossfadeDuration();
        ImGui::Text("Crossfade:");
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::SliderFloat("##Crossfade", &xfade, 0.0f, 8.0f, xfade < 0.1f ? "Off" : "%.1fs")) {
            m_audio.setCrossfadeDuration(xfade);
        }

        ImGui::NextColumn();

        // 3. Silence Skipping
        bool silenceSkip = m_audio.isSilenceSkipping();
        ImGui::Text("Silence Filter:");
        if (ImGui::Checkbox("Skip Silence", &silenceSkip)) {
            m_audio.setSilenceSkipping(silenceSkip);
        }

        ImGui::Columns(1);
    }
    ImGui::End();
    if (!m_showEqualizer) {
        m_audio.saveAudioConfig();
    }
}

void MainWindow::renderAddFolderModal() {
    if (!m_showAddFolderModal) return;

    ImGui::OpenPopup("Add Music Directory");
    if (ImGui::BeginPopupModal("Add Music Directory", &m_showAddFolderModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter the full path to your music collection:");
        ImGui::InputText("##FolderPath", m_newFolderPathBuffer, sizeof(m_newFolderPathBuffer));

        ImGui::Spacing();
        if (ImGui::Button("Browse...", ImVec2(100.0f, 24.0f))) {
            std::string folder = Platform::openFolderDialog("Select Music Folder");
            if (!folder.empty()) {
                strncpy(m_newFolderPathBuffer, folder.c_str(), sizeof(m_newFolderPathBuffer) - 1);
                m_newFolderPathBuffer[sizeof(m_newFolderPathBuffer) - 1] = '\0';
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Add & Scan", ImVec2(100.0f, 24.0f))) {
            if (m_newFolderPathBuffer[0] != '\0' && fs::exists(m_newFolderPathBuffer)) {
                m_library.addMonitoredFolder(m_newFolderPathBuffer);
                m_library.scanDirectories({m_newFolderPathBuffer});
            }
            m_showAddFolderModal = false;
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f, 24.0f))) {
            m_showAddFolderModal = false;
        }

        ImGui::EndPopup();
    }
}

void MainWindow::renderCreatePlaylistModal() {
    if (!m_showCreatePlaylistModal) return;

    ImGui::OpenPopup("Create New Playlist");
    if (ImGui::BeginPopupModal("Create New Playlist", &m_showCreatePlaylistModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter playlist name:");
        ImGui::InputText("##PlName", m_newPlaylistNameBuffer, sizeof(m_newPlaylistNameBuffer));

        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(100.0f, 24.0f))) {
            if (m_newPlaylistNameBuffer[0] != '\0') {
                m_library.createPlaylist(m_newPlaylistNameBuffer);
                m_newPlaylistNameBuffer[0] = '\0';
            }
            m_showCreatePlaylistModal = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f, 24.0f))) {
            m_showCreatePlaylistModal = false;
        }

        ImGui::EndPopup();
    }
}

void MainWindow::drawVectorHeart(ImDrawList* dl, ImVec2 center, float r, bool loved, bool hovered) {
    ImU32 col = loved ? IM_COL32(255, 55, 95, 255)
                      : (hovered ? IM_COL32(235, 240, 250, 255) : IM_COL32(110, 118, 135, 200));

    ImVec2 bottom(center.x, center.y + r * 0.85f);
    ImVec2 topCenter(center.x, center.y - r * 0.30f);

    // Left half
    dl->PathLineTo(bottom);
    dl->PathBezierCubicCurveTo(
        ImVec2(center.x - r * 1.25f, center.y + r * 0.15f),
        ImVec2(center.x - r * 1.10f, center.y - r * 0.95f),
        ImVec2(center.x - r * 0.38f, center.y - r * 0.95f)
    );
    dl->PathBezierCubicCurveTo(
        ImVec2(center.x - r * 0.12f, center.y - r * 0.70f),
        ImVec2(center.x, center.y - r * 0.45f),
        topCenter
    );

    // Right half
    dl->PathBezierCubicCurveTo(
        ImVec2(center.x, center.y - r * 0.45f),
        ImVec2(center.x + r * 0.12f, center.y - r * 0.70f),
        ImVec2(center.x + r * 0.38f, center.y - r * 0.95f)
    );
    dl->PathBezierCubicCurveTo(
        ImVec2(center.x + r * 1.10f, center.y - r * 0.95f),
        ImVec2(center.x + r * 1.25f, center.y + r * 0.15f),
        bottom
    );

    if (loved) {
        dl->AddCircleFilled(center, r * 0.90f, IM_COL32(255, 55, 95, 35));
        dl->PathFillConvex(col);
    } else {
        dl->PathStroke(col, ImDrawFlags_Closed, 1.4f);
    }
}

void MainWindow::drawVectorDislike(ImDrawList* dl, ImVec2 center, float r, bool disliked, bool hovered) {
    ImU32 col = disliked ? IM_COL32(245, 65, 80, 255)
                         : (hovered ? IM_COL32(235, 240, 250, 255) : IM_COL32(110, 118, 135, 200));

    // Feather-style thumbs-down icon, clean silhouette at small sizes
    float s = r / 10.0f;
    float ox = 11.65f;
    float oy = 12.00f;

    auto pt = [&](float x, float y) -> ImVec2 {
        return ImVec2(center.x + (x - ox) * s, center.y + (y - oy) * s);
    };

    if (disliked) {
        // Subtle ambient glow
        dl->AddCircleFilled(center, r * 1.15f, IM_COL32(245, 65, 80, 35));

        // Palm (convex polygon)
        dl->PathLineTo(pt(17.0f, 2.0f));
        dl->PathLineTo(pt(5.7f, 2.0f));
        dl->PathLineTo(pt(3.7f, 3.7f));
        dl->PathLineTo(pt(2.3f, 12.7f));
        dl->PathLineTo(pt(4.3f, 15.0f));
        dl->PathLineTo(pt(17.0f, 13.0f));
        dl->PathFillConvex(col);

        // Thumb (convex polygon)
        dl->PathLineTo(pt(10.0f, 13.0f));
        dl->PathLineTo(pt(10.0f, 19.0f));
        dl->PathLineTo(pt(11.0f, 21.2f));
        dl->PathLineTo(pt(12.2f, 22.0f));
        dl->PathLineTo(pt(13.2f, 21.8f));
        dl->PathLineTo(pt(17.0f, 13.0f));
        dl->PathFillConvex(col);

        // Wrist (convex polygon)
        dl->PathLineTo(pt(17.0f, 2.0f));
        dl->PathLineTo(pt(21.0f, 2.0f));
        dl->PathLineTo(pt(21.0f, 13.0f));
        dl->PathLineTo(pt(17.0f, 13.0f));
        dl->PathFillConvex(col);

        // Thin separator between wrist and palm
        dl->AddLine(pt(17.0f, 2.5f), pt(17.0f, 12.5f), IM_COL32(20, 22, 28, 255), (std::max)(1.0f, s * 1.2f));
    } else {
        float strokeW = (r >= 6.0f) ? 1.3f : 1.1f;

        // Hand + thumb perimeter
        dl->PathLineTo(pt(17.0f, 2.0f));
        dl->PathLineTo(pt(5.7f, 2.0f));
        dl->PathLineTo(pt(3.7f, 3.7f));
        dl->PathLineTo(pt(2.3f, 12.7f));
        dl->PathLineTo(pt(4.3f, 15.0f));
        dl->PathLineTo(pt(10.0f, 15.0f));
        dl->PathLineTo(pt(10.0f, 19.0f));
        dl->PathLineTo(pt(11.0f, 21.2f));
        dl->PathLineTo(pt(12.2f, 22.0f));
        dl->PathLineTo(pt(13.2f, 21.8f));
        dl->PathLineTo(pt(17.0f, 13.0f));
        dl->PathStroke(col, ImDrawFlags_Closed, strokeW);

        // Wrist perimeter
        dl->PathLineTo(pt(17.0f, 2.0f));
        dl->PathLineTo(pt(21.0f, 2.0f));
        dl->PathLineTo(pt(21.0f, 13.0f));
        dl->PathLineTo(pt(17.0f, 13.0f));
        dl->PathStroke(col, ImDrawFlags_Closed, strokeW);
    }
}

bool MainWindow::drawInteractiveStarRating(ImDrawList* dl, ImVec2 pos, int& rating, uint64_t trackId) {
    float starRadius = 6.0f;
    float starSpacing = 14.0f;
    bool changed = false;
    ImVec2 mousePos = ImGui::GetMousePos();

    // Determine which star (1 to 5) is hovered, if any
    int hoveredStar = 0;
    for (int star = 1; star <= 5; ++star) {
        ImVec2 center(pos.x + (star - 1) * starSpacing + starRadius, pos.y + starRadius);
        float dist = std::hypot(mousePos.x - center.x, mousePos.y - center.y);
        if (dist <= starRadius + 2.5f) {
            hoveredStar = star;
        }
    }

    for (int star = 1; star <= 5; ++star) {
        ImVec2 center(pos.x + (star - 1) * starSpacing + starRadius, pos.y + starRadius);
        bool isPreview = (hoveredStar > 0 && star <= hoveredStar);
        bool isRated = (hoveredStar == 0 && rating >= star);
        bool filled = isPreview || isRated;

        ImU32 col = isPreview ? IM_COL32(245, 205, 80, 255)
                              : (isRated ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(75, 80, 95, 180));

        drawVectorStar(dl, center, starRadius, filled, col);
    }

    if (hoveredStar > 0 && ImGui::IsMouseClicked(0)) {
        if (rating == hoveredStar) {
            rating = 0; // Click same star to clear
        } else {
            rating = hoveredStar;
        }
        if (trackId > 0) {
            m_library.setRating(trackId, rating);
        }
        changed = true;
    }
    return changed;
}

void MainWindow::drawNavVectorIcon(ImDrawList* dl, ImVec2 center, float size, NavVectorIcon icon, unsigned int color) {
    if (!dl) return;
    switch (icon) {
        case NavVectorIcon::AllTracks: {
            float headR = 2.2f;
            ImVec2 h1(center.x - 3.5f, center.y + 3.0f);
            ImVec2 h2(center.x + 3.5f, center.y + 1.2f);
            dl->AddCircleFilled(h1, headR, color);
            dl->AddCircleFilled(h2, headR, color);
            dl->AddLine(ImVec2(h1.x + headR - 0.5f, h1.y), ImVec2(h1.x + headR - 0.5f, center.y - 4.5f), color, 1.4f);
            dl->AddLine(ImVec2(h2.x + headR - 0.5f, h2.y), ImVec2(h2.x + headR - 0.5f, center.y - 6.0f), color, 1.4f);
            dl->AddLine(ImVec2(h1.x + headR - 0.5f, center.y - 4.5f), ImVec2(h2.x + headR - 0.5f, center.y - 6.0f), color, 2.0f);
            break;
        }
        case NavVectorIcon::Favorites: {
            drawVectorHeart(dl, center, size * 0.44f, true, false);
            break;
        }
        case NavVectorIcon::TopRated: {
            drawVectorStar(dl, center, size * 0.52f, true, color);
            break;
        }
        case NavVectorIcon::MostPlayed: {
            ImVec2 pA(center.x - 3.0f, center.y - 4.5f);
            ImVec2 pB(center.x + 4.5f, center.y);
            ImVec2 pC(center.x - 3.0f, center.y + 4.5f);
            dl->AddTriangleFilled(pA, pB, pC, color);
            dl->AddLine(ImVec2(center.x - 5.5f, center.y - 2.5f), ImVec2(center.x - 4.5f, center.y - 2.5f), color, 1.3f);
            dl->AddLine(ImVec2(center.x - 5.5f, center.y + 2.5f), ImVec2(center.x - 4.5f, center.y + 2.5f), color, 1.3f);
            break;
        }
        case NavVectorIcon::RecentlyAdded: {
            dl->AddCircle(center, 5.5f, color, 16, 1.3f);
            dl->AddLine(center, ImVec2(center.x, center.y - 3.2f), color, 1.4f);
            dl->AddLine(center, ImVec2(center.x + 2.6f, center.y), color, 1.4f);
            break;
        }
        case NavVectorIcon::NeverPlayed: {
            dl->AddCircle(center, 5.5f, color, 16, 1.3f);
            dl->AddCircleFilled(center, 1.8f, color, 12);
            break;
        }
        case NavVectorIcon::Disliked: {
            drawVectorDislike(dl, center, size * 0.48f, true, false);
            break;
        }
        case NavVectorIcon::Radio: {
            dl->AddCircleFilled(center, 1.8f, color, 12);
            dl->PathArcTo(center, 4.2f, 2.5f, 3.8f, 6); dl->PathStroke(color, 0, 1.3f);
            dl->PathArcTo(center, 4.2f, -0.65f, 0.65f, 6); dl->PathStroke(color, 0, 1.3f);
            dl->PathArcTo(center, 6.8f, 2.6f, 3.7f, 6); dl->PathStroke(color, 0, 1.3f);
            dl->PathArcTo(center, 6.8f, -0.55f, 0.55f, 6); dl->PathStroke(color, 0, 1.3f);
            break;
        }
        case NavVectorIcon::Podcasts: {
            dl->AddRectFilled(ImVec2(center.x - 2.2f, center.y - 5.5f), ImVec2(center.x + 2.2f, center.y + 1.2f), color, 2.0f);
            dl->PathArcTo(center, 4.2f, 0.0f, 3.14159f, 8); dl->PathStroke(color, 0, 1.3f);
            dl->AddLine(ImVec2(center.x, center.y + 4.2f), ImVec2(center.x, center.y + 6.2f), color, 1.3f);
            dl->AddLine(ImVec2(center.x - 3.2f, center.y + 6.2f), ImVec2(center.x + 3.2f, center.y + 6.2f), color, 1.3f);
            break;
        }
        case NavVectorIcon::History: {
            dl->PathArcTo(center, 5.0f, 0.5f, 5.5f, 12); dl->PathStroke(color, 0, 1.3f);
            dl->AddTriangleFilled(ImVec2(center.x + 1.2f, center.y - 6.5f), ImVec2(center.x + 4.5f, center.y - 4.5f), ImVec2(center.x + 1.2f, center.y - 2.5f), color);
            dl->AddLine(center, ImVec2(center.x, center.y - 2.6f), color, 1.3f);
            dl->AddLine(center, ImVec2(center.x - 2.0f, center.y), color, 1.3f);
            break;
        }
    }
}

bool MainWindow::drawModernNavRow(const char* strId, const char* label, NavVectorIcon icon,
                                  size_t count, bool isSelected, float width, float rowH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + width, p0.y + rowH);

    bool clicked = ImGui::InvisibleButton(strId, ImVec2(width, rowH));
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    ImU32 accentCol = ImGui::GetColorU32(Theme::AccentColor());

    // Row Background
    if (isSelected) {
        ImVec4 acc = Theme::AccentColor();
        ImU32 bgCol = IM_COL32(static_cast<int>(acc.x * 255.0f),
                               static_cast<int>(acc.y * 255.0f),
                               static_cast<int>(acc.z * 255.0f), 38);
        dl->AddRectFilled(p0, p1, bgCol, 6.0f);
        // Left glowing indicator pill
        dl->AddRectFilled(ImVec2(p0.x + 2.0f, p0.y + 4.0f), ImVec2(p0.x + 5.0f, p1.y - 4.0f), accentCol, 2.0f);
    } else if (hovered) {
        dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, active ? 22 : 12), 6.0f);
    }

    // Vector Icon
    ImVec2 iconCenter(p0.x + 16.0f, p0.y + rowH * 0.5f);
    ImU32 iconCol = isSelected ? accentCol : (hovered ? IM_COL32(245, 248, 255, 255) : IM_COL32(138, 148, 164, 230));
    drawNavVectorIcon(dl, iconCenter, 14.0f, icon, iconCol);

    // Text Label
    ImU32 textCol = isSelected ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(235, 240, 248, 255) : IM_COL32(165, 175, 192, 230));
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float textY = p0.y + (rowH - textSize.y) * 0.5f;
    dl->AddText(ImVec2(p0.x + 32.0f, textY), textCol, label);

    // Count Badge
    if (count > 0) {
        std::string cntStr = std::to_string(count);
        ImVec2 cntSize = ImGui::CalcTextSize(cntStr.c_str());
        float badgeW = cntSize.x + 12.0f;
        float badgeH = 16.0f;
        float badgeX = p1.x - badgeW - 6.0f;
        float badgeY = p0.y + (rowH - badgeH) * 0.5f;

        ImU32 badgeBg = isSelected ? IM_COL32(255, 255, 255, 28) : (hovered ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 10));
        ImU32 badgeTxt = isSelected ? IM_COL32(245, 248, 255, 250) : IM_COL32(140, 148, 162, 200);

        dl->AddRectFilled(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH), badgeBg, 8.0f);
        dl->AddText(ImVec2(badgeX + 6.0f, badgeY + (badgeH - cntSize.y) * 0.5f), badgeTxt, cntStr.c_str());
    }

    return clicked;
}

void MainWindow::renderNowPlayingStage(const Track* curr, float width, float height) {
    if (!curr) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 1. Top Sleek Action Bar for Lyrics
    ImGui::Spacing();
    ImVec2 barPos = ImGui::GetCursorScreenPos();
    float cardW = std::max(60.0f, width - 16.0f);
    float barH = 34.0f;
    ImVec2 barP1 = ImVec2(barPos.x + cardW, barPos.y + barH);

    dl->AddRectFilled(barPos, barP1, IM_COL32(18, 21, 27, 200), 8.0f);
    dl->AddRect(barPos, barP1, IM_COL32(32, 38, 48, 180), 8.0f, 0, 1.0f);

    float btnW = 0.0f;
    bool fullBtns = (cardW >= 460.0f);
    bool miniBtns = (!fullBtns && cardW >= 220.0f);
    if (fullBtns) {
        btnW = 230.0f;
    } else if (miniBtns) {
        btnW = 60.0f;
    }

    float textAvailW = cardW - 24.0f - (btnW > 0.0f ? btnW + 12.0f : 0.0f);
    std::string barStatus;
    if (textAvailW >= 240.0f) {
        barStatus = "○ Instrumental / No Synchronized Lyrics";
    } else if (textAvailW >= 160.0f) {
        barStatus = "○ Instrumental / No Lyrics";
    } else if (textAvailW >= 100.0f) {
        barStatus = "○ Instrumental";
    } else {
        barStatus = "○ No Lyrics";
    }
    barStatus = truncateTextToWidth(barStatus, std::max(10.0f, textAvailW));

    dl->PushClipRect(barPos, barP1, true);
    dl->AddText(ImVec2(barPos.x + 12.0f, barPos.y + 9.0f), IM_COL32(140, 154, 178, 230), barStatus.c_str());
    dl->PopClipRect();

    if (fullBtns || miniBtns) {
        float btnsX = barPos.x + cardW - 8.0f - btnW;
        ImGui::SetCursorScreenPos(ImVec2(btnsX, barPos.y + 5.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.17f, 0.22f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.26f, 0.34f, 1.0f));

        if (fullBtns) {
            if (ImGui::SmallButton("📁 Link .LRC File...")) {
                std::string lrcFile = Platform::openFileDialog("Lyrics Files (*.lrc;*.txt)", "*.lrc;*.txt");
                if (!lrcFile.empty()) {
                    m_lyrics.setCustomLyricsFile(curr->id, lrcFile);
                }
            }
            ImGui::SameLine(0.0f, 6.0f);
            if (ImGui::SmallButton("🔍 Search Online")) {
                std::string q = "https://www.google.com/search?q=" + curr->getDisplayArtist() + "+" + curr->getDisplayTitle() + "+lyrics";
                ShellExecuteA(nullptr, "open", q.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else {
            if (ImGui::SmallButton("📁##NP_LRC")) {
                std::string lrcFile = Platform::openFileDialog("Lyrics Files (*.lrc;*.txt)", "*.lrc;*.txt");
                if (!lrcFile.empty()) {
                    m_lyrics.setCustomLyricsFile(curr->id, lrcFile);
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Link .LRC Lyrics File...");

            ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::SmallButton("🔍##NP_SearchLyr")) {
                std::string q = "https://www.google.com/search?q=" + curr->getDisplayArtist() + "+" + curr->getDisplayTitle() + "+lyrics";
                ShellExecuteA(nullptr, "open", q.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search Lyrics Online");
        }
        ImGui::PopStyleColor(2);
    }

    // 2. Real-time Studio Spectrum Visualizer Card (32 Bands)
    float specCardH = 100.0f;
    ImVec2 spCard0 = ImVec2(barPos.x, barPos.y + barH + 10.0f);
    ImVec2 spCard1 = ImVec2(spCard0.x + cardW, spCard0.y + specCardH);

    dl->AddRectFilled(spCard0, spCard1, IM_COL32(14, 16, 22, 220), 10.0f);
    dl->AddRect(spCard0, spCard1, IM_COL32(30, 35, 46, 200), 10.0f, 0, 1.0f);

    std::string specHeader = "LIVE AUDIO SPECTRUM (32-BAND REALTIME)";
    if (cardW < 240.0f) specHeader = "LIVE SPECTRUM";
    else if (cardW < 340.0f) specHeader = "LIVE AUDIO SPECTRUM";
    specHeader = truncateTextToWidth(specHeader, cardW - 28.0f);
    dl->AddText(ImVec2(spCard0.x + 14.0f, spCard0.y + 10.0f), IM_COL32(130, 140, 158, 220), specHeader.c_str());

    std::vector<float> spectrumBars;
    m_audio.getSpectrum(spectrumBars, 32);
    if (!spectrumBars.empty()) {
        float plotX0 = spCard0.x + 14.0f;
        float plotW = cardW - 28.0f;
        float plotH = 50.0f;
        float plotY0 = spCard0.y + 36.0f;
        float plotY1 = plotY0 + plotH;

        size_t numBands = spectrumBars.size();
        if (plotW < 120.0f && numBands > 16) {
            numBands = 16;
        }
        float stepW = plotW / static_cast<float>(numBands);
        float bW = std::max(1.5f, stepW - 2.0f);

        ImVec4 acc = Theme::AccentColor();
        ImU32 bColBottom = IM_COL32(static_cast<int>(acc.x * 255.0f), static_cast<int>(acc.y * 255.0f), static_cast<int>(acc.z * 255.0f), 220);
        ImU32 bColTop = IM_COL32(255, 230, 120, 255);

        dl->PushClipRect(spCard0, spCard1, true);
        for (size_t b = 0; b < numBands; ++b) {
            size_t srcIdx = (numBands == spectrumBars.size()) ? b : (b * spectrumBars.size() / numBands);
            float val = std::clamp(spectrumBars[srcIdx], 0.03f, 1.0f);
            float barH = val * plotH;
            float bx0 = plotX0 + b * stepW;
            float bx1 = bx0 + bW;
            float by0 = plotY1 - barH;
            dl->AddRectFilledMultiColor(ImVec2(bx0, by0), ImVec2(bx1, plotY1), bColTop, bColTop, bColBottom, bColBottom);
        }
        dl->PopClipRect();
    }

    // 3. Studio Audio Specifications Card
    bool isCompactSpecs = (cardW < 400.0f);
    float badgeH = 34.0f;
    int numCols = isCompactSpecs ? 2 : 4;
    float colGap = 8.0f;
    float badgeColW = (cardW - 28.0f - (numCols - 1) * colGap) / static_cast<float>(numCols);
    if (badgeColW < 50.0f) badgeColW = 50.0f;
    float specsH = isCompactSpecs ? (32.0f + 2 * badgeH + 8.0f + 10.0f) : (32.0f + badgeH + 12.0f);
    ImVec2 spec0 = ImVec2(spCard0.x, spCard1.y + 10.0f);
    ImVec2 spec1 = ImVec2(spec0.x + cardW, spec0.y + specsH);

    dl->AddRectFilled(spec0, spec1, IM_COL32(14, 16, 22, 220), 10.0f);
    dl->AddRect(spec0, spec1, IM_COL32(30, 35, 46, 200), 10.0f, 0, 1.0f);

    std::string specHdr = "STUDIO AUDIO SPECIFICATIONS";
    if (cardW < 240.0f) specHdr = "AUDIO SPECS";
    else if (cardW < 340.0f) specHdr = "AUDIO SPECIFICATIONS";
    specHdr = truncateTextToWidth(specHdr, cardW - 28.0f);
    dl->AddText(ImVec2(spec0.x + 14.0f, spec0.y + 10.0f), IM_COL32(130, 140, 158, 220), specHdr.c_str());

    std::string ext = curr->filePath.substr(curr->filePath.find_last_of(".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
    bool isLossless = (ext == "FLAC" || ext == "WAV" || ext == "ALAC" || ext == "DSD" || ext == "AIFF");

    auto drawSpecPill = [&](int idx, const char* title, const char* value, bool highlight) {
        int r = idx / numCols;
        int c = idx % numCols;
        float px0 = spec0.x + 14.0f + c * (badgeColW + colGap);
        float py0 = spec0.y + 32.0f + r * (badgeH + 8.0f);
        float px1 = px0 + badgeColW;
        float py1 = py0 + badgeH;

        dl->AddRectFilled(ImVec2(px0, py0), ImVec2(px1, py1), IM_COL32(20, 24, 32, 240), 6.0f);
        ImU32 borderCol = highlight ? IM_COL32(50, 130, 175, 160) : IM_COL32(34, 40, 50, 180);
        dl->AddRect(ImVec2(px0, py0), ImVec2(px1, py1), borderCol, 6.0f, 0, 1.0f);

        float maxPillTextW = badgeColW - 12.0f;
        std::string dispT = truncateTextToWidth(title, maxPillTextW);
        std::string dispV = truncateTextToWidth(value, maxPillTextW);

        dl->PushClipRect(ImVec2(px0 + 2.0f, py0 + 2.0f), ImVec2(px1 - 2.0f, py1 - 2.0f), true);
        dl->AddText(ImVec2(px0 + 6.0f, py0 + 3.0f), IM_COL32(115, 125, 142, 210), dispT.c_str());
        ImU32 valCol = highlight ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(235, 240, 250, 255);
        dl->AddText(ImVec2(px0 + 6.0f, py0 + 17.0f), valCol, dispV.c_str());
        dl->PopClipRect();
    };

    std::string fmtVal = ext;
    if (badgeColW >= 120.0f && isLossless) fmtVal += " (Lossless)";
    drawSpecPill(0, "FORMAT", fmtVal.c_str(), isLossless);

    char brBuf[32];
    int br = curr->bitrate > 0 ? curr->bitrate : (isLossless ? 1411 : 320);
    if (badgeColW >= 85.0f) {
        snprintf(brBuf, sizeof(brBuf), "%d kbps", br);
    } else {
        snprintf(brBuf, sizeof(brBuf), "%dk", br);
    }
    drawSpecPill(1, "BITRATE", brBuf, curr->bitrate >= 320);

    char srBuf[32];
    float srKhz = (curr->sampleRate > 0 ? curr->sampleRate : 44100) / 1000.0f;
    if (badgeColW >= 80.0f) {
        snprintf(srBuf, sizeof(srBuf), "%.1f kHz", srKhz);
    } else {
        snprintf(srBuf, sizeof(srBuf), "%.0f kHz", srKhz);
    }
    const char* srLabel = (badgeColW >= 100.0f) ? "SAMPLE RATE" : "SAMPLE";
    drawSpecPill(2, srLabel, srBuf, curr->sampleRate >= 48000);

    const char* chLabel = (badgeColW >= 90.0f) ? "CHANNELS" : "CH";
    const char* chStr;
    if (badgeColW >= 100.0f) {
        chStr = (curr->channels == 2 ? "Stereo 2.0" : (curr->channels == 1 ? "Mono" : "Multi-ch"));
    } else {
        chStr = (curr->channels == 2 ? "Stereo" : (curr->channels == 1 ? "Mono" : "Multi"));
    }
    drawSpecPill(3, chLabel, chStr, false);

    // 4. "Up Next in Queue" Card
    float remH = height - (spec1.y - barPos.y) - 16.0f;
    if (remH >= 100.0f) {
        ImVec2 qCard0 = ImVec2(spec0.x, spec1.y + 10.0f);
        ImVec2 qCard1 = ImVec2(qCard0.x + cardW, qCard0.y + remH);

        dl->AddRectFilled(qCard0, qCard1, IM_COL32(14, 16, 22, 220), 10.0f);
        dl->AddRect(qCard0, qCard1, IM_COL32(30, 35, 46, 200), 10.0f, 0, 1.0f);

        std::string qHeader = "UP NEXT IN QUEUE (" + std::to_string(m_queue.size()) + " TRACKS)";
        if (cardW < 240.0f) {
            qHeader = "UP NEXT (" + std::to_string(m_queue.size()) + ")";
        } else if (cardW < 330.0f) {
            qHeader = "UP NEXT IN QUEUE (" + std::to_string(m_queue.size()) + ")";
        }
        qHeader = truncateTextToWidth(qHeader, cardW - 28.0f);
        dl->AddText(ImVec2(qCard0.x + 14.0f, qCard0.y + 10.0f), IM_COL32(130, 140, 158, 220), qHeader.c_str());

        float listY0 = qCard0.y + 32.0f;
        float itemH = 40.0f;
        int maxItems = static_cast<int>((remH - 36.0f) / itemH);
        if (maxItems > 6) maxItems = 6;
        if (maxItems < 1) maxItems = 1;

        size_t nextStart = m_queueIndex + 1;
        int drawn = 0;

        for (size_t qIdx = nextStart; qIdx < m_queue.size() && drawn < maxItems; ++qIdx, ++drawn) {
            uint64_t nextTrId = m_queue[qIdx];
            const Track* nextTr = m_library.getTrackById(nextTrId);
            if (!nextTr) continue;

            float iy0 = listY0 + drawn * itemH;
            float iy1 = iy0 + itemH - 3.0f;
            ImVec2 itP0(qCard0.x + 6.0f, iy0);
            ImVec2 itP1(qCard1.x - 6.0f, iy1);
            float rowW = itP1.x - itP0.x;

            std::string qBtnId = "##NP_UpNext_" + std::to_string(qIdx);
            ImGui::SetCursorScreenPos(itP0);
            if (ImGui::InvisibleButton(qBtnId.c_str(), ImVec2(rowW, iy1 - iy0))) {
                playQueueIndex(qIdx);
            }
            bool itHov = ImGui::IsItemHovered();
            if (itHov) {
                dl->AddRectFilled(itP0, itP1, IM_COL32(255, 255, 255, 14), 6.0f);
            }

            char dBuf[16] = "";
            float durW = 0.0f;
            if (nextTr->duration > 0.0) {
                int sec = static_cast<int>(nextTr->duration);
                snprintf(dBuf, sizeof(dBuf), "%02d:%02d", sec / 60, sec % 60);
                durW = ImGui::CalcTextSize(dBuf).x;
            }

            int tw = 0, th = 0;
            ImTextureID nextArt = m_textures.getTrackArtwork(*nextTr, &tw, &th);
            if (!nextArt) nextArt = m_textures.getDefaultArtwork(&tw, &th);

            float textX0;
            if (rowW >= 240.0f) {
                std::string numStr = "#" + std::to_string(drawn + 1);
                dl->AddText(ImVec2(itP0.x + 6.0f, iy0 + 10.0f), IM_COL32(110, 120, 138, 200), numStr.c_str());
                if (nextArt) {
                    dl->AddImageRounded(nextArt, ImVec2(itP0.x + 28.0f, iy0 + 3.0f), ImVec2(itP0.x + 59.0f, iy0 + 34.0f),
                                       ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
                }
                textX0 = itP0.x + 66.0f;
            } else {
                if (nextArt) {
                    dl->AddImageRounded(nextArt, ImVec2(itP0.x + 4.0f, iy0 + 4.0f), ImVec2(itP0.x + 33.0f, iy0 + 33.0f),
                                       ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
                }
                textX0 = itP0.x + 38.0f;
            }

            float textRight = itP1.x - 6.0f;
            if (durW > 0.0f) {
                float durX = itP1.x - durW - 6.0f;
                dl->AddText(ImVec2(durX, iy0 + 10.0f), IM_COL32(120, 130, 146, 200), dBuf);
                textRight = durX - 8.0f;
            }

            float availTextW = std::max(10.0f, textRight - textX0);
            std::string dispTitle = truncateTextToWidth(nextTr->getDisplayTitle(), availTextW);
            std::string dispArtist = truncateTextToWidth(nextTr->getDisplayArtist(), availTextW);

            dl->PushClipRect(ImVec2(textX0, iy0), ImVec2(textRight, iy1), true);
            dl->AddText(ImVec2(textX0, iy0 + 2.0f), IM_COL32(245, 248, 255, 255), dispTitle.c_str());
            dl->AddText(ImVec2(textX0, iy0 + 19.0f), IM_COL32(140, 150, 168, 220), dispArtist.c_str());
            dl->PopClipRect();
        }

        if (drawn == 0) {
            std::string emptyMsg = truncateTextToWidth("No upcoming tracks in queue", cardW - 28.0f);
            dl->AddText(ImVec2(qCard0.x + 14.0f, listY0 + 10.0f), IM_COL32(110, 120, 135, 200), emptyMsg.c_str());
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(barPos.x, barPos.y + height - 2.0f));
    ImGui::Dummy(ImVec2(width, 2.0f));
}

void MainWindow::openPropertiesForTrack(uint64_t trackId) {
    const Track* t = m_library.getTrackById(trackId);
    if (!t) return;
    m_propTrackId = trackId;
    snprintf(m_propTitle, sizeof(m_propTitle), "%s", t->title.c_str());
    snprintf(m_propArtist, sizeof(m_propArtist), "%s", t->artist.c_str());
    snprintf(m_propAlbum, sizeof(m_propAlbum), "%s", t->album.c_str());
    snprintf(m_propAlbumArtist, sizeof(m_propAlbumArtist), "%s", t->albumArtist.c_str());
    snprintf(m_propComposer, sizeof(m_propComposer), "%s", t->composer.c_str());
    snprintf(m_propComment, sizeof(m_propComment), "%s", t->comment.c_str());
    snprintf(m_propGenre, sizeof(m_propGenre), "%s", t->genre.c_str());
    m_propYear = t->year;
    m_propTrackNumber = t->trackNumber;
    m_propTrackTotal = t->trackTotal;
    m_propDiscNumber = t->discNumber;
    m_propTotalDiscs = t->totalDiscs;
    m_propRating = t->rating;
    m_showTrackProperties = true;
}

void MainWindow::titleCaseText(char* buffer, size_t maxLen) {
    if (!buffer || maxLen == 0) return;
    bool newWord = true;
    for (size_t i = 0; i < maxLen && buffer[i] != '\0'; ++i) {
        char c = buffer[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == '-' || c == '(' || c == '[' || c == '.' || c == '/') {
            newWord = true;
        } else if (newWord) {
            buffer[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            newWord = false;
        } else {
            buffer[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
}

void MainWindow::openArtworkSearch(const std::string& artist, const std::string& album) {
    snprintf(m_artSearchArtist, sizeof(m_artSearchArtist), "%s", artist.c_str());
    snprintf(m_artSearchAlbum, sizeof(m_artSearchAlbum), "%s", album.c_str());
    {
        std::lock_guard<std::mutex> lock(m_artSearchMutex);
        m_artCandidates.clear();
        m_artSearchStatus = "Ready to search iTunes.";
    }
    m_artSearching = false;
    m_showArtworkSearchModal = true;
}

void MainWindow::renderArtworkSearchModal() {
    if (!m_showArtworkSearchModal) return;

    ImGui::SetNextWindowSize(ImVec2(560.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Online Album Artwork Downloader", &m_showArtworkSearchModal)) {
        ImGui::TextColored(Theme::AccentColor(), "SEARCH ONLINE COVER ARTWORK");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Artist:");
        ImGui::InputText("##artArtist", m_artSearchArtist, sizeof(m_artSearchArtist));

        ImGui::Text("Album:");
        ImGui::InputText("##artAlbum", m_artSearchAlbum, sizeof(m_artSearchAlbum));

        ImGui::Spacing();
        if (ImGui::Button("Search iTunes Store", ImVec2(160.0f, 28.0f)) && !m_artSearching.load()) {
            m_artSearching = true;
            m_artSearchStatus = "Searching iTunes API...";
            std::string artistStr = m_artSearchArtist;
            std::string albumStr = m_artSearchAlbum;

            OnlineMetadataFetcher::searchAlbumArtworkAsync(artistStr, albumStr, [this](const std::vector<ArtworkCandidate>& results) {
                std::lock_guard<std::mutex> lock(m_artSearchMutex);
                m_artCandidates = results;
                m_artSearching = false;
                m_artSearchStatus = results.empty() ? "No artwork matches found." : ("Found " + std::to_string(results.size()) + " matches.");
            });
        }

        ImGui::SameLine();
        std::string statusText;
        {
            std::lock_guard<std::mutex> lock(m_artSearchMutex);
            statusText = m_artSearchStatus;
        }
        ImGui::TextDisabled("%s", statusText.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        std::vector<ArtworkCandidate> candidatesCopy;
        {
            std::lock_guard<std::mutex> lock(m_artSearchMutex);
            candidatesCopy = m_artCandidates;
        }

        if (candidatesCopy.empty()) {
            ImGui::TextDisabled("Enter artist and album name above, then click Search.");
        } else {
            ImGui::Text("Select artwork candidate to apply:");
            ImGui::BeginChild("##ArtResultsChild", ImVec2(0.0f, 220.0f), true);

            for (size_t i = 0; i < candidatesCopy.size(); ++i) {
                const auto& cand = candidatesCopy[i];
                ImGui::PushID(static_cast<int>(i));

                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", cand.albumName.c_str());
                ImGui::TextDisabled("Artist: %s | Release: %s", cand.artistName.c_str(), cand.releaseDate.c_str());

                if (ImGui::Button("Download & Apply Artwork (600x600)")) {
                    std::string targetDir;
                    for (const auto& trk : m_library.getTracks()) {
                        if (trk.album == m_artSearchAlbum || trk.artist == m_artSearchArtist) {
                            targetDir = fs::path(trk.filePath).parent_path().string();
                            break;
                        }
                    }
                    if (!targetDir.empty()) {
                        std::string dest = (fs::path(targetDir) / "cover.jpg").string();
                        std::string url = cand.highResUrl.empty() ? cand.previewUrl : cand.highResUrl;
                        std::thread([this, dest, url]() {
                            OnlineMetadataFetcher::downloadAndSaveArtwork(url, dest);
                            std::lock_guard<std::mutex> lock(m_artSearchMutex);
                            m_artSearchStatus = "Saved artwork to " + dest;
                        }).detach();
                    } else {
                        std::lock_guard<std::mutex> lock(m_artSearchMutex);
                        m_artSearchStatus = "No matching track directory found in library.";
                    }
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            ImGui::EndChild();
        }

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(80.0f, 26.0f))) {
            m_showArtworkSearchModal = false;
        }
    }
    ImGui::End();
}

void MainWindow::renderTrackPropertiesModal() {
    if (!m_showTrackProperties) return;

    const Track* t = m_library.getTrackById(m_propTrackId);
    if (!t) {
        m_showTrackProperties = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(620.0f, 540.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Track Properties - Tag Inspector", &m_showTrackProperties)) {
        if (ImGui::BeginTabBar("##TrackPropsTabBar")) {
            if (ImGui::BeginTabItem("Tags & Metadata")) {
                ImGui::Spacing();
                ImGui::TextColored(Theme::AccentColor(), "EDIT METADATA TAGS");
                ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);
                if (ImGui::SmallButton("Auto-Capitalize Tags")) {
                    titleCaseText(m_propTitle, sizeof(m_propTitle));
                    titleCaseText(m_propArtist, sizeof(m_propArtist));
                    titleCaseText(m_propAlbum, sizeof(m_propAlbum));
                    titleCaseText(m_propAlbumArtist, sizeof(m_propAlbumArtist));
                    titleCaseText(m_propComposer, sizeof(m_propComposer));
                }
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Title:");
                ImGui::InputText("##propTitle", m_propTitle, sizeof(m_propTitle));

                ImGui::Columns(2, "propCols1", false);
                ImGui::Text("Artist:");
                ImGui::InputText("##propArtist", m_propArtist, sizeof(m_propArtist));
                ImGui::NextColumn();
                ImGui::Text("Album Artist:");
                ImGui::InputText("##propAlbumArtist", m_propAlbumArtist, sizeof(m_propAlbumArtist));
                ImGui::Columns(1);

                ImGui::Text("Album:");
                ImGui::InputText("##propAlbum", m_propAlbum, sizeof(m_propAlbum));

                ImGui::Columns(3, "propCols2", false);
                ImGui::Text("Year:");
                ImGui::InputInt("##propYear", &m_propYear);
                ImGui::NextColumn();
                ImGui::Text("Track # / Total:");
                ImGui::PushItemWidth(55.0f);
                ImGui::InputInt("##propTrackNum", &m_propTrackNumber, 0);
                ImGui::SameLine();
                ImGui::Text("/");
                ImGui::SameLine();
                ImGui::InputInt("##propTrackTot", &m_propTrackTotal, 0);
                ImGui::PopItemWidth();
                ImGui::NextColumn();
                ImGui::Text("Disc # / Total:");
                ImGui::PushItemWidth(55.0f);
                ImGui::InputInt("##propDiscNum", &m_propDiscNumber, 0);
                ImGui::SameLine();
                ImGui::Text("/");
                ImGui::SameLine();
                ImGui::InputInt("##propDiscTot", &m_propTotalDiscs, 0);
                ImGui::PopItemWidth();
                ImGui::Columns(1);

                ImGui::Columns(2, "propCols3", false);
                ImGui::Text("Genre:");
                ImGui::InputText("##propGenre", m_propGenre, sizeof(m_propGenre));
                ImGui::NextColumn();
                ImGui::Text("Composer:");
                ImGui::InputText("##propComposer", m_propComposer, sizeof(m_propComposer));
                ImGui::Columns(1);

                ImGui::Text("Comment:");
                ImGui::InputText("##propComment", m_propComment, sizeof(m_propComment));

                ImGui::Text("Rating (0 to 5 Stars):");
                ImGui::SliderInt("##propRatingSlider", &m_propRating, 0, 5);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Technical Specs")) {
                ImGui::Spacing();
                ImGui::TextColored(Theme::AccentColor(), "STREAM & ENCODING SPECIFICATIONS");
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Codec: %s", t->codec.empty() ? "PCM / Unknown" : t->codec.c_str());
                ImGui::Text("Duration: %s (%.2f seconds)", t->formatDuration().c_str(), t->duration);
                ImGui::Text("Bitrate: %d kbps", t->bitrate);
                ImGui::Text("Sample Rate: %d Hz", t->sampleRate);
                ImGui::Text("Bits Per Sample: %d-bit", t->bitsPerSample > 0 ? t->bitsPerSample : 16);
                ImGui::Text("Channels: %d (%s)", t->channels, t->channels == 1 ? "Mono" : (t->channels == 2 ? "Stereo" : "Multi-channel"));
                ImGui::Text("File Size: %.2f MB", t->fileSizeBytes / (1024.0 * 1024.0));
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(Theme::AccentColor(), "REPLAYGAIN / VOLUME NORMALIZATION");
                ImGui::Text("Track Gain: %.2f dB  |  Track Peak: %.4f", t->replayGainTrackGain, t->replayGainTrackPeak);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextWrapped("File Path: %s", t->filePath.c_str());

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Raw Tag Inspector")) {
                ImGui::Spacing();
                ImGui::TextColored(Theme::AccentColor(), "RAW METADATA FRAMES & TAG INSPECTOR");
                ImGui::Separator();
                ImGui::Spacing();

                if (t->rawTags.empty()) {
                    ImGui::TextDisabled("No raw metadata tags extracted from audio file.");
                } else {
                    if (ImGui::BeginTable("RawTagTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 260.0f))) {
                        ImGui::TableSetupColumn("Tag Key", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        for (const auto& [tagKey, tagVal] : t->rawTags) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", tagKey.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextWrapped("%s", tagVal.c_str());
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Album Artwork")) {
                ImGui::Spacing();
                ImGui::TextColored(Theme::AccentColor(), "ALBUM ARTWORK");
                ImGui::Separator();
                ImGui::Spacing();

                int artW = 0, artH = 0;
                ImTextureID artTex = m_textures.getTrackArtwork(*t, &artW, &artH);
                if (artTex) {
                    ImGui::Image(artTex, ImVec2(120.0f, 120.0f));
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::Text("Resolution: %dx%d px", artW, artH);
                    if (t->hasEmbeddedArt) ImGui::TextDisabled("(Embedded in audio file)");
                    else if (!t->albumArtPath.empty()) ImGui::TextDisabled("External: %s", t->albumArtPath.c_str());
                    ImGui::Spacing();
                    if (ImGui::Button("Search Artwork Online...")) {
                        openArtworkSearch(t->artist, t->album);
                    }
                    ImGui::EndGroup();
                } else {
                    ImGui::TextDisabled("No album artwork found for this track.");
                    ImGui::Spacing();
                    if (ImGui::Button("Search Artwork Online...")) {
                        openArtworkSearch(t->artist, t->album);
                    }
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save Changes", ImVec2(120.0f, 28.0f))) {
            m_library.updateTrackMetadata(
                m_propTrackId,
                m_propTitle,
                m_propArtist,
                m_propAlbum,
                m_propYear,
                m_propGenre,
                m_propTrackNumber,
                m_propAlbumArtist,
                m_propComposer,
                m_propComment,
                m_propDiscNumber,
                m_propTotalDiscs,
                m_propTrackTotal
            );
            m_library.setRating(m_propTrackId, m_propRating);
            m_showTrackProperties = false;
        }
        ImGui::SameLine(0.0f, 12.0f);
        if (ImGui::Button("Cancel", ImVec2(80.0f, 28.0f))) {
            m_showTrackProperties = false;
        }
    }
    ImGui::End();
}

void MainWindow::renderAboutModal() {
    if (!m_showAboutModal) return;

    ImGui::SetNextWindowSize(ImVec2(380.0f, 220.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("About SlothPlayer", &m_showAboutModal)) {
        ImGui::TextColored(Theme::AccentColor(), "SlothPlayer - Desktop Audio Player");
        ImGui::Separator();
        ImGui::Text("A high-performance desktop music player and manager.");
        ImGui::Text("Built with DirectX 11, Dear ImGui, and miniaudio.");
        ImGui::Spacing();
        ImGui::BulletText("Low-latency WASAPI audio engine");
        ImGui::BulletText("10-Band Graphic DSP Equalizer");
        ImGui::BulletText("Interactive Album Covers Grid with A-Z jumpbar");
        ImGui::BulletText("Left Album Thumbnail Browser");
        ImGui::BulletText("Ultra-sleek 32px bottom transport bar");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(80.0f, 24.0f))) {
            m_showAboutModal = false;
        }
    }
    ImGui::End();
}

// ----------------- Mini Player (MusicBee Widget Modes) -----------------

// Custom sleek vector UI widgets for MusicBee-quality mini player
static bool drawMiniPlayPauseBtn(const char* id, ImVec2 size, bool isPlaying) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 center(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    float radius = (std::min)(size.x, size.y) * 0.46f;

    ImVec4 acc = Theme::AccentColor();
    ImU32 circleBg = ImGui::GetColorU32(acc);
    if (hov) {
        ImVec4 glow = acc;
        glow.w = 0.35f;
        dl->AddCircleFilled(center, radius + 2.5f, ImGui::GetColorU32(glow));
    }
    dl->AddCircleFilled(center, radius, circleBg);

    // High-contrast dark glyph inside circular button
    ImU32 glyphCol = IM_COL32(14, 16, 20, 255);
    if (!isPlaying) {
        float halfW = radius * 0.42f;
        float halfH = radius * 0.52f;
        float nudge = radius * 0.09f;
        ImVec2 p1(center.x - halfW + nudge, center.y - halfH);
        ImVec2 p2(center.x + halfW + nudge, center.y);
        ImVec2 p3(center.x - halfW + nudge, center.y + halfH);
        dl->AddTriangleFilled(p1, p2, p3, glyphCol);
    } else {
        float barW = radius * 0.22f;
        float barH = radius * 0.52f;
        float gap = radius * 0.14f;
        dl->AddRectFilled(ImVec2(center.x - gap - barW, center.y - barH), ImVec2(center.x - gap, center.y + barH), glyphCol, 1.0f);
        dl->AddRectFilled(ImVec2(center.x + gap, center.y - barH), ImVec2(center.x + gap + barW, center.y + barH), glyphCol, 1.0f);
    }
    return pressed;
}

static bool drawMiniPrevNextBtn(MainWindow* win, const char* id, ImVec2 size, bool isNext) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 center(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    float glyphSize = (std::min)(size.x, size.y) * 0.85f;
    win->drawVectorPrevNext(dl, center, glyphSize, isNext, hov, act);
    return pressed;
}

static bool drawMiniPinBtn(const char* id, ImVec2 size, bool pinned) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);

    if (hov) {
        dl->AddCircleFilled(c, size.x * 0.45f, IM_COL32(255, 255, 255, 18));
    }
    ImU32 col = pinned ? ImGui::GetColorU32(Theme::AccentColor())
                       : (hov ? IM_COL32(240, 245, 255, 255) : IM_COL32(140, 148, 162, 220));

    // Vector Pushpin icon
    dl->AddLine(ImVec2(c.x - 4.5f, c.y - 4.5f), ImVec2(c.x + 3.0f, c.y - 4.5f), col, 1.6f);
    dl->AddRectFilled(ImVec2(c.x - 2.5f, c.y - 4.0f), ImVec2(c.x + 1.0f, c.y + 0.5f), col, 0.5f);
    dl->AddLine(ImVec2(c.x - 4.0f, c.y + 0.5f), ImVec2(c.x + 2.5f, c.y + 0.5f), col, 1.4f);
    dl->AddLine(ImVec2(c.x - 0.5f, c.y + 1.0f), ImVec2(c.x - 0.5f, c.y + 6.0f), col, 1.3f);

    if (pinned) {
        ImVec4 glow = Theme::AccentColor();
        glow.w = 0.35f;
        dl->AddCircleFilled(c, size.x * 0.40f, ImGui::GetColorU32(glow));
    }
    return pressed;
}

static bool drawMiniExpandBtn(const char* id, ImVec2 size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);

    if (hov) {
        dl->AddCircleFilled(c, size.x * 0.45f, IM_COL32(255, 255, 255, 22));
    }
    ImU32 col = hov ? IM_COL32(255, 255, 255, 255) : IM_COL32(145, 152, 168, 220);

    float s = 3.8f;
    dl->AddLine(ImVec2(c.x + s - 3.5f, c.y - s), ImVec2(c.x + s, c.y - s), col, 1.4f);
    dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x + s, c.y - s + 3.5f), col, 1.4f);
    dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x + 0.5f, c.y - 0.5f), col, 1.3f);

    dl->AddLine(ImVec2(c.x - s + 3.5f, c.y + s), ImVec2(c.x - s, c.y + s), col, 1.4f);
    dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x - s, c.y + s - 3.5f), col, 1.4f);
    dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x - 0.5f, c.y + 0.5f), col, 1.3f);
    return pressed;
}

static bool drawMiniCloseBtn(const char* id, ImVec2 size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);

    if (hov) {
        dl->AddCircleFilled(c, size.x * 0.45f, IM_COL32(230, 50, 65, 45));
    }
    ImU32 col = hov ? IM_COL32(255, 80, 95, 255) : IM_COL32(145, 152, 168, 220);

    float s = 3.5f;
    dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, 1.5f);
    dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - s, c.y + s), col, 1.5f);
    return pressed;
}

static bool drawMiniDropdownBtn(const char* id, ImVec2 size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);

    if (hov) {
        dl->AddCircleFilled(c, size.x * 0.45f, IM_COL32(255, 255, 255, 18));
    }
    ImU32 col = hov ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(150, 158, 172, 220);

    float w = 3.6f;
    float h = 2.4f;
    ImVec2 p1(c.x - w, c.y - h * 0.5f);
    ImVec2 p2(c.x + w, c.y - h * 0.5f);
    ImVec2 p3(c.x, c.y + h);
    dl->AddTriangleFilled(p1, p2, p3, col);
    return pressed;
}

static bool drawMiniStopBtn(MainWindow* win, const char* id, ImVec2 size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    win->drawVectorStop(dl, c, size.y * 0.70f, hov, act);
    return pressed;
}

static bool drawMiniShuffleBtn(MainWindow* win, const char* id, ImVec2 size, bool active) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    win->drawVectorShuffle(dl, c, size.y * 0.75f, active, hov, act);
    return pressed;
}

static bool drawMiniRepeatBtn(MainWindow* win, const char* id, ImVec2 size, RepeatMode mode) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    win->drawVectorRepeat(dl, c, size.y * 0.75f, mode, hov, act);
    return pressed;
}

static bool drawMiniHeartBtn(MainWindow* win, const char* id, ImVec2 size, bool loved) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    win->drawVectorHeart(dl, c, size.y * 0.38f, loved, hov);
    return pressed;
}

static bool drawMiniDislikeBtn(MainWindow* win, const char* id, ImVec2 size, bool disliked) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    win->drawVectorDislike(dl, c, size.y * 0.38f, disliked, hov);
    return pressed;
}

static bool drawMiniSpeakerBtn(MainWindow* win, const char* id, ImVec2 size, float volume, bool muted) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    win->drawVectorSpeaker(dl, c, size.y * 0.75f, volume, muted, hov);
    return pressed;
}

static bool drawSleekProgressBar(const char* id, float width, float barHeight, double currentTime, double totalDuration, double* outSeekTime) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 totalSize(width, (std::max)(barHeight + 8.0f, 15.0f));
    bool pressed = ImGui::InvisibleButton(id, totalSize);
    (void)pressed;
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float frac = (totalDuration > 0.0) ? std::clamp(static_cast<float>(currentTime / totalDuration), 0.0f, 1.0f) : 0.0f;
    float centerY = p.y + totalSize.y * 0.5f;
    float trackH = (hov || act) ? barHeight + 1.2f : barHeight;

    // Track Background
    ImVec2 trackMin(p.x, centerY - trackH * 0.5f);
    ImVec2 trackMax(p.x + width, centerY + trackH * 0.5f);
    dl->AddRectFilled(trackMin, trackMax, IM_COL32(44, 48, 58, 255), trackH * 0.5f);

    // Filled Track
    float fillW = width * frac;
    if (fillW > 0.5f) {
        ImVec2 fillMax(p.x + fillW, centerY + trackH * 0.5f);
        dl->AddRectFilled(trackMin, fillMax, ImGui::GetColorU32(Theme::AccentColor()), trackH * 0.5f);
    }

    // Seeking on click or drag
    bool isSeeking = false;
    if (act || (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
        float mouseX = ImGui::GetIO().MousePos.x;
        float newFrac = std::clamp((mouseX - p.x) / width, 0.0f, 1.0f);
        if (outSeekTime && totalDuration > 0.0) {
            *outSeekTime = newFrac * totalDuration;
            isSeeking = true;
        }
        frac = newFrac;
    }

    // Glowing Thumb Knob on hover or drag
    if (hov || act) {
        float knobX = p.x + width * frac;
        ImVec2 knobCenter(knobX, centerY);
        float knobR = (act ? 5.2f : 4.2f);

        ImVec4 glow = Theme::AccentColor();
        glow.w = 0.35f;
        dl->AddCircleFilled(knobCenter, knobR + 3.0f, ImGui::GetColorU32(glow));
        dl->AddCircleFilled(knobCenter, knobR, ImGui::GetColorU32(Theme::AccentColor()));
        dl->AddCircle(knobCenter, knobR, IM_COL32(255, 255, 255, 230), 16, 1.2f);

        float hoverFrac = std::clamp((ImGui::GetIO().MousePos.x - p.x) / width, 0.0f, 1.0f);
        double hoverTime = hoverFrac * totalDuration;
        int hm = static_cast<int>(hoverTime) / 60;
        int hs = static_cast<int>(hoverTime) % 60;
        char ttBuf[32];
        snprintf(ttBuf, sizeof(ttBuf), "%02d:%02d", hm, hs);
        ImGui::SetTooltip("%s", ttBuf);
    }
    return isSeeking;
}

static bool drawMiniVolumeSlider(const char* id, float width, float barHeight, float currentVol, float* outVol) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 totalSize(width, (std::max)(barHeight + 8.0f, 14.0f));
    bool pressed = ImGui::InvisibleButton(id, totalSize);
    (void)pressed;
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float frac = std::clamp(currentVol, 0.0f, 1.0f);
    float centerY = p.y + totalSize.y * 0.5f;
    float trackH = (hov || act) ? barHeight + 1.0f : barHeight;

    ImVec2 trackMin(p.x, centerY - trackH * 0.5f);
    ImVec2 trackMax(p.x + width, centerY + trackH * 0.5f);
    dl->AddRectFilled(trackMin, trackMax, IM_COL32(44, 48, 58, 255), trackH * 0.5f);

    float fillW = width * frac;
    if (fillW > 0.5f) {
        ImVec2 fillMax(p.x + fillW, centerY + trackH * 0.5f);
        dl->AddRectFilled(trackMin, fillMax, ImGui::GetColorU32(Theme::AccentColor()), trackH * 0.5f);
    }

    if (act || (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
        float mouseX = ImGui::GetIO().MousePos.x;
        float newFrac = std::clamp((mouseX - p.x) / width, 0.0f, 1.0f);
        if (outVol) *outVol = newFrac;
    }

    if (hov || act) {
        float knobX = p.x + width * frac;
        ImVec2 knobCenter(knobX, centerY);
        float knobR = 3.5f;
        dl->AddCircleFilled(knobCenter, knobR, IM_COL32(245, 248, 255, 255));
        ImGui::SetTooltip("Volume: %d%%", static_cast<int>(frac * 100.0f));
    }
    return act;
}

enum class PiPButtonType {
    PrevTrack,
    Rewind5s,
    Stop,
    PlayPause,
    Forward5s,
    NextTrack,
    Expand
};

static bool drawPiPTileBtn(const char* id, ImVec2 size, PiPButtonType type, bool isPlaying, const char* tooltip) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);

    float rounding = 5.0f;
    ImVec2 bMin = p;
    ImVec2 bMax(p.x + size.x, p.y + size.y);

    bool isAccent = (type == PiPButtonType::Expand);
    ImU32 bgCol;
    ImU32 borderCol;
    ImU32 glyphCol = IM_COL32(235, 242, 252, 255);

    if (isAccent) {
        if (act) {
            bgCol = IM_COL32(21, 101, 192, 255);
            borderCol = IM_COL32(100, 181, 246, 255);
        } else if (hov) {
            bgCol = IM_COL32(33, 150, 243, 255);
            borderCol = IM_COL32(144, 202, 249, 255);
        } else {
            bgCol = IM_COL32(25, 118, 210, 240);
            borderCol = IM_COL32(66, 165, 245, 200);
        }
    } else {
        if (act) {
            bgCol = IM_COL32(50, 64, 80, 255);
            borderCol = IM_COL32(75, 95, 120, 255);
        } else if (hov) {
            bgCol = IM_COL32(38, 48, 62, 255);
            borderCol = IM_COL32(62, 80, 102, 255);
        } else {
            bgCol = IM_COL32(28, 34, 44, 230);
            borderCol = IM_COL32(46, 56, 72, 190);
        }
    }

    dl->AddRectFilled(bMin, bMax, bgCol, rounding);
    dl->AddRect(bMin, bMax, borderCol, rounding, 0, 1.0f);

    if (tooltip && hov) {
        ImGui::SetTooltip("%s", tooltip);
    }

    switch (type) {
        case PiPButtonType::PrevTrack: {
            // |<<
            dl->AddRectFilled(ImVec2(c.x - 6.5f, c.y - 4.5f), ImVec2(c.x - 5.0f, c.y + 4.5f), glyphCol, 0.5f);
            dl->AddTriangleFilled(ImVec2(c.x - 5.0f, c.y), ImVec2(c.x - 0.5f, c.y - 4.5f), ImVec2(c.x - 0.5f, c.y + 4.5f), glyphCol);
            dl->AddTriangleFilled(ImVec2(c.x - 0.5f, c.y), ImVec2(c.x + 4.0f, c.y - 4.5f), ImVec2(c.x + 4.0f, c.y + 4.5f), glyphCol);
            break;
        }
        case PiPButtonType::Rewind5s: {
            // <<
            dl->AddTriangleFilled(ImVec2(c.x - 5.5f, c.y), ImVec2(c.x - 0.5f, c.y - 4.5f), ImVec2(c.x - 0.5f, c.y + 4.5f), glyphCol);
            dl->AddTriangleFilled(ImVec2(c.x - 0.5f, c.y), ImVec2(c.x + 4.5f, c.y - 4.5f), ImVec2(c.x + 4.5f, c.y + 4.5f), glyphCol);
            break;
        }
        case PiPButtonType::Stop: {
            // Square stop
            dl->AddRectFilled(ImVec2(c.x - 4.5f, c.y - 4.5f), ImVec2(c.x + 4.5f, c.y + 4.5f), glyphCol, 1.0f);
            break;
        }
        case PiPButtonType::PlayPause: {
            if (!isPlaying) {
                // Triangle pointing right
                dl->AddTriangleFilled(ImVec2(c.x - 4.0f, c.y - 5.5f), ImVec2(c.x + 5.5f, c.y), ImVec2(c.x - 4.0f, c.y + 5.5f), glyphCol);
            } else {
                // Two vertical pause bars
                dl->AddRectFilled(ImVec2(c.x - 4.5f, c.y - 5.0f), ImVec2(c.x - 1.5f, c.y + 5.0f), glyphCol, 0.8f);
                dl->AddRectFilled(ImVec2(c.x + 1.5f, c.y - 5.0f), ImVec2(c.x + 4.5f, c.y + 5.0f), glyphCol, 0.8f);
            }
            break;
        }
        case PiPButtonType::Forward5s: {
            // >>
            dl->AddTriangleFilled(ImVec2(c.x - 4.5f, c.y - 4.5f), ImVec2(c.x + 0.5f, c.y), ImVec2(c.x - 4.5f, c.y + 4.5f), glyphCol);
            dl->AddTriangleFilled(ImVec2(c.x - 0.5f, c.y - 4.5f), ImVec2(c.x + 4.5f, c.y), ImVec2(c.x - 0.5f, c.y + 4.5f), glyphCol);
            break;
        }
        case PiPButtonType::NextTrack: {
            // >>|
            dl->AddTriangleFilled(ImVec2(c.x - 4.5f, c.y - 4.5f), ImVec2(c.x + 0.5f, c.y), ImVec2(c.x - 4.5f, c.y + 4.5f), glyphCol);
            dl->AddTriangleFilled(ImVec2(c.x - 0.5f, c.y - 4.5f), ImVec2(c.x + 4.5f, c.y), ImVec2(c.x - 0.5f, c.y + 4.5f), glyphCol);
            dl->AddRectFilled(ImVec2(c.x + 4.5f, c.y - 4.5f), ImVec2(c.x + 6.0f, c.y + 4.5f), glyphCol, 0.5f);
            break;
        }
        case PiPButtonType::Expand: {
            // ⤢ Diagonal Expand Corner Arrows
            float s = 4.2f;
            dl->AddLine(ImVec2(c.x + s - 3.5f, c.y - s), ImVec2(c.x + s, c.y - s), glyphCol, 1.6f);
            dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x + s, c.y - s + 3.5f), glyphCol, 1.6f);
            dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x + 1.0f, c.y - 1.0f), glyphCol, 1.5f);
            dl->AddLine(ImVec2(c.x - s + 3.5f, c.y + s), ImVec2(c.x - s, c.y + s), glyphCol, 1.6f);
            dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x - s, c.y + s - 3.5f), glyphCol, 1.6f);
            dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x - 1.0f, c.y + 1.0f), glyphCol, 1.5f);
            break;
        }
    }
    return pressed;
}

void MainWindow::applyMiniPlayerGeometry() {
    int w = 520;
    int h = 175;
    switch (m_miniPlayerMode) {
        case MiniPlayerMode::Compact:
            w = 520;
            h = (m_miniPlayerShowQueue || m_miniPlayerShowLyrics) ? 350 : 175;
            break;
        case MiniPlayerMode::AlbumArt:
            w = 280;
            h = 350;
            break;
        case MiniPlayerMode::Taskbar:
            w = 680;
            h = 60;
            break;
        case MiniPlayerMode::MinimalHUD:
            w = 400;
            h = 74;
            break;
        case MiniPlayerMode::Lyrics:
            w = 360;
            h = 420;
            break;
        case MiniPlayerMode::PiP:
            w = 340;
            h = 265;
            break;
    }
    Platform::resizeNativeWindow(m_hWnd, w, h, m_miniPlayerAlwaysOnTop);
    Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
}

void MainWindow::setMiniPlayerMode(MiniPlayerMode mode) {
    m_miniPlayerMode = mode;
    savePreferences();
    if (m_isMiniPlayer) {
        applyMiniPlayerGeometry();
    }
}

void MainWindow::openMiniPlayerInMode(MiniPlayerMode mode) {
    m_miniPlayerMode = mode;
    savePreferences();
    if (!m_isMiniPlayer) {
        toggleMiniPlayer();
    } else {
        applyMiniPlayerGeometry();
    }
}

void MainWindow::toggleMiniPlayer() {
    m_isMiniPlayer = !m_isMiniPlayer;
    if (m_isMiniPlayer) {
        int w = 520, h = 175;
        switch (m_miniPlayerMode) {
            case MiniPlayerMode::Compact:
                w = 520;
                h = (m_miniPlayerShowQueue || m_miniPlayerShowLyrics) ? 350 : 175;
                break;
            case MiniPlayerMode::AlbumArt:
                w = 280;
                h = 350;
                break;
            case MiniPlayerMode::Taskbar:
                w = 680;
                h = 60;
                break;
            case MiniPlayerMode::MinimalHUD:
                w = 400;
                h = 74;
                break;
            case MiniPlayerMode::Lyrics:
                w = 360;
                h = 420;
                break;
            case MiniPlayerMode::PiP:
                w = 340;
                h = 265;
                break;
        }
        Platform::setMiniPlayerWindow(m_hWnd, true, w, h, m_miniPlayerAlwaysOnTop);
        Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
    } else {
        Platform::setWindowOpacity(m_hWnd, 1.0f);
        Platform::setMiniPlayerWindow(m_hWnd, false);
    }
}

void MainWindow::renderMiniPlayerHeader(const char* modeTitle) {
    float availW = ImGui::GetContentRegionAvail().x;

    // Mode Selector on Left
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::Text("%s", modeTitle);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 4.0f);

    if (drawMiniDropdownBtn("##hdrModeDrop", ImVec2(14.0f, 16.0f))) {
        ImGui::OpenPopup("MiniPlayerModePopup");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch Mini Player Widget Mode");

    if (ImGui::BeginPopup("MiniPlayerModePopup")) {
        ImGui::TextColored(Theme::AccentColor(), "Widget Modes (MusicBee Style)");
        ImGui::Separator();
        if (ImGui::MenuItem("Compact Player (Classic)", nullptr, m_miniPlayerMode == MiniPlayerMode::Compact)) {
            setMiniPlayerMode(MiniPlayerMode::Compact);
        }
        if (ImGui::MenuItem("Album Art / Vinyl Card", nullptr, m_miniPlayerMode == MiniPlayerMode::AlbumArt)) {
            setMiniPlayerMode(MiniPlayerMode::AlbumArt);
        }
        if (ImGui::MenuItem("DeskBand / Taskbar Strip", nullptr, m_miniPlayerMode == MiniPlayerMode::Taskbar)) {
            setMiniPlayerMode(MiniPlayerMode::Taskbar);
        }
        if (ImGui::MenuItem("Minimalist HUD Ticker", nullptr, m_miniPlayerMode == MiniPlayerMode::MinimalHUD)) {
            setMiniPlayerMode(MiniPlayerMode::MinimalHUD);
        }
        if (ImGui::MenuItem("Floating Synced Lyrics", nullptr, m_miniPlayerMode == MiniPlayerMode::Lyrics)) {
            setMiniPlayerMode(MiniPlayerMode::Lyrics);
        }
        if (ImGui::MenuItem("Picture-in-Picture / Video Card", nullptr, m_miniPlayerMode == MiniPlayerMode::PiP)) {
            setMiniPlayerMode(MiniPlayerMode::PiP);
        }
        ImGui::EndPopup();
    }

    // Right-aligned cluster
    float rightClusterW = 100.0f;
    if (m_miniPlayerMode == MiniPlayerMode::AlbumArt) rightClusterW += 64.0f;
    if (availW > rightClusterW + 60.0f) {
        ImGui::SameLine(availW - rightClusterW);
    } else {
        ImGui::SameLine();
    }

    if (m_miniPlayerMode == MiniPlayerMode::AlbumArt) {
        const char* styleLabel = m_miniPlayerVinylStyle ? "[Vinyl]" : "[Art]";
        if (ImGui::SmallButton(styleLabel)) {
            m_miniPlayerVinylStyle = !m_miniPlayerVinylStyle;
            savePreferences();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle between Album Cover and Spinning Vinyl Record");
        ImGui::SameLine(0.0f, 4.0f);
    }

    // Pin Button (Vector Pushpin)
    if (drawMiniPinBtn("##hdrPin", ImVec2(18.0f, 18.0f), m_miniPlayerAlwaysOnTop)) {
        m_miniPlayerAlwaysOnTop = !m_miniPlayerAlwaysOnTop;
        Platform::setWindowAlwaysOnTop(m_hWnd, m_miniPlayerAlwaysOnTop);
        savePreferences();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_miniPlayerAlwaysOnTop ? "Always On Top: ON (Click to unpin)" : "Always On Top: OFF (Click to pin)");

    ImGui::SameLine(0.0f, 4.0f);

    // Opacity Selector Button
    char opBuf[16];
    snprintf(opBuf, sizeof(opBuf), "%d%%", static_cast<int>(m_miniPlayerOpacity * 100.0f));
    if (ImGui::SmallButton(opBuf)) {
        ImGui::OpenPopup("MiniPlayerOpacityPopup");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Window Opacity / Transparency");

    if (ImGui::BeginPopup("MiniPlayerOpacityPopup")) {
        ImGui::TextDisabled("Opacity");
        if (ImGui::MenuItem("100% (Solid)", nullptr, m_miniPlayerOpacity >= 0.99f)) {
            m_miniPlayerOpacity = 1.0f;
            Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
            savePreferences();
        }
        if (ImGui::MenuItem("90%", nullptr, std::abs(m_miniPlayerOpacity - 0.90f) < 0.05f)) {
            m_miniPlayerOpacity = 0.90f;
            Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
            savePreferences();
        }
        if (ImGui::MenuItem("80%", nullptr, std::abs(m_miniPlayerOpacity - 0.80f) < 0.05f)) {
            m_miniPlayerOpacity = 0.80f;
            Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
            savePreferences();
        }
        if (ImGui::MenuItem("70%", nullptr, std::abs(m_miniPlayerOpacity - 0.70f) < 0.05f)) {
            m_miniPlayerOpacity = 0.70f;
            Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
            savePreferences();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, 4.0f);

    // Expand Button (Vector Diagonal Corner Arrows)
    if (drawMiniExpandBtn("##hdrExp", ImVec2(18.0f, 18.0f))) {
        toggleMiniPlayer();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restore Full Player (Ctrl+Shift+M)");

    ImGui::SameLine(0.0f, 4.0f);

    // Close Button (Vector Cross)
    if (drawMiniCloseBtn("##hdrClose", ImVec2(18.0f, 18.0f))) {
        toggleMiniPlayer();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Exit Mini Player");
}

void MainWindow::renderMiniPlayerContextMenu() {
    if (ImGui::BeginPopup("MiniPlayerContextMenu")) {
        ImGui::TextColored(Theme::AccentColor(), "SlothPlayer Mini");
        ImGui::Separator();

        if (ImGui::BeginMenu("Widget Mode")) {
            if (ImGui::MenuItem("Compact Player (Classic)", nullptr, m_miniPlayerMode == MiniPlayerMode::Compact)) {
                setMiniPlayerMode(MiniPlayerMode::Compact);
            }
            if (ImGui::MenuItem("Album Art / Vinyl Card", nullptr, m_miniPlayerMode == MiniPlayerMode::AlbumArt)) {
                setMiniPlayerMode(MiniPlayerMode::AlbumArt);
            }
            if (ImGui::MenuItem("DeskBand / Taskbar Strip", nullptr, m_miniPlayerMode == MiniPlayerMode::Taskbar)) {
                setMiniPlayerMode(MiniPlayerMode::Taskbar);
            }
            if (ImGui::MenuItem("Minimalist HUD Ticker", nullptr, m_miniPlayerMode == MiniPlayerMode::MinimalHUD)) {
                setMiniPlayerMode(MiniPlayerMode::MinimalHUD);
            }
            if (ImGui::MenuItem("Floating Synced Lyrics", nullptr, m_miniPlayerMode == MiniPlayerMode::Lyrics)) {
                setMiniPlayerMode(MiniPlayerMode::Lyrics);
            }
            if (ImGui::MenuItem("Picture-in-Picture / Video Card", nullptr, m_miniPlayerMode == MiniPlayerMode::PiP)) {
                setMiniPlayerMode(MiniPlayerMode::PiP);
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Always on Top", nullptr, m_miniPlayerAlwaysOnTop)) {
            m_miniPlayerAlwaysOnTop = !m_miniPlayerAlwaysOnTop;
            Platform::setWindowAlwaysOnTop(m_hWnd, m_miniPlayerAlwaysOnTop);
            savePreferences();
        }

        if (ImGui::BeginMenu("Window Opacity")) {
            if (ImGui::MenuItem("100% (Solid)", nullptr, m_miniPlayerOpacity >= 0.99f)) {
                m_miniPlayerOpacity = 1.0f;
                Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
                savePreferences();
            }
            if (ImGui::MenuItem("90%", nullptr, std::abs(m_miniPlayerOpacity - 0.90f) < 0.05f)) {
                m_miniPlayerOpacity = 0.90f;
                Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
                savePreferences();
            }
            if (ImGui::MenuItem("80%", nullptr, std::abs(m_miniPlayerOpacity - 0.80f) < 0.05f)) {
                m_miniPlayerOpacity = 0.80f;
                Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
                savePreferences();
            }
            if (ImGui::MenuItem("70%", nullptr, std::abs(m_miniPlayerOpacity - 0.70f) < 0.05f)) {
                m_miniPlayerOpacity = 0.70f;
                Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
                savePreferences();
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        const char* playPauseTxt = m_audio.isPlaying() ? "Pause" : "Play";
        if (ImGui::MenuItem(playPauseTxt, "Space")) togglePlayPause();
        if (ImGui::MenuItem("Next Track", "Ctrl+Right")) playNext();
        if (ImGui::MenuItem("Previous Track", "Ctrl+Left")) playPrevious();

        bool shuf = m_audio.isShuffle();
        if (ImGui::MenuItem("Shuffle", nullptr, shuf)) {
            m_audio.setShuffle(!shuf);
            if (!shuf) rebuildShuffleOrder();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Restore Full Player", "Ctrl+Shift+M")) toggleMiniPlayer();
        if (ImGui::MenuItem("Exit Application", "Alt+F4")) Platform::requestQuit();

        ImGui::EndPopup();
    }
}

// Mode 1: Compact Player (Classic MusicBee Compact Player)
void MainWindow::renderCompactMiniPlayer() {
    renderMiniPlayerHeader("Compact");
    ImGui::Separator();

    const Track* curr = m_library.getTrackById(m_currentTrackId);
    float availW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float artDim = 110.0f;
    ImTextureID art = 0;
    if (curr) art = m_textures.getTrackArtwork(*curr);
    if (!art) art = m_textures.getDefaultArtwork();

    ImVec2 artPos = ImGui::GetCursorScreenPos();
    if (art) {
        dl->AddImageRounded(art, artPos, ImVec2(artPos.x + artDim, artPos.y + artDim), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
        dl->AddRect(artPos, ImVec2(artPos.x + artDim, artPos.y + artDim), IM_COL32(50, 56, 70, 200), 4.0f, 0, 1.0f);
    } else {
        dl->AddRectFilled(artPos, ImVec2(artPos.x + artDim, artPos.y + artDim), IM_COL32(24, 28, 36, 255), 4.0f);
        dl->AddRect(artPos, ImVec2(artPos.x + artDim, artPos.y + artDim), IM_COL32(50, 56, 70, 200), 4.0f, 0, 1.0f);
    }
    ImGui::Dummy(ImVec2(artDim, artDim));

    // Hover overlay with circular play/pause
    if (ImGui::IsItemHovered()) {
        dl->AddRectFilled(artPos, ImVec2(artPos.x + artDim, artPos.y + artDim), IM_COL32(0, 0, 0, 95), 4.0f);
        ImVec2 center(artPos.x + artDim * 0.5f, artPos.y + artDim * 0.5f);
        dl->AddCircleFilled(center, 18.0f, IM_COL32(20, 24, 30, 230));
        dl->AddCircle(center, 18.0f, IM_COL32(255, 255, 255, 180), 0, 1.5f);
        if (m_audio.isPlaying()) {
            dl->AddRectFilled(ImVec2(center.x - 5.0f, center.y - 6.0f), ImVec2(center.x - 2.0f, center.y + 6.0f), IM_COL32(255, 255, 255, 255));
            dl->AddRectFilled(ImVec2(center.x + 2.0f, center.y - 6.0f), ImVec2(center.x + 5.0f, center.y + 6.0f), IM_COL32(255, 255, 255, 255));
        } else {
            ImVec2 t1(center.x - 4.0f, center.y - 6.0f);
            ImVec2 t2(center.x - 4.0f, center.y + 6.0f);
            ImVec2 t3(center.x + 6.0f, center.y);
            dl->AddTriangleFilled(t1, t2, t3, IM_COL32(255, 255, 255, 255));
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            togglePlayPause();
        }
        ImGui::SetTooltip("Click to Play / Pause");
    }

    ImGui::SameLine(0.0f, 10.0f);

    // Right Content Area
    ImGui::BeginGroup();
    float rightContentW = availW - artDim - 14.0f;

    // Line 1: Title
    std::string titleStr = curr ? curr->getDisplayTitle() : "No track playing";
    if (titleStr.length() > 34) titleStr = titleStr.substr(0, 32) + "..";
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::Text("%s", titleStr.c_str());
    ImGui::PopStyleColor();

    // Line 2: Artist & Album
    std::string metaStr = curr ? (curr->getDisplayArtist() + " \xE2\x80\x94 " + curr->getDisplayAlbum()) : "Select a track to play";
    if (metaStr.length() > 38) metaStr = metaStr.substr(0, 36) + "..";
    ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.85f, 1.0f), "%s", metaStr.c_str());

    // Line 3: Vector Heart + Dislike + 5-Star Rating + Badges
    if (curr) {
        if (drawMiniHeartBtn(this, "##cmpFav", ImVec2(18.0f, 16.0f), curr->isFavorite)) {
            m_library.toggleFavorite(curr->id);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(curr->isFavorite ? "Remove from Favorites" : "Mark as Favorite");

        ImGui::SameLine(0.0f, 4.0f);
        if (drawMiniDislikeBtn(this, "##cmpDis", ImVec2(18.0f, 16.0f), curr->isDisliked)) {
            m_library.toggleDislike(curr->id);
            const Track* trk = m_library.getTrackById(curr->id);
            if (trk && trk->isDisliked) {
                if (m_audio.isShuffle()) rebuildShuffleOrder(false);
                playNext();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(curr->isDisliked ? "Remove Dislike" : "Dislike Song (Auto-skip)");

        ImGui::SameLine(0.0f, 8.0f);
        int curRating = curr->rating;
        ImVec2 starPos = ImGui::GetCursorScreenPos();
        drawInteractiveStarRating(dl, ImVec2(starPos.x, starPos.y - 1.0f), curRating, curr->id);
        ImGui::Dummy(ImVec2(76.0f, 16.0f));

        if (m_library.getAutoDJConfig().enabled) {
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextColored(Theme::AccentColor(), "[Auto-DJ]");
        }
        if (m_sleepTimer.active) {
            ImGui::SameLine(0.0f, 6.0f);
            auto now = std::chrono::steady_clock::now();
            int remSec = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(m_sleepTimer.targetTime - now).count());
            if (remSec < 0) remSec = 0;
            char sleepBuf[16];
            snprintf(sleepBuf, sizeof(sleepBuf), "Zzz %02d:%02d", remSec / 60, remSec % 60);
            ImGui::TextColored(Theme::AccentColor(), "%s", sleepBuf);
        }
    } else {
        ImGui::TextDisabled("Idle");
    }

    // Line 4: Sleek Seek Scrubber & Time
    double curTime = m_audio.getCurrentTime();
    double totalDur = m_audio.getTotalDuration();
    if (curr && totalDur <= 0.0) totalDur = curr->duration;

    double outSeekTime = 0.0;
    float seekW = rightContentW - 90.0f;
    if (seekW < 120.0f) seekW = 120.0f;
    if (drawSleekProgressBar("##cmpSeek", seekW, 3.2f, curTime, totalDur, &outSeekTime)) {
        m_audio.seekTo(outSeekTime);
    }
    ImGui::SameLine(0.0f, 6.0f);
    int curM = static_cast<int>(curTime) / 60;
    int curS = static_cast<int>(curTime) % 60;
    int totM = static_cast<int>(totalDur) / 60;
    int totS = static_cast<int>(totalDur) % 60;
    ImGui::TextDisabled("%02d:%02d/%02d:%02d", curM, curS, totM, totS);

    // Line 5: Compact Vector Transport Controls
    bool shuf = m_audio.isShuffle();
    if (drawMiniShuffleBtn(this, "##cmpShuf", ImVec2(20.0f, 20.0f), shuf)) {
        m_audio.setShuffle(!shuf);
        if (!shuf) rebuildShuffleOrder();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(shuf ? "Shuffle: ON" : "Shuffle: OFF");

    ImGui::SameLine(0.0f, 4.0f);
    if (drawMiniPrevNextBtn(this, "##cmpPrev", ImVec2(24.0f, 20.0f), false)) playPrevious();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous Track (Ctrl+Left)");

    ImGui::SameLine(0.0f, 4.0f);
    if (drawMiniPlayPauseBtn("##cmpPlay", ImVec2(26.0f, 22.0f), m_audio.isPlaying())) togglePlayPause();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / Pause (Space)");

    ImGui::SameLine(0.0f, 4.0f);
    if (drawMiniPrevNextBtn(this, "##cmpNext", ImVec2(24.0f, 20.0f), true)) playNext();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next Track (Ctrl+Right)");

    ImGui::SameLine(0.0f, 4.0f);
    if (drawMiniStopBtn(this, "##cmpStop", ImVec2(20.0f, 20.0f))) stop();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop");

    ImGui::SameLine(0.0f, 4.0f);
    RepeatMode rep = m_audio.getRepeatMode();
    if (drawMiniRepeatBtn(this, "##cmpRep", ImVec2(20.0f, 20.0f), rep)) {
        int nextRep = (static_cast<int>(rep) + 1) % 3;
        m_audio.setRepeatMode(static_cast<RepeatMode>(nextRep));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(rep == RepeatMode::Off ? "Repeat: OFF" : (rep == RepeatMode::All ? "Repeat: ALL" : "Repeat: ONE"));

    ImGui::SameLine(0.0f, 6.0f);
    float vol = m_audio.getVolume();
    bool muted = m_audio.isMuted();
    if (drawMiniSpeakerBtn(this, "##cmpSpk", ImVec2(18.0f, 18.0f), vol, muted)) {
        m_audio.setMuted(!muted);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(muted ? "Unmute" : "Mute");

    ImGui::SameLine(0.0f, 4.0f);
    float newVol = vol;
    if (drawMiniVolumeSlider("##cmpVol", 46.0f, 3.0f, vol, &newVol)) {
        m_audio.setVolume(newVol);
        if (m_audio.isMuted()) m_audio.setMuted(false);
    }

    // Right Tabs: [Queue] and [Lyrics]
    ImGui::SameLine(0.0f, 8.0f);
    std::string qBtn = "Queue (" + std::to_string(m_queue.size()) + ")";
    if (m_miniPlayerShowQueue) ImGui::PushStyleColor(ImGuiCol_Button, Theme::AccentColor());
    if (ImGui::SmallButton(qBtn.c_str())) {
        m_miniPlayerShowQueue = !m_miniPlayerShowQueue;
        if (m_miniPlayerShowQueue) m_miniPlayerShowLyrics = false;
        applyMiniPlayerGeometry();
        savePreferences();
    }
    if (m_miniPlayerShowQueue) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Upcoming Queue Drawer");

    ImGui::SameLine(0.0f, 4.0f);
    if (m_miniPlayerShowLyrics) ImGui::PushStyleColor(ImGuiCol_Button, Theme::AccentColor());
    if (ImGui::SmallButton("Lyrics")) {
        m_miniPlayerShowLyrics = !m_miniPlayerShowLyrics;
        if (m_miniPlayerShowLyrics) m_miniPlayerShowQueue = false;
        applyMiniPlayerGeometry();
        savePreferences();
    }
    if (m_miniPlayerShowLyrics) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Real-Time Lyrics Drawer");

    ImGui::EndGroup();

    // Drawer Content Area (when Queue or Lyrics is toggled open)
    if (m_miniPlayerShowQueue) {
        ImGui::Separator();
        float drawerH = ImGui::GetContentRegionAvail().y - 2.0f;
        if (drawerH < 60.0f) drawerH = 140.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::BeginChild("MiniQueueScroll", ImVec2(0.0f, drawerH), true, ImGuiWindowFlags_None);
        if (m_queue.empty()) {
            ImGui::TextDisabled("Queue is currently empty.");
        } else {
            for (size_t i = 0; i < m_queue.size(); ++i) {
                const Track* trk = m_library.getTrackById(m_queue[i]);
                if (!trk) continue;

                bool isCurrent = (i == m_queueIndex);
                if (isCurrent) {
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
                }

                char rowBuf[256];
                int durM = static_cast<int>(trk->duration) / 60;
                int durS = static_cast<int>(trk->duration) % 60;
                snprintf(rowBuf, sizeof(rowBuf), "%zu. %s %s - %s (%02d:%02d)###qRow_%zu",
                         i + 1, isCurrent ? "[PLAYING]" : "",
                         trk->getDisplayArtist().c_str(), trk->getDisplayTitle().c_str(),
                         durM, durS, i);

                if (ImGui::Selectable(rowBuf, isCurrent)) {
                    playQueueIndex(i);
                }
                if (isCurrent) ImGui::PopStyleColor();

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Click to jump to this track");
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else if (m_miniPlayerShowLyrics) {
        ImGui::Separator();
        float drawerH = ImGui::GetContentRegionAvail().y - 2.0f;
        if (drawerH < 60.0f) drawerH = 140.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::BeginChild("MiniLyricsScroll", ImVec2(0.0f, drawerH), true, ImGuiWindowFlags_None);
        if (!curr) {
            ImGui::TextDisabled("No track playing.");
        } else {
            const LyricsData& lyrics = getCachedLyrics(*curr);
            if (lyrics.hasLyrics && lyrics.isSynced) {
                double currentPos = m_audio.getCurrentTime();
                int activeIdx = LyricsManager::findActiveLineIndex(lyrics.lines, currentPos);

                static int lastActiveIdx = -1;
                bool needScroll = (activeIdx != lastActiveIdx);
                lastActiveIdx = activeIdx;

                for (int i = 0; i < static_cast<int>(lyrics.lines.size()); ++i) {
                    const auto& line = lyrics.lines[i];
                    bool isActive = (i == activeIdx);
                    if (isActive) {
                        if (needScroll) ImGui::SetScrollHereY(0.40f);
                        ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
                    } else if (i < activeIdx) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.55f, 1.0f));
                    }

                    std::string lineStr = (isActive ? "> " : "  ") + (line.text.empty() ? "..." : line.text) + "###lyr_" + std::to_string(i);
                    if (ImGui::Selectable(lineStr.c_str(), isActive)) {
                        m_audio.seekTo(line.timeSeconds);
                    }
                    ImGui::PopStyleColor();
                }
            } else if (lyrics.hasLyrics) {
                ImGui::TextWrapped("%s", lyrics.plainText.c_str());
            } else {
                ImGui::TextDisabled("No lyrics found for this track.");
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
}

// Mode 2: Album Art / Vinyl Disc Widget (MusicBee Cover Art / Vinyl Mode)
void MainWindow::renderAlbumArtMiniPlayer() {
    renderMiniPlayerHeader(m_miniPlayerVinylStyle ? "Vinyl Disc" : "Album Art");
    ImGui::Separator();

    const Track* curr = m_library.getTrackById(m_currentTrackId);
    float availW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImTextureID art = 0;
    if (curr) art = m_textures.getTrackArtwork(*curr);
    if (!art) art = m_textures.getDefaultArtwork();

    float centerAreaDim = 190.0f;
    float posX = (availW - centerAreaDim) * 0.5f;
    if (posX < 0.0f) posX = 0.0f;

    ImGui::SetCursorPosX(posX);
    ImVec2 centerPos = ImGui::GetCursorScreenPos();

    if (!m_miniPlayerVinylStyle) {
        // --- Album Cover Card Style ---
        if (art) {
            dl->AddImageRounded(art, centerPos, ImVec2(centerPos.x + centerAreaDim, centerPos.y + centerAreaDim), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
            dl->AddRect(centerPos, ImVec2(centerPos.x + centerAreaDim, centerPos.y + centerAreaDim), IM_COL32(50, 56, 70, 200), 6.0f, 0, 1.0f);
        } else {
            dl->AddRectFilled(centerPos, ImVec2(centerPos.x + centerAreaDim, centerPos.y + centerAreaDim), IM_COL32(24, 28, 36, 255), 6.0f);
            dl->AddRect(centerPos, ImVec2(centerPos.x + centerAreaDim, centerPos.y + centerAreaDim), IM_COL32(50, 56, 70, 200), 6.0f, 0, 1.0f);
        }
        ImGui::Dummy(ImVec2(centerAreaDim, centerAreaDim));

        // Hover overlay on cover
        if (ImGui::IsItemHovered()) {
            dl->AddRectFilled(centerPos, ImVec2(centerPos.x + centerAreaDim, centerPos.y + centerAreaDim), IM_COL32(0, 0, 0, 100), 6.0f);
            ImVec2 c(centerPos.x + centerAreaDim * 0.5f, centerPos.y + centerAreaDim * 0.5f);
            dl->AddCircleFilled(c, 24.0f, IM_COL32(18, 20, 26, 230));
            dl->AddCircle(c, 24.0f, IM_COL32(255, 255, 255, 180), 0, 1.5f);
            if (m_audio.isPlaying()) {
                dl->AddRectFilled(ImVec2(c.x - 6.0f, c.y - 8.0f), ImVec2(c.x - 2.0f, c.y + 8.0f), IM_COL32(255, 255, 255, 255));
                dl->AddRectFilled(ImVec2(c.x + 2.0f, c.y - 8.0f), ImVec2(c.x + 6.0f, c.y + 8.0f), IM_COL32(255, 255, 255, 255));
            } else {
                dl->AddTriangleFilled(ImVec2(c.x - 5.0f, c.y - 8.0f), ImVec2(c.x - 5.0f, c.y + 8.0f), ImVec2(c.x + 8.0f, c.y), IM_COL32(255, 255, 255, 255));
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                togglePlayPause();
            }
            ImGui::SetTooltip("Click to Play / Pause");
        }
    } else {
        // --- Spinning Vinyl Disc Style ---
        if (m_audio.isPlaying()) {
            m_vinylRotation += ImGui::GetIO().DeltaTime * 1.8f;
        }

        ImVec2 discCenter(centerPos.x + centerAreaDim * 0.5f, centerPos.y + centerAreaDim * 0.5f);
        float discRadius = centerAreaDim * 0.48f;

        // Outer black vinyl body
        dl->AddCircleFilled(discCenter, discRadius, IM_COL32(16, 17, 20, 255), 64);
        dl->AddCircle(discCenter, discRadius, IM_COL32(40, 42, 50, 255), 64, 1.5f);

        // Concentric vinyl grooves
        for (float r = discRadius * 0.46f; r < discRadius * 0.96f; r += 4.5f) {
            dl->AddCircle(discCenter, r, IM_COL32(50, 52, 60, 40), 64, 1.0f);
        }

        // Dynamic sheen highlights rotating with m_vinylRotation
        float angle1 = m_vinylRotation;
        float angle2 = m_vinylRotation + 3.14159f;
        ImVec2 s1(discCenter.x + cosf(angle1) * (discRadius * 0.85f), discCenter.y + sinf(angle1) * (discRadius * 0.85f));
        ImVec2 s2(discCenter.x + cosf(angle2) * (discRadius * 0.85f), discCenter.y + sinf(angle2) * (discRadius * 0.85f));
        dl->AddLine(discCenter, s1, IM_COL32(255, 255, 255, 22), 2.5f);
        dl->AddLine(discCenter, s2, IM_COL32(255, 255, 255, 22), 2.5f);

        // Center circular label with cropped album art
        float labelRadius = discRadius * 0.40f;
        if (art) {
            ImVec2 lp0(discCenter.x - labelRadius, discCenter.y - labelRadius);
            ImVec2 lp1(discCenter.x + labelRadius, discCenter.y + labelRadius);
            dl->AddImageRounded(art, lp0, lp1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, labelRadius);
        } else {
            dl->AddCircleFilled(discCenter, labelRadius, IM_COL32(35, 40, 50, 255), 32);
        }
        dl->AddCircle(discCenter, labelRadius, IM_COL32(180, 185, 200, 160), 32, 1.2f);

        // Center spindle hole with silver grommet
        dl->AddCircleFilled(discCenter, 6.0f, IM_COL32(10, 10, 12, 255), 16);
        dl->AddCircle(discCenter, 6.0f, IM_COL32(220, 225, 235, 255), 16, 1.5f);

        ImGui::Dummy(ImVec2(centerAreaDim, centerAreaDim));
        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                togglePlayPause();
            }
            ImGui::SetTooltip("Spinning Vinyl Record (Click to Play/Pause)");
        }
    }

    // Info & Floating Controls
    ImGui::Spacing();
    std::string titleStr = curr ? curr->getDisplayTitle() : "No track playing";
    if (titleStr.length() > 28) titleStr = titleStr.substr(0, 26) + "..";
    ImVec2 tSz = ImGui::CalcTextSize(titleStr.c_str());
    ImGui::SetCursorPosX((availW - tSz.x) * 0.5f > 0.0f ? (availW - tSz.x) * 0.5f : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::Text("%s", titleStr.c_str());
    ImGui::PopStyleColor();

    std::string artistStr = curr ? (curr->getDisplayArtist() + " \xE2\x80\x94 " + curr->getDisplayAlbum()) : "Select a track to play";
    if (artistStr.length() > 32) artistStr = artistStr.substr(0, 30) + "..";
    ImVec2 aSz = ImGui::CalcTextSize(artistStr.c_str());
    ImGui::SetCursorPosX((availW - aSz.x) * 0.5f > 0.0f ? (availW - aSz.x) * 0.5f : 0.0f);
    ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.85f, 1.0f), "%s", artistStr.c_str());

    // Scrubber
    double curTime = m_audio.getCurrentTime();
    double totalDur = m_audio.getTotalDuration();
    if (curr && totalDur <= 0.0) totalDur = curr->duration;

    double outSeekTime = 0.0;
    float seekW = availW - 75.0f;
    if (seekW < 100.0f) seekW = 100.0f;
    if (drawSleekProgressBar("##artSeek", seekW, 3.2f, curTime, totalDur, &outSeekTime)) {
        m_audio.seekTo(outSeekTime);
    }
    ImGui::SameLine(0.0f, 6.0f);
    int curM = static_cast<int>(curTime) / 60;
    int curS = static_cast<int>(curTime) % 60;
    int totM = static_cast<int>(totalDur) / 60;
    int totS = static_cast<int>(totalDur) % 60;
    ImGui::TextDisabled("%02d:%02d/%02d:%02d", curM, curS, totM, totS);

    // Centered Transport Row
    float rowW = 200.0f;
    ImGui::SetCursorPosX((availW - rowW) * 0.5f > 0.0f ? (availW - rowW) * 0.5f : 0.0f);

    if (curr) {
        if (drawMiniHeartBtn(this, "##artFav", ImVec2(20.0f, 20.0f), curr->isFavorite)) {
            m_library.toggleFavorite(curr->id);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(curr->isFavorite ? "Remove from Favorites" : "Mark as Favorite");
        ImGui::SameLine(0.0f, 4.0f);
    }

    if (drawMiniPrevNextBtn(this, "##artPrev", ImVec2(24.0f, 22.0f), false)) playPrevious();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous Track (Ctrl+Left)");
    ImGui::SameLine(0.0f, 4.0f);

    if (drawMiniPlayPauseBtn("##artPlay", ImVec2(28.0f, 24.0f), m_audio.isPlaying())) togglePlayPause();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / Pause (Space)");
    ImGui::SameLine(0.0f, 4.0f);

    if (drawMiniPrevNextBtn(this, "##artNext", ImVec2(24.0f, 22.0f), true)) playNext();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next Track (Ctrl+Right)");
    ImGui::SameLine(0.0f, 4.0f);

    if (curr) {
        if (drawMiniDislikeBtn(this, "##artDis", ImVec2(20.0f, 20.0f), curr->isDisliked)) {
            m_library.toggleDislike(curr->id);
            const Track* trk = m_library.getTrackById(curr->id);
            if (trk && trk->isDisliked) {
                if (m_audio.isShuffle()) rebuildShuffleOrder(false);
                playNext();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dislike Track (Auto-skip)");
        ImGui::SameLine(0.0f, 6.0f);
    }

    // Mini volume
    float vol = m_audio.getVolume();
    float newVol = vol;
    if (drawMiniVolumeSlider("##artVol", 36.0f, 3.0f, vol, &newVol)) {
        m_audio.setVolume(newVol);
        if (m_audio.isMuted()) m_audio.setMuted(false);
    }
}

// Mode 3: DeskBand / Ribbon Strip Player (MusicBee Taskbar Strip)
void MainWindow::renderTaskbarMiniPlayer() {
    const Track* curr = m_library.getTrackById(m_currentTrackId);
    float availW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 1. Far Left: 36x36 album thumbnail
    float thumbDim = 36.0f;
    ImTextureID art = 0;
    if (curr) art = m_textures.getTrackArtwork(*curr);
    if (!art) art = m_textures.getDefaultArtwork();

    ImVec2 artPos = ImGui::GetCursorScreenPos();
    if (art) {
        dl->AddImageRounded(art, artPos, ImVec2(artPos.x + thumbDim, artPos.y + thumbDim), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 3.0f);
        dl->AddRect(artPos, ImVec2(artPos.x + thumbDim, artPos.y + thumbDim), IM_COL32(50, 56, 70, 200), 3.0f, 0, 1.0f);
    } else {
        dl->AddRectFilled(artPos, ImVec2(artPos.x + thumbDim, artPos.y + thumbDim), IM_COL32(24, 28, 36, 255), 3.0f);
        dl->AddRect(artPos, ImVec2(artPos.x + thumbDim, artPos.y + thumbDim), IM_COL32(50, 56, 70, 200), 3.0f, 0, 1.0f);
    }
    ImGui::Dummy(ImVec2(thumbDim, thumbDim));

    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) togglePlayPause();
        ImGui::SetTooltip("Click to Play / Pause");
    }

    ImGui::SameLine(0.0f, 6.0f);

    // 2. Mode dropdown icon
    if (drawMiniDropdownBtn("##tskMode", ImVec2(14.0f, 20.0f))) {
        ImGui::OpenPopup("TaskbarModePopup");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch Widget Mode");

    if (ImGui::BeginPopup("TaskbarModePopup")) {
        ImGui::TextColored(Theme::AccentColor(), "Widget Modes");
        ImGui::Separator();
        if (ImGui::MenuItem("Compact Player (Classic)", nullptr, m_miniPlayerMode == MiniPlayerMode::Compact)) setMiniPlayerMode(MiniPlayerMode::Compact);
        if (ImGui::MenuItem("Album Art / Vinyl Card", nullptr, m_miniPlayerMode == MiniPlayerMode::AlbumArt)) setMiniPlayerMode(MiniPlayerMode::AlbumArt);
        if (ImGui::MenuItem("DeskBand / Taskbar Strip", nullptr, m_miniPlayerMode == MiniPlayerMode::Taskbar)) setMiniPlayerMode(MiniPlayerMode::Taskbar);
        if (ImGui::MenuItem("Minimalist HUD Ticker", nullptr, m_miniPlayerMode == MiniPlayerMode::MinimalHUD)) setMiniPlayerMode(MiniPlayerMode::MinimalHUD);
        if (ImGui::MenuItem("Floating Synced Lyrics", nullptr, m_miniPlayerMode == MiniPlayerMode::Lyrics)) setMiniPlayerMode(MiniPlayerMode::Lyrics);
        if (ImGui::MenuItem("Picture-in-Picture / Video Card", nullptr, m_miniPlayerMode == MiniPlayerMode::PiP)) setMiniPlayerMode(MiniPlayerMode::PiP);
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, 6.0f);

    // 3. Track Title & Artist (Compact stacked block)
    ImGui::BeginGroup();
    std::string titleStr = curr ? curr->getDisplayTitle() : "No track playing";
    if (titleStr.length() > 20) titleStr = titleStr.substr(0, 18) + "..";
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::Text("%s", titleStr.c_str());
    ImGui::PopStyleColor();

    std::string artistStr = curr ? (curr->getDisplayArtist() + " - " + curr->getDisplayAlbum()) : "SlothPlayer";
    if (artistStr.length() > 24) artistStr = artistStr.substr(0, 22) + "..";
    ImGui::TextColored(ImVec4(0.70f, 0.74f, 0.82f, 1.0f), "%s", artistStr.c_str());
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 8.0f);

    // 4. Vector Transport Controls
    if (drawMiniPrevNextBtn(this, "##tskPrev", ImVec2(20.0f, 20.0f), false)) playPrevious();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous (Ctrl+Left)");

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniPlayPauseBtn("##tskPlay", ImVec2(24.0f, 22.0f), m_audio.isPlaying())) togglePlayPause();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / Pause (Space)");

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniPrevNextBtn(this, "##tskNext", ImVec2(20.0f, 20.0f), true)) playNext();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next (Ctrl+Right)");

    ImGui::SameLine(0.0f, 6.0f);

    // 5. Scrubber & Time
    double curTime = m_audio.getCurrentTime();
    double totalDur = m_audio.getTotalDuration();
    if (curr && totalDur <= 0.0) totalDur = curr->duration;

    double outSeekTime = 0.0;
    if (drawSleekProgressBar("##tskSeek", 95.0f, 3.0f, curTime, totalDur, &outSeekTime)) {
        m_audio.seekTo(outSeekTime);
    }
    ImGui::SameLine(0.0f, 5.0f);
    int curM = static_cast<int>(curTime) / 60;
    int curS = static_cast<int>(curTime) % 60;
    int totM = static_cast<int>(totalDur) / 60;
    int totS = static_cast<int>(totalDur) % 60;
    ImGui::TextDisabled("%02d:%02d/%02d:%02d", curM, curS, totM, totS);

    // 6. Fast Actions: Dislike & Fav
    if (curr) {
        ImGui::SameLine(0.0f, 5.0f);
        if (drawMiniHeartBtn(this, "##tskFav", ImVec2(16.0f, 16.0f), curr->isFavorite)) {
            m_library.toggleFavorite(curr->id);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(curr->isFavorite ? "Remove from Favorites" : "Mark as Favorite");

        ImGui::SameLine(0.0f, 3.0f);
        if (drawMiniDislikeBtn(this, "##tskDis", ImVec2(16.0f, 16.0f), curr->isDisliked)) {
            m_library.toggleDislike(curr->id);
            const Track* trk = m_library.getTrackById(curr->id);
            if (trk && trk->isDisliked) {
                if (m_audio.isShuffle()) rebuildShuffleOrder(false);
                playNext();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dislike (Auto-skip)");
    }

    // 7. Volume
    ImGui::SameLine(0.0f, 5.0f);
    float vol = m_audio.getVolume();
    bool muted = m_audio.isMuted();
    if (drawMiniSpeakerBtn(this, "##tskSpk", ImVec2(16.0f, 16.0f), vol, muted)) {
        m_audio.setMuted(!muted);
    }
    ImGui::SameLine(0.0f, 3.0f);
    float newVol = vol;
    if (drawMiniVolumeSlider("##tskVol", 38.0f, 3.0f, vol, &newVol)) {
        m_audio.setVolume(newVol);
        if (m_audio.isMuted()) m_audio.setMuted(false);
    }

    // 8. Live Micro Spectrum Visualizer (16 bands)
    ImGui::SameLine(0.0f, 6.0f);
    ImVec2 visPos = ImGui::GetCursorScreenPos();
    float visW = 46.0f;
    float visH = 20.0f;

    std::vector<float> bars;
    m_audio.getSpectrum(bars, 16);
    float barW = (visW / 16.0f) - 1.0f;
    for (size_t b = 0; b < bars.size() && b < 16; ++b) {
        float val = std::clamp(bars[b], 0.06f, 1.0f);
        float h = val * visH;
        ImVec2 b0(visPos.x + b * (barW + 1.0f), visPos.y + visH - h);
        ImVec2 b1(b0.x + barW, visPos.y + visH);
        ImU32 barCol = (val > 0.65f) ? IM_COL32(245, 185, 25, 255) : IM_COL32(215, 155, 15, 200);
        dl->AddRectFilled(b0, b1, barCol, 0.5f);
    }
    ImGui::Dummy(ImVec2(visW, visH));

    // 9. Far Right Buttons: Pin, Expand, Close
    ImGui::SameLine(availW - 62.0f);
    if (drawMiniPinBtn("##tskPin", ImVec2(16.0f, 16.0f), m_miniPlayerAlwaysOnTop)) {
        m_miniPlayerAlwaysOnTop = !m_miniPlayerAlwaysOnTop;
        Platform::setWindowAlwaysOnTop(m_hWnd, m_miniPlayerAlwaysOnTop);
        savePreferences();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pin (Always on Top)");

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniExpandBtn("##tskExp", ImVec2(16.0f, 16.0f))) toggleMiniPlayer();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restore Full Player");

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniCloseBtn("##tskClose", ImVec2(16.0f, 16.0f))) toggleMiniPlayer();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close");
}

// Mode 4: Minimalist HUD / Floating Ticker
void MainWindow::renderMinimalHUDMiniPlayer() {
    const Track* curr = m_library.getTrackById(m_currentTrackId);
    float availW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Album art thumbnail (46x46) vertically centered
    float artDim = 46.0f;
    ImTextureID art = 0;
    if (curr) art = m_textures.getTrackArtwork(*curr);
    if (!art) art = m_textures.getDefaultArtwork();

    ImVec2 artPos = ImGui::GetCursorScreenPos();
    float availH = ImGui::GetContentRegionAvail().y;
    float artOffsetY = (availH - artDim) * 0.5f;
    if (artOffsetY > 0.0f) {
        artPos.y += artOffsetY;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + artOffsetY);
    }
    if (art) {
        dl->AddImageRounded(art, artPos, ImVec2(artPos.x + artDim, artPos.y + artDim), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
        dl->AddRect(artPos, ImVec2(artPos.x + artDim, artPos.y + artDim), IM_COL32(50, 56, 70, 200), 4.0f, 0, 1.0f);
    } else {
        dl->AddRectFilled(artPos, ImVec2(artPos.x + artDim, artPos.y + artDim), IM_COL32(24, 28, 36, 255), 4.0f);
        dl->AddRect(artPos, ImVec2(artPos.x + artDim, artPos.y + artDim), IM_COL32(50, 56, 70, 200), 4.0f, 0, 1.0f);
    }
    ImGui::Dummy(ImVec2(artDim, artDim));

    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) togglePlayPause();
        ImGui::SetTooltip("Click to Play / Pause");
    }

    ImGui::SameLine(0.0f, 8.0f);

    // Center Info Block
    float rightColsW = 86.0f;
    float midW = availW - artDim - rightColsW - 18.0f;
    if (midW < 130.0f) midW = 130.0f;

    ImGui::BeginGroup();
    std::string titleStr = curr ? curr->getDisplayTitle() : "No track playing";
    if (titleStr.length() > 24) titleStr = titleStr.substr(0, 22) + "..";
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::Text("%s", titleStr.c_str());
    ImGui::PopStyleColor();

    std::string metaStr = curr ? curr->getDisplayArtist() : "SlothPlayer";
    if (metaStr.length() > 26) metaStr = metaStr.substr(0, 24) + "..";
    ImGui::TextColored(ImVec4(0.70f, 0.74f, 0.82f, 1.0f), "%s", metaStr.c_str());

    // Sleek progress bar spanning full width under text
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
    double curTime = m_audio.getCurrentTime();
    double totalDur = m_audio.getTotalDuration();
    if (curr && totalDur <= 0.0) totalDur = curr->duration;

    double outSeekTime = 0.0;
    if (drawSleekProgressBar("##hudSeek", midW, 3.0f, curTime, totalDur, &outSeekTime)) {
        m_audio.seekTo(outSeekTime);
    }
    ImGui::EndGroup();

    // Right Controls Column
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::BeginGroup();

    // Top utility row: Pin, Mode, Expand, Close
    if (drawMiniPinBtn("##hudPin", ImVec2(16.0f, 16.0f), m_miniPlayerAlwaysOnTop)) {
        m_miniPlayerAlwaysOnTop = !m_miniPlayerAlwaysOnTop;
        Platform::setWindowAlwaysOnTop(m_hWnd, m_miniPlayerAlwaysOnTop);
        savePreferences();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pin (Always on Top)");

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniDropdownBtn("##hudMode", ImVec2(14.0f, 16.0f))) ImGui::OpenPopup("HUDModePopup");
    if (ImGui::BeginPopup("HUDModePopup")) {
        if (ImGui::MenuItem("Compact Player", nullptr, m_miniPlayerMode == MiniPlayerMode::Compact)) setMiniPlayerMode(MiniPlayerMode::Compact);
        if (ImGui::MenuItem("Album Art / Vinyl", nullptr, m_miniPlayerMode == MiniPlayerMode::AlbumArt)) setMiniPlayerMode(MiniPlayerMode::AlbumArt);
        if (ImGui::MenuItem("DeskBand / Taskbar Strip", nullptr, m_miniPlayerMode == MiniPlayerMode::Taskbar)) setMiniPlayerMode(MiniPlayerMode::Taskbar);
        if (ImGui::MenuItem("Minimalist HUD", nullptr, m_miniPlayerMode == MiniPlayerMode::MinimalHUD)) setMiniPlayerMode(MiniPlayerMode::MinimalHUD);
        if (ImGui::MenuItem("Floating Synced Lyrics", nullptr, m_miniPlayerMode == MiniPlayerMode::Lyrics)) setMiniPlayerMode(MiniPlayerMode::Lyrics);
        if (ImGui::MenuItem("Picture-in-Picture / Video Card", nullptr, m_miniPlayerMode == MiniPlayerMode::PiP)) setMiniPlayerMode(MiniPlayerMode::PiP);
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniExpandBtn("##hudExp", ImVec2(16.0f, 16.0f))) toggleMiniPlayer();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restore Full Player");

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniCloseBtn("##hudClose", ImVec2(16.0f, 16.0f))) toggleMiniPlayer();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close");

    // Vertical spacing
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

    // Bottom transport buttons row
    if (drawMiniPrevNextBtn(this, "##hudPrev", ImVec2(20.0f, 20.0f), false)) playPrevious();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous");

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniPlayPauseBtn("##hudPlay", ImVec2(24.0f, 22.0f), m_audio.isPlaying())) togglePlayPause();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / Pause");

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniPrevNextBtn(this, "##hudNext", ImVec2(20.0f, 20.0f), true)) playNext();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next");

    ImGui::EndGroup();
}

// Mode 5: Dedicated Floating Synced Lyrics HUD (MusicBee Lyrics Mini Widget)
const LyricsData& MainWindow::getCachedLyrics(const Track& track) {
    if (m_cachedLyricsTrackId != track.id) {
        m_cachedLyricsData = m_lyrics.getLyricsForTrack(track);
        m_cachedLyricsTrackId = track.id;
    }
    return m_cachedLyricsData;
}

void MainWindow::renderLyricsMiniPlayer() {
    renderMiniPlayerHeader("Synced Lyrics");
    ImGui::Separator();

    const Track* curr = m_library.getTrackById(m_currentTrackId);
    float availW = ImGui::GetContentRegionAvail().x;

    // Header info: Title & Artist + Font Scale Buttons
    std::string titleStr = curr ? (curr->getDisplayTitle() + " \xE2\x80\x94 " + curr->getDisplayArtist()) : "No track playing";
    if (titleStr.length() > 32) titleStr = titleStr.substr(0, 30) + "..";
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::Text("%s", titleStr.c_str());
    ImGui::PopStyleColor();

    ImGui::SameLine(availW - 60.0f);
    if (ImGui::SmallButton("A-")) m_lyricsScale = std::max(0.7f, m_lyricsScale - 0.1f);
    ImGui::SameLine(0.0f, 3.0f);
    if (ImGui::SmallButton("A+")) m_lyricsScale = std::min(1.8f, m_lyricsScale + 0.1f);

    // Scrollable Central Lyrics Area
    float lyricsH = ImGui::GetContentRegionAvail().y - 40.0f;
    if (lyricsH < 80.0f) lyricsH = 120.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild("FloatLyricsScroll", ImVec2(0.0f, lyricsH), true, ImGuiWindowFlags_None);
    ImGui::SetWindowFontScale(m_lyricsScale);

    if (!curr) {
        ImGui::TextDisabled("Select a track to start playing lyrics.");
    } else {
        const LyricsData& lyrics = getCachedLyrics(*curr);
        if (lyrics.hasLyrics && lyrics.isSynced) {
            double currentPos = m_audio.getCurrentTime();
            int activeIdx = LyricsManager::findActiveLineIndex(lyrics.lines, currentPos);

            static int lastActiveIdx = -1;
            bool needScroll = (activeIdx != lastActiveIdx);
            lastActiveIdx = activeIdx;

            for (int i = 0; i < static_cast<int>(lyrics.lines.size()); ++i) {
                const auto& line = lyrics.lines[i];
                bool isActive = (i == activeIdx);

                if (isActive) {
                    if (needScroll) ImGui::SetScrollHereY(0.40f);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
                } else if (i < activeIdx) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.76f, 0.84f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.55f, 1.0f));
                }

                std::string lineStr = (isActive ? "\xE2\x96\xB6 " : "  ") + (line.text.empty() ? "..." : line.text) + "###flLyr_" + std::to_string(i);
                if (ImGui::Selectable(lineStr.c_str(), isActive)) {
                    m_audio.seekTo(line.timeSeconds);
                }
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered()) {
                    int mm = static_cast<int>(line.timeSeconds) / 60;
                    int ss = static_cast<int>(line.timeSeconds) % 60;
                    ImGui::SetTooltip("Jump to %02d:%02d", mm, ss);
                }
            }
        } else if (lyrics.hasLyrics) {
            ImGui::TextWrapped("%s", lyrics.plainText.c_str());
        } else {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.75f, 0.40f, 0.40f, 1.0f), "No lyrics found.");
            if (ImGui::SmallButton("Load .LRC file...")) {
                std::string lrc = Platform::openFileDialog("Lyrics Files (*.lrc;*.txt)", "*.lrc;*.txt");
                if (!lrc.empty()) {
                    m_lyrics.setCustomLyricsFile(curr->id, lrc);
                    invalidateLyricsCache();
                }
            }
        }
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Bottom Mini Playback Row
    ImGui::Spacing();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImTextureID art = 0;
    if (curr) art = m_textures.getTrackArtwork(*curr);
    if (!art) art = m_textures.getDefaultArtwork();

    float bDim = 24.0f;
    ImVec2 bPos = ImGui::GetCursorScreenPos();
    if (art) {
        dl->AddImageRounded(art, bPos, ImVec2(bPos.x + bDim, bPos.y + bDim), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 2.0f);
    } else {
        dl->AddRectFilled(bPos, ImVec2(bPos.x + bDim, bPos.y + bDim), IM_COL32(24, 28, 36, 255), 2.0f);
    }
    ImGui::Dummy(ImVec2(bDim, bDim));

    ImGui::SameLine(0.0f, 6.0f);
    if (drawMiniPrevNextBtn(this, "##flPrev", ImVec2(20.0f, 20.0f), false)) playPrevious();
    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniPlayPauseBtn("##flPlay", ImVec2(24.0f, 22.0f), m_audio.isPlaying())) togglePlayPause();
    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniPrevNextBtn(this, "##flNext", ImVec2(20.0f, 20.0f), true)) playNext();

    ImGui::SameLine(0.0f, 6.0f);
    double curTime = m_audio.getCurrentTime();
    double totalDur = m_audio.getTotalDuration();
    if (curr && totalDur <= 0.0) totalDur = curr->duration;

    double outSeekTime = 0.0;
    float seekW = availW - 170.0f;
    if (seekW < 80.0f) seekW = 80.0f;
    if (drawSleekProgressBar("##flSeek", seekW, 3.0f, curTime, totalDur, &outSeekTime)) {
        m_audio.seekTo(outSeekTime);
    }

    ImGui::SameLine(0.0f, 4.0f);
    float vol = m_audio.getVolume();
    float newVol = vol;
    if (drawMiniVolumeSlider("##flVol", 36.0f, 3.0f, vol, &newVol)) {
        m_audio.setVolume(newVol);
        if (m_audio.isMuted()) m_audio.setMuted(false);
    }
}

void MainWindow::renderPiPMiniPlayer() {
    float availW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Track* curr = m_library.getTrackById(m_currentTrackId);
    bool isPlaying = m_audio.isPlaying();
    double curTime = m_audio.getCurrentTime();
    double totalDur = m_audio.getTotalDuration();
    if (curr && totalDur <= 0.0) totalDur = curr->duration;

    // --- 1. Top Header Bar ---
    ImVec2 hdrPos = ImGui::GetCursorScreenPos();
    float hdrH = 18.0f;

    // Header dragging support
    if (m_hWnd && ImGui::IsMouseHoveringRect(hdrPos, ImVec2(hdrPos.x + availW - 85.0f, hdrPos.y + hdrH + 2.0f))) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
            if (delta.x != 0.0f || delta.y != 0.0f) {
                RECT rc;
                GetWindowRect(static_cast<HWND>(m_hWnd), &rc);
                SetWindowPos(static_cast<HWND>(m_hWnd), nullptr,
                             rc.left + static_cast<int>(delta.x),
                             rc.top + static_cast<int>(delta.y),
                             0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
        }
    }

    // Yellow round media badge with play triangle (matching screenshot)
    ImVec2 badgeCenter(hdrPos.x + 8.5f, hdrPos.y + hdrH * 0.5f);
    float badgeR = 7.0f;
    dl->AddCircleFilled(badgeCenter, badgeR, IM_COL32(250, 204, 21, 255));
    dl->AddTriangleFilled(ImVec2(badgeCenter.x - 2.0f, badgeCenter.y - 3.5f),
                          ImVec2(badgeCenter.x + 3.5f, badgeCenter.y),
                          ImVec2(badgeCenter.x - 2.0f, badgeCenter.y + 3.5f),
                          IM_COL32(20, 24, 30, 255));

    // Title string
    ImGui::SetCursorScreenPos(ImVec2(hdrPos.x + 20.0f, hdrPos.y));
    std::string titleStr = curr ? curr->getDisplayTitle() : "No media playing";
    float maxTitleW = availW - 110.0f;
    if (ImGui::CalcTextSize(titleStr.c_str()).x > maxTitleW) {
        while (!titleStr.empty() && ImGui::CalcTextSize((titleStr + "...").c_str()).x > maxTitleW) {
            titleStr.pop_back();
        }
        titleStr += "...";
    }

    ImGui::TextColored(ImVec4(0.90f, 0.94f, 0.98f, 1.0f), "%s", titleStr.c_str());
    if (curr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\nArtist: %s\nAlbum: %s", curr->getDisplayTitle().c_str(),
                          curr->getDisplayArtist().c_str(), curr->getDisplayAlbum().c_str());
    }

    // Right Controls on Header
    ImGui::SameLine(availW - 84.0f);
    if (drawMiniDropdownBtn("##pipMode", ImVec2(14.0f, 16.0f))) ImGui::OpenPopup("PiPModePopup");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch Widget Mode");

    if (ImGui::BeginPopup("PiPModePopup")) {
        ImGui::TextColored(Theme::AccentColor(), "Widget Modes");
        ImGui::Separator();
        if (ImGui::MenuItem("Compact Player (Classic)", nullptr, m_miniPlayerMode == MiniPlayerMode::Compact)) setMiniPlayerMode(MiniPlayerMode::Compact);
        if (ImGui::MenuItem("Album Art / Vinyl Card", nullptr, m_miniPlayerMode == MiniPlayerMode::AlbumArt)) setMiniPlayerMode(MiniPlayerMode::AlbumArt);
        if (ImGui::MenuItem("DeskBand / Taskbar Strip", nullptr, m_miniPlayerMode == MiniPlayerMode::Taskbar)) setMiniPlayerMode(MiniPlayerMode::Taskbar);
        if (ImGui::MenuItem("Minimalist HUD Ticker", nullptr, m_miniPlayerMode == MiniPlayerMode::MinimalHUD)) setMiniPlayerMode(MiniPlayerMode::MinimalHUD);
        if (ImGui::MenuItem("Floating Synced Lyrics", nullptr, m_miniPlayerMode == MiniPlayerMode::Lyrics)) setMiniPlayerMode(MiniPlayerMode::Lyrics);
        if (ImGui::MenuItem("Picture-in-Picture / Video Card", nullptr, m_miniPlayerMode == MiniPlayerMode::PiP)) setMiniPlayerMode(MiniPlayerMode::PiP);
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniPinBtn("##pipPin", ImVec2(16.0f, 16.0f), m_miniPlayerAlwaysOnTop)) {
        m_miniPlayerAlwaysOnTop = !m_miniPlayerAlwaysOnTop;
        Platform::setWindowAlwaysOnTop(m_hWnd, m_miniPlayerAlwaysOnTop);
        savePreferences();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_miniPlayerAlwaysOnTop ? "Pin: Always on Top (ON)" : "Pin: Always on Top (OFF)");

    ImGui::SameLine(0.0f, 3.0f);
    char opBuf[8];
    snprintf(opBuf, sizeof(opBuf), "%d%%", static_cast<int>(m_miniPlayerOpacity * 100.0f));
    if (ImGui::SmallButton(opBuf)) ImGui::OpenPopup("PiPOpacityPopup");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Window Opacity");

    if (ImGui::BeginPopup("PiPOpacityPopup")) {
        ImGui::TextDisabled("Opacity");
        if (ImGui::MenuItem("100% (Solid)", nullptr, m_miniPlayerOpacity >= 0.99f)) {
            m_miniPlayerOpacity = 1.0f;
            Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
            savePreferences();
        }
        if (ImGui::MenuItem("90%", nullptr, std::abs(m_miniPlayerOpacity - 0.90f) < 0.05f)) {
            m_miniPlayerOpacity = 0.90f;
            Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
            savePreferences();
        }
        if (ImGui::MenuItem("80%", nullptr, std::abs(m_miniPlayerOpacity - 0.80f) < 0.05f)) {
            m_miniPlayerOpacity = 0.80f;
            Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
            savePreferences();
        }
        if (ImGui::MenuItem("70%", nullptr, std::abs(m_miniPlayerOpacity - 0.70f) < 0.05f)) {
            m_miniPlayerOpacity = 0.70f;
            Platform::setWindowOpacity(m_hWnd, m_miniPlayerOpacity);
            savePreferences();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, 3.0f);
    if (drawMiniCloseBtn("##pipClose", ImVec2(16.0f, 16.0f))) toggleMiniPlayer();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Exit Mini Player");

    // --- 2. 16:9 Aspect-Ratio Media Viewport / Screen Canvas ---
    ImGui::Spacing();
    float screenW = availW;
    float screenH = roundf(screenW * 9.0f / 16.0f); // 16:9 ratio
    ImVec2 screenPos = ImGui::GetCursorScreenPos();
    ImVec2 screenMax(screenPos.x + screenW, screenPos.y + screenH);

    // Deep Cinema screen background & Bezel
    dl->AddRectFilled(screenPos, screenMax, IM_COL32(10, 12, 16, 255), 4.0f);

    // Artwork rendering (letterbox centered if square, or filled)
    ImTextureID art = 0;
    if (curr) art = m_textures.getTrackArtwork(*curr);
    if (!art) art = m_textures.getDefaultArtwork();

    if (art) {
        float artSide = screenH;
        float artX = screenPos.x + (screenW - artSide) * 0.5f;
        dl->AddImageRounded(art, ImVec2(artX, screenPos.y), ImVec2(artX + artSide, screenPos.y + screenH),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
    }

    // Dynamic Live Waveform / Spectrum Overlay on lower area of video canvas
    if (isPlaying) {
        std::vector<float> bars;
        m_audio.getSpectrum(bars, 32);
        if (!bars.empty()) {
            float barW = (screenW / 32.0f) - 1.0f;
            float maxVisH = 26.0f;
            for (size_t b = 0; b < bars.size() && b < 32; ++b) {
                float val = std::clamp(bars[b], 0.04f, 1.0f);
                float bh = val * maxVisH;
                ImVec2 b0(screenPos.x + b * (barW + 1.0f), screenMax.y - bh - 2.0f);
                ImVec2 b1(b0.x + barW, screenMax.y - 2.0f);
                dl->AddRectFilled(b0, b1, IM_COL32(50, 160, 240, 130), 1.0f);
            }
        }
    }

    // Interactive Screen Hover & Click
    ImGui::SetCursorScreenPos(screenPos);
    bool screenClicked = ImGui::InvisibleButton("##pipScreenBtn", ImVec2(screenW, screenH));
    bool screenHov = ImGui::IsItemHovered();
    if (screenClicked) togglePlayPause();

    if (screenHov) {
        // Semi-transparent overlay
        dl->AddRectFilled(screenPos, screenMax, IM_COL32(0, 0, 0, 75), 4.0f);

        // Center Play / Pause Indicator
        ImVec2 screenCenter(screenPos.x + screenW * 0.5f, screenPos.y + screenH * 0.5f);
        dl->AddCircleFilled(screenCenter, 22.0f, IM_COL32(20, 24, 30, 200));
        dl->AddCircle(screenCenter, 22.0f, IM_COL32(255, 255, 255, 120), 0, 1.2f);
        if (!isPlaying) {
            dl->AddTriangleFilled(ImVec2(screenCenter.x - 5.0f, screenCenter.y - 8.0f),
                                  ImVec2(screenCenter.x + 8.0f, screenCenter.y),
                                  ImVec2(screenCenter.x - 5.0f, screenCenter.y + 8.0f),
                                  IM_COL32(255, 255, 255, 240));
        } else {
            dl->AddRectFilled(ImVec2(screenCenter.x - 6.0f, screenCenter.y - 7.0f),
                              ImVec2(screenCenter.x - 2.0f, screenCenter.y + 7.0f),
                              IM_COL32(255, 255, 255, 240), 1.0f);
            dl->AddRectFilled(ImVec2(screenCenter.x + 2.0f, screenCenter.y - 7.0f),
                              ImVec2(screenCenter.x + 6.0f, screenCenter.y + 7.0f),
                              IM_COL32(255, 255, 255, 240), 1.0f);
        }

        // Time Pill Badge in Bottom Right of Screen
        int curM = static_cast<int>(curTime) / 60;
        int curS = static_cast<int>(curTime) % 60;
        int totM = static_cast<int>(totalDur) / 60;
        int totS = static_cast<int>(totalDur) % 60;
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d / %02d:%02d", curM, curS, totM, totS);

        ImVec2 tSz = ImGui::CalcTextSize(timeBuf);
        ImVec2 pillMin(screenMax.x - tSz.x - 14.0f, screenMax.y - tSz.y - 10.0f);
        ImVec2 pillMax(screenMax.x - 4.0f, screenMax.y - 4.0f);
        dl->AddRectFilled(pillMin, pillMax, IM_COL32(10, 14, 20, 220), 4.0f);
        dl->AddText(ImVec2(pillMin.x + 5.0f, pillMin.y + 3.0f), IM_COL32(230, 238, 250, 255), timeBuf);
    }

    // Outer screen border
    dl->AddRect(screenPos, screenMax, IM_COL32(40, 50, 65, 220), 4.0f, 0, 1.0f);

    // --- 3. Sleek Progress Bar (Right Under Video Canvas) ---
    ImGui::SetCursorScreenPos(ImVec2(screenPos.x, screenMax.y + 3.0f));
    double outSeekTime = 0.0;
    if (drawSleekProgressBar("##pipSeek", screenW, 3.5f, curTime, totalDur, &outSeekTime)) {
        m_audio.seekTo(outSeekTime);
    }

    // --- 4. Bottom 7-Button Rounded Tile Deck (Exact Screenshot Match) ---
    ImGui::Spacing();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

    float btnW = 38.0f;
    float btnH = 28.0f;
    ImVec2 btnSize(btnW, btnH);
    float totalBtnsW = 7.0f * btnW;
    float gap = (availW - totalBtnsW) / 6.0f;
    if (gap < 2.0f) gap = 2.0f;

    // 1. Prev Track
    if (drawPiPTileBtn("##pipPrev", btnSize, PiPButtonType::PrevTrack, isPlaying, "Previous Track")) {
        playPrevious();
    }
    ImGui::SameLine(0.0f, gap);

    // 2. Fast Rewind -5s
    if (drawPiPTileBtn("##pipRwd", btnSize, PiPButtonType::Rewind5s, isPlaying, "Rewind 5s (Seek -5s)")) {
        m_audio.seekTo((std::max)(0.0, curTime - 5.0));
    }
    ImGui::SameLine(0.0f, gap);

    // 3. Stop
    if (drawPiPTileBtn("##pipStop", btnSize, PiPButtonType::Stop, isPlaying, "Stop Playback")) {
        stop();
    }
    ImGui::SameLine(0.0f, gap);

    // 4. Play / Pause
    if (drawPiPTileBtn("##pipPlay", btnSize, PiPButtonType::PlayPause, isPlaying, isPlaying ? "Pause (Space)" : "Play (Space)")) {
        togglePlayPause();
    }
    ImGui::SameLine(0.0f, gap);

    // 5. Fast Forward +5s
    if (drawPiPTileBtn("##pipFwd", btnSize, PiPButtonType::Forward5s, isPlaying, "Forward 5s (Seek +5s)")) {
        m_audio.seekTo((std::min)(totalDur, curTime + 5.0));
    }
    ImGui::SameLine(0.0f, gap);

    // 6. Next Track
    if (drawPiPTileBtn("##pipNext", btnSize, PiPButtonType::NextTrack, isPlaying, "Next Track")) {
        playNext();
    }
    ImGui::SameLine(0.0f, gap);

    // 7. Expand / Fullscreen (Highlighted in Accent Blue)
    if (drawPiPTileBtn("##pipExp", btnSize, PiPButtonType::Expand, isPlaying, "Restore Full Window")) {
        toggleMiniPlayer();
    }
}

void MainWindow::renderMiniPlayer() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoCollapse;

    const float cornerRadius = 10.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, cornerRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::WindowBackground());

    ImGui::Begin("SlothMiniPlayer", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(5);

    // Global smooth non-blocking drag on window background:
    if (m_hWnd && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
            if (delta.x != 0.0f || delta.y != 0.0f) {
                RECT rc;
                GetWindowRect(static_cast<HWND>(m_hWnd), &rc);
                SetWindowPos(static_cast<HWND>(m_hWnd), nullptr,
                             rc.left + static_cast<int>(delta.x),
                             rc.top + static_cast<int>(delta.y),
                             0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("MiniPlayerContextMenu");
        }
    }

    // Outer subtle 1.2px antialiased border with smooth rounded corners matching cornerRadius
    ImDrawList* fgDl = ImGui::GetForegroundDrawList();
    ImVec2 bMin(viewport->Pos.x + 0.5f, viewport->Pos.y + 0.5f);
    ImVec2 bMax(viewport->Pos.x + viewport->Size.x - 0.5f, viewport->Pos.y + viewport->Size.y - 0.5f);
    fgDl->AddRect(bMin, bMax, IM_COL32(75, 84, 105, 230), cornerRadius, 0, 1.2f);

    renderMiniPlayerContextMenu();

    // Hotkeys inside mini player:
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) togglePlayPause();
    if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_M, false)) toggleMiniPlayer();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) toggleMiniPlayer();

    switch (m_miniPlayerMode) {
        case MiniPlayerMode::Compact:
            renderCompactMiniPlayer();
            break;
        case MiniPlayerMode::AlbumArt:
            renderAlbumArtMiniPlayer();
            break;
        case MiniPlayerMode::Taskbar:
            renderTaskbarMiniPlayer();
            break;
        case MiniPlayerMode::MinimalHUD:
            renderMinimalHUDMiniPlayer();
            break;
        case MiniPlayerMode::Lyrics:
            renderLyricsMiniPlayer();
            break;
        case MiniPlayerMode::PiP:
            renderPiPMiniPlayer();
            break;
    }

    ImGui::End();
}

// ----------------- Auto-DJ -----------------

void MainWindow::toggleAutoDJ() {
    auto cfg = m_library.getAutoDJConfig();
    cfg.enabled = !cfg.enabled;
    m_library.setAutoDJConfig(cfg);
}

void MainWindow::renderAutoDJModal() {
    if (!m_showAutoDJModal) return;

    ImGui::SetNextWindowSize(ImVec2(460.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Auto-DJ Configuration", &m_showAutoDJModal)) {
        auto cfg = m_library.getAutoDJConfig();

        ImGui::TextColored(Theme::AccentColor(), "Auto-DJ Continuous Playback Engine");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("Enable Auto-DJ (Intelligent Continuous Playback)", &cfg.enabled);
        ImGui::Spacing();

        ImGui::Checkbox("Favorites / Loved Tracks Only", &cfg.favoritesOnly);
        ImGui::SliderInt("Minimum Track Rating (0-5 Stars)", &cfg.minRating, 0, 5);
        ImGui::SliderInt("Do not repeat artist within N tracks", &cfg.artistSeparationTracks, 1, 20);
        ImGui::SliderInt("Do not repeat track within N tracks", &cfg.trackSeparationTracks, 5, 50);

        char genreBuf[64] = {0};
        strncpy_s(genreBuf, cfg.genreFilter.c_str(), sizeof(genreBuf) - 1);
        ImGui::Text("Genre Filter (empty for all genres):");
        if (ImGui::InputText("##djGenre", genreBuf, sizeof(genreBuf))) {
            cfg.genreFilter = genreBuf;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save Settings", ImVec2(110.0f, 26.0f))) {
            m_library.setAutoDJConfig(cfg);
            m_showAutoDJModal = false;
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Close", ImVec2(80.0f, 26.0f))) {
            m_showAutoDJModal = false;
        }
    }
    ImGui::End();
}

// ----------------- Sleep Timer -----------------

void MainWindow::checkSleepTimer() {
    if (!m_sleepTimer.active) return;

    if (m_sleepTimer.stopAfterCurrent) {
        // Track-ended callback handles this
        return;
    }

    auto now = std::chrono::steady_clock::now();
    int remSec = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(m_sleepTimer.targetTime - now).count());

    if (remSec <= 30 && remSec > 0 && m_sleepTimer.fadeOutVolume) {
        float fade = std::clamp(static_cast<float>(remSec) / 30.0f, 0.0f, 1.0f);
        m_audio.setFadeMultiplier(fade);
    }

    if (remSec <= 0) {
        m_sleepTimer.active = false;
        m_audio.setFadeMultiplier(1.0f);
        m_audio.stop();
    }
}

void MainWindow::renderSleepTimerModal() {
    if (!m_showSleepTimerModal) return;

    ImGui::SetNextWindowSize(ImVec2(400.0f, 280.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Sleep Timer", &m_showSleepTimerModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Sleep Timer Configuration");
        ImGui::Separator();
        ImGui::Spacing();

        if (m_sleepTimer.active) {
            auto now = std::chrono::steady_clock::now();
            int remSec = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(m_sleepTimer.targetTime - now).count());
            if (remSec < 0) remSec = 0;
            int remM = remSec / 60;
            int remS = remSec % 60;

            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "● Sleep Timer is RUNNING");
            ImGui::Text("Time Remaining: %02d:%02d", remM, remS);
            ImGui::Spacing();

            if (ImGui::Button("Cancel Timer", ImVec2(120.0f, 28.0f))) {
                m_sleepTimer.active = false;
                m_audio.setFadeMultiplier(1.0f);
            }
        } else {
            ImGui::Text("Stop Playback After:");
            static int timerChoice = 0; // 0: Minutes, 1: End of current track
            ImGui::RadioButton("Duration (Minutes)", &timerChoice, 0);
            ImGui::SameLine(0.0f, 14.0f);
            ImGui::RadioButton("Current Track Finishes", &timerChoice, 1);

            if (timerChoice == 0) {
                ImGui::SliderInt("Minutes", &m_sleepTimer.durationMinutes, 5, 120);
                if (ImGui::Button("15 min")) m_sleepTimer.durationMinutes = 15;
                ImGui::SameLine(0.0f, 6.0f);
                if (ImGui::Button("30 min")) m_sleepTimer.durationMinutes = 30;
                ImGui::SameLine(0.0f, 6.0f);
                if (ImGui::Button("45 min")) m_sleepTimer.durationMinutes = 45;
                ImGui::SameLine(0.0f, 6.0f);
                if (ImGui::Button("60 min")) m_sleepTimer.durationMinutes = 60;
            }

            ImGui::Spacing();
            ImGui::Checkbox("Fade out volume during last 30 seconds", &m_sleepTimer.fadeOutVolume);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Start Sleep Timer", ImVec2(130.0f, 28.0f))) {
                m_sleepTimer.active = true;
                if (timerChoice == 1) {
                    m_sleepTimer.stopAfterCurrent = true;
                } else {
                    m_sleepTimer.stopAfterCurrent = false;
                    m_sleepTimer.targetTime = std::chrono::steady_clock::now() + std::chrono::minutes(m_sleepTimer.durationMinutes);
                }
                m_showSleepTimerModal = false;
            }
        }

        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Close", ImVec2(80.0f, 28.0f))) {
            m_showSleepTimerModal = false;
        }
    }
    ImGui::End();
}

// ----------------- Batch Tag Editor -----------------

void MainWindow::showBatchTagModal() {
    if (m_selectedTrackIds.empty() && m_selectedTrackId > 0) {
        m_selectedTrackIds.insert(m_selectedTrackId);
    }
    if (!m_selectedTrackIds.empty()) {
        uint64_t firstId = *m_selectedTrackIds.begin();
        const Track* t = m_library.getTrackById(firstId);
        if (t) {
            strncpy_s(m_batchArtist, t->artist.c_str(), sizeof(m_batchArtist) - 1);
            strncpy_s(m_batchAlbum, t->album.c_str(), sizeof(m_batchAlbum) - 1);
            strncpy_s(m_batchGenre, t->genre.c_str(), sizeof(m_batchGenre) - 1);
            m_batchYear = t->year;
            m_batchRating = t->rating;
        }
    }
    m_showBatchTagModal = true;
}

void MainWindow::renderBatchTagModal() {
    if (!m_showBatchTagModal) return;

    ImGui::SetNextWindowSize(ImVec2(480.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Batch Tag Editor", &m_showBatchTagModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Batch Metadata Editor");
        ImGui::TextDisabled("Modifying %zu selected track(s)", m_selectedTrackIds.size());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("##applyArt", &m_batchApplyArtist);
        ImGui::SameLine();
        ImGui::Text("Artist:");
        ImGui::InputText("##batchArt", m_batchArtist, sizeof(m_batchArtist));

        ImGui::Checkbox("##applyAlb", &m_batchApplyAlbum);
        ImGui::SameLine();
        ImGui::Text("Album:");
        ImGui::InputText("##batchAlb", m_batchAlbum, sizeof(m_batchAlbum));

        ImGui::Checkbox("##applyGen", &m_batchApplyGenre);
        ImGui::SameLine();
        ImGui::Text("Genre:");
        ImGui::InputText("##batchGen", m_batchGenre, sizeof(m_batchGenre));

        ImGui::Checkbox("##applyYr", &m_batchApplyYear);
        ImGui::SameLine();
        ImGui::Text("Year:");
        ImGui::InputInt("##batchYr", &m_batchYear);

        ImGui::Checkbox("##applyRat", &m_batchApplyRating);
        ImGui::SameLine();
        ImGui::Text("Rating (0-5 Stars):");
        ImGui::SliderInt("##batchRat", &m_batchRating, 0, 5);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Case Conversion for Selected Tracks:");
        if (ImGui::Button("Title Case")) {
            std::vector<uint64_t> ids(m_selectedTrackIds.begin(), m_selectedTrackIds.end());
            m_library.batchConvertCase(ids, CaseConversion::TitleCase);
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button("UPPERCASE")) {
            std::vector<uint64_t> ids(m_selectedTrackIds.begin(), m_selectedTrackIds.end());
            m_library.batchConvertCase(ids, CaseConversion::UpperCase);
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button("lowercase")) {
            std::vector<uint64_t> ids(m_selectedTrackIds.begin(), m_selectedTrackIds.end());
            m_library.batchConvertCase(ids, CaseConversion::LowerCase);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Apply to Selected", ImVec2(140.0f, 28.0f))) {
            std::vector<uint64_t> ids(m_selectedTrackIds.begin(), m_selectedTrackIds.end());
            m_library.updateBatchMetadata(
                ids,
                m_batchApplyArtist ? m_batchArtist : "",
                m_batchApplyAlbum ? m_batchAlbum : "",
                m_batchApplyYear ? m_batchYear : -1,
                m_batchApplyGenre ? m_batchGenre : "",
                m_batchApplyRating ? m_batchRating : -1
            );
            m_showBatchTagModal = false;
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Cancel", ImVec2(80.0f, 28.0f))) {
            m_showBatchTagModal = false;
        }
    }
    ImGui::End();
}

// ----------------- Library Statistics (Multithreaded) -----------------

void MainWindow::showLibraryStatsModal() {
    m_statsLoading = true;
    m_statsFuture = std::async(std::launch::async, [this]() {
        return m_library.calculateStats();
    });
    m_showLibraryStatsModal = true;
}

void MainWindow::renderLibraryStatsModal() {
    if (!m_showLibraryStatsModal) return;

    if (m_statsLoading && m_statsFuture.valid()) {
        if (m_statsFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            m_cachedStats = m_statsFuture.get();
            m_statsLoading = false;
        }
    }

    ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Library Statistics", &m_showLibraryStatsModal)) {
        ImGui::TextColored(Theme::AccentColor(), "SlothPlayer Library Analysis");
        ImGui::Separator();
        ImGui::Spacing();

        if (m_statsLoading) {
            ImGui::Text("Analyzing library across worker threads...");
        } else {
            const auto& s = m_cachedStats;
            int totalHours = static_cast<int>(s.totalDurationSeconds) / 3600;
            int totalMins = (static_cast<int>(s.totalDurationSeconds) % 3600) / 60;
            double totalSizeGB = static_cast<double>(s.totalFileSizeBytes) / (1024.0 * 1024.0 * 1024.0);

            ImGui::Columns(2, "statsCols", false);
            ImGui::Text("Total Tracks: %zu", s.totalTracks);
            ImGui::Text("Total Albums: %zu", s.totalAlbums);
            ImGui::Text("Total Artists: %zu", s.totalArtists);
            ImGui::Text("Total Playlists: %zu", s.totalPlaylists);
            ImGui::Text("Total Duration: %dh %dm", totalHours, totalMins);

            ImGui::NextColumn();
            ImGui::Text("Total Library Size: %.2f GB", totalSizeGB);
            ImGui::Text("Total Plays: %u", s.totalPlayCount);
            ImGui::Text("Monitored Folders: %zu", m_library.getMonitoredFolders().size());
            ImGui::Columns(1);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(Theme::AccentColor(), "Audio File Formats:");
            for (const auto& f : s.formatCounts) {
                float fraction = (s.totalTracks > 0) ? (static_cast<float>(f.second) / static_cast<float>(s.totalTracks)) : 0.0f;
                char buf[64];
                snprintf(buf, sizeof(buf), "%s: %zu tracks (%.1f%%)", f.first.c_str(), f.second, fraction * 100.0f);
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 18.0f), buf);
            }

            if (!s.topArtists.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(Theme::AccentColor(), "Top Artists by Play Count:");
                for (size_t i = 0; i < std::min<size_t>(5, s.topArtists.size()); ++i) {
                    ImGui::BulletText("%s (%u plays)", s.topArtists[i].first.c_str(), s.topArtists[i].second);
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(80.0f, 26.0f))) {
            m_showLibraryStatsModal = false;
        }
    }
    ImGui::End();
}

// ----------------- Duplicate Tracks (Multithreaded) -----------------

void MainWindow::showDuplicatesModal() {
    m_dupesLoading = true;
    m_dupesFuture = std::async(std::launch::async, [this]() {
        return m_library.findDuplicates();
    });
    m_showDuplicatesModal = true;
}

void MainWindow::renderDuplicatesModal() {
    if (!m_showDuplicatesModal) return;

    if (m_dupesLoading && m_dupesFuture.valid()) {
        if (m_dupesFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            m_duplicateCandidates = m_dupesFuture.get();
            m_dupesLoading = false;
        }
    }

    ImGui::SetNextWindowSize(ImVec2(680.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Duplicate Track Finder", &m_showDuplicatesModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Duplicate Track Manager");
        ImGui::Separator();
        ImGui::Spacing();

        if (m_dupesLoading) {
            ImGui::Text("Scanning library for duplicate titles & artists across worker threads...");
        } else if (m_duplicateCandidates.empty()) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "✓ No duplicate tracks detected in your library!");
        } else {
            ImGui::Text("Found %zu duplicate track pairs:", m_duplicateCandidates.size());
            ImGui::Spacing();

            if (ImGui::BeginTable("DupesTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 280.0f))) {
                ImGui::TableSetupColumn("Artist & Title", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                ImGui::TableSetupColumn("Duplicate 1 (Path)", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                ImGui::TableSetupColumn("Duplicate 2 (Path)", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < m_duplicateCandidates.size(); ++i) {
                    const auto& cand = m_duplicateCandidates[i];
                    ImGui::TableNextRow(0, 22.0f);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s - %s", cand.artist.c_str(), cand.title.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("%s", cand.path1.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled("%s", cand.path2.c_str());

                    ImGui::TableSetColumnIndex(3);
                    std::string btnId = "Del##dup_" + std::to_string(i);
                    if (ImGui::SmallButton(btnId.c_str())) {
                        m_library.deleteTracksFromLibrary({cand.trackId2});
                        showDuplicatesModal();
                        break;
                    }
                }
                ImGui::EndTable();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Rescan Duplicates", ImVec2(140.0f, 26.0f))) {
            showDuplicatesModal();
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Close", ImVec2(80.0f, 26.0f))) {
            m_showDuplicatesModal = false;
        }
    }
    ImGui::End();
}

// ----------------- Shortcuts Reference -----------------

void MainWindow::renderShortcutsModal() {
    if (!m_showShortcutsModal) return;

    ImGui::SetNextWindowSize(ImVec2(560.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Keyboard Shortcuts", &m_showShortcutsModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Default Keyboard Shortcuts Reference");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##filterShortcuts", "Filter shortcuts...", m_shortcutsFilter, sizeof(m_shortcutsFilter));
        ImGui::Spacing();

        struct ShortcutEntry {
            const char* category;
            const char* keys;
            const char* desc;
        };

        static const ShortcutEntry shortcuts[] = {
            {"Playback", "Space", "Play / Pause"},
            {"Playback", "Ctrl+Right", "Next Track"},
            {"Playback", "Ctrl+Left", "Previous Track"},
            {"Playback", "Ctrl+S", "Stop Playback"},
            {"Playback", "Left Arrow", "Seek backward 5 seconds"},
            {"Playback", "Right Arrow", "Seek forward 5 seconds"},
            {"Playback", "Shift+Left Arrow", "Seek backward 30 seconds"},
            {"Playback", "Shift+Right Arrow", "Seek forward 30 seconds"},
            {"Volume", "Ctrl+Up Arrow", "Volume Up 5%"},
            {"Volume", "Ctrl+Down Arrow", "Volume Down 5%"},
            {"Volume", "Ctrl+M", "Mute / Unmute Audio"},
            {"Rating", "Ctrl+L", "Toggle Loved / Favorite"},
            {"Rating", "Ctrl+Shift+L", "Dislike & Skip Current Track"},
            {"Rating", "Ctrl+0", "Clear Star Rating"},
            {"Rating", "Ctrl+1 .. Ctrl+5", "Set Star Rating (1 to 5 Stars)"},
            {"View", "Ctrl+Shift+M", "Toggle Compact / Mini Player"},
            {"View", "Ctrl+E", "Toggle Graphic Equalizer"},
            {"Library", "Ctrl+O", "Open Audio File..."},
            {"Library", "Ctrl+Shift+O", "Add Music Folder..."},
            {"Tools", "Ctrl+D", "Toggle Auto-DJ Continuous Playback"},
            {"Tools", "Ctrl+Shift+S", "Configure Sleep Timer..."},
            {"Tools", "Ctrl+T", "Batch Tag Editor (Selected Tracks)"},
            {"Tools", "Ctrl+I", "Library Statistics Overview"},
            {"General", "Alt+Enter", "Track Properties & Metadata"},
            {"General", "F1", "Open Shortcuts Reference"}
        };

        std::string filter = m_shortcutsFilter;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

        if (ImGui::BeginTable("ShortcutsTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 290.0f))) {
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Shortcut Key", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Action Description", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& s : shortcuts) {
                std::string matchStr = std::string(s.category) + " " + s.keys + " " + s.desc;
                std::transform(matchStr.begin(), matchStr.end(), matchStr.begin(), ::tolower);
                if (!filter.empty() && matchStr.find(filter) == std::string::npos) continue;

                ImGui::TableNextRow(0, 20.0f);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", s.category);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(Theme::AccentColor(), "%s", s.keys);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", s.desc);
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(80.0f, 26.0f))) {
            m_showShortcutsModal = false;
        }
    }
    ImGui::End();
}

// ----------------- Web Radio Streams -----------------

void MainWindow::renderRadioStreamsView() {
    float horizPad = 14.0f;
    float topPad = 12.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);
    float availW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::Text("INTERNET RADIO STREAMS");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetCursorPosX() + availW - 320.0f);
    if (ImGui::Button("+ Add Stream...", ImVec2(130.0f, 24.0f))) {
        m_showAddRadioStreamModal = true;
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::Separator();
    ImGui::Spacing();

    const auto& streams = m_library.getRadioStreams();
    if (streams.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
        ImGui::TextDisabled("No radio streams configured. Click '+ Add Stream...' above to add one.");
        return;
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    if (ImGui::BeginTable("RadioStreamsTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(availW, -1.0f))) {
        ImGui::TableSetupColumn("Play", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Station Name", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Genre", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("Description / URL", ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (const auto& s : streams) {
            ImGui::TableNextRow(0, 26.0f);

            // Play column: Crisp vector play button (immune to font atlas glyph issues)
            ImGui::TableSetColumnIndex(0);
            std::string pId = "##stream_play_" + std::to_string(s.id);
            ImVec2 curPos = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton(pId.c_str(), ImVec2(28.0f, 22.0f))) {
                m_audio.loadAndPlay(s.url);
            }
            bool isHov = ImGui::IsItemHovered();
            bool isAct = ImGui::IsItemActive();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 center(curPos.x + 14.0f, curPos.y + 11.0f);
            float r = 9.0f;
            ImU32 circleBg = isAct ? IM_COL32(255, 255, 255, 60) : (isHov ? IM_COL32(235, 168, 15, 60) : IM_COL32(255, 255, 255, 20));
            dl->AddCircleFilled(center, r, circleBg, 16);
            ImU32 triCol = isAct ? IM_COL32(255, 255, 255, 255) : (isHov ? ImGui::GetColorU32(Theme::AccentColor()) : IM_COL32(200, 210, 225, 240));
            dl->AddTriangleFilled(
                ImVec2(center.x - 2.5f, center.y - 4.5f),
                ImVec2(center.x + 4.5f, center.y),
                ImVec2(center.x - 2.5f, center.y + 4.5f),
                triCol
            );

            // Name
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", s.name.c_str());

            // Genre
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(Theme::AccentColor(), "%s", s.genre.c_str());

            // Description / URL
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", s.description.empty() ? s.url.c_str() : s.description.c_str());

            // Action
            ImGui::TableSetColumnIndex(4);
            std::string delId = "Del##sdel_" + std::to_string(s.id);
            if (ImGui::SmallButton(delId.c_str())) {
                m_library.removeRadioStream(s.id);
                break;
            }
        }
        ImGui::EndTable();
    }
}

void MainWindow::renderAddRadioStreamModal() {
    if (!m_showAddRadioStreamModal) return;

    ImGui::SetNextWindowSize(ImVec2(440.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Add Radio Station", &m_showAddRadioStreamModal)) {
        ImGui::TextColored(Theme::AccentColor(), "New Internet Radio Stream");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Station Name:");
        ImGui::InputText("##stName", m_newStreamName, sizeof(m_newStreamName));

        ImGui::Text("Stream URL (HTTP / Shoutcast / Icecast):");
        ImGui::InputText("##stUrl", m_newStreamUrl, sizeof(m_newStreamUrl));

        ImGui::Text("Genre:");
        ImGui::InputText("##stGenre", m_newStreamGenre, sizeof(m_newStreamGenre));

        ImGui::Text("Description:");
        ImGui::InputText("##stDesc", m_newStreamDesc, sizeof(m_newStreamDesc));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Add Station", ImVec2(110.0f, 26.0f))) {
            if (strlen(m_newStreamName) > 0 && strlen(m_newStreamUrl) > 0) {
                m_library.addRadioStream(m_newStreamName, m_newStreamUrl, m_newStreamGenre, m_newStreamDesc);
                m_newStreamName[0] = '\0';
                m_newStreamUrl[0] = '\0';
                m_newStreamGenre[0] = '\0';
                m_newStreamDesc[0] = '\0';
                m_showAddRadioStreamModal = false;
            }
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Cancel", ImVec2(80.0f, 26.0f))) {
            m_showAddRadioStreamModal = false;
        }
    }
    ImGui::End();
}

void MainWindow::showFileOrganizerModal() {
    std::vector<Track> tracks;
    if (!m_selectedTrackIds.empty()) {
        for (uint64_t id : m_selectedTrackIds) {
            Track* t = m_library.getTrackById(id);
            if (t) tracks.push_back(*t);
        }
    } else if (m_selectedTrackId > 0) {
        Track* t = m_library.getTrackById(m_selectedTrackId);
        if (t) tracks.push_back(*t);
    } else {
        tracks = m_library.getTracks();
    }
    m_organizePreviews = FileOrganizer::generatePreview(tracks, m_organizerPattern, m_organizerBaseDir);
    m_showFileOrganizerModal = true;
}

void MainWindow::showAudioConverterModal() {
    m_showAudioConverterModal = true;
}

void MainWindow::showSmartPlaylistBuilderModal() {
    m_showSmartPlaylistBuilderModal = true;
}

void MainWindow::toggleTheaterMode() {
    if (m_viewMode == ViewMode::TheaterMode) {
        m_viewMode = ViewMode::NowPlaying;
    } else {
        m_viewMode = ViewMode::TheaterMode;
    }
}

void MainWindow::requestWaveform(const std::string& path) {
    if (path.empty() || path == m_waveformTrackPath) return;
    m_waveformTrackPath = path;
    m_waveformLoading = true;
    m_cachedWaveform.clear();
    m_waveformFuture = std::async(std::launch::async, [path]() {
        return AudioEngine::generateWaveformPeaks(path, 160);
    });
}

void MainWindow::renderWaveformSeeker(float width, float height, double currentSec, double totalSec) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + width, p0.y + height);

    ImGui::InvisibleButton("##WavebarSeek", ImVec2(width, height));
    bool isHov = ImGui::IsItemHovered();
    bool isAct = ImGui::IsItemActive();

    // Subtle dark background and bottom divider
    dl->AddRectFilled(p0, p1, IM_COL32(16, 18, 22, 255));
    dl->AddLine(ImVec2(p0.x, p1.y), p1, IM_COL32(35, 38, 45, 255), 1.0f);

    float progress = (totalSec > 0.0) ? std::clamp(static_cast<float>(currentSec / totalSec), 0.0f, 1.0f) : 0.0f;
    float playedX = p0.x + width * progress;
    float midY = p0.y + height * 0.5f;

    if (m_cachedWaveform.empty()) {
        dl->AddLine(ImVec2(p0.x, midY), ImVec2(p1.x, midY), IM_COL32(48, 52, 62, 255), 1.0f);
    } else {
        float barSpacing = 2.5f;
        int numBars = static_cast<int>(width / barSpacing);
        size_t n = m_cachedWaveform.size();

        ImU32 accentCol = ImGui::GetColorU32(Theme::AccentColor());
        ImU32 unplayedCol = isHov ? IM_COL32(95, 102, 115, 255) : IM_COL32(65, 70, 80, 255);

        for (int i = 0; i < numBars; ++i) {
            float barX = p0.x + i * barSpacing + 1.0f;
            size_t srcIdx = static_cast<size_t>((static_cast<float>(i) / numBars) * n);
            if (srcIdx >= n) srcIdx = n - 1;

            float amp = std::clamp(m_cachedWaveform[srcIdx], 0.05f, 1.0f);
            float halfH = std::max(1.0f, (height * 0.42f) * amp);

            ImU32 col = (barX <= playedX) ? accentCol : unplayedCol;
            dl->AddLine(ImVec2(barX, midY - halfH), ImVec2(barX, midY + halfH), col, 1.5f);
        }
    }

    // Crisp playhead line
    if (playedX >= p0.x && playedX <= p1.x) {
        dl->AddLine(ImVec2(playedX, p0.y), ImVec2(playedX, p1.y), IM_COL32(255, 255, 255, 230), 1.5f);
    }

    if (isHov || isAct) {
        float mouseX = ImGui::GetIO().MousePos.x;
        float frac = std::clamp((mouseX - p0.x) / width, 0.0f, 1.0f);
        double hovTime = frac * totalSec;
        int hovSec = static_cast<int>(hovTime);

        dl->AddLine(ImVec2(mouseX, p0.y), ImVec2(mouseX, p1.y), IM_COL32(255, 255, 255, 140), 1.0f);

        char hovBuf[16];
        snprintf(hovBuf, sizeof(hovBuf), "%02d:%02d", hovSec / 60, hovSec % 60);
        ImVec2 badgeSize = ImGui::CalcTextSize(hovBuf);
        ImVec2 badgePos = ImVec2(mouseX - badgeSize.x * 0.5f - 6.0f, p0.y - 24.0f);

        dl->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x + 12.0f, badgePos.y + badgeSize.y + 4.0f), IM_COL32(18, 20, 25, 245), 3.0f);
        dl->AddRect(badgePos, ImVec2(badgePos.x + badgeSize.x + 12.0f, badgePos.y + badgeSize.y + 4.0f), ImGui::GetColorU32(Theme::AccentColor()), 3.0f, 0, 1.0f);
        dl->AddText(ImVec2(badgePos.x + 6.0f, badgePos.y + 2.0f), IM_COL32(250, 250, 250, 255), hovBuf);

        if (isAct && totalSec > 0.0) {
            m_audio.seekTo(hovTime);
        }
    }
}

void MainWindow::renderTheaterMode() {
    float availW = ImGui::GetContentRegionAvail().x;
    float availH = ImGui::GetContentRegionAvail().y;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 screenP0 = ImGui::GetCursorScreenPos();

    dl->AddRectFilledMultiColor(
        screenP0,
        ImVec2(screenP0.x + availW, screenP0.y + availH),
        IM_COL32(10, 12, 16, 255),
        IM_COL32(14, 18, 24, 255),
        IM_COL32(6, 7, 9, 255),
        IM_COL32(10, 12, 16, 255)
    );

    ImGui::SetCursorPos(ImVec2(availW - 140.0f, 16.0f));
    if (ImGui::Button("Exit Theater [Esc]", ImVec2(125.0f, 28.0f))) {
        toggleTheaterMode();
    }

    const Track* curr = m_library.getTrackById(m_currentTrackId);
    float centerX = screenP0.x + availW * 0.5f;
    float centerY = screenP0.y + availH * 0.40f;

    float maxArtSize = std::min(320.0f, std::min(availW * 0.45f, availH * 0.45f));
    int artW = 0, artH = 0;
    ImTextureID artTex = curr ? m_textures.getTrackArtwork(*curr, &artW, &artH) : m_textures.getDefaultArtwork(&artW, &artH);
    ImVec2 fitSize = TextureManager::getAspectFitSize(artW, artH, maxArtSize, maxArtSize);

    if (artTex) {
        ImVec2 artP0(centerX - fitSize.x * 0.5f, centerY - fitSize.y * 0.5f - 40.0f);
        ImVec2 artP1(artP0.x + fitSize.x, artP0.y + fitSize.y);

        dl->AddRectFilled(ImVec2(artP0.x - 6.0f, artP0.y - 6.0f), ImVec2(artP1.x + 6.0f, artP1.y + 6.0f), IM_COL32(235, 168, 15, 35), 10.0f);
        dl->AddImage(artTex, artP0, artP1);
        dl->AddRect(artP0, artP1, IM_COL32(235, 168, 15, 140), 4.0f, 0, 1.5f);
    }

    float textY = centerY + fitSize.y * 0.5f - 20.0f;
    std::string title = curr ? curr->getDisplayTitle() : "No Track Playing";
    std::string artist = curr ? curr->getDisplayArtist() : "Select a track to enjoy theater mode";
    std::string album = curr ? curr->getDisplayAlbum() : "";

    ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
    dl->AddText(ImVec2(centerX - titleSize.x * 0.5f, textY), IM_COL32(255, 255, 255, 255), title.c_str());

    std::string sub = artist;
    if (!album.empty()) sub += " — " + album;
    ImVec2 subSize = ImGui::CalcTextSize(sub.c_str());
    dl->AddText(ImVec2(centerX - subSize.x * 0.5f, textY + 26.0f), ImGui::GetColorU32(Theme::AccentColor()), sub.c_str());

    std::vector<float> bars;
    m_audio.getSpectrum(bars, 32);
    float visW = std::min(480.0f, availW - 60.0f);
    float visH = 42.0f;
    float visX = centerX - visW * 0.5f;
    float visY = textY + 58.0f;
    float barW = (visW / 32.0f) - 2.0f;

    for (size_t b = 0; b < bars.size(); ++b) {
        float bH = bars[b] * visH;
        float bx = visX + b * (barW + 2.0f);
        dl->AddRectFilled(
            ImVec2(bx, visY + visH - bH),
            ImVec2(bx + barW, visY + visH),
            IM_COL32(235, 168, 15, 200),
            2.0f
        );
    }
}

void MainWindow::renderPodcastsView() {
    float horizPad = 14.0f;
    float topPad = 12.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);
    float availW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::Text("Podcasts & Audio Shows");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetCursorPosX() + availW - 300.0f);
    if (ImGui::Button("Reset Feeds", ImVec2(120.0f, 24.0f))) {
        m_podcasts.initCuratedChannels();
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::Separator();
    ImGui::Spacing();

    const auto& channels = m_podcasts.getChannels();
    if (channels.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
        ImGui::TextDisabled("No subscribed podcasts.");
        return;
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::Columns(2, "PodcastCols", false);
    ImGui::SetColumnWidth(0, 240.0f);

    ImGui::TextColored(Theme::HeaderMuted(), "CHANNELS");
    ImGui::Spacing();
    for (const auto& ch : channels) {
        bool isSel = (m_selectedPodcastChannelId == ch.id);
        std::string label = ch.title;
        if (ImGui::Selectable(label.c_str(), isSel, 0, ImVec2(0.0f, 26.0f))) {
            m_selectedPodcastChannelId = ch.id;
        }
        ImGui::SameLine(180.0f);
        ImGui::TextDisabled("(%zu)", ch.episodes.size());
    }

    ImGui::NextColumn();

    const PodcastChannel* curCh = m_podcasts.getChannelById(m_selectedPodcastChannelId);
    if (!curCh && !channels.empty()) curCh = &channels[0];

    if (curCh) {
        ImGui::TextColored(Theme::AccentColor(), "%s", curCh->title.c_str());
        ImGui::TextDisabled("Author: %s | Category: %s", curCh->author.c_str(), curCh->category.c_str());
        ImGui::TextWrapped("%s", curCh->description.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(Theme::HeaderMuted(), "EPISODES");
        if (ImGui::BeginTable("EpTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 320.0f))) {
            ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("Published", ImGuiTableColumnFlags_WidthStretch, 0.20f);
            ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < curCh->episodes.size(); ++i) {
                const auto& ep = curCh->episodes[i];
                ImGui::TableNextRow(0, 24.0f);
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", ep.title.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", ep.publishDate.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", ep.durationStr.c_str());

                ImGui::TableSetColumnIndex(3);
                std::string btnId = "Play##ep_" + std::to_string(ep.id);
                if (ImGui::SmallButton(btnId.c_str())) {
                    m_audio.loadAndPlay(ep.audioUrl);
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::Columns(1);
}

void MainWindow::renderHistoryView() {
    float horizPad = 14.0f;
    float topPad = 12.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);
    float availW = ImGui::GetContentRegionAvail().x - (horizPad * 2.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
    ImGui::Text("Playback History");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetCursorPosX() + availW - 270.0f);
    if (ImGui::Button("Clear History", ImVec2(110.0f, 24.0f))) {
        m_library.clearHistory();
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    ImGui::Separator();
    ImGui::Spacing();

    const auto& history = m_library.getHistory();
    if (history.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
        ImGui::TextDisabled("No tracks in playback history yet.");
        return;
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);
    if (ImGui::BeginTable("HistTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(availW, -1.0f))) {
        ImGui::TableSetupColumn("Track Title", ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableSetupColumn("Artist", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Played Time", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("Play", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < history.size(); ++i) {
            const auto& h = history[i];
            ImGui::TableNextRow(0, 22.0f);

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", h.title.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", h.artist.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", h.timestampStr.c_str());

            ImGui::TableSetColumnIndex(3);
            std::string btnId = "Play##hist_" + std::to_string(i);
            if (ImGui::SmallButton(btnId.c_str())) {
                playTrack(h.trackId);
            }
        }
        ImGui::EndTable();
    }
}

void MainWindow::renderFileOrganizerModal() {
    if (!m_showFileOrganizerModal) return;

    ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Auto-Organize Audio Files", &m_showFileOrganizerModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Batch File Organizer & Renamer");
        ImGui::TextDisabled("Rename and move audio files based on tag metadata tokens:");
        ImGui::TextDisabled("Tokens: <Artist>, <Album>, <Title>, <Track#>, <Genre>, <Year>");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Naming Template:");
        ImGui::SetNextItemWidth(450.0f);
        if (ImGui::InputText("##orgPat", m_organizerPattern, sizeof(m_organizerPattern))) {
            showFileOrganizerModal();
        }

        ImGui::Spacing();
        ImGui::Text("Destination Base Folder (Leave empty for source folder):");
        ImGui::SetNextItemWidth(450.0f);
        if (ImGui::InputText("##orgBase", m_organizerBaseDir, sizeof(m_organizerBaseDir))) {
            showFileOrganizerModal();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float curProg = 0.0f;
        std::string curStatus;
        {
            std::lock_guard<std::mutex> lock(m_asyncToolsMutex);
            curProg = m_organizeProgress;
            curStatus = m_organizeStatus;
        }

        if (m_organizingActive) {
            ImGui::Text("Organizing audio files...");
            ImGui::ProgressBar(curProg, ImVec2(-1.0f, 22.0f), curStatus.c_str());
        } else {
            ImGui::Text("Preview of %zu operations:", m_organizePreviews.size());
            if (ImGui::BeginTable("OrgTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f))) {
                ImGui::TableSetupColumn("Current Path", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupColumn("Proposed Destination", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.10f);
                ImGui::TableHeadersRow();

                for (const auto& it : m_organizePreviews) {
                    ImGui::TableNextRow(0, 20.0f);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", it.originalPath.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", it.proposedPath.c_str());

                    ImGui::TableSetColumnIndex(2);
                    if (it.isValid) {
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Ready");
                    } else {
                        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "Conflict");
                    }
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            if (ImGui::Button("Execute Batch Move", ImVec2(150.0f, 28.0f))) {
                m_organizingActive = true;
                {
                    std::lock_guard<std::mutex> lock(m_asyncToolsMutex);
                    m_organizeProgress = 0.0f;
                    m_organizeStatus = "Starting...";
                }
                FileOrganizer::executeAsync(
                    m_organizePreviews,
                    [this](float prog, const std::string& cur) {
                        std::lock_guard<std::mutex> lock(m_asyncToolsMutex);
                        m_organizeProgress = prog;
                        m_organizeStatus = cur;
                    },
                    [this](size_t succ, size_t fail, const std::vector<std::pair<uint64_t, std::string>>& paths) {
                        m_organizingActive = false;
                        for (const auto& p : paths) {
                            Track* t = m_library.getTrackById(p.first);
                            if (t) t->filePath = p.second;
                        }
                        m_library.saveLibrary();
                    }
                );
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Close", ImVec2(80.0f, 28.0f))) {
                m_showFileOrganizerModal = false;
            }
        }
    }
    ImGui::End();
}

void MainWindow::renderAudioConverterModal() {
    if (!m_showAudioConverterModal) return;

    ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Audio Format Converter", &m_showAudioConverterModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Multithreaded Format Transcoder");
        ImGui::TextDisabled("Transcode audio collection to uncompressed studio WAV formats.");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Target Audio Format:");
        ImGui::RadioButton("WAV (16-bit Studio Master)", &m_converterFormatIndex, 0);
        ImGui::RadioButton("WAV (32-bit Floating Point High-Res)", &m_converterFormatIndex, 1);

        ImGui::Spacing();
        ImGui::Text("Target Sample Rate:");
        ImGui::RadioButton("44.1 kHz (CD Audio)", &m_converterSampleRateIndex, 0);
        ImGui::SameLine();
        ImGui::RadioButton("48.0 kHz (Studio)", &m_converterSampleRateIndex, 1);
        ImGui::SameLine();
        ImGui::RadioButton("96.0 kHz (Hi-Res)", &m_converterSampleRateIndex, 2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ConvertProgress curConvProg;
        {
            std::lock_guard<std::mutex> lock(m_asyncToolsMutex);
            curConvProg = m_converterProgress;
        }

        if (m_convertingActive) {
            ImGui::Text("Converting audio tracks in background...");
            ImGui::ProgressBar(curConvProg.totalProgress, ImVec2(-1.0f, 24.0f), curConvProg.statusMessage.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Cancel Conversion", ImVec2(130.0f, 26.0f))) {
                AudioConverter::cancelBatch();
            }
        } else {
            size_t selCount = m_selectedTrackIds.empty() ? (m_selectedTrackId > 0 ? 1 : m_library.getTracks().size()) : m_selectedTrackIds.size();
            ImGui::Text("Ready to convert %zu track(s).", selCount);
            ImGui::Spacing();

            if (ImGui::Button("Start Conversion", ImVec2(140.0f, 28.0f))) {
                std::vector<ConvertJob> jobs;
                std::vector<uint64_t> idsToConvert;
                if (!m_selectedTrackIds.empty()) {
                    idsToConvert.assign(m_selectedTrackIds.begin(), m_selectedTrackIds.end());
                } else if (m_selectedTrackId > 0) {
                    idsToConvert.push_back(m_selectedTrackId);
                } else {
                    for (const auto& tr : m_library.getTracks()) idsToConvert.push_back(tr.id);
                }

                uint32_t sRate = (m_converterSampleRateIndex == 0) ? 44100 : (m_converterSampleRateIndex == 1 ? 48000 : 96000);
                TargetFormat fmt = (m_converterFormatIndex == 0) ? TargetFormat::WAV_16Bit : TargetFormat::WAV_32BitFloat;

                for (uint64_t tid : idsToConvert) {
                    const Track* tr = m_library.getTrackById(tid);
                    if (!tr || tr->isStream) continue;

                    ConvertJob job;
                    job.trackId = tid;
                    job.sourcePath = tr->filePath;
                    fs::path p(tr->filePath);
                    job.destPath = (p.parent_path() / (p.stem().string() + "_converted.wav")).string();
                    job.trackTitle = tr->getDisplayTitle();
                    job.targetFormat = fmt;
                    job.targetSampleRate = sRate;
                    jobs.push_back(job);
                }

                m_convertingActive = true;
                AudioConverter::startBatchConvertAsync(
                    jobs,
                    [this](const ConvertProgress& prog) {
                        std::lock_guard<std::mutex> lock(m_asyncToolsMutex);
                        m_converterProgress = prog;
                    },
                    [this](size_t succ, size_t fail) {
                        m_convertingActive = false;
                    }
                );
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Close", ImVec2(80.0f, 28.0f))) {
                m_showAudioConverterModal = false;
            }
        }
    }
    ImGui::End();
}

void MainWindow::renderSmartPlaylistBuilderModal() {
    if (!m_showSmartPlaylistBuilderModal) return;

    ImGui::SetNextWindowSize(ImVec2(520.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("New Smart Auto-Playlist (Rules Engine)", &m_showSmartPlaylistBuilderModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Dynamic Smart Playlist (Rules Engine)");
        ImGui::TextDisabled("Smart playlists automatically query tracks based on live matching rules.");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Playlist Name:");
        ImGui::InputText("##splName", m_splName, sizeof(m_splName));
        ImGui::Spacing();

        ImGui::Text("Rule Condition:");
        const char* fieldNames[7] = { "Title", "Artist", "Album", "Genre", "Rating", "Year", "PlayCount" };
        ImGui::Combo("Field##splField", &m_splRuleField, fieldNames, 7);

        const char* compNames[4] = { "Contains", "Equals", "Greater Than (>)", "Less Than (<)" };
        ImGui::Combo("Comparison##splComp", &m_splRuleComparison, compNames, 4);

        ImGui::Text("Value:");
        ImGui::InputText("##splVal", m_splRuleValue, sizeof(m_splRuleValue));

        ImGui::Spacing();
        ImGui::SliderInt("Max Tracks Limit", &m_splLimit, 10, 200);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save Smart Playlist", ImVec2(150.0f, 28.0f))) {
            CustomSmartPlaylist spl;
            spl.name = m_splName;
            spl.matchAll = m_splMatchAll;
            spl.limitTracks = m_splLimit;

            SmartPlaylistRule rule;
            rule.field = static_cast<RuleField>(m_splRuleField);
            rule.comparison = static_cast<RuleComparison>(m_splRuleComparison);
            rule.value = m_splRuleValue;
            spl.rules.push_back(rule);

            m_library.addCustomSmartPlaylist(spl);
            m_library.saveLibrary();
            m_showSmartPlaylistBuilderModal = false;
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Cancel", ImVec2(80.0f, 28.0f))) {
            m_showSmartPlaylistBuilderModal = false;
        }
    }
    ImGui::End();
}

void MainWindow::renderSaveQueueAsPlaylistModal() {
    if (!m_showSaveQueueAsPlaylistModal) return;

    ImGui::OpenPopup("Save Queue as Playlist");
    if (ImGui::BeginPopupModal("Save Queue as Playlist", &m_showSaveQueueAsPlaylistModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save all %zu tracks in the active queue to a playlist:", m_queue.size());
        ImGui::Spacing();

        ImGui::RadioButton("Create New Playlist", &m_saveQueueTargetMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Add to Existing Playlist", &m_saveQueueTargetMode, 1);
        ImGui::Spacing();

        if (m_saveQueueTargetMode == 0) {
            ImGui::Text("Playlist Name:");
            ImGui::InputText("##SaveQueueName", m_saveQueuePlaylistNameBuffer, sizeof(m_saveQueuePlaylistNameBuffer));
        } else {
            ImGui::Text("Select Playlist:");
            const auto& playlists = m_library.getPlaylists();
            std::string previewText = m_saveQueueSelectedExisting.empty() ? "(Choose a playlist)" : m_saveQueueSelectedExisting;
            if (ImGui::BeginCombo("##ExistingPlCombo", previewText.c_str())) {
                for (const auto& pl : playlists) {
                    bool isSelected = (m_saveQueueSelectedExisting == pl.first);
                    if (ImGui::Selectable(pl.first.c_str(), isSelected)) {
                        m_saveQueueSelectedExisting = pl.first;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool canSave = (m_saveQueueTargetMode == 0 && m_saveQueuePlaylistNameBuffer[0] != '\0') ||
                       (m_saveQueueTargetMode == 1 && !m_saveQueueSelectedExisting.empty());

        if (!canSave) ImGui::BeginDisabled();
        if (ImGui::Button("Save Playlist", ImVec2(120.0f, 26.0f))) {
            std::string targetPl;
            if (m_saveQueueTargetMode == 0) {
                targetPl = m_saveQueuePlaylistNameBuffer;
                m_library.createPlaylist(targetPl);
            } else {
                targetPl = m_saveQueueSelectedExisting;
            }

            for (uint64_t tid : m_queue) {
                m_library.addToPlaylist(targetPl, tid);
            }

            m_showSaveQueueAsPlaylistModal = false;
        }
        if (!canSave) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f, 26.0f))) {
            m_showSaveQueueAsPlaylistModal = false;
        }

        ImGui::EndPopup();
    }
}

void MainWindow::initDefaultTabs() {
    m_tabs.clear();
    BrowserTab t1; t1.id = "tab_1"; t1.title = "Music"; t1.viewMode = ViewMode::Tracks; t1.navSource = static_cast<int>(NavSource::AllTracks); t1.isPinned = true;
    m_tabs.push_back(t1);

    BrowserTab t2; t2.id = "tab_2"; t2.title = "Now Playing"; t2.viewMode = ViewMode::NowPlaying; t2.navSource = static_cast<int>(NavSource::AllTracks);
    m_tabs.push_back(t2);

    BrowserTab t3; t3.id = "tab_3"; t3.title = "Podcasts"; t3.viewMode = ViewMode::Tracks; t3.navSource = static_cast<int>(NavSource::Podcasts);
    m_tabs.push_back(t3);

    BrowserTab t4; t4.id = "tab_4"; t4.title = "History"; t4.viewMode = ViewMode::Tracks; t4.navSource = static_cast<int>(NavSource::History);
    m_tabs.push_back(t4);

    BrowserTab t5; t5.id = "tab_5"; t5.title = "Web Radio"; t5.viewMode = ViewMode::Tracks; t5.navSource = static_cast<int>(NavSource::RadioStreams);
    m_tabs.push_back(t5);

    m_activeTabIndex = 0;
}

void MainWindow::openNewTab(const std::string& title, ViewMode vm, int ns) {
    BrowserTab tab;
    tab.id = "tab_" + std::to_string(m_tabs.size() + 1);
    tab.title = title;
    tab.viewMode = vm;
    tab.navSource = ns;
    tab.isPinned = false;
    m_tabs.push_back(tab);
    m_activeTabIndex = m_tabs.size() - 1;
    m_viewMode = vm;
    m_navSource = static_cast<NavSource>(ns);
}

void MainWindow::renderTabsBar() {
    float totalW = ImGui::GetWindowWidth();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));

    ImGui::BeginChild("TabsBar", ImVec2(totalW, 30.0f), false, ImGuiWindowFlags_NoScrollbar);

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        bool isActive = (i == m_activeTabIndex);

        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::CardBackground());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::AccentColor());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.13f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.73f, 0.80f, 1.0f));
        }

        std::string tabLabel = m_tabs[i].title;

        if (ImGui::Button(tabLabel.c_str(), ImVec2(0.0f, 24.0f))) {
            m_activeTabIndex = i;
            m_viewMode = m_tabs[i].viewMode;
            m_navSource = static_cast<NavSource>(m_tabs[i].navSource);
            if (!m_tabs[i].playlistName.empty()) {
                m_selectedPlaylistName = m_tabs[i].playlistName;
            }
        }

        ImGui::PopStyleColor(2);

        // Tab context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem(m_tabs[i].isPinned ? "Unpin Tab" : "Pin Tab")) {
                m_tabs[i].isPinned = !m_tabs[i].isPinned;
            }
            if (!m_tabs[i].isPinned && m_tabs.size() > 1) {
                if (ImGui::MenuItem("Close Tab", "Ctrl+W")) {
                    m_tabs.erase(m_tabs.begin() + i);
                    if (m_activeTabIndex >= m_tabs.size()) m_activeTabIndex = m_tabs.size() - 1;
                    m_viewMode = m_tabs[m_activeTabIndex].viewMode;
                    m_navSource = static_cast<NavSource>(m_tabs[m_activeTabIndex].navSource);
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                }
            }
            ImGui::EndPopup();
        }

        // Premium Vector Close button on tab (if not pinned and more than 1 tab)
        if (!m_tabs[i].isPinned && m_tabs.size() > 1) {
            ImGui::SameLine(0.0f, 2.0f);
            float tabBtnSz = 18.0f;
            ImVec2 closePos = ImGui::GetCursorScreenPos();
            ImVec2 closeCenter(closePos.x + tabBtnSz * 0.5f, closePos.y + tabBtnSz * 0.5f + 3.0f);

            std::string closeBtnId = "##CloseTab_" + std::to_string(i);
            ImGui::SetCursorScreenPos(ImVec2(closeCenter.x - tabBtnSz * 0.5f, closeCenter.y - tabBtnSz * 0.5f));
            bool closeClicked = ImGui::InvisibleButton(closeBtnId.c_str(), ImVec2(tabBtnSz, tabBtnSz));
            bool isTabClsHov = ImGui::IsItemHovered();
            bool isTabClsAct = ImGui::IsItemActive();

            ImDrawList* dlTab = ImGui::GetWindowDrawList();
            if (isTabClsAct) {
                dlTab->AddCircleFilled(closeCenter, 8.5f, IM_COL32(232, 17, 35, 200));
            } else if (isTabClsHov) {
                dlTab->AddCircleFilled(closeCenter, 8.5f, IM_COL32(232, 17, 35, 160));
                ImGui::SetTooltip("Close Tab (Ctrl+W)");
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }

            float arm = 3.2f;
            ImU32 tabIconCol = (isTabClsHov || isTabClsAct)
                ? IM_COL32(255, 255, 255, 255)
                : (isActive ? IM_COL32(110, 115, 125, 220) : IM_COL32(140, 146, 158, 200));
            dlTab->AddLine(ImVec2(closeCenter.x - arm, closeCenter.y - arm),
                           ImVec2(closeCenter.x + arm, closeCenter.y + arm),
                           tabIconCol, 1.3f);
            dlTab->AddLine(ImVec2(closeCenter.x + arm, closeCenter.y - arm),
                           ImVec2(closeCenter.x - arm, closeCenter.y + arm),
                           tabIconCol, 1.3f);

            if (closeClicked) {
                m_tabs.erase(m_tabs.begin() + i);
                if (m_activeTabIndex >= m_tabs.size()) m_activeTabIndex = m_tabs.size() - 1;
                m_viewMode = m_tabs[m_activeTabIndex].viewMode;
                m_navSource = static_cast<NavSource>(m_tabs[m_activeTabIndex].navSource);
                ImGui::PopID();
                break;
            }
        }

        ImGui::SameLine(0.0f, 6.0f);
        ImGui::PopID();
    }

    // '+' Button to add a new tab
    if (ImGui::Button("+##NewTab", ImVec2(24.0f, 24.0f))) {
        openNewTab("Music", ViewMode::Tracks, static_cast<int>(NavSource::AllTracks));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open new browser tab");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void MainWindow::renderAZJumpBar() {
    float horizPad = 14.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizPad);

    ImVec2 startPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));

    const char chars[] = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < 27; ++i) {
        char ch = chars[i];
        bool isSel = (m_activeJumpLetter == ch);
        if (isSel) {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::AccentColor());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::AccentHoverColor());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::AccentActiveColor());
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.13f, 0.17f, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.28f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.27f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.68f, 0.72f, 0.80f, 1.0f));
        }

        char btnLabel[2] = { ch, '\0' };
        if (ImGui::SmallButton(btnLabel)) {
            m_activeJumpLetter = (m_activeJumpLetter == ch) ? '\0' : ch;
        }

        ImGui::PopStyleColor(4);
        ImGui::SameLine();
    }

    bool isAll = (m_activeJumpLetter == '\0');
    if (isAll) {
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::AccentColor());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::AccentHoverColor());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::AccentActiveColor());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.13f, 0.17f, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.28f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.27f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.68f, 0.72f, 0.80f, 1.0f));
    }
    if (ImGui::SmallButton("All")) {
        m_activeJumpLetter = '\0';
    }
    ImGui::PopStyleColor(4);

    ImVec2 endPos = ImGui::GetItemRectMax();
    ImU32 containerBorder = IM_COL32(36, 40, 52, 180);
    dl->AddRect(ImVec2(startPos.x - 3.0f, startPos.y - 2.0f), ImVec2(endPos.x + 3.0f, endPos.y + 2.0f), containerBorder, 6.0f);

    ImGui::PopStyleVar(3);
    ImGui::Spacing();
}

void MainWindow::renderColumnBrowser(float width, float height, const std::vector<const Track*>& sourceTracks, std::vector<const Track*>& outFilteredTracks) {
    // Collect unique genres, artists, albums from source tracks
    std::map<std::string, int> genres;
    std::map<std::string, int> artists;
    std::map<std::string, int> albumMap;

    for (const Track* t : sourceTracks) {
        std::string g = t->genre.empty() ? "(No Genre)" : t->genre;
        std::string a = t->getDisplayArtist();
        std::string al = t->getDisplayAlbum();
        genres[g]++;
        artists[a]++;
        albumMap[al]++;
    }

    // Filter artists by selected genre
    std::map<std::string, int> filteredArtists;
    std::map<std::string, int> filteredAlbums;

    std::vector<const Track*> genreFiltered;
    for (const Track* t : sourceTracks) {
        std::string g = t->genre.empty() ? "(No Genre)" : t->genre;
        if (m_cbSelectedGenre.empty() || g == m_cbSelectedGenre) {
            genreFiltered.push_back(t);
            filteredArtists[t->getDisplayArtist()]++;
        }
    }

    std::vector<const Track*> artistFiltered;
    for (const Track* t : genreFiltered) {
        if (m_cbSelectedArtist.empty() || t->getDisplayArtist() == m_cbSelectedArtist) {
            artistFiltered.push_back(t);
            filteredAlbums[t->getDisplayAlbum()]++;
        }
    }

    // Final album filter
    outFilteredTracks.clear();
    for (const Track* t : artistFiltered) {
        if (m_cbSelectedAlbum.empty() || t->getDisplayAlbum() == m_cbSelectedAlbum) {
            outFilteredTracks.push_back(t);
        }
    }

    // Render 3-column browser
    float colW = width / 3.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(Theme::PanelBackground().x + 0.02f, Theme::PanelBackground().y + 0.02f, Theme::PanelBackground().z + 0.03f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));

    // Column 1: Genre
    ImGui::BeginChild("##CB_Genre", ImVec2(colW - 4.0f, height), true);
    ImGui::TextColored(Theme::AccentColor(), "Genre");
    ImGui::Separator();
    {
        bool isAll = m_cbSelectedGenre.empty();
        char allLabel[32];
        snprintf(allLabel, sizeof(allLabel), "All (%zu)", genres.size());
        if (ImGui::Selectable(allLabel, isAll)) {
            m_cbSelectedGenre.clear();
            m_cbSelectedArtist.clear();
            m_cbSelectedAlbum.clear();
        }
        for (const auto& [name, count] : genres) {
            char label[128];
            snprintf(label, sizeof(label), "%s (%d)", name.c_str(), count);
            bool sel = (m_cbSelectedGenre == name);
            if (ImGui::Selectable(label, sel)) {
                m_cbSelectedGenre = sel ? "" : name;
                m_cbSelectedArtist.clear();
                m_cbSelectedAlbum.clear();
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 4.0f);

    // Column 2: Artist
    ImGui::BeginChild("##CB_Artist", ImVec2(colW - 4.0f, height), true);
    ImGui::TextColored(Theme::AccentColor(), "Artist");
    ImGui::Separator();
    {
        bool isAll = m_cbSelectedArtist.empty();
        char allLabel[32];
        snprintf(allLabel, sizeof(allLabel), "All (%zu)", filteredArtists.size());
        if (ImGui::Selectable(allLabel, isAll)) {
            m_cbSelectedArtist.clear();
            m_cbSelectedAlbum.clear();
        }
        for (const auto& [name, count] : filteredArtists) {
            char label[128];
            snprintf(label, sizeof(label), "%s (%d)", name.c_str(), count);
            bool sel = (m_cbSelectedArtist == name);
            if (ImGui::Selectable(label, sel)) {
                m_cbSelectedArtist = sel ? "" : name;
                m_cbSelectedAlbum.clear();
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 4.0f);

    // Column 3: Album
    ImGui::BeginChild("##CB_Album", ImVec2(colW - 4.0f, height), true);
    ImGui::TextColored(Theme::AccentColor(), "Album");
    ImGui::Separator();
    {
        bool isAll = m_cbSelectedAlbum.empty();
        char allLabel[32];
        snprintf(allLabel, sizeof(allLabel), "All (%zu)", filteredAlbums.size());
        if (ImGui::Selectable(allLabel, isAll)) {
            m_cbSelectedAlbum.clear();
        }
        for (const auto& [name, count] : filteredAlbums) {
            char label[128];
            snprintf(label, sizeof(label), "%s (%d)", name.c_str(), count);
            bool sel = (m_cbSelectedAlbum == name);
            if (ImGui::Selectable(label, sel)) {
                m_cbSelectedAlbum = sel ? "" : name;
            }
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void MainWindow::renderMultiVisualizer(ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    ImVec2 p0 = pos;
    ImVec2 p1(pos.x + size.x, pos.y + size.y);

    ImGui::SetCursorScreenPos(pos);
    if (ImGui::InvisibleButton("##MultiVisClick", size)) {
        int nextMode = (static_cast<int>(m_visualizerMode) + 1) % 4;
        m_visualizerMode = static_cast<VisualizerMode>(nextMode);
    }
    if (ImGui::IsItemHovered()) {
        const char* modeNames[4] = { "16-Band Spectrum", "32-Band Spectrum", "Dual Analog VU Meter", "Oscilloscope Waveform" };
        ImGui::SetTooltip("Visualizer: %s (Click to toggle mode)", modeNames[static_cast<int>(m_visualizerMode)]);
    }

    if (m_visualizerMode == VisualizerMode::Spectrum16 || m_visualizerMode == VisualizerMode::Spectrum32) {
        size_t numBars = (m_visualizerMode == VisualizerMode::Spectrum16) ? 16 : 32;
        std::vector<float> bars;
        m_audio.getSpectrum(bars, numBars);
        float barW = (size.x / static_cast<float>(numBars)) - 1.0f;
        for (size_t b = 0; b < bars.size(); ++b) {
            float val = std::clamp(bars[b], 0.05f, 1.0f);
            float h = val * size.y;
            ImVec2 b0(p0.x + b * (barW + 1.0f), p1.y - h);
            ImVec2 b1(b0.x + barW, p1.y);
            ImU32 barCol = (val > 0.65f) ? IM_COL32(245, 185, 25, 255) : IM_COL32(215, 155, 15, 200);
            dl->AddRectFilled(b0, b1, barCol, 1.0f);
        }
    } else if (m_visualizerMode == VisualizerMode::DualVU) {
        float vuL = 0.0f, vuR = 0.0f;
        m_audio.getVUMeters(vuL, vuR);
        float meterW = (size.x - 4.0f) * 0.5f;

        // Left meter
        dl->AddRectFilled(p0, ImVec2(p0.x + meterW, p1.y), IM_COL32(20, 22, 28, 255), 2.0f);
        dl->AddRect(p0, ImVec2(p0.x + meterW, p1.y), IM_COL32(50, 55, 65, 255), 2.0f);
        float pivotLx = p0.x + meterW * 0.5f;
        float pivotLy = p1.y + 6.0f;
        float angleL = -0.6f + (std::clamp(vuL, 0.0f, 1.0f) * 1.2f);
        float needleL = size.y * 1.1f;
        dl->AddLine(ImVec2(pivotLx, pivotLy), ImVec2(pivotLx + std::sin(angleL) * needleL, pivotLy - std::cos(angleL) * needleL), IM_COL32(235, 168, 15, 255), 1.5f);

        // Right meter
        ImVec2 p0R(p0.x + meterW + 4.0f, p0.y);
        ImVec2 p1R(p1.x, p1.y);
        dl->AddRectFilled(p0R, p1R, IM_COL32(20, 22, 28, 255), 2.0f);
        dl->AddRect(p0R, p1R, IM_COL32(50, 55, 65, 255), 2.0f);
        float pivotRx = p0R.x + meterW * 0.5f;
        float pivotRy = p1.y + 6.0f;
        float angleR = -0.6f + (std::clamp(vuR, 0.0f, 1.0f) * 1.2f);
        float needleR = size.y * 1.1f;
        dl->AddLine(ImVec2(pivotRx, pivotRy), ImVec2(pivotRx + std::sin(angleR) * needleR, pivotRy - std::cos(angleR) * needleR), IM_COL32(235, 168, 15, 255), 1.5f);
    } else if (m_visualizerMode == VisualizerMode::Oscilloscope) {
        std::vector<float> samples;
        m_audio.getOscilloscope(samples, 64);
        dl->AddRectFilled(p0, p1, IM_COL32(16, 20, 18, 255), 2.0f);
        float midY = p0.y + size.y * 0.5f;
        dl->AddLine(ImVec2(p0.x, midY), ImVec2(p1.x, midY), IM_COL32(30, 45, 35, 255), 1.0f);
        if (samples.size() >= 2) {
            float stepX = size.x / static_cast<float>(samples.size() - 1);
            for (size_t i = 0; i + 1 < samples.size(); ++i) {
                float x0 = p0.x + i * stepX;
                float y0 = midY - samples[i] * (size.y * 0.45f);
                float x1 = p0.x + (i + 1) * stepX;
                float y1 = midY - samples[i + 1] * (size.y * 0.45f);
                dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(75, 235, 130, 230), 1.5f);
            }
        }
    }
}

void MainWindow::showTrackInfoModal(uint64_t trackId) {
    m_inspectTrackId = (trackId > 0) ? trackId : (m_selectedTrackId > 0 ? m_selectedTrackId : m_currentTrackId);
    m_showTrackInfoModal = true;
}

void MainWindow::renderTrackInfoModal() {
    if (!m_showTrackInfoModal) return;

    ImGui::SetNextWindowSize(ImVec2(520.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Technical Track Information", &m_showTrackInfoModal)) {
        const Track* t = m_library.getTrackById(m_inspectTrackId);
        if (!t) {
            ImGui::TextDisabled("No track selected for inspection.");
            if (ImGui::Button("Close")) m_showTrackInfoModal = false;
            ImGui::End();
            return;
        }

        ImGui::TextColored(Theme::AccentColor(), "%s", t->getDisplayTitle().c_str());
        ImGui::Text("Artist: %s | Album: %s", t->getDisplayArtist().c_str(), t->getDisplayAlbum().c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Columns(2, "TrackInfoCols", false);
        ImGui::SetColumnWidth(0, 160.0f);

        ImGui::TextDisabled("Format / Codec:");
        ImGui::NextColumn();
        ImGui::Text("%s", t->codec.empty() ? (fs::path(t->filePath).extension().string().c_str()) : t->codec.c_str());
        ImGui::NextColumn();

        ImGui::TextDisabled("Bitrate:");
        ImGui::NextColumn();
        ImGui::Text("%d kbps", t->bitrate);
        ImGui::NextColumn();

        ImGui::TextDisabled("Sample Rate:");
        ImGui::NextColumn();
        ImGui::Text("%d Hz", t->sampleRate);
        ImGui::NextColumn();

        ImGui::TextDisabled("Channels:");
        ImGui::NextColumn();
        ImGui::Text("%d (%s)", t->channels, t->channels == 1 ? "Mono" : (t->channels == 2 ? "Stereo" : "Multi-channel"));
        ImGui::NextColumn();

        ImGui::TextDisabled("Bit Depth:");
        ImGui::NextColumn();
        ImGui::Text("%d-bit", t->bitsPerSample);
        ImGui::NextColumn();

        ImGui::TextDisabled("Duration:");
        ImGui::NextColumn();
        ImGui::Text("%s", t->formatDuration().c_str());
        ImGui::NextColumn();

        ImGui::TextDisabled("File Size:");
        ImGui::NextColumn();
        double mb = static_cast<double>(t->fileSizeBytes) / (1024.0 * 1024.0);
        ImGui::Text("%.2f MB", mb);
        ImGui::NextColumn();

        ImGui::TextDisabled("ReplayGain Gain:");
        ImGui::NextColumn();
        ImGui::Text("%+.2f dB", t->replayGainTrackGain);
        ImGui::NextColumn();

        ImGui::TextDisabled("ReplayGain Peak:");
        ImGui::NextColumn();
        ImGui::Text("%.4f", t->replayGainTrackPeak);
        ImGui::NextColumn();

        ImGui::TextDisabled("Play Count / Rating:");
        ImGui::NextColumn();
        ImGui::Text("%d plays | %d stars", t->playCount, t->rating);
        ImGui::NextColumn();

        int artW = 0, artH = 0;
        ImTextureID artTex = m_textures.getTrackArtwork(*t, &artW, &artH);
        ImGui::TextDisabled("Cover Artwork:");
        ImGui::NextColumn();
        if (artTex) {
            std::string artDesc = std::to_string(artW) + " x " + std::to_string(artH) + " px";
            if (artW == artH) artDesc += " (1:1 Square)";
            else if (artW > artH) artDesc += " (Landscape)";
            else artDesc += " (Portrait)";
            ImGui::Text("%s", artDesc.c_str());
            ImVec2 fitArt = TextureManager::getAspectFitSize(artW, artH, 80.0f, 80.0f);
            ImGui::Image(artTex, fitArt);
        } else {
            ImGui::TextDisabled("No embedded artwork found");
        }
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("Path:");
        ImGui::TextWrapped("%s", t->filePath.c_str());

        ImGui::Spacing();
        if (ImGui::Button("Locate in File Manager", ImVec2(160.0f, 26.0f))) {
            Platform::openInFileManager(t->filePath);
        }
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.18f, 0.18f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("Delete Track...", ImVec2(120.0f, 26.0f))) {
            showDeleteTrackModal(t->id, false);
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Close", ImVec2(80.0f, 26.0f))) {
            m_showTrackInfoModal = false;
        }
    }
    ImGui::End();
}

void MainWindow::showVolumeScannerModal() {
    m_showVolumeScannerModal = true;
}

void MainWindow::renderVolumeScannerModal() {
    if (!m_showVolumeScannerModal) return;

    ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("ReplayGain Volume Analyzer", &m_showVolumeScannerModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Multithreaded Volume & ReplayGain Analyzer");
        ImGui::TextDisabled("Scans audio collections and computes loudness to target 89 dB SPL (-18 LUFS).");
        ImGui::Separator();
        ImGui::Spacing();

        if (m_volumeScanningActive) {
            ImGui::Text("Analyzing track volume: %zu / %zu", m_volumeScanProgress.processedCount, m_volumeScanProgress.totalCount);
            ImGui::ProgressBar(m_volumeScanProgress.progress, ImVec2(-1.0f, 24.0f), m_volumeScanProgress.currentTrackTitle.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Cancel Analysis", ImVec2(130.0f, 26.0f))) {
                VolumeScanner::cancel();
            }
        } else {
            size_t count = m_selectedTrackIds.empty() ? (m_selectedTrackId > 0 ? 1 : m_library.getTracks().size()) : m_selectedTrackIds.size();
            ImGui::Text("Ready to analyze %zu track(s).", count);
            ImGui::Spacing();

            if (!m_volumeScanResults.empty()) {
                ImGui::Text("Results of last scan (%zu tracks):", m_volumeScanResults.size());
                if (ImGui::BeginTable("VolResults", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 180.0f))) {
                    ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                    ImGui::TableSetupColumn("RMS", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                    ImGui::TableSetupColumn("Peak", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                    ImGui::TableSetupColumn("ReplayGain", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& res : m_volumeScanResults) {
                        const Track* tr = m_library.getTrackById(res.trackId);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", tr ? tr->getDisplayTitle().c_str() : res.filePath.c_str());

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.4f", res.rms);

                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.4f", res.peak);

                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%+.2f dB", res.replayGainDb);
                    }
                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }

            if (ImGui::Button("Start Analysis", ImVec2(130.0f, 28.0f))) {
                std::vector<Track> toScan;
                if (!m_selectedTrackIds.empty()) {
                    for (uint64_t tid : m_selectedTrackIds) {
                        const Track* tr = m_library.getTrackById(tid);
                        if (tr && !tr->isStream) toScan.push_back(*tr);
                    }
                } else if (m_selectedTrackId > 0) {
                    const Track* tr = m_library.getTrackById(m_selectedTrackId);
                    if (tr && !tr->isStream) toScan.push_back(*tr);
                } else {
                    for (const auto& tr : m_library.getTracks()) {
                        if (!tr.isStream) toScan.push_back(tr);
                    }
                }

                m_volumeScanningActive = true;
                VolumeScanner::startBatchAnalysisAsync(
                    toScan,
                    [this](const VolumeScanProgress& p) {
                        m_volumeScanProgress = p;
                    },
                    [this](const std::vector<VolumeScanResult>& res) {
                        m_volumeScanningActive = false;
                        m_volumeScanResults = res;
                        for (const auto& r : res) {
                            if (!r.success) continue;
                            Track* tr = m_library.getTrackById(r.trackId);
                            if (tr) {
                                tr->replayGainTrackGain = r.replayGainDb;
                                tr->replayGainTrackPeak = r.peak;
                            }
                        }
                        m_library.saveLibrary();
                    }
                );
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Close", ImVec2(80.0f, 28.0f))) {
                m_showVolumeScannerModal = false;
            }
        }
    }
    ImGui::End();
}

void MainWindow::renderScrobblerModal() {
    if (!m_showScrobblerModal) return;

    ImGui::SetNextWindowSize(ImVec2(600.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Last.fm & Playback Scrobbler", &m_showScrobblerModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Last.fm / Offline Playback Scrobbler");
        ImGui::TextDisabled("Tracks are scrobbled after 50%% or 4 minutes of continuous playback.");
        ImGui::Separator();
        ImGui::Spacing();

        bool enabled = m_scrobbler.isEnabled();
        if (ImGui::Checkbox("Enable Scrobbler", &enabled)) {
            m_scrobbler.setEnabled(enabled);
        }

        ImGui::SameLine(180.0f);
        ImGui::TextDisabled("Total Scrobbles: %zu", m_scrobbler.getScrobbleCount());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(Theme::AccentColor(), "LAST.FM ACCOUNT CONNECTION");

        static char s_lfmUser[64] = {0};
        static char s_lfmSk[64] = {0};
        static bool s_lfmLoaded = false;
        if (!s_lfmLoaded) {
            snprintf(s_lfmUser, sizeof(s_lfmUser), "%s", m_scrobbler.getLastFmUsername().c_str());
            snprintf(s_lfmSk, sizeof(s_lfmSk), "%s", m_scrobbler.getLastFmSessionKey().c_str());
            s_lfmLoaded = true;
        }

        if (m_scrobbler.hasLastFmAuth()) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Connected as: %s", m_scrobbler.getLastFmUsername().c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
            if (ImGui::SmallButton("Disconnect")) {
                m_scrobbler.setLastFmCredentials("", "");
                s_lfmUser[0] = '\0';
                s_lfmSk[0] = '\0';
            }
        } else {
            ImGui::Columns(2, "lfmAuthCols", false);
            ImGui::SetColumnWidth(0, 260.0f);
            ImGui::Text("Username:");
            ImGui::InputText("##lfmUser", s_lfmUser, sizeof(s_lfmUser));
            ImGui::NextColumn();
            ImGui::Text("Session Key:");
            ImGui::InputText("##lfmSk", s_lfmSk, sizeof(s_lfmSk), ImGuiInputTextFlags_Password);
            ImGui::Columns(1);

            if (ImGui::Button("Link Last.fm Account", ImVec2(160.0f, 24.0f))) {
                m_scrobbler.setLastFmCredentials(s_lfmUser, s_lfmSk);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Enables real-time Scrobble & Now Playing)");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        std::vector<ScrobbleRecord> hist = m_scrobbler.getHistory();
        if (hist.empty()) {
            ImGui::TextDisabled("No scrobbles recorded yet.");
        } else {
            if (ImGui::BeginTable("ScrobTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 180.0f))) {
                ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 0.40f);
                ImGui::TableSetupColumn("Artist", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                ImGui::TableSetupColumn("Date & Time", ImGuiTableColumnFlags_WidthStretch, 0.20f);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableHeadersRow();

                for (const auto& r : hist) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", r.title.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("%s", r.artist.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled("%s", r.timestampStr.c_str());

                    ImGui::TableSetColumnIndex(3);
                    if (r.submitted) {
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Scrobbled");
                    } else {
                        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Pending");
                    }
                }
                ImGui::EndTable();
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Clear Scrobble Log", ImVec2(140.0f, 26.0f))) {
            m_scrobbler.clearHistory();
        }
        if (m_scrobbler.hasLastFmAuth()) {
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("Retry Pending Scrobbles", ImVec2(170.0f, 26.0f))) {
                m_scrobbler.retryUnsubmitted();
            }
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Close", ImVec2(80.0f, 26.0f))) {
            m_showScrobblerModal = false;
        }
    }
    ImGui::End();
}

void MainWindow::renderPanelsConfigModal() {
    if (!m_showPanelsConfigModal) return;

    ImGui::SetNextWindowSize(ImVec2(480.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Arrange Panels Configuration", &m_showPanelsConfigModal)) {
        ImGui::TextColored(Theme::AccentColor(), "Arrange Panels & Components");
        ImGui::TextDisabled("Customize visibility and behavior of desktop layout regions.");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("Show Left Navigator (Library Tree)", &m_showLeftNavigator);
        ImGui::Checkbox("Show Right Sidebar (Playing Tracks & Lyrics)", &m_showRightSidebar);
        if (m_showRightSidebar) {
            ImGui::Indent(20.0f);
            ImGui::Checkbox("Show Bottom Track Information Panel", &m_showRightTrackInfo);
            if (m_showRightTrackInfo) {
                ImGui::SameLine(0.0f, 12.0f);
                int rbMode = static_cast<int>(m_rightBottomView);
                const char* rbNames[3] = { "Artwork & Info", "Album Cover", "Track Info Only" };
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::Combo("##RightBottomMode", &rbMode, rbNames, 3)) {
                    m_rightBottomView = static_cast<RightBottomView>(rbMode);
                    savePreferences();
                }
            }
            ImGui::Unindent(20.0f);
        }
        ImGui::Checkbox("Show A-Z Alphabetical Jumpbar", &m_showJumpbar);
        ImGui::Checkbox("Show Waveform Progress Bar (Wavebar)", &m_useWavebar);
        ImGui::Checkbox("Show Real-Time Audio Visualizer", &m_showVisualizer);
        ImGui::Checkbox("Show Bottom Status Bar", &m_showStatusBar);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("Lock All Panels & Splitters (Prevent Resizing)", &m_lockPanels);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Default Visualizer Mode:");
        int vm = static_cast<int>(m_visualizerMode);
        const char* vmNames[4] = { "16-Band Spectrum", "32-Band Spectrum", "Dual Analog VU Meter", "Oscilloscope Waveform" };
        if (ImGui::Combo("##VisDefMode", &vm, vmNames, 4)) {
            m_visualizerMode = static_cast<VisualizerMode>(vm);
        }

        ImGui::Spacing();
        if (ImGui::Button("Done", ImVec2(100.0f, 28.0f))) {
            m_showPanelsConfigModal = false;
            savePreferences();
        }
    }
    ImGui::End();
}

void MainWindow::openAudioDriverModal() {
    m_showAudioDriverModal = true;
    m_uiDriverType = static_cast<int>(m_audio.getAudioDriverType());
    m_uiDeviceIndex = m_audio.getSelectedDeviceIndex();
    m_uiBufferLatencyMs = m_audio.getBufferLatencyMs();
    m_uiReleaseDriverWhenPaused = m_audio.isReleaseDriverWhenPaused();
    m_uiDeviceList = m_audio.getAudioDevices(m_audio.getAudioDriverType());
    m_uiDriverApplyStatus = "";
}

void MainWindow::renderAudioDriverModal() {
    if (!m_showAudioDriverModal) return;

    ImGui::SetNextWindowSize(ImVec2(570.0f, 620.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Audio Drivers & Hardware Output Preferences", &m_showAudioDriverModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(Theme::AccentColor(), "Windows Sound Drivers & Hardware Output");
        ImGui::TextDisabled("Select audio backend architecture, direct hardware endpoints, and hardware buffer latency.");
        ImGui::Separator();
        ImGui::Spacing();

        // 1. Audio Driver Architecture
        ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Sound Driver Architecture:");
        int prevDriver = m_uiDriverType;

        bool rWasapiExcl = (m_uiDriverType == 1);
        if (ImGui::RadioButton("WASAPI (Exclusive Mode) - Bit-Perfect", rWasapiExcl)) {
            m_uiDriverType = 1;
        }
        ImGui::Indent(24.0f);
        ImGui::TextDisabled("Direct exclusive hardware stream bypassing Windows mixer. Bit-perfect DAC output.");
        ImGui::Unindent(24.0f);
        ImGui::Spacing();

        bool rWasapiShared = (m_uiDriverType == 0);
        if (ImGui::RadioButton("WASAPI (Shared Mode) - Windows Audio Session", rWasapiShared)) {
            m_uiDriverType = 0;
        }
        ImGui::Indent(24.0f);
        ImGui::TextDisabled("Standard Windows audio engine. Allows simultaneous multi-app sound mixing.");
        ImGui::Unindent(24.0f);
        ImGui::Spacing();

        bool rDSound = (m_uiDriverType == 2);
        if (ImGui::RadioButton("DirectSound - Legacy Driver", rDSound)) {
            m_uiDriverType = 2;
        }
        ImGui::Indent(24.0f);
        ImGui::TextDisabled("DirectSound compatibility layer for vintage sound cards and secondary audio routing.");
        ImGui::Unindent(24.0f);
        ImGui::Spacing();

        // Re-enumerate if driver changed
        if (m_uiDriverType != prevDriver || m_uiDeviceList.empty()) {
            AudioDriverType dt = static_cast<AudioDriverType>(m_uiDriverType);
            m_uiDeviceList = m_audio.getAudioDevices(dt);
            m_uiDeviceIndex = -1;
        }

        ImGui::Separator();
        ImGui::Spacing();

        // 2. Playback Device Selection
        ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Audio Output Device:");
        std::string previewStr = "Default System Device";
        if (m_uiDeviceIndex >= 0 && static_cast<size_t>(m_uiDeviceIndex) < m_uiDeviceList.size()) {
            previewStr = m_uiDeviceList[m_uiDeviceIndex].name;
            if (m_uiDeviceList[m_uiDeviceIndex].isDefault) previewStr += " [Default]";
        }

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##AudioOutputDeviceCombo", previewStr.c_str())) {
            bool isDefSelected = (m_uiDeviceIndex == -1);
            if (ImGui::Selectable("Default System Device", isDefSelected)) {
                m_uiDeviceIndex = -1;
            }
            for (size_t i = 0; i < m_uiDeviceList.size(); ++i) {
                const auto& d = m_uiDeviceList[i];
                std::string itemLabel = d.name;
                if (d.isDefault) itemLabel += " [System Default]";
                bool isSelected = (m_uiDeviceIndex == static_cast<int>(i));
                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                    m_uiDeviceIndex = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Hardware Buffer Latency
        ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Hardware Buffer Latency:");
        ImGui::SetNextItemWidth(280.0f);
        ImGui::SliderInt("##BufferLatency", &m_uiBufferLatencyMs, 10, 200, "%d ms");
        ImGui::SameLine();
        if (m_uiBufferLatencyMs <= 25) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "(Ultra Low Latency)");
        } else if (m_uiBufferLatencyMs <= 60) {
            ImGui::TextColored(ImVec4(0.4f, 0.75f, 0.95f, 1.0f), "(Optimal Balance)");
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f), "(High Stability Buffer)");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 4. Audiophile Hardware & Device Management
        ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Audiophile Hardware & Device Management:");
        if (ImGui::Checkbox("Release audio driver when paused or stopped", &m_uiReleaseDriverWhenPaused)) {
            m_audio.setReleaseDriverWhenPaused(m_uiReleaseDriverWhenPaused);
            m_audio.saveAudioConfig();
        }
        ImGui::Indent(24.0f);
        ImGui::TextDisabled("Releases WASAPI Exclusive and hardware audio endpoints when playback is paused or stopped,");
        ImGui::TextDisabled("allowing other applications (browsers, DAWs, games) to use the sound card / DAC freely.");
        ImGui::Unindent(24.0f);

        ImGui::Spacing();

        // 5. Current Hardware Status Card
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        ImGui::BeginChild("DriverStatusCard", ImVec2(0.0f, 76.0f), true);
        {
            ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.80f, 1.0f), "Active Hardware Output:");
            bool isExcl = m_audio.isExclusiveActive();
            if (isExcl) {
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.50f, 1.0f), "● WASAPI Exclusive (Bit-Perfect Direct Hardware Output)");
            } else if (m_audio.getAudioDriverType() == AudioDriverType::DirectSound) {
                ImGui::TextColored(ImVec4(0.40f, 0.70f, 0.95f, 1.0f), "● DirectSound (Standard Windows Driver)");
            } else {
                ImGui::TextColored(ImVec4(0.40f, 0.70f, 0.95f, 1.0f), "● WASAPI Shared (System Audio Session Mixer)");
            }
            ImGui::Text("Device: %s", m_audio.getActiveDeviceName().c_str());
            ImGui::TextDisabled("Status: %s", m_audio.getDriverStatusString().c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        if (!m_uiDriverApplyStatus.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", m_uiDriverApplyStatus.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 6. Actions
        if (ImGui::Button("Apply & Switch Output", ImVec2(180.0f, 30.0f))) {
            AudioDriverType dt = static_cast<AudioDriverType>(m_uiDriverType);
            std::string selectedDevName;
            if (m_uiDeviceIndex >= 0 && static_cast<size_t>(m_uiDeviceIndex) < m_uiDeviceList.size()) {
                selectedDevName = m_uiDeviceList[m_uiDeviceIndex].name;
            }
            bool ok = m_audio.reinitAudioDevice(dt, m_uiDeviceIndex, m_uiBufferLatencyMs, selectedDevName);
            if (ok) {
                if (m_audio.isReleaseDriverWhenPaused() && (!m_audio.isPlaying() || m_audio.isPaused())) {
                    m_audio.releaseDevice();
                }
                if (m_audio.isExclusiveActive()) {
                    m_uiDriverApplyStatus = "Connected in WASAPI Exclusive mode (Bit-Perfect direct hardware lock)!";
                } else if (dt == AudioDriverType::WASAPI_Exclusive) {
                    m_uiDriverApplyStatus = "Exclusive mode unavailable on this device; fell back safely to WASAPI Shared.";
                } else {
                    m_uiDriverApplyStatus = "Audio output device connected successfully!";
                }
            } else {
                m_uiDriverApplyStatus = "Failed to switch audio device. Check device settings.";
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(100.0f, 30.0f))) {
            m_showAudioDriverModal = false;
        }
    }
    ImGui::End();
}

void MainWindow::showPreferencesModal(int initialTab) {
    m_preferencesActiveTab = initialTab;
    m_showPreferencesModal = true;
}

void MainWindow::renderPreferencesModal() {
    if (!m_showPreferencesModal) return;

    ImGui::SetNextWindowSize(ImVec2(640.0f, 540.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Preferences & Display Settings##PrefsModal", &m_showPreferencesModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(Theme::AccentColor(), "SlothPlayer Preferences");
        ImGui::TextDisabled("Customize artwork sizing, playback rules, dislike behaviors, themes, and workspace layout.");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("PreferencesTabBar", ImGuiTabBarFlags_None)) {
            ImGuiTabItemFlags tabArtFlags = (m_preferencesActiveTab == 0) ? ImGuiTabItemFlags_SetSelected : 0;
            ImGuiTabItemFlags tabPlayFlags = (m_preferencesActiveTab == 1) ? ImGuiTabItemFlags_SetSelected : 0;
            ImGuiTabItemFlags tabThemeFlags = (m_preferencesActiveTab == 2) ? ImGuiTabItemFlags_SetSelected : 0;
            ImGuiTabItemFlags tabPanelsFlags = (m_preferencesActiveTab == 3) ? ImGuiTabItemFlags_SetSelected : 0;
            m_preferencesActiveTab = -1;

            // Tab 1: Artwork & Display
            if (ImGui::BeginTabItem("Artwork & Display##PrefTab", nullptr, tabArtFlags)) {
                ImGui::Spacing();

                // 1. Album Grid Cards
                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Album Grid Card Size (Albums & Artists Views):");
                ImGui::TextDisabled("Adjust the target dimensions for album cover cards in grid views.");
                ImGui::SetNextItemWidth(320.0f);
                if (ImGui::SliderFloat("##AlbumCardSizeSlider", &m_albumCardSize, 80.0f, 260.0f, "%.0f px")) {
                    savePreferences();
                }
                ImGui::SameLine(0.0f, 8.0f);
                if (ImGui::Button("Small##ArtSz", ImVec2(55.0f, 22.0f))) { m_albumCardSize = 110.0f; savePreferences(); }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button("Medium##ArtSz", ImVec2(60.0f, 22.0f))) { m_albumCardSize = 150.0f; savePreferences(); }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button("Large##ArtSz", ImVec2(55.0f, 22.0f))) { m_albumCardSize = 180.0f; savePreferences(); }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button("XL##ArtSz", ImVec2(45.0f, 22.0f))) { m_albumCardSize = 230.0f; savePreferences(); }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // 2. Right Sidebar Bottom Panel View & Sizing
                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Right Sidebar Bottom Panel Display:");
                ImGui::TextDisabled("Select what is shown in the bottom section of the right sidebar.");

                int curRb = static_cast<int>(m_rightBottomView);
                if (ImGui::RadioButton("Artwork & Track Information (Both Together)", curRb == 0)) {
                    m_rightBottomView = RightBottomView::ArtworkAndInfo;
                    savePreferences();
                }
                ImGui::Indent(22.0f);
                ImGui::TextDisabled("Displays the album cover together with track typography and audio codec specs.");
                ImGui::Unindent(22.0f);
                ImGui::Spacing();

                if (ImGui::RadioButton("Album Covers Only (Large Square Cover)", curRb == 1)) {
                    m_rightBottomView = RightBottomView::AlbumCover;
                    savePreferences();
                }
                ImGui::Indent(22.0f);
                ImGui::TextDisabled("Fills the bottom panel with the high-resolution album cover.");
                ImGui::Unindent(22.0f);
                ImGui::Spacing();

                if (ImGui::RadioButton("Track Information Only (Typographic Specs)", curRb == 2)) {
                    m_rightBottomView = RightBottomView::TrackInfo;
                    savePreferences();
                }
                ImGui::Indent(22.0f);
                ImGui::TextDisabled("Displays title, artist, album, year, format, bit depth, sample rate, bitrate, and duration.");
                ImGui::Unindent(22.0f);
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Right Sidebar Artwork Max Size:");
                ImGui::SetNextItemWidth(320.0f);
                if (ImGui::SliderFloat("##RightArtSizeSlider", &m_rightPanelArtSize, 80.0f, 300.0f, "%.0f px")) {
                    savePreferences();
                }
                ImGui::SameLine(0.0f, 8.0f);
                if (ImGui::Button("Small##RbArt", ImVec2(55.0f, 22.0f))) { m_rightPanelArtSize = 120.0f; savePreferences(); }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button("Default##RbArt", ImVec2(60.0f, 22.0f))) { m_rightPanelArtSize = 180.0f; savePreferences(); }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button("Large##RbArt", ImVec2(55.0f, 22.0f))) { m_rightPanelArtSize = 220.0f; savePreferences(); }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // 3. Expanded Album Card Styling
                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Expanded Album Card Styling:");
                bool lightCard = Theme::useLightExpandedCard();
                if (ImGui::Checkbox("Use Light Background for Expanded Album Card", &lightCard)) {
                    Theme::setUseLightExpandedCard(lightCard);
                    savePreferences();
                }

                ImGui::EndTabItem();
            }

            // Tab 2: Playback & Dislikes
            if (ImGui::BeginTabItem("Playback & Dislikes##PrefTab", nullptr, tabPlayFlags)) {
                ImGui::Spacing();

                ImGui::TextColored(Theme::AccentColor(), "Dislike & Autoplay Prevention Rules:");
                ImGui::Spacing();

                if (ImGui::Checkbox("Never autoplay disliked songs during playback advance", &m_skipDislikedOnAutoplay)) {
                    savePreferences();
                }
                ImGui::Indent(22.0f);
                ImGui::TextDisabled("Applies to: Sequential queue advance, Repeat All/Off, Shuffle order, and Auto-DJ candidate pools.");
                ImGui::TextDisabled("Note: You can always explicitly double-click or play a disliked song manually.");
                ImGui::Unindent(22.0f);
                ImGui::Spacing();

                if (ImGui::Checkbox("Auto-skip to next song immediately when current track is marked Disliked", &m_autoSkipDislikedOnMark)) {
                    savePreferences();
                }
                ImGui::Indent(22.0f);
                ImGui::TextDisabled("If active, clicking the Dislike button on the player bar skips instantly to the next track.");
                ImGui::Unindent(22.0f);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Count disliked songs
                size_t dislikedCount = 0;
                for (const auto& t : m_library.getTracks()) {
                    if (t.isDisliked) dislikedCount++;
                }

                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Disliked Songs Library Status:");
                ImGui::BulletText("Currently disliked tracks: %zu", dislikedCount);
                ImGui::Spacing();

                if (ImGui::Button("View Disliked Songs in Library", ImVec2(220.0f, 26.0f))) {
                    m_navSource = NavSource::Disliked;
                    m_viewMode = ViewMode::Tracks;
                    m_showPreferencesModal = false;
                }
                ImGui::SameLine(0.0f, 10.0f);
                if (ImGui::Button("Reset / Clear All Dislikes", ImVec2(180.0f, 26.0f))) {
                    for (const auto& t : m_library.getTracks()) {
                        if (t.isDisliked) m_library.setDisliked(t.id, false);
                    }
                    savePreferences();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "General Playback Options:");
                if (ImGui::Checkbox("Stop playback after current track finishes", &m_stopAfterCurrent)) {
                    savePreferences();
                }
                if (ImGui::Checkbox("Show remaining countdown time on track timer", &m_showRemainingTime)) {
                    savePreferences();
                }
                bool relDrv = m_audio.isReleaseDriverWhenPaused();
                if (ImGui::Checkbox("Release audio device when paused or stopped", &relDrv)) {
                    m_audio.setReleaseDriverWhenPaused(relDrv);
                    m_audio.saveAudioConfig();
                }
                ImGui::Indent(22.0f);
                ImGui::TextDisabled("Frees WASAPI Exclusive and sound endpoints when paused so other apps can output sound.");
                ImGui::Unindent(22.0f);

                ImGui::EndTabItem();
            }

            // Tab 3: Themes & Appearance
            if (ImGui::BeginTabItem("Themes & Appearance##PrefTab", nullptr, tabThemeFlags)) {
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Color Scheme / Skin:");
                AppTheme curTheme = Theme::getCurrentTheme();
                struct ThemeChoice { const char* label; AppTheme theme; const char* desc; };
                static const ThemeChoice kThemes[] = {
                    { "Dark Slate (Classic Default)", AppTheme::ClassicDark, "Authentic dark slate palette with warm gold accent." },
                    { "Modern Obsidian", AppTheme::ModernObsidian, "Deep onyx charcoal with electric cyan accent." },
                    { "Midnight Navy", AppTheme::MidnightNavy, "Refined deep navy blue with sapphire sky accents." },
                    { "Forest Emerald", AppTheme::ForestEmerald, "Natural pine green tones with mint emerald accents." },
                    { "Cyberpunk Neon", AppTheme::CyberpunkNeon, "High-contrast dark violet with luminous neon pink accents." },
                    { "Nordic Frost", AppTheme::NordicFrost, "Arctic deep slate with crisp glacial frost ice accents." },
                    { "Amber Sunset", AppTheme::AmberSunset, "Warm espresso mahogany with glowing amber orange accents." },
                    { "Dracula", AppTheme::Dracula, "Gothic dark slate with radiant purple and lilac accents." },
                    { "Tokyo Night", AppTheme::TokyoNight, "Deep indigo twilight with vibrant Tokyo rose accents." },
                    { "Metro Light", AppTheme::MetroLight, "Clean light gray and crisp white minimalist aesthetic." }
                };

                for (const auto& tc : kThemes) {
                    bool sel = (curTheme == tc.theme);
                    if (ImGui::RadioButton(tc.label, sel)) {
                        Theme::applyTheme(tc.theme);
                        savePreferences();
                    }
                    ImGui::Indent(22.0f);
                    ImGui::TextDisabled("%s", tc.desc);
                    ImGui::Unindent(22.0f);
                    ImGui::Spacing();
                }

                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Visualizer Style:");
                int vm = static_cast<int>(m_visualizerMode);
                const char* vmNames[4] = { "16-Band Spectrum", "32-Band Spectrum", "Dual Analog VU Meter", "Oscilloscope Waveform" };
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::Combo("##PrefVisMode", &vm, vmNames, 4)) {
                    m_visualizerMode = static_cast<VisualizerMode>(vm);
                    savePreferences();
                }

                ImGui::EndTabItem();
            }

            // Tab 4: Panels & Layout
            if (ImGui::BeginTabItem("Panels & Layout##PrefTab", nullptr, tabPanelsFlags)) {
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Workspace Panel Visibility:");
                if (ImGui::Checkbox("Show Left Navigator (Library Tree)", &m_showLeftNavigator)) savePreferences();
                if (ImGui::Checkbox("Show Right Sidebar (Queue, Lyrics, Track Info)", &m_showRightSidebar)) savePreferences();
                if (m_showRightSidebar) {
                    ImGui::Indent(20.0f);
                    if (ImGui::Checkbox("Show Bottom Track Information Panel", &m_showRightTrackInfo)) savePreferences();
                    ImGui::Unindent(20.0f);
                }
                if (ImGui::Checkbox("Show A-Z Alphabetical Jumpbar", &m_showJumpbar)) savePreferences();
                if (ImGui::Checkbox("Show Waveform Progress Bar (Wavebar)", &m_useWavebar)) savePreferences();
                if (ImGui::Checkbox("Show Real-Time Audio Visualizer", &m_showVisualizer)) savePreferences();
                if (ImGui::Checkbox("Show Bottom Status Bar", &m_showStatusBar)) savePreferences();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.9f, 0.93f, 0.98f, 1.0f), "Layout Protection:");
                if (ImGui::Checkbox("Lock All Panels & Splitters (Prevent Resizing)", &m_lockPanels)) savePreferences();

                ImGui::Spacing();
                if (ImGui::Button("Reset Layout Splitters to Default", ImVec2(240.0f, 26.0f))) {
                    m_leftPanelWidth = 220.0f;
                    m_rightPanelWidth = 270.0f;
                    m_rightQueueSplitRatio = 0.52f;
                    savePreferences();
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save & Close", ImVec2(120.0f, 28.0f))) {
            savePreferences();
            m_showPreferencesModal = false;
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::Button("Close", ImVec2(80.0f, 28.0f))) {
            m_showPreferencesModal = false;
        }
    }
    ImGui::End();
}

void MainWindow::showDeleteTrackModal(uint64_t trackId, bool alsoFromDisk) {
    m_deleteTrackId = trackId;
    m_deleteAlsoFromDisk = alsoFromDisk;
    m_showDeleteConfirmModal = true;
}

void MainWindow::renderDeleteTrackModal() {
    if (!m_showDeleteConfirmModal) return;

    const Track* t = m_library.getTrackById(m_deleteTrackId);
    if (!t) {
        m_showDeleteConfirmModal = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(500.0f, 270.0f), ImGuiCond_Always);
    if (ImGui::Begin("Confirm Delete Track", &m_showDeleteConfirmModal, ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(Theme::AccentColor(), "Delete Track Confirmation");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Are you sure you want to delete this track?");
        ImGui::Spacing();
        ImGui::BulletText("Title:  %s", t->getDisplayTitle().c_str());
        ImGui::BulletText("Artist: %s", t->getDisplayArtist().c_str());
        ImGui::BulletText("Album:  %s", t->getDisplayAlbum().c_str());
        ImGui::Spacing();

        ImGui::Checkbox("Permanently delete physical file from hard disk", &m_deleteAlsoFromDisk);
        if (m_deleteAlsoFromDisk) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: Physical audio file will be permanently removed from storage!");
        } else {
            ImGui::TextDisabled("Track will only be removed from SlothPlayer library & playlists.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));

        std::string delBtnText = m_deleteAlsoFromDisk ? "Delete from Disk & Library" : "Remove from Library";
        if (ImGui::Button(delBtnText.c_str(), ImVec2(210.0f, 28.0f))) {
            if (m_currentTrackId == m_deleteTrackId) {
                m_audio.stop();
            }
            // Remove from playback queue
            for (auto it = m_queue.begin(); it != m_queue.end(); ) {
                if (*it == m_deleteTrackId) {
                    it = m_queue.erase(it);
                } else {
                    ++it;
                }
            }
            if (m_queue.empty()) {
                m_queueIndex = 0;
                m_currentTrackId = 0;
            } else if (m_queueIndex >= m_queue.size()) {
                m_queueIndex = m_queue.size() - 1;
            }
            if (m_selectedTrackId == m_deleteTrackId) {
                m_selectedTrackId = 0;
            }
            m_library.deleteTrackPermanently(m_deleteTrackId, m_deleteAlsoFromDisk);
            m_showDeleteConfirmModal = false;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f, 14.0f);
        if (ImGui::Button("Cancel", ImVec2(90.0f, 28.0f))) {
            m_showDeleteConfirmModal = false;
        }

        ImGui::End();
    }
}

void MainWindow::showDeleteAlbumModal(const std::string& albumName, const std::string& artistName, const std::vector<uint64_t>& trackIds, bool alsoFromDisk) {
    m_deleteAlbumName = albumName;
    m_deleteAlbumArtist = artistName;
    m_deleteAlbumTrackIds = trackIds;
    m_deleteAlbumAlsoFromDisk = alsoFromDisk;
    m_showDeleteAlbumModal = true;
}

void MainWindow::renderDeleteAlbumModal() {
    if (!m_showDeleteAlbumModal) return;

    ImGui::SetNextWindowSize(ImVec2(520.0f, 290.0f), ImGuiCond_Always);
    if (ImGui::Begin("Confirm Delete Album", &m_showDeleteAlbumModal, ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(Theme::AccentColor(), "Delete Album Confirmation");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Are you sure you want to delete this entire album?");
        ImGui::Spacing();
        ImGui::BulletText("Album:  %s", m_deleteAlbumName.c_str());
        if (!m_deleteAlbumArtist.empty()) {
            ImGui::BulletText("Artist: %s", m_deleteAlbumArtist.c_str());
        }
        ImGui::BulletText("Tracks: %zu track%s", m_deleteAlbumTrackIds.size(), m_deleteAlbumTrackIds.size() == 1 ? "" : "s");
        ImGui::Spacing();

        ImGui::Checkbox("Permanently delete physical audio files from hard disk", &m_deleteAlbumAlsoFromDisk);
        if (m_deleteAlbumAlsoFromDisk) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: All audio files for this album will be permanently erased from your hard disk!");
        } else {
            ImGui::TextDisabled("Album tracks will only be removed from SlothPlayer library & playlists.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));

        std::string delBtnText = m_deleteAlbumAlsoFromDisk ? "Delete from Disk & Library" : "Remove from Library";
        if (ImGui::Button(delBtnText.c_str(), ImVec2(210.0f, 28.0f))) {
            // If current track is part of the deleted album, stop playback
            for (uint64_t tid : m_deleteAlbumTrackIds) {
                if (m_currentTrackId == tid) {
                    m_audio.stop();
                    break;
                }
            }

            // Remove from playback queue
            std::set<uint64_t> toDeleteSet(m_deleteAlbumTrackIds.begin(), m_deleteAlbumTrackIds.end());
            for (auto it = m_queue.begin(); it != m_queue.end(); ) {
                if (toDeleteSet.count(*it) > 0) {
                    it = m_queue.erase(it);
                } else {
                    ++it;
                }
            }
            if (m_queue.empty()) {
                m_queueIndex = 0;
                m_currentTrackId = 0;
            } else if (m_queueIndex >= m_queue.size()) {
                m_queueIndex = m_queue.size() - 1;
            }

            // If deleted album was expanded, collapse it
            if (m_expandedAlbumName == m_deleteAlbumName) {
                m_expandedAlbumName.clear();
                m_expandedAlbumRepId = 0;
            }
            if (m_selectedAlbumName == m_deleteAlbumName) {
                m_selectedAlbumName.clear();
                m_selectedAlbumRepId = 0;
            }

            m_library.deleteAlbumPermanently(m_deleteAlbumName, m_deleteAlbumArtist, m_deleteAlbumAlsoFromDisk);
            m_showDeleteAlbumModal = false;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f, 14.0f);
        if (ImGui::Button("Cancel", ImVec2(90.0f, 28.0f))) {
            m_showDeleteAlbumModal = false;
        }

        ImGui::End();
    }
}

void MainWindow::renderScanIndicatorOverlay() {
    bool isScanning = m_library.isScanning();
    double now = ImGui::GetTime();

    if (m_wasScanning && !isScanning) {
        m_scanFinishedTime = now;
        m_scanFinishedTrackCount = m_library.getTracks().size();
    }
    m_wasScanning = isScanning;

    bool showFinishedBadge = (!isScanning && (now - m_scanFinishedTime < 3.5) && m_scanFinishedTime > 0.0);

    if (!isScanning && !showFinishedBadge) return;

    float cardW = 320.0f;
    float cardH = 68.0f;
    float margin = 20.0f;
    ImVec2 cardP0(ImGui::GetIO().DisplaySize.x - cardW - margin, 40.0f);
    ImVec2 cardP1(cardP0.x + cardW, cardP0.y + cardH);

    ImDrawList* fgDl = ImGui::GetForegroundDrawList();

    // Smooth fade alpha
    float alpha = 1.0f;
    if (showFinishedBadge) {
        float elapsed = static_cast<float>(now - m_scanFinishedTime);
        if (elapsed > 2.5f) {
            alpha = (3.5f - elapsed) / 1.0f;
        }
    }

    ImU32 bgCol = IM_COL32(22, 26, 32, static_cast<int>(245 * alpha));
    ImVec4 acc = Theme::AccentColor();
    ImU32 borderCol = isScanning ? IM_COL32(static_cast<int>(acc.x * 255.0f), static_cast<int>(acc.y * 255.0f), static_cast<int>(acc.z * 255.0f), static_cast<int>(215.0f * alpha))
                                 : IM_COL32(75, 210, 110, static_cast<int>(240.0f * alpha));

    // Card shadow and background
    fgDl->AddRectFilled(ImVec2(cardP0.x + 3.0f, cardP0.y + 3.0f), ImVec2(cardP1.x + 3.0f, cardP1.y + 3.0f), IM_COL32(0, 0, 0, static_cast<int>(120 * alpha)), 8.0f);
    fgDl->AddRectFilled(cardP0, cardP1, bgCol, 8.0f);
    fgDl->AddRect(cardP0, cardP1, borderCol, 8.0f, 0, 1.5f);

    if (isScanning) {
        // Rotating spinner
        ImVec2 spinCenter(cardP0.x + 24.0f, cardP0.y + 24.0f);
        drawVectorSpinner(fgDl, spinCenter, 10.0f, 2.2f, ImGui::GetColorU32(Theme::AccentColor()));

        // Title
        fgDl->AddText(ImVec2(cardP0.x + 44.0f, cardP0.y + 10.0f), IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), "Rescanning Library...");

        // Status text
        std::string status = m_library.getScanStatus();
        if (status.length() > 34) status = status.substr(0, 31) + "...";
        fgDl->AddText(ImVec2(cardP0.x + 44.0f, cardP0.y + 28.0f), IM_COL32(180, 190, 205, static_cast<int>(230 * alpha)), status.c_str());

        // Mini progress bar
        float prog = std::clamp(m_library.getScanProgress(), 0.0f, 1.0f);
        float barX0 = cardP0.x + 44.0f;
        float barY0 = cardP0.y + 48.0f;
        float barW = 195.0f;
        float barH = 5.0f;
        fgDl->AddRectFilled(ImVec2(barX0, barY0), ImVec2(barX0 + barW, barY0 + barH), IM_COL32(40, 45, 55, static_cast<int>(200 * alpha)), 2.5f);
        fgDl->AddRectFilled(ImVec2(barX0, barY0), ImVec2(barX0 + barW * prog, barY0 + barH), ImGui::GetColorU32(Theme::AccentColor()), 2.5f);

        // Cancel button
        ImGui::SetCursorScreenPos(ImVec2(cardP1.x - 62.0f, cardP0.y + 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.15f, 0.15f, alpha));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.20f, 0.20f, alpha));
        if (ImGui::Button("Cancel##Scan", ImVec2(52.0f, 22.0f))) {
            m_library.cancelScan();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    } else if (showFinishedBadge) {
        // Green Checkmark icon
        ImVec2 chkCenter(cardP0.x + 24.0f, cardP0.y + 34.0f);
        fgDl->AddCircleFilled(chkCenter, 11.0f, IM_COL32(45, 175, 80, static_cast<int>(230 * alpha)));
        fgDl->AddLine(ImVec2(chkCenter.x - 5.0f, chkCenter.y), ImVec2(chkCenter.x - 1.0f, chkCenter.y + 4.0f), IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), 2.0f);
        fgDl->AddLine(ImVec2(chkCenter.x - 1.0f, chkCenter.y + 4.0f), ImVec2(chkCenter.x + 5.0f, chkCenter.y - 4.0f), IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), 2.0f);

        // Success text
        fgDl->AddText(ImVec2(cardP0.x + 44.0f, cardP0.y + 14.0f), IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), "Library Scan Complete");
        char cntStr[64];
        snprintf(cntStr, sizeof(cntStr), "%zu tracks indexed in library", m_scanFinishedTrackCount);
        fgDl->AddText(ImVec2(cardP0.x + 44.0f, cardP0.y + 34.0f), IM_COL32(165, 235, 185, static_cast<int>(255 * alpha)), cntStr);
    }
}

