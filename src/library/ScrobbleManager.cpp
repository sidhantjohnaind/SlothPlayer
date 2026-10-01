#include "ScrobbleManager.h"
#include "../network/HttpClient.h"
#include "../third_party/json.hpp"
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <map>
#include <cmath>
#include <cstring>

// Standard RFC 1321 MD5 Implementation
namespace {
    struct MD5Context {
        uint32_t state[4];
        uint32_t count[2];
        uint8_t buffer[64];
    };

    static void MD5Transform(uint32_t state[4], const uint8_t block[64]);

    static void MD5Init(MD5Context* context) {
        context->count[0] = context->count[1] = 0;
        context->state[0] = 0x67452301;
        context->state[1] = 0xefcdab89;
        context->state[2] = 0x98badcfe;
        context->state[3] = 0x10325476;
    }

    static void MD5Update(MD5Context* context, const uint8_t* input, size_t inputLen) {
        size_t i = 0, index = 0, partLen = 0;
        index = (size_t)((context->count[0] >> 3) & 0x3F);
        if ((context->count[0] += ((uint32_t)inputLen << 3)) < ((uint32_t)inputLen << 3)) {
            context->count[1]++;
        }
        context->count[1] += ((uint32_t)inputLen >> 29);
        partLen = 64 - index;

        if (inputLen >= partLen) {
            std::memcpy(&context->buffer[index], input, partLen);
            MD5Transform(context->state, context->buffer);
            for (i = partLen; i + 63 < inputLen; i += 64) {
                MD5Transform(context->state, &input[i]);
            }
            index = 0;
        } else {
            i = 0;
        }
        std::memcpy(&context->buffer[index], &input[i], inputLen - i);
    }

