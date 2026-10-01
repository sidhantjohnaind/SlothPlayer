#include "LibraryManager.h"
#include "TagReader.h"
#include "CueSheetParser.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\0");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\0");
    return str.substr(first, (last - first + 1));
}

LibraryManager::LibraryManager() {
    initDefaultRadioStreams();
}

LibraryManager::~LibraryManager() {
    cancelScan();
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
}

std::string LibraryManager::getScanStatus() const {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_scanStatus;
}

Track* LibraryManager::getTrackById(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_trackIdToIndex.find(id);
    if (it != m_trackIdToIndex.end() && it->second < m_tracks.size()) {
        return &m_tracks[it->second];
    }
    return nullptr;
}

const Track* LibraryManager::getTrackById(uint64_t id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_trackIdToIndex.find(id);
    if (it != m_trackIdToIndex.end() && it->second < m_tracks.size()) {
        return &m_tracks[it->second];
    }
    return nullptr;
}

void LibraryManager::rebuildIndices() {
    m_trackIdToIndex.clear();
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        m_trackIdToIndex[m_tracks[i].id] = i;
        if (m_tracks[i].id >= m_nextId) {
            m_nextId = m_tracks[i].id + 1;
        }
    }

    // Rebuild Albums (Group by albumArtist + album name to handle Various Artists & Multi-Disc cleanly)
    m_albums.clear();
    std::map<std::string, AlbumInfo> albumMap;
    std::map<std::string, std::set<std::string>> albumArtistsMap;

    for (const auto& track : m_tracks) {
        std::string albumName = track.getDisplayAlbum();
        if (albumName.empty() || albumName == "Unknown Album") {
            albumName = fs::path(track.filePath).parent_path().filename().string();
            if (albumName.empty()) albumName = "Unknown Album";
        }
        std::string albArtist = track.getDisplayAlbumArtist();
        std::string key = albArtist + " - " + albumName;

        auto& alb = albumMap[key];
        alb.name = albumName;
        if (alb.albumArtist.empty() && !track.albumArtist.empty()) {
            alb.albumArtist = track.albumArtist;
        }
        albumArtistsMap[key].insert(track.getDisplayArtist());

        if (alb.year == 0 && track.year != 0) alb.year = track.year;
        if (track.discNumber > alb.discCount) alb.discCount = track.discNumber;
        if (track.totalDiscs > alb.discCount) alb.discCount = track.totalDiscs;

        alb.trackIds.push_back(track.id);
        if (alb.representativeTrackId == 0 || track.hasEmbeddedArt || !track.albumArtPath.empty()) {
            alb.representativeTrackId = track.id;
            alb.coverArtTrackPath = track.filePath;
        }
    }

    for (auto& pair : albumMap) {
        const auto& artists = albumArtistsMap[pair.first];
        if (!pair.second.albumArtist.empty()) {
            pair.second.artist = pair.second.albumArtist;
        } else if (artists.size() == 1) {
            pair.second.artist = *artists.begin();
        } else if (artists.size() > 1) {
            pair.second.artist = "Various Artists";
        } else {
            pair.second.artist = "Unknown Artist";
        }

        // Sort tracks sequentially by discNumber then track number
        std::sort(pair.second.trackIds.begin(), pair.second.trackIds.end(), [this](uint64_t aId, uint64_t bId) {
            auto itA = m_trackIdToIndex.find(aId);
            auto itB = m_trackIdToIndex.find(bId);
            if (itA == m_trackIdToIndex.end() || itB == m_trackIdToIndex.end()) return aId < bId;
            const auto& a = m_tracks[itA->second];
            const auto& b = m_tracks[itB->second];
            if (a.discNumber != b.discNumber) {
                return a.discNumber < b.discNumber;
            }
            if (a.trackNumber != b.trackNumber && a.trackNumber > 0 && b.trackNumber > 0) {
                return a.trackNumber < b.trackNumber;
            }
            return a.filePath < b.filePath;
        });

        m_albums.push_back(std::move(pair.second));
    }
    std::sort(m_albums.begin(), m_albums.end(), [](const AlbumInfo& a, const AlbumInfo& b) {
        return a.name < b.name;
    });

    // Rebuild Artists
    m_artists.clear();
    std::map<std::string, ArtistInfo> artistMap;
    for (const auto& track : m_tracks) {
        std::string artistName = track.getDisplayArtist();
        auto& art = artistMap[artistName];
        art.name = artistName;
        art.trackIds.push_back(track.id);
    }
    for (auto& pair : artistMap) {
        std::set<std::string> artistAlbums;
        for (uint64_t tid : pair.second.trackIds) {
            auto it = m_trackIdToIndex.find(tid);
            if (it != m_trackIdToIndex.end()) {
                artistAlbums.insert(m_tracks[it->second].getDisplayAlbum());
            }
        }
        pair.second.albumCount = static_cast<int>(artistAlbums.size());
        m_artists.push_back(std::move(pair.second));
    }
    std::sort(m_artists.begin(), m_artists.end(), [](const ArtistInfo& a, const ArtistInfo& b) {
        return a.name < b.name;
    });
}

