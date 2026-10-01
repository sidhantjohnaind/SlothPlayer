#include "TextureManager.h"
#include "../library/TagReader.h"
#include <cmath>
#include <iostream>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
#include <d3d11.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

TextureManager::TextureManager() {
}

TextureManager::~TextureManager() {
    shutdown();
}

#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
void TextureManager::init(ID3D11Device* device, ID3D11DeviceContext* context) {
    m_device = device;
    m_context = context;
    generateDefaultVinylArtwork();

    m_workerRunning.store(true);
    if (!m_workerThread.joinable()) {
        m_workerThread = std::thread(&TextureManager::workerLoop, this);
    }
}
#else
void TextureManager::init() {
    generateDefaultVinylArtwork();

    m_workerRunning.store(true);
    if (!m_workerThread.joinable()) {
        m_workerThread = std::thread(&TextureManager::workerLoop, this);
    }
}
#endif

void TextureManager::shutdown() {
    m_workerRunning.store(false);
    m_queueCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_jobQueue.clear();
        m_pendingKeys.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);
        m_completedQueue.clear();
    }

    clearCache();
    if (m_defaultArtwork.id) {
#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
        ID3D11ShaderResourceView* srv = reinterpret_cast<ID3D11ShaderResourceView*>(m_defaultArtwork.id);
        if (srv) srv->Release();
#else
        GLuint tex = (GLuint)(uintptr_t)(m_defaultArtwork.id);
        if (tex != 0) glDeleteTextures(1, &tex);
#endif
        m_defaultArtwork = { 0, 0, 0 };
    }
#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
    m_device = nullptr;
    m_context = nullptr;
#endif
}

void TextureManager::freeTextureItem(TextureItem& item) {
    if (item.id && item.id != m_defaultArtwork.id) {
#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
        ID3D11ShaderResourceView* srv = reinterpret_cast<ID3D11ShaderResourceView*>(item.id);
        if (srv) srv->Release();
#else
        GLuint tex = (GLuint)(uintptr_t)(item.id);
        if (tex != 0) glDeleteTextures(1, &tex);
#endif
    }
    item = { 0, 0, 0 };
}

void TextureManager::evictOldestIfNeeded() {
    while (m_textureCache.size() >= MAX_CACHED_TEXTURES && !m_lruOrder.empty()) {
        std::string oldestKey = m_lruOrder.front();
        m_lruOrder.pop_front();
        auto it = m_textureCache.find(oldestKey);
        if (it != m_textureCache.end()) {
            freeTextureItem(it->second);
            m_textureCache.erase(it);
        }
    }
}

void TextureManager::clearCache() {
    for (auto& pair : m_textureCache) {
        freeTextureItem(pair.second);
    }
    m_textureCache.clear();
    m_lruOrder.clear();
}

TextureItem TextureManager::createTextureFromRGBA(const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return { 0, 0, 0 };

#if defined(_WIN32) && !defined(SLOTH_USE_OPENGL)
    if (!m_device || !m_context) return { 0, 0, 0 };

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 0; // 0 = allocate full mip pyramid
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &pTexture);
    if (SUCCEEDED(hr) && pTexture) {
        m_context->UpdateSubresource(pTexture, 0, nullptr, rgba, width * 4, 0);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = -1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        ID3D11ShaderResourceView* pSRV = nullptr;
        hr = m_device->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
        pTexture->Release();

        if (SUCCEEDED(hr) && pSRV) {
            m_context->GenerateMips(pSRV);
            return { reinterpret_cast<ImTextureID>(pSRV), width, height };
        }
    }

    // Driver fallback without dynamic mipmap generation
    desc.MipLevels = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA subData = {};
    subData.pSysMem = rgba;
    subData.SysMemPitch = desc.Width * 4;
    hr = m_device->CreateTexture2D(&desc, &subData, &pTexture);
    if (FAILED(hr) || !pTexture) return { 0, 0, 0 };

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* pSRV = nullptr;
    hr = m_device->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
    pTexture->Release();
    return (FAILED(hr)) ? TextureItem{ 0, 0, 0 } : TextureItem{ reinterpret_cast<ImTextureID>(pSRV), width, height };

#else
    // OpenGL 3.3 / GLES (Linux: x86_64, ARM, RISC-V, macOS)
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) return { 0, 0, 0 };

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    return { (ImTextureID)(uintptr_t)tex, width, height };
#endif
}

TextureItem TextureManager::createTextureFromEncodedMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) return { 0, 0, 0 };

    int width = 0, height = 0, channels = 0;
    unsigned char* rgba = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!rgba) return { 0, 0, 0 };

    TextureItem item = createTextureFromRGBA(rgba, width, height);
    stbi_image_free(rgba);
    return item;
}