    static void MD5Final(uint8_t digest[16], MD5Context* context) {
        uint8_t bits[8];
        for (int i = 0; i < 4; i++) bits[i] = (uint8_t)((context->count[0] >> (i * 8)) & 0xFF);
        for (int i = 0; i < 4; i++) bits[i + 4] = (uint8_t)((context->count[1] >> (i * 8)) & 0xFF);

        size_t index = (size_t)((context->count[0] >> 3) & 0x3f);
        size_t padLen = (index < 56) ? (56 - index) : (120 - index);
        static const uint8_t PADDING[64] = { 0x80 };
        MD5Update(context, PADDING, padLen);
        MD5Update(context, bits, 8);

        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                digest[i * 4 + j] = (uint8_t)((context->state[i] >> (j * 8)) & 0xFF);
            }
        }
    }

    #define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
    #define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
    #define H(x, y, z) ((x) ^ (y) ^ (z))
    #define I(x, y, z) ((y) ^ ((x) | (~z)))
    #define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))
    #define FF(a, b, c, d, x, s, ac) { (a) += F((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = ROTATE_LEFT((a), (s)); (a) += (b); }
    #define GG(a, b, c, d, x, s, ac) { (a) += G((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = ROTATE_LEFT((a), (s)); (a) += (b); }
    #define HH(a, b, c, d, x, s, ac) { (a) += H((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = ROTATE_LEFT((a), (s)); (a) += (b); }
    #define II(a, b, c, d, x, s, ac) { (a) += I((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = ROTATE_LEFT((a), (s)); (a) += (b); }

    static void MD5Transform(uint32_t state[4], const uint8_t block[64]) {
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
        for (int i = 0; i < 16; i++) {
            x[i] = ((uint32_t)block[i * 4]) | (((uint32_t)block[i * 4 + 1]) << 8) |
                   (((uint32_t)block[i * 4 + 2]) << 16) | (((uint32_t)block[i * 4 + 3]) << 24);
        }

        FF(a, b, c, d, x[ 0],  7, 0xd76aa478); FF(d, a, b, c, x[ 1], 12, 0xe8c7b756);
        FF(c, d, a, b, x[ 2], 17, 0x242070db); FF(b, c, d, a, x[ 3], 22, 0xc1bdceee);
        FF(a, b, c, d, x[ 4],  7, 0xf57c0faf); FF(d, a, b, c, x[ 5], 12, 0x4787c62a);
        FF(c, d, a, b, x[ 6], 17, 0xa8304613); FF(b, c, d, a, x[ 7], 22, 0xfd469501);
        FF(a, b, c, d, x[ 8],  7, 0x698098d8); FF(d, a, b, c, x[ 9], 12, 0x8b44f7af);
        FF(c, d, a, b, x[10], 17, 0xffff5bb1); FF(b, c, d, a, x[11], 22, 0x895cd7be);
        FF(a, b, c, d, x[12],  7, 0x6b901122); FF(d, a, b, c, x[13], 12, 0xfd987193);
        FF(c, d, a, b, x[14], 17, 0xa679438e); FF(b, c, d, a, x[15], 22, 0x49b40821);

        GG(a, b, c, d, x[ 1],  5, 0xf61e2562); GG(d, a, b, c, x[ 6],  9, 0xc040b340);
        GG(c, d, a, b, x[11], 14, 0x265e5a51); GG(b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
        GG(a, b, c, d, x[ 5],  5, 0xd62f105d); GG(d, a, b, c, x[10],  9, 0x02441453);
        GG(c, d, a, b, x[15], 14, 0xd8a1e681); GG(b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
        GG(a, b, c, d, x[ 9],  5, 0x21e1cde6); GG(d, a, b, c, x[14],  9, 0xc33707d6);
        GG(c, d, a, b, x[ 3], 14, 0xf4d50d87); GG(b, c, d, a, x[ 8], 20, 0x455a14ed);
        GG(a, b, c, d, x[13],  5, 0xa9e3e905); GG(d, a, b, c, x[ 2],  9, 0xfcefa3f8);
        GG(c, d, a, b, x[ 7], 14, 0x676f02d9); GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

        HH(a, b, c, d, x[ 5],  4, 0xfffa3942); HH(d, a, b, c, x[ 8], 11, 0x8771f681);
        HH(c, d, a, b, x[11], 16, 0x6d9d6122); HH(b, c, d, a, x[14], 23, 0xfde5380c);
        HH(a, b, c, d, x[ 1],  4, 0xa4beea44); HH(d, a, b, c, x[ 4], 11, 0x4bdecfa9);
        HH(c, d, a, b, x[ 7], 16, 0xf6bb4b60); HH(b, c, d, a, x[10], 23, 0xbebfbc70);
        HH(a, b, c, d, x[13],  4, 0x289b7ec6); HH(d, a, b, c, x[ 0], 11, 0xeaa127fa);
        HH(c, d, a, b, x[ 3], 16, 0xd4ef3085); HH(b, c, d, a, x[ 6], 23, 0x04881d05);
        HH(a, b, c, d, x[ 9],  4, 0xd9d4d039); HH(d, a, b, c, x[12], 11, 0xe6db99e5);
        HH(c, d, a, b, x[15], 16, 0x1fa27cf8); HH(b, c, d, a, x[ 2], 23, 0xc4ac5665);

        II(a, b, c, d, x[ 0],  6, 0xf4292244); II(d, a, b, c, x[ 7], 10, 0x432aff97);
        II(c, d, a, b, x[14], 15, 0xab9423a7); II(b, c, d, a, x[ 5], 21, 0xfc93a039);
        II(a, b, c, d, x[12],  6, 0x655b59c3); II(d, a, b, c, x[ 3], 10, 0x8f0ccc92);
        II(c, d, a, b, x[10], 15, 0xffeff47d); II(b, c, d, a, x[ 1], 21, 0x85845dd1);
        II(a, b, c, d, x[ 8],  6, 0x6fa87e4f); II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
        II(c, d, a, b, x[ 6], 15, 0xa3014314); II(b, c, d, a, x[13], 21, 0x4e0811a1);
        II(a, b, c, d, x[ 4],  6, 0xf7537e82); II(d, a, b, c, x[11], 10, 0xbd3af235);
        II(c, d, a, b, x[ 2], 15, 0x2ad7d2bb); II(b, c, d, a, x[ 9], 21, 0xeb86d391);

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    }

    static std::string computeMD5(const std::string& input) {
        MD5Context ctx;
        MD5Init(&ctx);
        MD5Update(&ctx, reinterpret_cast<const uint8_t*>(input.data()), input.size());
        uint8_t digest[16];
        MD5Final(digest, &ctx);

        std::ostringstream oss;
        for (int i = 0; i < 16; i++) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
        }
        return oss.str();
    }
}

ScrobbleManager::ScrobbleManager() {
    loadFromFile();
}

void ScrobbleManager::setLastFmCredentials(
    const std::string& username,
    const std::string& sessionKey,
    const std::string& apiKey,
    const std::string& apiSecret
) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_lastFmUsername = username;
    m_lastFmSessionKey = sessionKey;
    if (!apiKey.empty()) m_lastFmApiKey = apiKey;
    if (!apiSecret.empty()) m_lastFmApiSecret = apiSecret;
    saveToFile();
}

std::string ScrobbleManager::getLastFmUsername() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_lastFmUsername;
}