bool LibraryManager::loadLibrary(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        std::string actualPath = dbPath;
        std::ifstream file(actualPath);
        if (!file.is_open()) return false;

        nlohmann::json j;
        file >> j;

        if (j.contains("monitoredFolders")) {
            m_monitoredFolders = j["monitoredFolders"].get<std::vector<std::string>>();
        }
        if (j.contains("tracks")) {
            m_tracks = j["tracks"].get<std::vector<Track>>();
            // Ensure tracks have valid fileModifiedTime from disk
            for (auto& t : m_tracks) {
                if (t.fileModifiedTime == 0 && !t.filePath.empty()) {
                    try {
                        if (fs::exists(t.filePath)) {
                            auto ftime = fs::last_write_time(t.filePath);
                            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                            );
                            t.fileModifiedTime = std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
                        }
                    } catch (...) {}
                }
            }
        }
        if (j.contains("playlists")) {
            m_playlists = j["playlists"].get<std::map<std::string, std::vector<uint64_t>>>();
        }
        if (j.contains("radioStreams")) {
            m_radioStreams.clear();
            for (const auto& item : j["radioStreams"]) {
                RadioStream s;
                if (item.contains("id")) s.id = item["id"].get<uint64_t>();
                if (item.contains("name")) s.name = item["name"].get<std::string>();
                if (item.contains("url")) s.url = item["url"].get<std::string>();
                if (item.contains("genre")) s.genre = item["genre"].get<std::string>();
                if (item.contains("description")) s.description = item["description"].get<std::string>();
                m_radioStreams.push_back(s);
                if (s.id >= m_nextStreamId) m_nextStreamId = s.id + 1;
            }
        }
        initDefaultRadioStreams();

        if (j.contains("customSmartPlaylists") && j["customSmartPlaylists"].is_array()) {
            m_customSmartPlaylists.clear();
            for (const auto& splJ : j["customSmartPlaylists"]) {
                CustomSmartPlaylist spl;
                spl.name = splJ.value("name", "Untitled");
                spl.matchAll = splJ.value("matchAll", true);
                spl.limitTracks = splJ.value("limitTracks", 100);
                if (splJ.contains("rules") && splJ["rules"].is_array()) {
                    for (const auto& rJ : splJ["rules"]) {
                        SmartPlaylistRule r;
                        r.field = static_cast<RuleField>(rJ.value("field", 0));
                        r.comparison = static_cast<RuleComparison>(rJ.value("comparison", 0));
                        r.value = rJ.value("value", "");
                        spl.rules.push_back(r);
                    }
                }
                m_customSmartPlaylists.push_back(spl);
            }
        }
        if (j.contains("bookmarks") && j["bookmarks"].is_object()) {
            m_bookmarks.clear();
            for (auto& el : j["bookmarks"].items()) {
                try {
                    uint64_t tid = std::stoull(el.key());
                    m_bookmarks[tid] = el.value().get<double>();
                } catch (...) {}
            }
        }

        rebuildIndices();
        return true;
    } catch (...) {
        return false;
    }
}

