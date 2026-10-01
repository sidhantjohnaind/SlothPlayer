#pragma once

#include "../audio/AudioEngine.h"
#include "../library/LibraryManager.h"
#include "../library/LyricsManager.h"
#include "../library/PodcastManager.h"
#include "../library/FileOrganizer.h"
#include "../library/AudioConverter.h"
#include "../library/VolumeScanner.h"
#include "../library/ScrobbleManager.h"
#include "../library/OnlineMetadataFetcher.h"
#include "../library/CueSheetParser.h"
#include "../graphics/TextureManager.h"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <set>
#include <future>
#include <atomic>
#include "../platform/Platform.h"

enum class ViewMode {
    NowPlaying,
    Tracks,
    Albums,
    Artists,
    Folders,
    Playlists,
    MusicExplorer,
    TheaterMode
};

enum class VisualizerMode {
    Spectrum16 = 0,
    Spectrum32 = 1,
    DualVU = 2,
    Oscilloscope = 3
};

enum class MiniPlayerMode {
    Compact = 0,    // Classic MusicBee Compact Player (artwork, metadata, controls, expandable drawer)
    AlbumArt = 1,   // Square Now Playing Album Artwork / Spinning Vinyl Record Card
    Taskbar = 2,    // DeskBand / Ribbon Strip Player (ultra-slim horizontal bar with live spectrum)
    MinimalHUD = 3, // Minimalist Floating HUD / Ticker (compact pill with essential controls)
    Lyrics = 4,     // Dedicated Floating Synced Lyrics HUD (karaoke style with auto-scroll)
    PiP = 5         // Picture-in-Picture / Video Card Player (16:9 media canvas, seek bar, full tile deck)
};

struct BrowserTab {
    std::string id;
    std::string title;
    ViewMode viewMode = ViewMode::Albums;
    int navSource = 0; // cast to/from NavSource
    std::string playlistName;
    bool isPinned = false;
};

enum class RightPanelTab {
    Queue,
    TrackInfo,
    Lyrics
};

enum class ExplorerSubTab {
    AlbumsAndStats,
    MoreAlbums,
    Profile,
    SimilarArtists
};

enum class SmartPlaylistType {
    RecentlyAdded,
    Top25MostPlayed,
    Favorites,
    TopRated,
    NeverPlayed,
    Custom
};

enum class NavSource {
    AllTracks,
    Favorites,
    Disliked,
    TopRated,
    RecentlyAdded,
    Top25MostPlayed,
    NeverPlayed,
    Playlist,
    Folder,
    RadioStreams,
    Podcasts,
    History
};

struct SleepTimerConfig {
    bool active = false;
    std::chrono::steady_clock::time_point targetTime;
    int durationMinutes = 30;
    bool stopAfterCurrent = false;
    int remainingTracks = 0;
    bool fadeOutVolume = true;
};

enum class MediaAction : uint32_t {
    None = 0,
    Play,
    Pause,
    TogglePlayPause,
    Next,
    Previous,
    Stop
};

class MainWindow {
public:
    MainWindow(AudioEngine& audio, LibraryManager& library, TextureManager& textures);
    ~MainWindow();

    void update();
    void render();
    void playTrack(uint64_t trackId, const std::vector<uint64_t>& contextQueue = {});
    void playQueueIndex(size_t index);
    void removeQueueIndex(size_t index);
    void removeQueueIndices(const std::set<size_t>& indices);
    void playNext();
    void playPrevious();
    void togglePlayPause();
    void play();
    void pause();
    void stop();

    void triggerMediaAction(MediaAction action);

    bool isMiniPlayer() const { return m_isMiniPlayer; }
    const Track* getCurrentTrack() const { return m_library.getTrackById(m_currentTrackId); }
    void toggleMiniPlayer();
    void setMiniPlayerMode(MiniPlayerMode mode);
    void openMiniPlayerInMode(MiniPlayerMode mode);
    MiniPlayerMode getMiniPlayerMode() const { return m_miniPlayerMode; }
    void applyMiniPlayerGeometry();
    void setWindowHandle(void* hWnd) { m_hWnd = hWnd; }

