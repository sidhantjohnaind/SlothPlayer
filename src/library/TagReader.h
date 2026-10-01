#pragma once

#include "Track.h"
#include <vector>
#include <cstdint>

class TagReader {
public:
    static bool readMetadata(const std::string& filePath, Track& outTrack);
    static bool extractAlbumArt(const Track& track, std::vector<uint8_t>& outBytes);

private:
    static bool parseID3v2(std::ifstream& file, Track& outTrack);
    static bool parseID3v1(std::ifstream& file, Track& outTrack);
    static bool parseFLAC(std::ifstream& file, Track& outTrack);
    static void deduceFromPath(const std::string& filePath, Track& outTrack);
    static void findExternalCoverArt(const std::string& filePath, Track& outTrack);
};