void TextureManager::generateDefaultVinylArtwork() {
    const int W = 512;
    const int H = 512;
    std::vector<uint8_t> pixels(W * H * 4, 0);

    const float cx = W * 0.5f;
    const float cy = H * 0.5f;
    const float maxR = 240.0f;
    const float spindleR = 14.0f;
    const float labelR = 96.0f;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float dx = x - cx;
            float dy = y - cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            int idx = (y * W + x) * 4;

            if (dist > maxR + 1.0f) {
                pixels[idx + 0] = 0;
                pixels[idx + 1] = 0;
                pixels[idx + 2] = 0;
                pixels[idx + 3] = 0;
                continue;
            }

            float edgeAlpha = 1.0f;
            if (dist > maxR - 1.0f) {
                edgeAlpha = std::clamp(maxR + 1.0f - dist, 0.0f, 1.0f);
            }

            if (dist <= spindleR) {
                float centerAlpha = (dist < spindleR - 1.0f) ? 0.0f : (dist - (spindleR - 1.0f));
                pixels[idx + 0] = 16;
                pixels[idx + 1] = 17;
                pixels[idx + 2] = 20;
                pixels[idx + 3] = static_cast<uint8_t>(255.0f * centerAlpha);
            } else if (dist <= labelR) {
                uint8_t r = 235, g = 168, b = 15; // Golden amber label
                if (dist <= 26.0f) {
                    r = 30; g = 32; b = 38;
                }
                if (std::abs(dist - 88.0f) < 1.5f || std::abs(dist - 56.0f) < 1.0f) {
                    r = 185; g = 125; b = 10;
                } else if (std::abs(dist - 72.0f) < 1.2f || std::abs(dist - 38.0f) < 1.0f) {
                    r = 250; g = 195; b = 45;
                }
                pixels[idx + 0] = r;
                pixels[idx + 1] = g;
                pixels[idx + 2] = b;
                pixels[idx + 3] = 255;
            } else {
                float angle = std::atan2(dy, dx);
                float sheen1 = std::pow(std::abs(std::sin(angle - 0.785f)), 6.0f);
                float sheen2 = std::pow(std::abs(std::cos(angle - 0.785f)), 14.0f);
                float sheen = (sheen1 * 0.55f + sheen2 * 0.45f) * 48.0f;
                float groove = (std::sin(dist * 2.5f) > 0.0f ? 7.0f : -4.0f);

                float base = 22.0f + sheen + groove;
                base = std::clamp(base, 14.0f, 120.0f);

                pixels[idx + 0] = static_cast<uint8_t>(base);
                pixels[idx + 1] = static_cast<uint8_t>(base + 1.0f);
                pixels[idx + 2] = static_cast<uint8_t>(base + 3.0f);
                pixels[idx + 3] = static_cast<uint8_t>(255.0f * edgeAlpha);
            }
        }
    }

    m_defaultArtwork = createTextureFromRGBA(pixels.data(), W, H);
}

ImTextureID TextureManager::getDefaultArtwork(int* outW, int* outH) {
    if (outW) *outW = m_defaultArtwork.width;
    if (outH) *outH = m_defaultArtwork.height;
    return m_defaultArtwork.id;
}

void TextureManager::queueTrackArtworkAsync(const std::string& cacheKey, const Track& track) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_pendingKeys.find(cacheKey) != m_pendingKeys.end()) return;
    m_pendingKeys.insert(cacheKey);

    AsyncJob job;
    job.cacheKey = cacheKey;
    job.track = track;
    job.isFromFile = false;
    m_jobQueue.push_back(std::move(job));
    m_queueCv.notify_one();
}

void TextureManager::queueFileArtworkAsync(const std::string& imagePath) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_pendingKeys.find(imagePath) != m_pendingKeys.end()) return;
    m_pendingKeys.insert(imagePath);

    AsyncJob job;
    job.cacheKey = imagePath;
    job.filePath = imagePath;
    job.isFromFile = true;
    m_jobQueue.push_back(std::move(job));
    m_queueCv.notify_one();
}

void TextureManager::workerLoop() {
    while (m_workerRunning.load()) {
        AsyncJob job;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this]() {
                return !m_workerRunning.load() || !m_jobQueue.empty();
            });
            if (!m_workerRunning.load()) break;
            if (m_jobQueue.empty()) continue;
            // Pop LIFO so the most recently requested visible cards are decoded first!
            job = std::move(m_jobQueue.back());
            m_jobQueue.pop_back();
        }

        DecodedImage result;
        result.cacheKey = job.cacheKey;

        if (job.isFromFile) {
            int w = 0, h = 0, ch = 0;
            unsigned char* rgba = stbi_load(job.filePath.c_str(), &w, &h, &ch, 4);
            if (rgba && w > 0 && h > 0) {
                result.width = w;
                result.height = h;
                result.rgba.assign(rgba, rgba + (w * h * 4));
                stbi_image_free(rgba);
            }
        } else {
            std::vector<uint8_t> artBytes;
            if (TagReader::extractAlbumArt(job.track, artBytes) && !artBytes.empty()) {
                int w = 0, h = 0, ch = 0;
                unsigned char* rgba = stbi_load_from_memory(artBytes.data(), static_cast<int>(artBytes.size()), &w, &h, &ch, 4);
                if (rgba && w > 0 && h > 0) {
                    result.width = w;
                    result.height = h;
                    result.rgba.assign(rgba, rgba + (w * h * 4));
                    stbi_image_free(rgba);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_completedMutex);
            m_completedQueue.push_back(std::move(result));
        }
    }
}