std::string ScrobbleManager::getLastFmSessionKey() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_lastFmSessionKey;
}

bool ScrobbleManager::hasLastFmAuth() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return !m_lastFmUsername.empty() && !m_lastFmSessionKey.empty();
}

void ScrobbleManager::onTrackStarted(const Track& track) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_currentTrack = track;
    m_hasActiveTrack = true;
    m_alreadyScrobbled = false;
    m_maxElapsed = 0.0;

    if (m_enabled && hasLastFmAuth()) {
        sendLastFmNowPlaying(track);
    }
}

void ScrobbleManager::onTrackProgress(double currentSec, double totalSec) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_enabled || !m_hasActiveTrack || m_alreadyScrobbled) {
        return;
    }

    if (currentSec > m_maxElapsed) {
        m_maxElapsed = currentSec;
    }

    double dur = totalSec > 0.0 ? totalSec : m_currentTrack.duration;
    if (dur < 30.0) {
        return;
    }

    bool reachHalf = (dur > 0.0 && m_maxElapsed >= (dur * 0.5));
    bool reachFourMinutes = (m_maxElapsed >= 240.0);

    if (reachHalf || reachFourMinutes) {
        doScrobbleCurrent();
    }
}

void ScrobbleManager::onTrackEnded() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_hasActiveTrack = false;
    m_alreadyScrobbled = false;
    m_maxElapsed = 0.0;
}

void ScrobbleManager::sendLastFmNowPlaying(const Track& track) {
    if (!hasLastFmAuth() || track.isStream) return;
    std::string artist = track.getDisplayArtist();
    std::string title = track.getDisplayTitle();
    std::string album = track.getDisplayAlbum();
    std::string sk = m_lastFmSessionKey;
    std::string apiKey = m_lastFmApiKey.empty() ? "c4d7ec6b6cfc8b0e8b849fb2a4d3ec63" : m_lastFmApiKey;
    std::string secret = m_lastFmApiSecret;

    std::thread([artist, title, album, sk, apiKey, secret]() {
        std::map<std::string, std::string> params;
        params["album"] = album;
        params["api_key"] = apiKey;
        params["artist"] = artist;
        params["method"] = "track.updateNowPlaying";
        params["sk"] = sk;
        params["track"] = title;

        if (!secret.empty()) {
            std::string sigStr;
            for (const auto& [k, v] : params) {
                sigStr += k + v;
            }
            sigStr += secret;
            params["api_sig"] = computeMD5(sigStr);
        }

        std::string postData;
        for (const auto& [k, v] : params) {
            if (!postData.empty()) postData += "&";
            postData += HttpClient::urlEncode(k) + "=" + HttpClient::urlEncode(v);
        }

        HttpClient::post("http://ws.audioscrobbler.com/2.0/", postData, "application/x-www-form-urlencoded");
    }).detach();
}

bool ScrobbleManager::sendLastFmScrobble(
    const std::string& artist,
    const std::string& title,
    const std::string& album,
    uint64_t timestamp
) {
    if (!hasLastFmAuth()) return false;
    std::string sk = m_lastFmSessionKey;
    std::string apiKey = m_lastFmApiKey.empty() ? "c4d7ec6b6cfc8b0e8b849fb2a4d3ec63" : m_lastFmApiKey;
    std::string secret = m_lastFmApiSecret;

    std::map<std::string, std::string> params;
    params["album"] = album;
    params["api_key"] = apiKey;
    params["artist"] = artist;
    params["method"] = "track.scrobble";
    params["sk"] = sk;
    params["timestamp"] = std::to_string(timestamp);
    params["track"] = title;

    if (!secret.empty()) {
        std::string sigStr;
        for (const auto& [k, v] : params) {
            sigStr += k + v;
        }
        sigStr += secret;
        params["api_sig"] = computeMD5(sigStr);
    }

    std::string postData;
    for (const auto& [k, v] : params) {
        if (!postData.empty()) postData += "&";
        postData += HttpClient::urlEncode(k) + "=" + HttpClient::urlEncode(v);
    }

    HttpResponse resp = HttpClient::post("http://ws.audioscrobbler.com/2.0/", postData, "application/x-www-form-urlencoded");
    return (resp.success && resp.statusCode == 200);
}

