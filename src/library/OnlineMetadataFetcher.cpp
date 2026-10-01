#include "OnlineMetadataFetcher.h"
#include "../network/HttpClient.h"
#include "../third_party/json.hpp"
#include <fstream>
#include <thread>
#include <regex>

std::vector<ArtworkCandidate> OnlineMetadataFetcher::searchAlbumArtwork(const std::string& artist, const std::string& album) {
    std::vector<ArtworkCandidate> candidates;
    if (artist.empty() && album.empty()) return candidates;

    std::string query = artist;
    if (!album.empty()) {
        if (!query.empty()) query += " ";
        query += album;
    }

    std::string encodedQuery = HttpClient::urlEncode(query);
    std::string url = "https://itunes.apple.com/search?term=" + encodedQuery + "&entity=album&limit=8";

    HttpResponse resp = HttpClient::get(url);
    if (!resp.success || resp.statusCode != 200 || resp.body.empty()) {
        return candidates;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(resp.body);
        if (root.contains("results") && root["results"].is_array()) {
            for (const auto& item : root["results"]) {
                ArtworkCandidate cand;
                if (item.contains("collectionName") && item["collectionName"].is_string()) {
                    cand.albumName = item["collectionName"].get<std::string>();
                }
                if (item.contains("artistName") && item["artistName"].is_string()) {
                    cand.artistName = item["artistName"].get<std::string>();
                }
                if (item.contains("releaseDate") && item["releaseDate"].is_string()) {
                    cand.releaseDate = item["releaseDate"].get<std::string>();
                    if (cand.releaseDate.length() >= 10) {
                        cand.releaseDate = cand.releaseDate.substr(0, 10);
                    }
                }
                if (item.contains("artworkUrl100") && item["artworkUrl100"].is_string()) {
                    std::string url100 = item["artworkUrl100"].get<std::string>();
                    cand.previewUrl = url100;
                    // Upgrade 100x100 to 600x600 for sharp cover art
                    std::string highRes = url100;
                    size_t pos = highRes.find("100x100bb");
                    if (pos != std::string::npos) {
                        highRes.replace(pos, 9, "600x600bb");
                    }
                    cand.highResUrl = highRes;
                }
                if (!cand.previewUrl.empty() || !cand.highResUrl.empty()) {
                    candidates.push_back(cand);
                }
            }
        }
    } catch (...) {
        // Fallback gracefully on parsing errors
    }

    return candidates;
}

void OnlineMetadataFetcher::searchAlbumArtworkAsync(
    const std::string& artist,
    const std::string& album,
    std::function<void(const std::vector<ArtworkCandidate>& candidates)> callback
) {
    std::thread([artist, album, callback]() {
        auto results = searchAlbumArtwork(artist, album);
        if (callback) {
            callback(results);
        }
    }).detach();
}

bool OnlineMetadataFetcher::downloadArtwork(const std::string& url, std::vector<uint8_t>& outBytes) {
    if (url.empty()) return false;
    HttpResponse resp = HttpClient::get(url);
    if (resp.success && resp.statusCode == 200 && !resp.binaryData.empty()) {
        outBytes = std::move(resp.binaryData);
        return true;
    }
    if (resp.success && resp.statusCode == 200 && !resp.body.empty()) {
        outBytes.assign(resp.body.begin(), resp.body.end());
        return true;
    }
    return false;
}

bool OnlineMetadataFetcher::downloadAndSaveArtwork(const std::string& url, const std::string& destFilePath) {
    std::vector<uint8_t> bytes;
    if (!downloadArtwork(url, bytes) || bytes.empty()) {
        return false;
    }

    std::ofstream out(destFilePath, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}