void TextureManager::processCompletedTextures(int maxPerFrame) {
    std::vector<DecodedImage> ready;
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);
        if (m_completedQueue.empty()) return;
        int count = 0;
        while (!m_completedQueue.empty() && count < maxPerFrame) {
            ready.push_back(std::move(m_completedQueue.back()));
            m_completedQueue.pop_back();
            count++;
        }
    }

    for (auto& item : ready) {
        if (!item.rgba.empty()) {
            TextureItem tex = createTextureFromRGBA(item.rgba.data(), item.width, item.height);
            if (tex.id) {
                evictOldestIfNeeded();
                m_textureCache[item.cacheKey] = tex;
                m_lruOrder.push_back(item.cacheKey);
            } else {
                m_textureCache[item.cacheKey] = m_defaultArtwork;
                m_lruOrder.push_back(item.cacheKey);
            }
        } else {
            // Failed or no embedded art: negative cache so it doesn't queue again
            m_textureCache[item.cacheKey] = m_defaultArtwork;
            m_lruOrder.push_back(item.cacheKey);
        }
    }
}

ImTextureID TextureManager::getArtworkFromFile(const std::string& imagePath, int* outW, int* outH) {
    processCompletedTextures();

    if (imagePath.empty()) return getDefaultArtwork(outW, outH);

    auto it = m_textureCache.find(imagePath);
    if (it != m_textureCache.end()) {
        m_lruOrder.remove(imagePath);
        m_lruOrder.push_back(imagePath);
        if (outW) *outW = it->second.width;
        if (outH) *outH = it->second.height;
        return it->second.id ? it->second.id : getDefaultArtwork(outW, outH);
    }

    queueFileArtworkAsync(imagePath);
    return getDefaultArtwork(outW, outH);
}

ImTextureID TextureManager::getTrackArtwork(const Track& track, int* outW, int* outH) {
    processCompletedTextures();

    std::string cacheKey = track.album.empty() ? track.filePath : (track.getDisplayAlbum() + "___" + track.getDisplayArtist());
    auto it = m_textureCache.find(cacheKey);
    if (it != m_textureCache.end()) {
        m_lruOrder.remove(cacheKey);
        m_lruOrder.push_back(cacheKey);
        if (outW) *outW = it->second.width;
        if (outH) *outH = it->second.height;
        return it->second.id ? it->second.id : getDefaultArtwork(outW, outH);
    }

    queueTrackArtworkAsync(cacheKey, track);
    return getDefaultArtwork(outW, outH);
}

bool TextureManager::getDimensions(ImTextureID texId, int& outW, int& outH) const {
    if (!texId) { outW = outH = 0; return false; }
    if (texId == m_defaultArtwork.id) {
        outW = m_defaultArtwork.width;
        outH = m_defaultArtwork.height;
        return true;
    }
    for (const auto& pair : m_textureCache) {
        if (pair.second.id == texId) {
            outW = pair.second.width;
            outH = pair.second.height;
            return true;
        }
    }
    outW = outH = 0;
    return false;
}

ImVec2 TextureManager::getAspectFitSize(int srcW, int srcH, float maxW, float maxH) {
    if (srcW <= 0 || srcH <= 0 || maxW <= 0.0f || maxH <= 0.0f) return ImVec2(maxW, maxH);
    float srcAspect = static_cast<float>(srcW) / static_cast<float>(srcH);
    float targetAspect = maxW / maxH;
    if (srcAspect > targetAspect) {
        return ImVec2(maxW, maxW / srcAspect);
    } else {
        return ImVec2(maxH * srcAspect, maxH);
    }
}

void TextureManager::getAspectFillUV(int srcW, int srcH, float targetAspect, ImVec2& outUV0, ImVec2& outUV1) {
    outUV0 = ImVec2(0.0f, 0.0f);
    outUV1 = ImVec2(1.0f, 1.0f);
    if (srcW <= 0 || srcH <= 0 || targetAspect <= 0.0f) return;

    float srcAspect = static_cast<float>(srcW) / static_cast<float>(srcH);
    if (srcAspect > targetAspect) {
        // Source is wider than target: trim left and right
        float excess = 1.0f - (targetAspect / srcAspect);
        outUV0.x = excess * 0.5f;
        outUV1.x = 1.0f - (excess * 0.5f);
    } else if (srcAspect < targetAspect) {
        // Source is taller than target: trim top and bottom
        float excess = 1.0f - (srcAspect / targetAspect);
        outUV0.y = excess * 0.5f;
        outUV1.y = 1.0f - (excess * 0.5f);
    }
}
