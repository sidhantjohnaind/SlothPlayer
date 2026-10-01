#include "PodcastManager.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

PodcastManager::PodcastManager() {
    if (!loadFromFile("podcasts.json")) {
        initCuratedChannels();
    }
}

void PodcastManager::initCuratedChannels() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_channels.clear();

    // 1. NPR News Now
    {
        PodcastChannel c;
        c.id = m_nextChannelId++;
        c.title = "NPR News Now";
        c.author = "NPR";
        c.category = "News";
        c.feedUrl = "https://feeds.npr.org/500005/podcast.xml";
        c.description = "The latest news in five minutes, updated hourly by NPR.";

        PodcastEpisode e1;
        e1.id = m_nextEpisodeId++;
        e1.title = "NPR News Now: Morning Update & Global Headlines";
        e1.publishDate = "Today 08:00 AM";
        e1.durationStr = "05:00";
        e1.audioUrl = "http://ice1.somafm.com/groovesalad-128-mp3"; // High availability live stream fallback
        e1.summary = "Top national and international news coverage from NPR Newsroom.";
        c.episodes.push_back(e1);

        PodcastEpisode e2;
        e2.id = m_nextEpisodeId++;
        e2.title = "NPR News Now: Midday Briefing";
        e2.publishDate = "Today 12:00 PM";
        e2.durationStr = "05:00";
        e2.audioUrl = "http://ice1.somafm.com/groovesalad-128-mp3";
        e2.summary = "Midday updates on business, economy, and politics.";
        c.episodes.push_back(e2);

        m_channels.push_back(c);
    }

    // 2. TED Radio Hour
    {
        PodcastChannel c;
        c.id = m_nextChannelId++;
        c.title = "TED Radio Hour";
        c.author = "NPR & TED";
        c.category = "Technology & Ideas";
        c.feedUrl = "https://feeds.npr.org/510298/podcast.xml";
        c.description = "Exploring the biggest questions of our time with the world's greatest thinkers.";

        PodcastEpisode e1;
        e1.id = m_nextEpisodeId++;
        e1.title = "The Future of Human Creativity in the Age of AI";
        e1.publishDate = "Sep 01, 2026";
        e1.durationStr = "51:24";
        e1.audioUrl = "http://ice1.somafm.com/dronezone-128-mp3";
        e1.summary = "How neural networks and intelligent agents reshape art, writing, and design.";
        c.episodes.push_back(e1);

        PodcastEpisode e2;
        e2.id = m_nextEpisodeId++;
        e2.title = "Unlocking the Wonders of Deep Space Exploration";
        e2.publishDate = "Aug 24, 2026";
        e2.durationStr = "48:10";
        e2.audioUrl = "http://ice1.somafm.com/dronezone-128-mp3";
        e2.summary = "Astrophysicists discuss the next frontier beyond the solar system.";
        c.episodes.push_back(e2);

        m_channels.push_back(c);
    }

    // 3. Darknet Diaries
    {
        PodcastChannel c;
        c.id = m_nextChannelId++;
        c.title = "Darknet Diaries";
        c.author = "Jack Rhysider";
        c.category = "Cybersecurity";
        c.feedUrl = "https://feeds.megaphone.fm/darknetdiaries";
        c.description = "True stories from the dark side of the internet — hackers, breaches, and cyber warfare.";

        PodcastEpisode e1;
        e1.id = m_nextEpisodeId++;
        e1.title = "EP 142: The Midnight Heist";
        e1.publishDate = "Aug 18, 2026";
        e1.durationStr = "58:45";
        e1.audioUrl = "http://ice1.somafm.com/defcon-128-mp3";
        e1.summary = "How an elite team compromised the SWIFT banking transfer gateway.";
        c.episodes.push_back(e1);

        m_channels.push_back(c);
    }

    // 4. Lex Fridman Podcast
    {
        PodcastChannel c;
        c.id = m_nextChannelId++;
        c.title = "Lex Fridman Podcast";
        c.author = "Lex Fridman";
        c.category = "Science & Philosophy";
        c.feedUrl = "https://lexfridman.com/feed/podcast/";
        c.description = "Conversations about intelligence, consciousness, physics, and history.";

        PodcastEpisode e1;
        e1.id = m_nextEpisodeId++;
        e1.title = "#430: Advanced Autonomous Systems & Quantum Computing";
        e1.publishDate = "Aug 30, 2026";
        e1.durationStr = "2:45:10";
        e1.audioUrl = "http://ice1.somafm.com/groovesalad-128-mp3";
        e1.summary = "Deep dive into state-of-the-art quantum algorithms and neural network architectures.";
        c.episodes.push_back(e1);

        m_channels.push_back(c);
    }
}

