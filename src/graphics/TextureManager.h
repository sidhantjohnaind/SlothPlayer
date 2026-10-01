#pragma once

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>
#include "../../third_party/imgui/imgui.h"
#include "../library/Track.h"

#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
#endif

struct TextureItem {
    ImTextureID id = 0;
    int width = 0;
    int height = 0;
};

class TextureManager {
public:
    TextureManager();
    ~TextureManager();

#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
    void init(ID3D11Device* device, ID3D11DeviceContext* context);
#else
    void init();
#endif
    void shutdown();

    ImTextureID getTrackArtwork(const Track& track, int* outW = nullptr, int* outH = nullptr);
    ImTextureID getArtworkFromFile(const std::string& imagePath, int* outW = nullptr, int* outH = nullptr);
    ImTextureID getDefaultArtwork(int* outW = nullptr, int* outH = nullptr);

    bool getDimensions(ImTextureID texId, int& outW, int& outH) const;

    // Calculates fitted dimensions within maxW x maxH preserving original aspect ratio
    static ImVec2 getAspectFitSize(int srcW, int srcH, float maxW, float maxH);

    // Calculates UV coordinates to crop excess symmetrically so rendering into a target box (e.g. square) does not stretch
    static void getAspectFillUV(int srcW, int srcH, float targetAspect, ImVec2& outUV0, ImVec2& outUV1);

    void clearCache();
    void processCompletedTextures(int maxPerFrame = 8);

private:
    struct AsyncJob {
        std::string cacheKey;
        Track track;
        std::string filePath;
        bool isFromFile = false;
    };

    struct DecodedImage {
        std::string cacheKey;
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
    };

    void workerLoop();
    void queueTrackArtworkAsync(const std::string& cacheKey, const Track& track);
    void queueFileArtworkAsync(const std::string& imagePath);

    TextureItem createTextureFromRGBA(const uint8_t* rgba, int width, int height);
    TextureItem createTextureFromEncodedMemory(const uint8_t* data, size_t size);
    void generateDefaultVinylArtwork();
    void freeTextureItem(TextureItem& item);
    void evictOldestIfNeeded();

#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
#endif

    TextureItem m_defaultArtwork;
    std::map<std::string, TextureItem> m_textureCache;
    std::list<std::string> m_lruOrder;
    static constexpr size_t MAX_CACHED_TEXTURES = 250;

    // Asynchronous background artwork decoder
    std::thread m_workerThread;
    std::atomic<bool> m_workerRunning{false};
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::vector<AsyncJob> m_jobQueue;
    std::set<std::string> m_pendingKeys;

    std::mutex m_completedMutex;
    std::vector<DecodedImage> m_completedQueue;
};