bool LibraryManager::saveLibrary(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        nlohmann::json j;
        j["monitoredFolders"] = m_monitoredFolders;
        j["tracks"] = m_tracks;
        j["playlists"] = m_playlists;

        nlohmann::json streamsJson = nlohmann::json::array();
        for (const auto& s : m_radioStreams) {
            streamsJson.push_back({
                {"id", s.id},
                {"name", s.name},
                {"url", s.url},
                {"genre", s.genre},
                {"description", s.description}
            });
        }
        j["radioStreams"] = streamsJson;

        nlohmann::json smartPlJson = nlohmann::json::array();
        for (const auto& spl : m_customSmartPlaylists) {
            nlohmann::json rulesJson = nlohmann::json::array();
            for (const auto& r : spl.rules) {
                rulesJson.push_back({
                    {"field", static_cast<int>(r.field)},
                    {"comparison", static_cast<int>(r.comparison)},
                    {"value", r.value}
                });
            }
            smartPlJson.push_back({
                {"name", spl.name},
                {"matchAll", spl.matchAll},
                {"limitTracks", spl.limitTracks},
                {"rules", rulesJson}
            });
        }
        j["customSmartPlaylists"] = smartPlJson;

        nlohmann::json bmJson = nlohmann::json::object();
        for (const auto& pair : m_bookmarks) {
            bmJson[std::to_string(pair.first)] = pair.second;
        }
        j["bookmarks"] = bmJson;

        std::ofstream file(dbPath);
        if (!file.is_open()) return false;
        file << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

void LibraryManager::scanDirectories(const std::vector<std::string>& folders) {
    if (m_isScanning.load()) return;
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }

    m_isScanning.store(true);
    m_cancelRequested.store(false);
    m_scanProgress.store(0.0f);

    m_scanThread = std::thread([this, folders]() {
        std::vector<std::string> audioFiles;
        const std::set<std::string> validExtensions = {
            ".mp3", ".flac", ".wav", ".ogg", ".m4a", ".aac", ".wma"
        };

        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_scanStatus = "Finding audio files...";
        }

        std::vector<std::string> cueFiles;
        for (const auto& folder : folders) {
            if (m_cancelRequested.load()) break;
            try {
                if (!fs::exists(folder) || !fs::is_directory(folder)) continue;
                for (const auto& entry : fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied)) {
                    if (m_cancelRequested.load()) break;
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (validExtensions.find(ext) != validExtensions.end()) {
                            audioFiles.push_back(entry.path().string());
                        } else if (ext == ".cue") {
                            cueFiles.push_back(entry.path().string());
                        }
                    }
                }
            } catch (...) {}
        }

        if (m_cancelRequested.load() || audioFiles.empty()) {
            m_isScanning.store(false);
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_scanStatus = audioFiles.empty() ? "No audio files found." : "Scan cancelled.";
            return;
        }

        std::map<std::string, Track> existingMap;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& t : m_tracks) {
                existingMap[t.filePath] = t;
            }
        }

        size_t total = audioFiles.size();
        std::vector<Track> processedTracks(total);
        std::vector<bool> trackSuccess(total, false);

        unsigned int numThreads = std::clamp(std::thread::hardware_concurrency(), 2u, 32u);
        std::atomic<size_t> nextIndex{0};
        std::atomic<size_t> completedCount{0};

        std::vector<std::thread> workers;
        workers.reserve(numThreads);

        for (unsigned int t = 0; t < numThreads; ++t) {
            workers.emplace_back([&]() {
                while (!m_cancelRequested.load()) {
                    size_t i = nextIndex.fetch_add(1);
                    if (i >= total) break;

                    const std::string& path = audioFiles[i];
                    auto it = existingMap.find(path);
                    if (it != existingMap.end()) {
                        Track tr = it->second;
                        if (tr.fileModifiedTime == 0) {
                            try {
                                auto ftime = fs::last_write_time(path);
                                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                                );
                                tr.fileModifiedTime = std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
                            } catch (...) {}
                        }
                        processedTracks[i] = std::move(tr);
                        trackSuccess[i] = true;
                    } else {
                        Track tr;
                        if (TagReader::readMetadata(path, tr)) {
                            try {
                                auto ftime = fs::last_write_time(path);
                                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                                );
                                tr.fileModifiedTime = std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
                                tr.dateAdded = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                            } catch (...) {}
                            processedTracks[i] = std::move(tr);
                            trackSuccess[i] = true;
                        }
                    }

                    size_t done = completedCount.fetch_add(1) + 1;
                    if (done % 10 == 0 || done == total) {
                        m_scanProgress.store(static_cast<float>(done) / static_cast<float>(total));
                        std::lock_guard<std::mutex> lock(m_statusMutex);
                        m_scanStatus = "Reading (" + std::to_string(done) + "/" + std::to_string(total) + ") [" + std::to_string(numThreads) + " worker threads]";
                    }
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        if (!m_cancelRequested.load()) {
            std::vector<Track> newTracks;
            newTracks.reserve(total);
            for (size_t i = 0; i < total; ++i) {
                if (trackSuccess[i]) {
                    if (processedTracks[i].id == 0) {
                        processedTracks[i].id = m_nextId++;
                    }
                    newTracks.push_back(std::move(processedTracks[i]));
                }
            }

            // Process any .cue sheets found to expand into subtracks
            for (const auto& cuePath : cueFiles) {
                if (m_cancelRequested.load()) break;
                auto cueTracks = CueSheetParser::parseCueFile(cuePath);
                if (!cueTracks.empty()) {
                    std::set<std::string> cueAudioFiles;
                    for (const auto& ct : cueTracks) cueAudioFiles.insert(ct.filePath);

                    newTracks.erase(
                        std::remove_if(newTracks.begin(), newTracks.end(), [&](const Track& trk) {
                            return cueAudioFiles.find(trk.filePath) != cueAudioFiles.end() && !trk.isCueSubtrack;
                        }),
                        newTracks.end()
                    );

                    for (auto& ct : cueTracks) {
                        ct.id = m_nextId++;
                        newTracks.push_back(std::move(ct));
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_tracks = std::move(newTracks);
                rebuildIndices();
            }
            saveLibrary();
            {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_scanStatus = "Scan complete. Loaded " + std::to_string(m_tracks.size()) + " tracks via " + std::to_string(numThreads) + " threads.";
            }
        }

        m_isScanning.store(false);
    });
}

void LibraryManager::cancelScan() {
    m_cancelRequested.store(true);
}

bool LibraryManager::createPlaylist(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (name.empty() || m_playlists.find(name) != m_playlists.end()) return false;
    m_playlists[name] = {};
    return true;
}

bool LibraryManager::deletePlaylist(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_playlists.find(name);
    if (it != m_playlists.end()) {
        m_playlists.erase(it);
        return true;
    }
    return false;
}

bool LibraryManager::renamePlaylist(const std::string& oldName, const std::string& newName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (newName.empty() || oldName == newName || m_playlists.find(newName) != m_playlists.end()) return false;
    auto it = m_playlists.find(oldName);
    if (it != m_playlists.end()) {
        m_playlists[newName] = std::move(it->second);
        m_playlists.erase(it);
        return true;
    }
    return false;
}

bool LibraryManager::addToPlaylist(const std::string& name, uint64_t trackId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_playlists.find(name);
    if (it != m_playlists.end()) {
        it->second.push_back(trackId);
        return true;
    }
    return false;
}

bool LibraryManager::removeFromPlaylist(const std::string& name, size_t index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_playlists.find(name);
    if (it != m_playlists.end() && index < it->second.size()) {
        it->second.erase(it->second.begin() + index);
        return true;
    }
    return false;
}

bool LibraryManager::exportM3U(const std::string& playlistName, const std::string& m3uPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_playlists.find(playlistName);
    if (it == m_playlists.end()) return false;

    std::ofstream out(m3uPath);
    if (!out.is_open()) return false;

    out << "#EXTM3U\n";
    for (uint64_t tid : it->second) {
        auto idxIt = m_trackIdToIndex.find(tid);
        if (idxIt != m_trackIdToIndex.end()) {
            const auto& t = m_tracks[idxIt->second];
            out << "#EXTINF:" << static_cast<int>(t.duration) << "," << t.getDisplayArtist() << " - " << t.getDisplayTitle() << "\n";
            out << t.filePath << "\n";
        }
    }
    return true;
}

bool LibraryManager::importM3U(const std::string& m3uPath) {
    std::ifstream in(m3uPath);
    if (!in.is_open()) return false;

    std::string plName = fs::path(m3uPath).stem().string();
    std::vector<uint64_t> trackIds;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        fs::path p(line);
        if (!p.is_absolute()) {
            p = fs::path(m3uPath).parent_path() / p;
        }

        std::string fullPath = p.string();
        // Look up track in library
        uint64_t foundId = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& t : m_tracks) {
                if (t.filePath == fullPath) {
                    foundId = t.id;
                    break;
                }
            }
        }

        if (foundId == 0 && fs::exists(fullPath)) {
            Track t;
            if (TagReader::readMetadata(fullPath, t)) {
                std::lock_guard<std::mutex> lock(m_mutex);
                t.id = m_nextId++;
                m_tracks.push_back(t);
                rebuildIndices();
                foundId = t.id;
            }
        }

        if (foundId != 0) {
            trackIds.push_back(foundId);
        }
    }

    if (!trackIds.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_playlists[plName] = trackIds;
        saveLibrary();
        return true;
    }
    return false;
}

void LibraryManager::toggleFavorite(uint64_t trackId) {
    Track* t = getTrackById(trackId);
    if (t) {
        t->isFavorite = !t->isFavorite;
        if (t->isFavorite) {
            t->isDisliked = false;
        }
        saveLibrary();
    }
}