    void toggleAutoDJ();
    void showAutoDJModal() { m_showAutoDJModal = true; }
    void showSleepTimerModal() { m_showSleepTimerModal = true; }
    void showBatchTagModal();
    void showLibraryStatsModal();
    void showDuplicatesModal();
    void showShortcutsModal() { m_showShortcutsModal = true; }
    void showFileOrganizerModal();
    void showAudioConverterModal();
    void showSmartPlaylistBuilderModal();
    void showTrackInfoModal(uint64_t trackId = 0);
    void openAudioDriverModal();
    void showVolumeScannerModal();
    void showScrobblerModal() { m_showScrobblerModal = true; }
    void showPanelsConfigModal() { m_showPanelsConfigModal = true; }
    void showPreferencesModal(int initialTab = 0);
    void toggleTheaterMode();

    void handleDroppedFiles(const std::vector<std::string>& files);
    void openSystemFileDialog();
    void openSystemFolderDialog();
    void loadDemoAudio();

    void loadPreferences();
    void savePreferences();

private:
    void renderTopMenuBar();
    void renderTabsBar();
    void renderAZJumpBar();
    void renderLeftPanel(float width, float height);
    void renderCenterPanel(float width, float height);
    void renderRightPanel(float width, float height);
    void renderBottomPlayerBar(float height);

    void renderNowPlayingView();
    void renderTracksView();
    void renderAlbumsView();
    void renderArtistsView();
    void renderFoldersView();
    void renderPlaylistsView();
    void renderMusicExplorerView();
    void renderRadioView();
    void renderPodcastsView();
    void renderHistoryView();
    void renderTheaterMode();
    void renderPlaylistExplorer(float width, float height);
    void renderPlaylistBanner(const std::string& title, size_t trackCount, size_t albumCount, double totalDuration, ImTextureID coverArt, const std::vector<uint64_t>& trackQueue);
    void renderSectionedAlbumsView(const std::vector<std::pair<std::string, std::vector<AlbumInfo>>>& sections);
    void renderExpandedAlbumPanel(const AlbumInfo& alb, float width, float arrowCenterX);
    void renderExplorerLeftPanel(float width, float height);
    void renderArtistHeroBanner(const std::string& artistName, uint32_t totalPlays, size_t albumCount, size_t trackCount, const std::string& genreTag, ImTextureID avatarArt, const std::vector<uint64_t>& allTracks);
    void renderArtistTopTracks(const std::vector<uint64_t>& topTrackIds, float width, float height);
    void renderArtistAlbumCards(const std::vector<AlbumInfo>& albums, float width, float height);
    void renderLyricsPanel(float width, float height);
    void renderNowPlayingStage(const Track* curr, float width, float height);
    void renderEmptyDropzone();

    void renderTrackContextMenu(uint64_t trackId, bool isFromQueue = false, size_t queueIdx = 0);
    void renderAlbumContextMenu(const AlbumInfo& alb);
    void renderAllAlbumsContextMenu(const std::vector<AlbumInfo>& albums);

public:
    enum class NavVectorIcon {
        AllTracks,
        Favorites,
        TopRated,
        MostPlayed,
        RecentlyAdded,
        NeverPlayed,
        Disliked,
        Radio,
        Podcasts,
        History
    };
    bool drawModernNavRow(const char* strId, const char* label, NavVectorIcon icon, size_t count, bool isSelected, float width, float rowH = 28.0f);
    void drawNavVectorIcon(struct ImDrawList* dl, struct ImVec2 center, float size, NavVectorIcon icon, unsigned int color);
    // Vector drawing helpers for PotPlayer transport
    void drawVectorPlayPause(struct ImDrawList* dl, struct ImVec2 center, float radius, bool isPlaying, bool isHovered, bool isActive);
    void drawVectorPrevNext(struct ImDrawList* dl, struct ImVec2 center, float size, bool isNext, bool isHovered, bool isActive);
    void drawVectorStop(struct ImDrawList* dl, struct ImVec2 center, float size, bool isHovered, bool isActive);
    void drawVectorShuffle(struct ImDrawList* dl, struct ImVec2 center, float size, bool active, bool isHovered, bool isActive);
    void drawVectorRepeat(struct ImDrawList* dl, struct ImVec2 center, float size, RepeatMode mode, bool isHovered, bool isActive);
    void drawVectorSpeaker(struct ImDrawList* dl, struct ImVec2 center, float size, float volume, bool muted, bool isHovered);
    void drawVectorEQIcon(struct ImDrawList* dl, struct ImVec2 center, float size, bool active, bool isHovered);
    void drawVectorStar(struct ImDrawList* dl, struct ImVec2 center, float r, bool filled, unsigned int col);
    void drawVectorHeart(struct ImDrawList* dl, struct ImVec2 center, float r, bool loved, bool hovered);
    void drawVectorDislike(struct ImDrawList* dl, struct ImVec2 center, float r, bool disliked, bool hovered);
    void drawVectorWavebar(struct ImDrawList* dl, struct ImVec2 center, float size, bool active, bool isHovered);
    void drawVectorMiniPlayer(struct ImDrawList* dl, struct ImVec2 center, float size, bool isHovered);
    void drawVectorAudioDriver(struct ImDrawList* dl, struct ImVec2 center, float size, bool isExclusive, bool isHovered);
    void drawMicroEqualizer(struct ImDrawList* dl, struct ImVec2 center, unsigned int col, bool isPlaying);
    void drawMagnifyingGlass(struct ImDrawList* dl, struct ImVec2 center, float radius, unsigned int col);
    bool drawInteractiveStarRating(struct ImDrawList* dl, struct ImVec2 pos, int& rating, uint64_t trackId = 0);
    void openPropertiesForTrack(uint64_t trackId);