void ScrobbleManager::doScrobbleCurrent() {
    if (m_currentTrack.isStream || m_alreadyScrobbled) {
        return;
    }

    ScrobbleRecord rec;
    rec.title = m_currentTrack.getDisplayTitle();
    rec.artist = m_currentTrack.getDisplayArtist();
    rec.album = m_currentTrack.getDisplayAlbum();

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    rec.timestamp = static_cast<uint64_t>(tt);

    std::tm tm{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    rec.timestampStr = oss.str();
    rec.submitted = !hasLastFmAuth(); // If no online auth, marked true as local record

    if (hasLastFmAuth()) {
        std::string artist = rec.artist;
        std::string title = rec.title;
        std::string album = rec.album;
        uint64_t ts = rec.timestamp;

        std::thread([this, artist, title, album, ts]() {
            bool ok = sendLastFmScrobble(artist, title, album, ts);
            if (ok) {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                for (auto& r : m_history) {
                    if (r.timestamp == ts && r.title == title) {
                        r.submitted = true;
                        break;
                    }
                }
                saveToFile();
            }
        }).detach();
    }

    m_history.insert(m_history.begin(), rec);
    m_alreadyScrobbled = true;

    saveToFile();
}

void ScrobbleManager::retryUnsubmitted() {
    if (!hasLastFmAuth()) return;
    std::thread([this]() {
        std::vector<ScrobbleRecord> unsubmitted;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            for (const auto& r : m_history) {
                if (!r.submitted) unsubmitted.push_back(r);
            }
        }

        for (const auto& r : unsubmitted) {
            bool ok = sendLastFmScrobble(r.artist, r.title, r.album, r.timestamp);
            if (ok) {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                for (auto& item : m_history) {
                    if (item.timestamp == r.timestamp && item.title == r.title) {
                        item.submitted = true;
                        break;
                    }
                }
            }
        }
        saveToFile();
    }).detach();
}

std::vector<ScrobbleRecord> ScrobbleManager::getHistory() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_history;
}

void ScrobbleManager::clearHistory() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_history.clear();
    saveToFile();
}

size_t ScrobbleManager::getScrobbleCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_history.size();
}

bool ScrobbleManager::saveToFile(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    nlohmann::json root = nlohmann::json::object();
    
    root["auth"] = {
        {"username", m_lastFmUsername},
        {"sessionKey", m_lastFmSessionKey},
        {"apiKey", m_lastFmApiKey},
        {"apiSecret", m_lastFmApiSecret}
    };

    nlohmann::json jHistory = nlohmann::json::array();
    for (const auto& r : m_history) {
        jHistory.push_back({
            {"title", r.title},
            {"artist", r.artist},
            {"album", r.album},
            {"timestamp", r.timestamp},
            {"timestampStr", r.timestampStr},
            {"submitted", r.submitted}
        });
    }
    root["history"] = jHistory;

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << root.dump(2);
    return true;
}

bool ScrobbleManager::loadFromFile(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        nlohmann::json j;
        f >> j;

        if (j.is_object()) {
            if (j.contains("auth") && j["auth"].is_object()) {
                const auto& a = j["auth"];
                if (a.contains("username")) a.at("username").get_to(m_lastFmUsername);
                if (a.contains("sessionKey")) a.at("sessionKey").get_to(m_lastFmSessionKey);
                if (a.contains("apiKey")) a.at("apiKey").get_to(m_lastFmApiKey);
                if (a.contains("apiSecret")) a.at("apiSecret").get_to(m_lastFmApiSecret);
            }
            if (j.contains("history") && j["history"].is_array()) {
                m_history.clear();
                for (const auto& item : j["history"]) {
                    ScrobbleRecord r;
                    if (item.contains("title")) item.at("title").get_to(r.title);
                    if (item.contains("artist")) item.at("artist").get_to(r.artist);
                    if (item.contains("album")) item.at("album").get_to(r.album);
                    if (item.contains("timestamp")) item.at("timestamp").get_to(r.timestamp);
                    if (item.contains("timestampStr")) item.at("timestampStr").get_to(r.timestampStr);
                    if (item.contains("submitted")) item.at("submitted").get_to(r.submitted);
                    m_history.push_back(r);
                }
            }
            return true;
        } else if (j.is_array()) {
            // Backward compatibility
            m_history.clear();
            for (const auto& item : j) {
                ScrobbleRecord r;
                if (item.contains("title")) item.at("title").get_to(r.title);
                if (item.contains("artist")) item.at("artist").get_to(r.artist);
                if (item.contains("album")) item.at("album").get_to(r.album);
                if (item.contains("timestamp")) item.at("timestamp").get_to(r.timestamp);
                if (item.contains("timestampStr")) item.at("timestampStr").get_to(r.timestampStr);
                if (item.contains("submitted")) item.at("submitted").get_to(r.submitted);
                m_history.push_back(r);
            }
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}