void LibraryManager::toggleDislike(uint64_t trackId) {
    Track* t = getTrackById(trackId);
    if (t) {
        t->isDisliked = !t->isDisliked;
        if (t->isDisliked) {
            t->isFavorite = false;
        }
        saveLibrary();
    }
}

void LibraryManager::setDisliked(uint64_t trackId, bool disliked) {
    Track* t = getTrackById(trackId);
    if (t) {
        t->isDisliked = disliked;
        if (t->isDisliked) {
            t->isFavorite = false;
        }
        saveLibrary();
    }
}

void LibraryManager::incrementPlayCount(uint64_t trackId) {
    Track* t = getTrackById(trackId);
    if (t) {
        t->playCount++;
        saveLibrary();
    }
}

void LibraryManager::setRating(uint64_t trackId, int rating) {
    Track* t = getTrackById(trackId);
    if (t) {
        t->rating = std::clamp(rating, 0, 5);
        saveLibrary();
    }
}

void LibraryManager::addMonitoredFolder(const std::string& folder) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (std::find(m_monitoredFolders.begin(), m_monitoredFolders.end(), folder) == m_monitoredFolders.end()) {
        m_monitoredFolders.push_back(folder);
    }
}

void LibraryManager::removeMonitoredFolder(const std::string& folder) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find(m_monitoredFolders.begin(), m_monitoredFolders.end(), folder);
    if (it != m_monitoredFolders.end()) {
        m_monitoredFolders.erase(it);
    }
}

void LibraryManager::recordTrackPlay(uint64_t trackId) {
    Track* t = getTrackById(trackId);
    if (t) {
        t->playCount++;
        t->lastPlayedTime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        saveLibrary();
    }
}

std::vector<uint64_t> LibraryManager::getRecentlyAddedTrackIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const Track*> sortedTracks;
    sortedTracks.reserve(m_tracks.size());
    for (const auto& t : m_tracks) sortedTracks.push_back(&t);
    std::sort(sortedTracks.begin(), sortedTracks.end(), [](const Track* a, const Track* b) {
        return a->fileModifiedTime > b->fileModifiedTime;
    });
    std::vector<uint64_t> result;
    result.reserve(sortedTracks.size());
    for (const auto* t : sortedTracks) result.push_back(t->id);
    return result;
}

std::vector<std::pair<std::string, std::vector<AlbumInfo>>> LibraryManager::getRecentlyAddedAlbumsCategorized() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_tracks.empty()) return {};

    uint64_t maxTime = 0;
    for (const auto& t : m_tracks) {
        if (t.fileModifiedTime > maxTime) maxTime = t.fileModifiedTime;
    }

    uint64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t refTime = (nowSec > maxTime && (nowSec - maxTime) < 365ULL * 86400ULL) ? nowSec : maxTime;

    uint64_t oneMonthSec = 30ULL * 86400ULL;
    uint64_t threeMonthsSec = 90ULL * 86400ULL;

    std::vector<const Track*> lastMonthTracks;
    std::vector<const Track*> last3MonthsTracks;
    std::vector<const Track*> earlierTracks;

    for (const auto& t : m_tracks) {
        if (refTime >= t.fileModifiedTime && (refTime - t.fileModifiedTime) <= oneMonthSec) {
            lastMonthTracks.push_back(&t);
        } else if (refTime >= t.fileModifiedTime && (refTime - t.fileModifiedTime) <= threeMonthsSec) {
            last3MonthsTracks.push_back(&t);
        } else {
            earlierTracks.push_back(&t);
        }
    }

    auto groupIntoAlbums = [](const std::vector<const Track*>& trackList) -> std::vector<AlbumInfo> {
        std::map<std::string, AlbumInfo> albumMap;
        for (const auto* t : trackList) {
            std::string albName = t->getDisplayAlbum();
            auto& alb = albumMap[albName];
            alb.name = albName;
            if (alb.artist.empty()) alb.artist = t->getDisplayArtist();
            if (alb.year == 0 && t->year > 0) alb.year = t->year;
            alb.trackIds.push_back(t->id);
            if (alb.representativeTrackId == 0) {
                alb.representativeTrackId = t->id;
            }
        }
        std::vector<AlbumInfo> res;
        res.reserve(albumMap.size());
        for (auto& pair : albumMap) res.push_back(std::move(pair.second));
        return res;
    };

    std::vector<std::pair<std::string, std::vector<AlbumInfo>>> sections;
    if (!lastMonthTracks.empty()) {
        sections.emplace_back("Last Month", groupIntoAlbums(lastMonthTracks));
    }
    if (!last3MonthsTracks.empty()) {
        sections.emplace_back("Last 3 Months", groupIntoAlbums(last3MonthsTracks));
    }
    if (!earlierTracks.empty()) {
        sections.emplace_back("Earlier", groupIntoAlbums(earlierTracks));
    }

    if (sections.size() == 1 && sections[0].second.size() > 2) {
        auto allAlbums = sections[0].second;
        sections.clear();
        size_t splitIdx = std::max(size_t(1), allAlbums.size() / 4);
        std::vector<AlbumInfo> topTier(allAlbums.begin(), allAlbums.begin() + splitIdx);
        std::vector<AlbumInfo> restTier(allAlbums.begin() + splitIdx, allAlbums.end());
        sections.emplace_back("Last Month", std::move(topTier));
        sections.emplace_back("Last 3 Months", std::move(restTier));
    }

    return sections;
}

std::vector<uint64_t> LibraryManager::getTop25MostPlayedTrackIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const Track*> sortedTracks;
    sortedTracks.reserve(m_tracks.size());
    for (const auto& t : m_tracks) sortedTracks.push_back(&t);
    std::sort(sortedTracks.begin(), sortedTracks.end(), [](const Track* a, const Track* b) {
        if (a->playCount != b->playCount) return a->playCount > b->playCount;
        return a->fileModifiedTime > b->fileModifiedTime;
    });
    std::vector<uint64_t> result;
    size_t limit = std::min(sortedTracks.size(), static_cast<size_t>(25));
    result.reserve(limit);
    for (size_t i = 0; i < limit; ++i) result.push_back(sortedTracks[i]->id);
    return result;
}