    AudioEngine& m_audio;
    LibraryManager& m_library;
    TextureManager& m_textures;
    LyricsManager m_lyrics;

    ViewMode m_viewMode = ViewMode::Albums;
    RightPanelTab m_rightPanelTab = RightPanelTab::Queue;
    NavSource m_navSource = NavSource::AllTracks;
    SmartPlaylistType m_smartPlaylist = SmartPlaylistType::RecentlyAdded;
    std::string m_activeCustomPlaylist;
    std::string m_selectedPlaylistName;
    std::string m_selectedFolderPath;
    std::string m_selectedArtistName;
    std::string m_selectedExplorerArtist;
    ExplorerSubTab m_explorerSubTab = ExplorerSubTab::AlbumsAndStats;
    char m_artistAlphabetFilter = '\0';

    char m_searchBuffer[128] = {0};
    uint64_t m_currentTrackId = 0;
    uint64_t m_selectedTrackId = 0;

    std::vector<uint64_t> m_queue;
    size_t m_queueIndex = 0;
    int m_selectedQueueIndex = -1;
    std::set<size_t> m_selectedQueueIndices;
    size_t m_queueAnchorIdx = 0;

    // Sorting
    int m_sortColumn = 1; // 0=Status, 1=Track#, 2=Title, 3=Artist, 4=Album, 5=Time, 6=Genre, 7=Year
    bool m_sortAscending = true;

    enum class AlbumSortMode { Artist, Title, Year, TrackCount };
    AlbumSortMode m_albumSortMode = AlbumSortMode::Artist;
    bool m_albumSortAscending = true;

    // Collapsible Left Navigator sections
    bool m_navSectionLibraryOpen = true;
    bool m_navSectionPlaylistsOpen = true;
    bool m_navSectionFoldersOpen = true;
    bool m_navSectionStreamingOpen = true;

    void renderEqualizerModal();
    void renderAddFolderModal();
    void renderCreatePlaylistModal();
    void renderTrackPropertiesModal();
    void renderArtworkSearchModal();
    void openArtworkSearch(const std::string& artist, const std::string& album);
    void renderAboutModal();

    void renderMiniPlayer();
    void renderMiniPlayerHeader(const char* modeTitle);
    void renderMiniPlayerContextMenu();
    void renderCompactMiniPlayer();
    void renderAlbumArtMiniPlayer();
    void renderTaskbarMiniPlayer();
    void renderMinimalHUDMiniPlayer();
    void renderLyricsMiniPlayer();
    void renderPiPMiniPlayer();
    const LyricsData& getCachedLyrics(const Track& track);
    void invalidateLyricsCache() { m_cachedLyricsTrackId = 0; }
    uint64_t m_cachedLyricsTrackId = 0;
    LyricsData m_cachedLyricsData;

