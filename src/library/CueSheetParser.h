#pragma once

#include "Track.h"
#include <string>
#include <vector>

class CueSheetParser {
public:
    // Parse a .cue file from disk and return virtual sub-tracks
    static std::vector<Track> parseCueFile(const std::string& cueFilePath);

    // Parse .cue sheet content string directly
    static std::vector<Track> parseCueContent(const std::string& cueContent, const std::string& cueDirectory, const std::string& cueFilePath = "");
};