std::vector<uint64_t> LibraryManager::getFavoritesTrackIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint64_t> result;
    for (const auto& t : m_tracks) {
        if (t.isFavorite) result.push_back(t.id);
    }
    return result;
}

std::vector<uint64_t> LibraryManager::getArtistTopTrackIds(const std::string& artistName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const Track*> artistTracks;
    for (const auto& t : m_tracks) {
        if (t.getDisplayArtist() == artistName || t.artist == artistName) {
            artistTracks.push_back(&t);
        }
    }
    std::sort(artistTracks.begin(), artistTracks.end(), [](const Track* a, const Track* b) {
        if (a->playCount != b->playCount) return a->playCount > b->playCount;
        return a->title < b->title;
    });
    std::vector<uint64_t> result;
    result.reserve(artistTracks.size());
    for (const auto* t : artistTracks) result.push_back(t->id);
    return result;
}

std::vector<AlbumInfo> LibraryManager::getAlbumsByArtist(const std::string& artistName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::map<std::string, AlbumInfo> albumMap;
    for (const auto& t : m_tracks) {
        if (t.getDisplayArtist() == artistName || t.artist == artistName) {
            std::string albName = t.getDisplayAlbum();
            auto& alb = albumMap[albName];
            alb.name = albName;
            if (alb.artist.empty()) alb.artist = t.getDisplayArtist();
            if (alb.year == 0 && t.year > 0) alb.year = t.year;
            alb.trackIds.push_back(t.id);
            if (alb.representativeTrackId == 0) {
                alb.representativeTrackId = t.id;
            }
        }
    }
    std::vector<AlbumInfo> result;
    result.reserve(albumMap.size());
    for (auto& pair : albumMap) {
        std::sort(pair.second.trackIds.begin(), pair.second.trackIds.end(), [this](uint64_t aId, uint64_t bId) {
            auto itA = m_trackIdToIndex.find(aId);
            auto itB = m_trackIdToIndex.find(bId);
            if (itA == m_trackIdToIndex.end() || itB == m_trackIdToIndex.end()) return aId < bId;
            const auto& a = m_tracks[itA->second];
            const auto& b = m_tracks[itB->second];
            if (a.trackNumber != b.trackNumber && a.trackNumber > 0 && b.trackNumber > 0) {
                return a.trackNumber < b.trackNumber;
            }
            return a.filePath < b.filePath;
        });
        result.push_back(std::move(pair.second));
    }
    std::sort(result.begin(), result.end(), [](const AlbumInfo& a, const AlbumInfo& b) {
        if (a.year != b.year) return a.year > b.year;
        return a.name < b.name;
    });
    return result;
}

void LibraryManager::getArtistStats(const std::string& artistName, uint32_t& outTotalPlays, size_t& outAlbumCount, size_t& outTrackCount, std::string& outGenre) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    outTotalPlays = 0;
    outAlbumCount = 0;
    outTrackCount = 0;
    outGenre.clear();

    std::set<std::string> uniqueAlbums;
    std::map<std::string, int> genreCounts;

    for (const auto& t : m_tracks) {
        if (t.getDisplayArtist() == artistName || t.artist == artistName) {
            outTotalPlays += t.playCount;
            outTrackCount++;
            uniqueAlbums.insert(t.getDisplayAlbum());
            if (!t.genre.empty()) {
                genreCounts[t.genre]++;
            }
        }
    }
    outAlbumCount = uniqueAlbums.size();

    std::string topGenre;
    int maxCount = 0;
    for (const auto& pair : genreCounts) {
        if (pair.second > maxCount) {
            maxCount = pair.second;
            topGenre = pair.first;
        }
    }
    outGenre = topGenre;
}

void LibraryManager::updateTrackMetadata(
    uint64_t trackId,
    const std::string& title,
    const std::string& artist,
    const std::string& album,
    int year,
    const std::string& genre,
    int trackNumber,
    const std::string& albumArtist,
    const std::string& composer,
    const std::string& comment,
    int discNumber,
    int totalDiscs,
    int trackTotal
) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_trackIdToIndex.find(trackId);
        if (it != m_trackIdToIndex.end()) {
            auto& t = m_tracks[it->second];
            t.title = title;
            t.artist = artist;
            t.album = album;
            t.year = year;
            t.genre = genre;
            t.trackNumber = trackNumber;
            if (!albumArtist.empty()) t.albumArtist = albumArtist;
            if (!composer.empty()) t.composer = composer;
            if (!comment.empty()) t.comment = comment;
            if (discNumber > 0) t.discNumber = discNumber;
            if (totalDiscs > 0) t.totalDiscs = totalDiscs;
            if (trackTotal > 0) t.trackTotal = trackTotal;
        }
    }
    rebuildIndices();
}

bool LibraryManager::importCueSheet(const std::string& cueFilePath) {
    auto cueTracks = CueSheetParser::parseCueFile(cueFilePath);
    if (cueTracks.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::set<std::string> cueAudioFiles;
        for (const auto& ct : cueTracks) cueAudioFiles.insert(ct.filePath);

        m_tracks.erase(
            std::remove_if(m_tracks.begin(), m_tracks.end(), [&](const Track& trk) {
                return cueAudioFiles.find(trk.filePath) != cueAudioFiles.end() && !trk.isCueSubtrack;
            }),
            m_tracks.end()
        );

        for (auto& ct : cueTracks) {
            ct.id = m_nextId++;
            m_tracks.push_back(std::move(ct));
        }
    }
    rebuildIndices();
    saveLibrary();
    return true;
}

std::vector<uint64_t> LibraryManager::getTopRatedTrackIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint64_t> result;
    for (const auto& t : m_tracks) {
        if (t.rating >= 4) result.push_back(t.id);
    }
    return result;
}

