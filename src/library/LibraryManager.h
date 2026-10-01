#pragma once

#include "Track.h"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

struct AlbumInfo {
    std::string name;
    std::string artist;
    std::string albumArtist;
    int year = 0;
    int discCount = 1;
    std::vector<uint64_t> trackIds;
    std::string coverArtTrackPath;
    uint64_t representativeTrackId = 0;
};

struct ArtistInfo {
    std::string name;
    std::vector<uint64_t> trackIds;
    int albumCount = 0;
};

struct RadioStream {
    uint64_t id = 0;
    std::string name;
    std::string url;
    std::string genre;
    std::string description;
};

struct AutoDJConfig {
    bool enabled = false;
    int minRating = 0;             // 0 = any, 1-5 stars
    bool favoritesOnly = false;
    int artistSeparationTracks = 5;// Do not repeat artist within N tracks
    int trackSeparationTracks = 15;// Do not repeat song within N tracks
    std::string genreFilter = "";  // Empty = all genres
};

struct LibraryStats {
    size_t totalTracks = 0;
    size_t totalAlbums = 0;
    size_t totalArtists = 0;
    size_t totalPlaylists = 0;
    double totalDurationSeconds = 0.0;
    uint64_t totalFileSizeBytes = 0;
    uint32_t totalPlayCount = 0;
    std::map<std::string, size_t> formatCounts;
    std::vector<std::pair<std::string, uint32_t>> topArtists;
    std::vector<std::pair<std::string, uint32_t>> topTracks;
};

struct DuplicateCandidate {
    uint64_t trackId1 = 0;
    uint64_t trackId2 = 0;
    std::string title;
    std::string artist;
    std::string path1;
    std::string path2;
    double duration1 = 0.0;
    double duration2 = 0.0;
    std::string matchReason;
};

enum class RuleField {
    Title = 0,
    Artist = 1,
    Album = 2,
    Genre = 3,
    Rating = 4,
    Year = 5,
    PlayCount = 6
};

enum class RuleComparison {
    Contains = 0,
    Equals = 1,
    GreaterThan = 2,
    LessThan = 3
};

struct SmartPlaylistRule {
    RuleField field = RuleField::Genre;
    RuleComparison comparison = RuleComparison::Contains;
    std::string value;
};

struct CustomSmartPlaylist {
    std::string name;
    std::vector<SmartPlaylistRule> rules;
    bool matchAll = true; // true = AND, false = OR
    size_t limitTracks = 100;
};

struct HistoryEntry {
    uint64_t trackId = 0;
    std::string title;
    std::string artist;
    std::string album;
    std::string timestampStr;
    uint64_t epochSeconds = 0;
};

class LibraryManager {
public:
    LibraryManager();
    ~LibraryManager();

    bool loadLibrary(const std::string& dbPath = "slothplayer_library.json");
    bool saveLibrary(const std::string& dbPath = "slothplayer_library.json");

    void scanDirectories(const std::vector<std::string>& folders);
    void cancelScan();
    bool isScanning() const { return m_isScanning.load(); }
    float getScanProgress() const { return m_scanProgress.load(); }
    std::string getScanStatus() const;

    const std::vector<Track>& getTracks() const { return m_tracks; }
    Track* getTrackById(uint64_t id);
    const Track* getTrackById(uint64_t id) const;

    // Playlists
    const std::map<std::string, std::vector<uint64_t>>& getPlaylists() const { return m_playlists; }
    bool createPlaylist(const std::string& name);
    bool deletePlaylist(const std::string& name);
    bool renamePlaylist(const std::string& oldName, const std::string& newName);
    bool addToPlaylist(const std::string& name, uint64_t trackId);
    bool removeFromPlaylist(const std::string& name, size_t index);
    bool exportM3U(const std::string& playlistName, const std::string& m3uPath);
    bool importM3U(const std::string& m3uPath);

    // Track metadata operations
    void toggleFavorite(uint64_t trackId);
    void toggleDislike(uint64_t trackId);
    void setDisliked(uint64_t trackId, bool disliked);
    void incrementPlayCount(uint64_t trackId);
    void setRating(uint64_t trackId, int rating);
    void recordTrackPlay(uint64_t trackId);
    void updateTrackMetadata(uint64_t trackId, const std::string& title, const std::string& artist, const std::string& album, int year, const std::string& genre, int trackNumber, const std::string& albumArtist = "", const std::string& composer = "", const std::string& comment = "", int discNumber = 1, int totalDiscs = 1, int trackTotal = 0);
    bool importCueSheet(const std::string& cueFilePath);