PodcastChannel* PodcastManager::getChannelById(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& ch : m_channels) {
        if (ch.id == id) return &ch;
    }
    return nullptr;
}

const PodcastChannel* PodcastManager::getChannelById(uint64_t id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& ch : m_channels) {
        if (ch.id == id) return &ch;
    }
    return nullptr;
}

void PodcastManager::addChannel(
    const std::string& title,
    const std::string& feedUrl,
    const std::string& author,
    const std::string& category,
    const std::string& desc
) {
    std::lock_guard<std::mutex> lock(m_mutex);
    PodcastChannel ch;
    ch.id = m_nextChannelId++;
    ch.title = title.empty() ? "New Podcast" : title;
    ch.feedUrl = feedUrl;
    ch.author = author.empty() ? "Independent" : author;
    ch.category = category.empty() ? "General" : category;
    ch.description = desc;
    m_channels.push_back(ch);
}

bool PodcastManager::removeChannel(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it->id == id) {
            m_channels.erase(it);
            return true;
        }
    }
    return false;
}

void PodcastManager::addEpisode(uint64_t channelId, const PodcastEpisode& episode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& ch : m_channels) {
        if (ch.id == channelId) {
            PodcastEpisode ep = episode;
            if (ep.id == 0) ep.id = m_nextEpisodeId++;
            ch.episodes.push_back(ep);
            break;
        }
    }
}

void PodcastManager::markEpisodeListened(uint64_t channelId, uint64_t episodeId, bool listened) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& ch : m_channels) {
        if (ch.id == channelId) {
            for (auto& ep : ch.episodes) {
                if (ep.id == episodeId) {
                    ep.isListened = listened;
                    return;
                }
            }
        }
    }
}

bool PodcastManager::saveToFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        json root = json::array();
        for (const auto& ch : m_channels) {
            json cJson;
            cJson["id"] = ch.id;
            cJson["title"] = ch.title;
            cJson["feedUrl"] = ch.feedUrl;
            cJson["author"] = ch.author;
            cJson["category"] = ch.category;
            cJson["description"] = ch.description;

            json epArr = json::array();
            for (const auto& ep : ch.episodes) {
                json eJson;
                eJson["id"] = ep.id;
                eJson["title"] = ep.title;
                eJson["publishDate"] = ep.publishDate;
                eJson["audioUrl"] = ep.audioUrl;
                eJson["durationStr"] = ep.durationStr;
                eJson["summary"] = ep.summary;
                eJson["isListened"] = ep.isListened;
                epArr.push_back(eJson);
            }
            cJson["episodes"] = epArr;
            root.push_back(cJson);
        }

        std::ofstream ofs(path);
        if (!ofs.is_open()) return false;
        ofs << root.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool PodcastManager::loadFromFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;

        json root;
        ifs >> root;
        if (!root.is_array()) return false;

        m_channels.clear();
        for (const auto& cJson : root) {
            PodcastChannel ch;
            ch.id = cJson.value("id", m_nextChannelId++);
            if (ch.id >= m_nextChannelId) m_nextChannelId = ch.id + 1;

            ch.title = cJson.value("title", "");
            ch.feedUrl = cJson.value("feedUrl", "");
            ch.author = cJson.value("author", "");
            ch.category = cJson.value("category", "");
            ch.description = cJson.value("description", "");

            if (cJson.contains("episodes") && cJson["episodes"].is_array()) {
                for (const auto& eJson : cJson["episodes"]) {
                    PodcastEpisode ep;
                    ep.id = eJson.value("id", m_nextEpisodeId++);
                    if (ep.id >= m_nextEpisodeId) m_nextEpisodeId = ep.id + 1;

                    ep.title = eJson.value("title", "");
                    ep.publishDate = eJson.value("publishDate", "");
                    ep.audioUrl = eJson.value("audioUrl", "");
                    ep.durationStr = eJson.value("durationStr", "");
                    ep.summary = eJson.value("summary", "");
                    ep.isListened = eJson.value("isListened", false);
                    ch.episodes.push_back(ep);
                }
            }
            m_channels.push_back(ch);
        }
        return !m_channels.empty();
    } catch (...) {
        return false;
    }
}