std::vector<uint64_t> LibraryManager::getNeverPlayedTrackIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint64_t> result;
    for (const auto& t : m_tracks) {
        if (t.playCount == 0) result.push_back(t.id);
    }
    return result;
}

void LibraryManager::initDefaultRadioStreams() {
    if (!m_radioStreams.empty()) return;
    m_radioStreams = {
        {m_nextStreamId++, "SomaFM Groove Salad", "https://ice2.somafm.com/groovesalad-128-mp3", "Ambient / Chillout", "Downtempo ambient grooves and chilled beats."},
        {m_nextStreamId++, "SomaFM Drone Zone", "https://ice2.somafm.com/dronezone-128-mp3", "Space Ambient", "Atmospheric, spaced-out ambient music with texture."},
        {m_nextStreamId++, "SomaFM DEF CON Radio", "https://ice2.somafm.com/defcon-128-mp3", "Electronic", "Hacker music, underground synth, and ambient cyber beats."},
        {m_nextStreamId++, "Lofi Girl Beats", "https://stream.zeno.fm/f3wvbbqmdg8uv", "Lofi / Study", "24/7 peaceful lofi hip hop radio - beats to relax/study to."},
        {m_nextStreamId++, "Jazz24", "https://live.wostreaming.net/manifest/ppm-jazz24aac-ibc1", "Jazz", "Premier jazz from Miles Davis, Ella Fitzgerald, and Coltrane."},
        {m_nextStreamId++, "Classic FM", "https://media-ssl.musicradio.com/ClassicFM", "Classical", "World's greatest classical music station from London."}
    };
}

void LibraryManager::addRadioStream(const std::string& name, const std::string& url, const std::string& genre, const std::string& description) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RadioStream s;
    s.id = m_nextStreamId++;
    s.name = name.empty() ? "Custom Stream" : name;
    s.url = url;
    s.genre = genre.empty() ? "Web Radio" : genre;
    s.description = description;
    m_radioStreams.push_back(s);
}

bool LibraryManager::removeRadioStream(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::remove_if(m_radioStreams.begin(), m_radioStreams.end(), [id](const RadioStream& s) {
        return s.id == id;
    });
    if (it != m_radioStreams.end()) {
        m_radioStreams.erase(it, m_radioStreams.end());
        return true;
    }
    return false;
}

uint64_t LibraryManager::selectAutoDJTrack(const std::vector<uint64_t>& recentQueue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_tracks.empty()) return 0;

    std::set<uint64_t> recentTrackIds;
    std::set<std::string> recentArtists;

    size_t trackHistoryLen = std::min<size_t>(recentQueue.size(), static_cast<size_t>(m_autoDJConfig.trackSeparationTracks));
    for (size_t i = recentQueue.size() - trackHistoryLen; i < recentQueue.size(); ++i) {
        recentTrackIds.insert(recentQueue[i]);
    }

    size_t artistHistoryLen = std::min<size_t>(recentQueue.size(), static_cast<size_t>(m_autoDJConfig.artistSeparationTracks));
    for (size_t i = recentQueue.size() - artistHistoryLen; i < recentQueue.size(); ++i) {
        auto it = m_trackIdToIndex.find(recentQueue[i]);
        if (it != m_trackIdToIndex.end() && it->second < m_tracks.size()) {
            recentArtists.insert(m_tracks[it->second].getDisplayArtist());
        }
    }

    std::vector<uint64_t> candidates;
    for (const auto& t : m_tracks) {
        if (t.isStream || t.isDisliked) continue;
        if (m_autoDJConfig.favoritesOnly && !t.isFavorite) continue;
        if (m_autoDJConfig.minRating > 0 && t.rating < m_autoDJConfig.minRating) continue;
        if (!m_autoDJConfig.genreFilter.empty() && t.genre != m_autoDJConfig.genreFilter) continue;
        if (recentTrackIds.count(t.id)) continue;
        if (recentArtists.count(t.getDisplayArtist())) continue;

        candidates.push_back(t.id);
    }

    if (candidates.empty()) {
        for (const auto& t : m_tracks) {
            if (t.isStream || t.isDisliked) continue;
            if (recentTrackIds.count(t.id)) continue;
            candidates.push_back(t.id);
        }
    }

    if (candidates.empty()) {
        for (const auto& t : m_tracks) {
            if (!t.isStream && !t.isDisliked) candidates.push_back(t.id);
        }
    }

    if (candidates.empty()) return 0;

    size_t idx = static_cast<size_t>(rand()) % candidates.size();
    return candidates[idx];
}

void LibraryManager::updateBatchMetadata(const std::vector<uint64_t>& trackIds, const std::string& artist, const std::string& album, int year, const std::string& genre, int rating) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (uint64_t id : trackIds) {
            auto it = m_trackIdToIndex.find(id);
            if (it != m_trackIdToIndex.end() && it->second < m_tracks.size()) {
                auto& t = m_tracks[it->second];
                if (!artist.empty()) t.artist = artist;
                if (!album.empty()) t.album = album;
                if (year > 0) t.year = year;
                if (!genre.empty()) t.genre = genre;
                if (rating >= 0) t.rating = rating;
            }
        }
    }
    rebuildIndices();
}

void LibraryManager::batchConvertCase(const std::vector<uint64_t>& trackIds, CaseConversion mode) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (uint64_t id : trackIds) {
            auto it = m_trackIdToIndex.find(id);
            if (it != m_trackIdToIndex.end() && it->second < m_tracks.size()) {
                auto& t = m_tracks[it->second];
                t.title = convertStringToCase(t.title, mode);
                t.artist = convertStringToCase(t.artist, mode);
                t.album = convertStringToCase(t.album, mode);
                t.genre = convertStringToCase(t.genre, mode);
            }
        }
    }
    rebuildIndices();
}

