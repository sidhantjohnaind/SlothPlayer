#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include "../third_party/json.hpp"

struct PodcastEpisode {
    uint64_t id = 0;
    std::string title;
    std::string publishDate;
    std::string audioUrl;
    std::string durationStr;
    std::string summary;
    bool isListened = false;
};

struct PodcastChannel {
    uint64_t id = 0;
    std::string title;
    std::string feedUrl;
    std::string author;
    std::string description;
    std::string category;
    std::vector<PodcastEpisode> episodes;
};

class PodcastManager {
public:
    PodcastManager();
    ~PodcastManager() = default;

    bool loadFromFile(const std::string& path = "podcasts.json");
    bool saveToFile(const std::string& path = "podcasts.json") const;

    const std::vector<PodcastChannel>& getChannels() const { return m_channels; }
    PodcastChannel* getChannelById(uint64_t id);
    const PodcastChannel* getChannelById(uint64_t id) const;

    void addChannel(const std::string& title, const std::string& feedUrl, const std::string& author, const std::string& category, const std::string& desc = "");
    bool removeChannel(uint64_t id);
    void addEpisode(uint64_t channelId, const PodcastEpisode& episode);
    void markEpisodeListened(uint64_t channelId, uint64_t episodeId, bool listened = true);

    void initCuratedChannels();

private:
    std::vector<PodcastChannel> m_channels;
    uint64_t m_nextChannelId = 1;
    uint64_t m_nextEpisodeId = 1;
    mutable std::mutex m_mutex;
};