    void renderRadioStreamsView();
    void renderAddRadioStreamModal();
    void renderAutoDJModal();
    void renderSleepTimerModal();
    void renderBatchTagModal();
    void renderLibraryStatsModal();
    void renderDuplicatesModal();
    void renderShortcutsModal();
    void checkSleepTimer();

    // Modals
    bool m_showEqualizer = false;
    bool m_showAddFolderModal = false;
    bool m_showCreatePlaylistModal = false;
    bool m_showTrackProperties = false;
    bool m_showAboutModal = false;
    bool m_isMiniPlayer = false;
    MiniPlayerMode m_miniPlayerMode = MiniPlayerMode::Compact;
    bool m_miniPlayerAlwaysOnTop = true;
    float m_miniPlayerOpacity = 1.0f;
    bool m_miniPlayerShowQueue = false;
    bool m_miniPlayerShowLyrics = false;
    bool m_miniPlayerVinylStyle = false;
    int m_miniQueueHoveredIdx = -1;

    bool m_showAutoDJModal = false;
    bool m_showSleepTimerModal = false;
    bool m_showBatchTagModal = false;
    bool m_showLibraryStatsModal = false;
    bool m_showDuplicatesModal = false;
    bool m_showShortcutsModal = false;
    bool m_showAddRadioStreamModal = false;

    SleepTimerConfig m_sleepTimer;
    std::set<uint64_t> m_selectedTrackIds;

    char m_newPlaylistNameBuffer[64] = {0};
    char m_newFolderPathBuffer[260] = {0};

    uint64_t m_propTrackId = 0;
    char m_propTitle[128] = {0};
    char m_propArtist[128] = {0};
    char m_propAlbum[128] = {0};
    char m_propAlbumArtist[128] = {0};
    char m_propComposer[128] = {0};
    char m_propComment[256] = {0};
    char m_propGenre[64] = {0};
    int m_propYear = 0;
    int m_propTrackNumber = 0;
    int m_propTrackTotal = 0;
    int m_propDiscNumber = 1;
    int m_propTotalDiscs = 1;
    int m_propRating = 0;

    // Online artwork search
    bool m_showArtworkSearchModal = false;
    char m_artSearchArtist[128] = {0};
    char m_artSearchAlbum[128] = {0};
    std::vector<ArtworkCandidate> m_artCandidates;
    std::atomic<bool> m_artSearching{false};
    std::string m_artSearchStatus;
    std::mutex m_artSearchMutex;

    // Batch properties buffer
    char m_batchArtist[128] = {0};
    char m_batchAlbum[128] = {0};
    char m_batchGenre[64] = {0};
    int m_batchYear = 0;
    int m_batchRating = -1;
    bool m_batchApplyArtist = false;
    bool m_batchApplyAlbum = false;
    bool m_batchApplyGenre = false;
    bool m_batchApplyYear = false;
    bool m_batchApplyRating = false;

    // New Stream buffer
    char m_newStreamName[128] = {0};
    char m_newStreamUrl[256] = {0};
    char m_newStreamGenre[64] = {0};
    char m_newStreamDesc[256] = {0};

    // Duplicates list & async worker future
    std::vector<DuplicateCandidate> m_duplicateCandidates;
    std::future<std::vector<DuplicateCandidate>> m_dupesFuture;
    bool m_dupesLoading = false;

    // Library stats & async worker future
    LibraryStats m_cachedStats;
    std::future<LibraryStats> m_statsFuture;
    bool m_statsLoading = false;

    // Shortcuts filter
    char m_shortcutsFilter[64] = {0};

    // Window handle for mini-player toggling
    void* m_hWnd = nullptr;

    // Scrubber dragging state
    bool m_isDraggingScrubber = false;
    float m_scrubberDragTime = 0.0f;

    // Vinyl animation & visualizer peak holds
    float m_vinylRotation = 0.0f;
    float m_peakBars[40] = {0};
    float m_peakHoldTimers[40] = {0};

    // A-Z Alphabet Filter
    char m_albumAlphabetFilter = '\0';