void LibraryManager::batchResetPlayCount(const std::vector<uint64_t>& trackIds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (uint64_t id : trackIds) {
        auto it = m_trackIdToIndex.find(id);
        if (it != m_trackIdToIndex.end() && it->second < m_tracks.size()) {
            m_tracks[it->second].playCount = 0;
            m_tracks[it->second].lastPlayedTime = 0;
        }
    }
}

void LibraryManager::batchSetRating(const std::vector<uint64_t>& trackIds, int rating) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (uint64_t id : trackIds) {
        auto it = m_trackIdToIndex.find(id);
        if (it != m_trackIdToIndex.end() && it->second < m_tracks.size()) {
            m_tracks[it->second].rating = rating;
        }
    }
}

void LibraryManager::deleteTracksFromLibrary(const std::vector<uint64_t>& trackIds) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::set<uint64_t> toDelete(trackIds.begin(), trackIds.end());
        m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(), [&toDelete](const Track& t) {
            return toDelete.count(t.id) > 0;
        }), m_tracks.end());

        for (auto& pair : m_playlists) {
            pair.second.erase(std::remove_if(pair.second.begin(), pair.second.end(), [&toDelete](uint64_t id) {
                return toDelete.count(id) > 0;
            }), pair.second.end());
        }
    }
    rebuildIndices();
}

bool LibraryManager::deleteTracksPermanently(const std::vector<uint64_t>& trackIds, bool deleteFromDisk) {
    if (trackIds.empty()) return false;
    std::vector<std::string> filesToDelete;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::set<uint64_t> toDelete(trackIds.begin(), trackIds.end());
        for (const auto& t : m_tracks) {
            if (toDelete.count(t.id) > 0 && !t.filePath.empty()) {
                filesToDelete.push_back(t.filePath);
            }
        }
        m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(), [&toDelete](const Track& t) {
            return toDelete.count(t.id) > 0;
        }), m_tracks.end());

        for (auto& pair : m_playlists) {
            pair.second.erase(std::remove_if(pair.second.begin(), pair.second.end(), [&toDelete](uint64_t id) {
                return toDelete.count(id) > 0;
            }), pair.second.end());
        }
    }
    rebuildIndices();

    if (deleteFromDisk) {
        for (const auto& f : filesToDelete) {
            std::error_code ec;
            fs::remove(f, ec);
        }
    }
    saveLibrary();
    return true;
}

bool LibraryManager::deleteTrackPermanently(uint64_t trackId, bool deleteFromDisk) {
    return deleteTracksPermanently({trackId}, deleteFromDisk);
}

bool LibraryManager::deleteAlbumPermanently(const std::string& albumName, const std::string& artistName, bool deleteFromDisk) {
    std::vector<uint64_t> toDeleteIds;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& t : m_tracks) {
            if (t.album == albumName) {
                if (artistName.empty() || t.artist == artistName || t.getDisplayArtist() == artistName) {
                    toDeleteIds.push_back(t.id);
                }
            }
        }
    }
    if (toDeleteIds.empty()) return false;
    return deleteTracksPermanently(toDeleteIds, deleteFromDisk);
}

LibraryStats LibraryManager::calculateStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    LibraryStats stats;
    stats.totalTracks = m_tracks.size();
    stats.totalAlbums = m_albums.size();
    stats.totalArtists = m_artists.size();
    stats.totalPlaylists = m_playlists.size();

    std::map<std::string, uint32_t> artistPlays;
    std::map<std::string, uint32_t> trackPlays;

    for (const auto& t : m_tracks) {
        stats.totalDurationSeconds += t.duration;
        stats.totalFileSizeBytes += t.fileSizeBytes;
        stats.totalPlayCount += t.playCount;

        size_t lastDot = t.filePath.find_last_of('.');
        if (lastDot != std::string::npos) {
            std::string ext = t.filePath.substr(lastDot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
            stats.formatCounts[ext]++;
        } else {
            stats.formatCounts["OTHER"]++;
        }

        artistPlays[t.getDisplayArtist()] += t.playCount;
        trackPlays[t.getDisplayArtist() + " - " + t.getDisplayTitle()] += t.playCount;
    }

    std::vector<std::pair<std::string, uint32_t>> sortedArtists(artistPlays.begin(), artistPlays.end());
    std::sort(sortedArtists.begin(), sortedArtists.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (sortedArtists.size() > 10) sortedArtists.resize(10);
    stats.topArtists = std::move(sortedArtists);

    std::vector<std::pair<std::string, uint32_t>> sortedTracks(trackPlays.begin(), trackPlays.end());
    std::sort(sortedTracks.begin(), sortedTracks.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (sortedTracks.size() > 10) sortedTracks.resize(10);
    stats.topTracks = std::move(sortedTracks);

    return stats;
}

std::vector<DuplicateCandidate> LibraryManager::findDuplicates() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DuplicateCandidate> dupes;
    std::map<std::string, std::vector<const Track*>> titleArtistMap;

    for (const auto& t : m_tracks) {
        if (t.isStream) continue;
        std::string key = trim(t.getDisplayArtist()) + "|||" + trim(t.getDisplayTitle());
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        titleArtistMap[key].push_back(&t);
    }

    for (const auto& pair : titleArtistMap) {
        if (pair.second.size() > 1) {
            for (size_t i = 0; i < pair.second.size() - 1; ++i) {
                for (size_t j = i + 1; j < pair.second.size(); ++j) {
                    DuplicateCandidate cand;
                    cand.trackId1 = pair.second[i]->id;
                    cand.trackId2 = pair.second[j]->id;
                    cand.artist = pair.second[i]->getDisplayArtist();
                    cand.title = pair.second[i]->getDisplayTitle();
                    cand.path1 = pair.second[i]->filePath;
                    cand.path2 = pair.second[j]->filePath;
                    cand.duration1 = pair.second[i]->duration;
                    cand.duration2 = pair.second[j]->duration;
                    cand.matchReason = "Identical Title & Artist";
                    dupes.push_back(cand);
                }
            }
        }
    }
    return dupes;
}

void LibraryManager::addCustomSmartPlaylist(const CustomSmartPlaylist& pl) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& existing : m_customSmartPlaylists) {
        if (existing.name == pl.name) {
            existing = pl;
            return;
        }
    }
    m_customSmartPlaylists.push_back(pl);
}