    // Batch Metadata Operations
    void updateBatchMetadata(const std::vector<uint64_t>& trackIds, const std::string& artist, const std::string& album, int year, const std::string& genre, int rating);
    void batchConvertCase(const std::vector<uint64_t>& trackIds, CaseConversion mode);
    void batchResetPlayCount(const std::vector<uint64_t>& trackIds);
    void batchSetRating(const std::vector<uint64_t>& trackIds, int rating);
    void deleteTracksFromLibrary(const std::vector<uint64_t>& trackIds);
    bool deleteTrackPermanently(uint64_t trackId, bool deleteFromDisk);
    bool deleteTracksPermanently(const std::vector<uint64_t>& trackIds, bool deleteFromDisk);
    bool deleteAlbumPermanently(const std::string& albumName, const std::string& artistName, bool deleteFromDisk);

    // Auto-DJ
    AutoDJConfig& getAutoDJConfig() { return m_autoDJConfig; }
    const AutoDJConfig& getAutoDJConfig() const { return m_autoDJConfig; }
    void setAutoDJConfig(const AutoDJConfig& cfg) { m_autoDJConfig = cfg; }
    uint64_t selectAutoDJTrack(const std::vector<uint64_t>& recentQueue);

    // Radio Streams
    const std::vector<RadioStream>& getRadioStreams() const { return m_radioStreams; }
    void addRadioStream(const std::string& name, const std::string& url, const std::string& genre, const std::string& description = "");
    bool removeRadioStream(uint64_t id);
    void initDefaultRadioStreams();

    // Library Statistics & Duplicate Finder
    LibraryStats calculateStats() const;
    std::vector<DuplicateCandidate> findDuplicates() const;

    // Smart Playlists & Inbox
    std::vector<uint64_t> getRecentlyAddedTrackIds() const;
    std::vector<std::pair<std::string, std::vector<AlbumInfo>>> getRecentlyAddedAlbumsCategorized() const;
    std::vector<uint64_t> getTop25MostPlayedTrackIds() const;
    std::vector<uint64_t> getFavoritesTrackIds() const;
    std::vector<uint64_t> getTopRatedTrackIds() const;
    std::vector<uint64_t> getNeverPlayedTrackIds() const;
    std::vector<uint64_t> getInboxTrackIds() const;
    void moveFromInboxToLibrary(const std::vector<uint64_t>& trackIds);

    // Music Explorer queries
    std::vector<uint64_t> getArtistTopTrackIds(const std::string& artistName) const;
    std::vector<AlbumInfo> getAlbumsByArtist(const std::string& artistName) const;
    void getArtistStats(const std::string& artistName, uint32_t& outTotalPlays, size_t& outAlbumCount, size_t& outTrackCount, std::string& outGenre) const;

    // Grouping
    const std::vector<AlbumInfo>& getAlbums() const { return m_albums; }
    const std::vector<ArtistInfo>& getArtists() const { return m_artists; }

    const std::vector<std::string>& getMonitoredFolders() const { return m_monitoredFolders; }
    void addMonitoredFolder(const std::string& folder);
    void removeMonitoredFolder(const std::string& folder);

    // Custom Smart Playlists
    const std::vector<CustomSmartPlaylist>& getCustomSmartPlaylists() const { return m_customSmartPlaylists; }
    void addCustomSmartPlaylist(const CustomSmartPlaylist& pl);
    bool deleteCustomSmartPlaylist(const std::string& name);
    std::vector<uint64_t> evaluateSmartPlaylist(const CustomSmartPlaylist& pl) const;

    // Playback History
    void addHistoryEntry(uint64_t trackId);
    const std::vector<HistoryEntry>& getHistory() const { return m_history; }
    void clearHistory();

    // Track Bookmarks
    void setTrackBookmark(uint64_t trackId, double timestampSeconds);
    double getTrackBookmark(uint64_t trackId) const;
    const std::map<uint64_t, double>& getBookmarks() const { return m_bookmarks; }

    void rebuildIndices();

private:
    std::vector<Track> m_tracks;
    std::map<uint64_t, size_t> m_trackIdToIndex;
    std::map<std::string, std::vector<uint64_t>> m_playlists;
    std::vector<std::string> m_monitoredFolders;

    std::vector<AlbumInfo> m_albums;
    std::vector<ArtistInfo> m_artists;

    mutable std::mutex m_mutex;
    std::thread m_scanThread;
    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<float> m_scanProgress{0.0f};
    std::string m_scanStatus;
    mutable std::mutex m_statusMutex;

    uint64_t m_nextId = 1;
    std::vector<RadioStream> m_radioStreams;
    AutoDJConfig m_autoDJConfig;
    uint64_t m_nextStreamId = 1;

    std::vector<CustomSmartPlaylist> m_customSmartPlaylists;
    std::vector<HistoryEntry> m_history;
    std::map<uint64_t, double> m_bookmarks;
};