    // Player Signature Feature States
    bool m_showRemainingTime = false; // FormPlayerShowRemaining
    bool m_stopAfterCurrent = false;  // PlaybackStopAfterCurrent
    float m_lyricsScale = 1.0f;       // ViewIncreaseLyricsFont / ViewDecreaseLyricsFont
    char m_savePresetNameBuffer[64] = {0};
    void titleCaseText(char* buffer, size_t maxLen);

    // Dynamic Resizable Splitter Bars (Interactive Layout)
    float m_leftPanelWidth = 190.0f;
    float m_rightPanelWidth = 240.0f;
    float m_rightQueueSplitRatio = 0.52f; // Vertical split between Queue and Track Info / Artwork
    float m_bottomBarHeight = 40.0f;
    bool m_isDraggingLeftSplitter = false;
    bool m_isDraggingRightSplitter = false;
    bool m_isDraggingRightQueueSplitter = false;
    bool m_isNearLeftSplitter = false;
    bool m_isNearRightSplitter = false;
    bool m_isNearRightQueueSplitter = false;
    float m_albumCardSize = 150.0f; // Responsive card target size (increased default)
    float m_rightPanelArtSize = 180.0f; // Artwork size in right sidebar panel
    bool m_skipDislikedOnAutoplay = true; // Never autoplay disliked tracks unless explicitly clicked
    bool m_autoSkipDislikedOnMark = true; // When marked disliked while playing, auto skip

    // True Non-Repeating Random Shuffle Order
    std::vector<size_t> m_shuffleOrder;
    size_t m_shuffleOrderPos = 0;
    void rebuildShuffleOrder(bool keepCurrentFirst = true);
    bool isShuffleOrderValid() const;
    std::atomic<bool> m_pendingTrackEnded{false};
    std::atomic<uint32_t> m_pendingMediaAction{0};

    // Expanded Album View (Accordion Dropdown Tracklist)
    uint64_t m_expandedAlbumRepId = 0;
    uint64_t m_selectedAlbumRepId = 0;
    std::string m_expandedAlbumName;
    std::string m_selectedAlbumName;
    float m_expandedArrowX = 0.0f;

    // Sub-header toolbar & Unified top bar helpers
    void renderUnifiedTopBar();
    void renderSubHeaderToolbar(float leftW, float centerW, float rightW);

    // Podcasts
    PodcastManager m_podcasts;
    uint64_t m_selectedPodcastChannelId = 1;

    // Wavebar Seeker
    bool m_useWavebar = false;
    std::vector<float> m_cachedWaveform;
    std::string m_waveformTrackPath;
    bool m_waveformLoading = false;
    std::future<std::vector<float>> m_waveformFuture;
    void requestWaveform(const std::string& path);
    void renderWaveformSeeker(float width, float height, double currentSec, double totalSec);

    // Modals & Tools
    bool m_showFileOrganizerModal = false;
    char m_organizerPattern[256] = "<Artist>/<Album>/<Track#> - <Title>";
    char m_organizerBaseDir[256] = "";
    std::vector<OrganizePreviewItem> m_organizePreviews;
    bool m_organizingActive = false;
    float m_organizeProgress = 0.0f;
    std::string m_organizeStatus;
    void renderFileOrganizerModal();

    bool m_showAudioConverterModal = false;
    int m_converterFormatIndex = 0;
    int m_converterSampleRateIndex = 0;
    ConvertProgress m_converterProgress;
    bool m_convertingActive = false;
    mutable std::mutex m_asyncToolsMutex;
    void renderAudioConverterModal();

    bool m_showSmartPlaylistBuilderModal = false;
    char m_splName[128] = "Dynamic Chill Mix";
    int m_splRuleField = 3;
    int m_splRuleComparison = 0;
    char m_splRuleValue[128] = "Rock";
    int m_splLimit = 50;
    bool m_splMatchAll = true;
    void renderSmartPlaylistBuilderModal();

    bool m_showSaveQueueAsPlaylistModal = false;
    char m_saveQueuePlaylistNameBuffer[128] = "Queue Mix";
    int m_saveQueueTargetMode = 0; // 0 = New Playlist, 1 = Existing Playlist
    std::string m_saveQueueSelectedExisting;
    void renderSaveQueueAsPlaylistModal();

    // Multi-Tab Browsing System
    std::vector<BrowserTab> m_tabs;
    size_t m_activeTabIndex = 0;
    void initDefaultTabs();
    void openNewTab(const std::string& title, ViewMode vm, int ns);