bool LibraryManager::deleteCustomSmartPlaylist(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_customSmartPlaylists.begin(); it != m_customSmartPlaylists.end(); ++it) {
        if (it->name == name) {
            m_customSmartPlaylists.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<uint64_t> LibraryManager::evaluateSmartPlaylist(const CustomSmartPlaylist& pl) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint64_t> result;
    if (pl.rules.empty()) {
        for (const auto& tr : m_tracks) {
            if (result.size() >= pl.limitTracks) break;
            result.push_back(tr.id);
        }
        return result;
    }

    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };

    for (const auto& tr : m_tracks) {
        if (tr.isStream) continue;

        bool trackMatched = pl.matchAll;
        if (!pl.matchAll) trackMatched = false;

        for (const auto& rule : pl.rules) {
            bool rulePass = false;
            std::string ruleValLower = toLower(rule.value);

            switch (rule.field) {
                case RuleField::Title: {
                    std::string t = toLower(tr.getDisplayTitle());
                    if (rule.comparison == RuleComparison::Contains) rulePass = (t.find(ruleValLower) != std::string::npos);
                    else if (rule.comparison == RuleComparison::Equals) rulePass = (t == ruleValLower);
                    break;
                }
                case RuleField::Artist: {
                    std::string a = toLower(tr.getDisplayArtist());
                    if (rule.comparison == RuleComparison::Contains) rulePass = (a.find(ruleValLower) != std::string::npos);
                    else if (rule.comparison == RuleComparison::Equals) rulePass = (a == ruleValLower);
                    break;
                }
                case RuleField::Album: {
                    std::string al = toLower(tr.getDisplayAlbum());
                    if (rule.comparison == RuleComparison::Contains) rulePass = (al.find(ruleValLower) != std::string::npos);
                    else if (rule.comparison == RuleComparison::Equals) rulePass = (al == ruleValLower);
                    break;
                }
                case RuleField::Genre: {
                    std::string g = toLower(tr.genre);
                    if (rule.comparison == RuleComparison::Contains) rulePass = (g.find(ruleValLower) != std::string::npos);
                    else if (rule.comparison == RuleComparison::Equals) rulePass = (g == ruleValLower);
                    break;
                }
                case RuleField::Rating: {
                    int val = 0;
                    try { val = std::stoi(rule.value); } catch (...) {}
                    if (rule.comparison == RuleComparison::Equals) rulePass = (tr.rating == val);
                    else if (rule.comparison == RuleComparison::GreaterThan) rulePass = (tr.rating >= val);
                    else if (rule.comparison == RuleComparison::LessThan) rulePass = (tr.rating <= val);
                    break;
                }
                case RuleField::Year: {
                    int val = 0;
                    try { val = std::stoi(rule.value); } catch (...) {}
                    if (rule.comparison == RuleComparison::Equals) rulePass = (tr.year == val);
                    else if (rule.comparison == RuleComparison::GreaterThan) rulePass = (tr.year >= val);
                    else if (rule.comparison == RuleComparison::LessThan) rulePass = (tr.year <= val);
                    break;
                }
                case RuleField::PlayCount: {
                    int val = 0;
                    try { val = std::stoi(rule.value); } catch (...) {}
                    if (rule.comparison == RuleComparison::Equals) rulePass = (tr.playCount == val);
                    else if (rule.comparison == RuleComparison::GreaterThan) rulePass = (tr.playCount >= val);
                    else if (rule.comparison == RuleComparison::LessThan) rulePass = (tr.playCount <= val);
                    break;
                }
            }

            if (pl.matchAll) {
                if (!rulePass) {
                    trackMatched = false;
                    break;
                }
            } else {
                if (rulePass) {
                    trackMatched = true;
                    break;
                }
            }
        }

        if (trackMatched) {
            result.push_back(tr.id);
            if (result.size() >= pl.limitTracks) break;
        }
    }
    return result;
}

void LibraryManager::addHistoryEntry(uint64_t trackId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_trackIdToIndex.find(trackId);
    if (it == m_trackIdToIndex.end() || it->second >= m_tracks.size()) return;

    const auto& tr = m_tracks[it->second];
    HistoryEntry entry;
    entry.trackId = trackId;
    entry.title = tr.getDisplayTitle();
    entry.artist = tr.getDisplayArtist();
    entry.album = tr.getDisplayAlbum();
    entry.epochSeconds = static_cast<uint64_t>(std::time(nullptr));

    std::time_t t = static_cast<std::time_t>(entry.epochSeconds);
    char buf[64];
    std::tm tmVal;
#ifdef _WIN32
    localtime_s(&tmVal, &t);
#else
    localtime_r(&t, &tmVal);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmVal);
    entry.timestampStr = buf;

    m_history.insert(m_history.begin(), entry);
    if (m_history.size() > 150) {
        m_history.pop_back();
    }
}

void LibraryManager::clearHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();
}

void LibraryManager::setTrackBookmark(uint64_t trackId, double timestampSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bookmarks[trackId] = timestampSeconds;
}

double LibraryManager::getTrackBookmark(uint64_t trackId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_bookmarks.find(trackId);
    if (it != m_bookmarks.end()) {
        return it->second;
    }
    return 0.0;
}

std::vector<uint64_t> LibraryManager::getInboxTrackIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint64_t> result;
    for (const auto& track : m_tracks) {
        if (track.isInbox) {
            result.push_back(track.id);
        }
    }
    return result;
}

void LibraryManager::moveFromInboxToLibrary(const std::vector<uint64_t>& trackIds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::set<uint64_t> setIds(trackIds.begin(), trackIds.end());
    for (auto& track : m_tracks) {
        if (setIds.count(track.id)) {
            track.isInbox = false;
        }
    }
}




