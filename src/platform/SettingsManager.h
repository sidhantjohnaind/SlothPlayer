#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

struct AppSettings {
    // Window Placement & Display
    int windowX = -1;
    int windowY = -1;
    int windowWidth = 1260;
    int windowHeight = 760;
    bool windowMaximized = false;

    // Theme & Styling
    int theme = 0; // AppTheme::ClassicDark
    bool useLightExpandedCard = true;

    // Audio Engine & Hardware Output
    int audioDriver = 1; // 0 = WASAPI Shared, 1 = WASAPI Exclusive, 2 = DirectSound
    int audioDeviceIndex = -1;
    std::string audioDeviceName;
    int bufferLatencyMs = 50;
    float volume = 0.85f;
    bool isMuted = false;
    int repeatMode = 0; // 0 = Off, 1 = All, 2 = One
    bool shuffle = false;
    float playbackSpeed = 1.0f;
    float stereoBalance = 0.0f;
    float crossfadeDuration = 0.0f;
    bool silenceSkipping = false;
    float replayGainPreamp = 0.0f;
    bool releaseDriverWhenPaused = true; // Releases WASAPI Exclusive & audio endpoints when paused/stopped

    // 10-Band Graphic Equalizer
    bool eqEnabled = true;
    float eqPreamp = 0.0f;
    float eqStereoPan = 0.0f;
    float eqStereoWidth = 1.0f;
    std::string eqCurrentPreset = "Flat";
    std::vector<float> eqBandGains = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // Playback State & Queue Persistence
    uint64_t lastTrackId = 0;
    std::string lastTrackPath;
    double lastPositionSeconds = 0.0;
    std::vector<uint64_t> queue;
    size_t queueIndex = 0;
    bool stopAfterCurrent = false;
    bool showRemainingTime = false;
    bool skipDislikedOnAutoplay = true; // Disliked songs will never autoplay unless explicitly clicked
    bool autoSkipDislikedOnMark = true; // When user clicks dislike on current track, skip immediately

    // UI Layout & Sizing
    int viewMode = 2; // ViewMode::Albums
    int navSource = 0; // NavSource::AllTracks
    float leftPanelWidth = 190.0f;
    float rightPanelWidth = 240.0f;
    float rightQueueSplitRatio = 0.52f;
    float bottomBarHeight = 40.0f;
    float albumCardSize = 150.0f; // Responsive album grid card target size (increased default)
    float rightPanelArtSize = 180.0f; // Artwork size in right sidebar panel
    int rightPanelTab = 0; // RightPanelTab::Queue
    float lyricsScale = 1.0f;
    bool useWavebar = false;
    int visualizerMode = 1; // VisualizerMode::Spectrum32

    // Panels Configuration
    bool showLeftNavigator = true;
    bool showRightSidebar = true;
    bool showRightTrackInfo = true;
    bool lockPanels = false;
    int rightBottomView = 0; // 0 = ArtworkAndInfo (Combined), 1 = AlbumCover, 2 = TrackInfo Only
    bool showJumpbar = true;
    bool showVisualizer = false;
    bool showStatusBar = true;
    bool showColumnBrowser = false;
    float columnBrowserHeight = 120.0f;
    std::string cbSelectedGenre;
    std::string cbSelectedArtist;
    std::string cbSelectedAlbum;

    // Mini Player Widget Modes
    int miniPlayerMode = 0; // 0 = Compact, 1 = AlbumArt, 2 = Taskbar, 3 = MinimalHUD, 4 = Lyrics, 5 = PiP
    bool miniPlayerAlwaysOnTop = true;
    float miniPlayerOpacity = 1.0f;
    bool miniPlayerShowQueue = false;
    bool miniPlayerShowLyrics = false;
    bool miniPlayerVinylStyle = false;

    // Auto-DJ & Sleep Timer
    bool autoDJEnabled = false;
    int autoDJTrackSeparation = 10;
    int autoDJArtistSeparation = 5;
    bool autoDJFavoritesOnly = false;
    int autoDJMinRating = 0;
    int sleepTimerDurationMinutes = 30;
    bool sleepTimerFadeOut = true;
};

class SettingsManager {
public:
    static SettingsManager& instance();

    AppSettings& settings() { return m_settings; }
    const AppSettings& settings() const { return m_settings; }

    bool load();
    bool save();

    std::string getSettingsFilePath() const;

private:
    SettingsManager() = default;
    ~SettingsManager() = default;
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    AppSettings m_settings;
    mutable std::mutex m_mutex;
    bool m_loaded = false;
};