    // A-Z Quick Jumpbar
    char m_activeJumpLetter = '\0';

    // Multi-Mode Visualizer
    VisualizerMode m_visualizerMode = VisualizerMode::Spectrum32;
    void renderMultiVisualizer(struct ImDrawList* dl, struct ImVec2 pos, struct ImVec2 size);

    // ReplayGain Volume Analyzer
    bool m_showVolumeScannerModal = false;
    bool m_volumeScanningActive = false;
    VolumeScanProgress m_volumeScanProgress;
    std::vector<VolumeScanResult> m_volumeScanResults;
    void renderVolumeScannerModal();

    // Scrobbler
    ScrobbleManager m_scrobbler;
    bool m_showScrobblerModal = false;
    void renderScrobblerModal();

    // Track Info Inspector
    bool m_showTrackInfoModal = false;
    uint64_t m_inspectTrackId = 0;
    void renderTrackInfoModal();

    // Panels Configuration
    bool m_showPanelsConfigModal = false;
    bool m_showLeftNavigator = true;
    bool m_showRightSidebar = true;
    bool m_showRightTrackInfo = true;
    bool m_lockPanels = false;
    bool m_showJumpbar = true;
    bool m_showVisualizer = false;
    bool m_showStatusBar = true;
    enum class RightBottomView { ArtworkAndInfo = 0, AlbumCover = 1, TrackInfo = 2 };
    RightBottomView m_rightBottomView = RightBottomView::ArtworkAndInfo;
    bool m_showPreferencesModal = false;
    int m_preferencesActiveTab = 0;
    void renderPreferencesModal();
    enum class QueueViewMode { PlayingTracks = 0, UpcomingTracks = 1 };
    QueueViewMode m_queueViewMode = QueueViewMode::PlayingTracks;
    enum class QueueLayoutMode { TrackDetails = 0, AlbumAndTracks = 1, TrackWithThumbnail = 2 };
    QueueLayoutMode m_queueLayoutMode = QueueLayoutMode::TrackWithThumbnail;
    bool m_showRightHeaderMenu = true;
    void renderPanelsConfigModal();

    // Audio Driver & Hardware Output Preferences
    bool m_showAudioDriverModal = false;
    int m_uiDriverType = 0; // 0 = WASAPI Shared, 1 = WASAPI Exclusive, 2 = DirectSound
    int m_uiDeviceIndex = -1;
    int m_uiBufferLatencyMs = 50;
    bool m_uiReleaseDriverWhenPaused = true;
    std::vector<AudioDeviceInfo> m_uiDeviceList;
    std::string m_uiDriverApplyStatus;
    void renderAudioDriverModal();

    // Delete Track Confirmation Modal
    bool m_showDeleteConfirmModal = false;
    uint64_t m_deleteTrackId = 0;
    bool m_deleteAlsoFromDisk = false;
    void showDeleteTrackModal(uint64_t trackId, bool alsoFromDisk = false);
    void renderDeleteTrackModal();

    // Delete Album Confirmation Modal
    bool m_showDeleteAlbumModal = false;
    std::string m_deleteAlbumName;
    std::string m_deleteAlbumArtist;
    std::vector<uint64_t> m_deleteAlbumTrackIds;
    bool m_deleteAlbumAlsoFromDisk = false;
    void showDeleteAlbumModal(const std::string& albumName, const std::string& artistName, const std::vector<uint64_t>& trackIds, bool alsoFromDisk = false);
    void renderDeleteAlbumModal();

    // Library Rescan Animation & Notification
    bool m_wasScanning = false;
    double m_scanFinishedTime = 0.0;
    size_t m_scanFinishedTrackCount = 0;
    void renderScanIndicatorOverlay();

    // Multi-Tier Column Browser
    bool m_showColumnBrowser = false;
    std::string m_cbSelectedGenre;
    std::string m_cbSelectedArtist;
    std::string m_cbSelectedAlbum;
    float m_columnBrowserHeight = 120.0f;
    void renderColumnBrowser(float width, float height, const std::vector<const Track*>& sourceTracks, std::vector<const Track*>& outFilteredTracks);
};
