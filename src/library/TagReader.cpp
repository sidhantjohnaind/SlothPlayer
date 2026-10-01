#include "TagReader.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <iostream>

#include "../third_party/miniaudio.h"

namespace fs = std::filesystem;

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\0");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\0");
    return str.substr(first, (last - first + 1));
}

static std::string utf16ToUtf8(const uint8_t* data, size_t len, bool bigEndian) {
    std::string result;
    if (len < 2) return result;
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint16_t ch = bigEndian ? ((data[i] << 8) | data[i + 1]) : ((data[i + 1] << 8) | data[i]);
        if (ch == 0) break;
        if (ch < 0x80) {
            result.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return result;
}

static std::string decodeId3Text(const uint8_t* data, size_t len) {
    if (len == 0 || !data) return "";
    uint8_t encoding = data[0];
    const uint8_t* text = data + 1;
    size_t textLen = len - 1;

    if (encoding == 0) { // ISO-8859-1
        std::string s;
        for (size_t i = 0; i < textLen; ++i) {
            if (text[i] == 0) break;
            s.push_back(static_cast<char>(text[i]));
        }
        return trim(s);
    } else if (encoding == 1) { // UTF-16 with BOM
        if (textLen < 2) return "";
        bool bigEndian = false;
        if (text[0] == 0xFE && text[1] == 0xFF) {
            bigEndian = true;
            return trim(utf16ToUtf8(text + 2, textLen - 2, bigEndian));
        } else if (text[0] == 0xFF && text[1] == 0xFE) {
            bigEndian = false;
            return trim(utf16ToUtf8(text + 2, textLen - 2, bigEndian));
        }
        return trim(utf16ToUtf8(text, textLen, false));
    } else if (encoding == 2) { // UTF-16BE
        return trim(utf16ToUtf8(text, textLen, true));
    } else if (encoding == 3) { // UTF-8
        std::string s;
        for (size_t i = 0; i < textLen; ++i) {
            if (text[i] == 0) break;
            s.push_back(static_cast<char>(text[i]));
        }
        return trim(s);
    }
    return "";
}

bool TagReader::parseID3v2(std::ifstream& file, Track& outTrack) {
    file.seekg(0, std::ios::beg);
    uint8_t header[10];
    if (!file.read(reinterpret_cast<char*>(header), 10)) return false;

    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') return false;

    uint8_t versionMajor = header[3];
    uint8_t flags = header[5];
    uint32_t tagSize = ((header[6] & 0x7F) << 21) |
                       ((header[7] & 0x7F) << 14) |
                       ((header[8] & 0x7F) << 7) |
                       (header[9] & 0x7F);

    size_t tagEnd = 10 + tagSize;
    size_t currentPos = 10;

    // Check for extended header
    if (flags & 0x40) {
        uint8_t extHeader[4];
        if (file.read(reinterpret_cast<char*>(extHeader), 4)) {
            uint32_t extSize = 0;
            if (versionMajor == 4) {
                extSize = ((extHeader[0] & 0x7F) << 21) | ((extHeader[1] & 0x7F) << 14) |
                          ((extHeader[2] & 0x7F) << 7) | (extHeader[3] & 0x7F);
            } else {
                extSize = (extHeader[0] << 24) | (extHeader[1] << 16) | (extHeader[2] << 8) | extHeader[3];
            }
            currentPos += 4 + extSize;
            file.seekg(currentPos, std::ios::beg);
        }
    }

    while (currentPos + 10 < tagEnd) {
        file.seekg(currentPos, std::ios::beg);
        uint8_t frameHeader[10];
        if (!file.read(reinterpret_cast<char*>(frameHeader), 10)) break;

        char frameID[5] = {0};
        std::memcpy(frameID, frameHeader, 4);

        if (frameID[0] == 0) break; // Padding reached

        uint32_t frameSize = 0;
        if (versionMajor == 4) {
            frameSize = ((frameHeader[4] & 0x7F) << 21) |
                        ((frameHeader[5] & 0x7F) << 14) |
                        ((frameHeader[6] & 0x7F) << 7) |
                        (frameHeader[7] & 0x7F);
        } else {
            frameSize = (frameHeader[4] << 24) | (frameHeader[5] << 16) |
                        (frameHeader[6] << 8) | frameHeader[7];
        }

        if (frameSize == 0 || frameSize > 50 * 1024 * 1024 || currentPos + 10 + frameSize > tagEnd + 100) {
            break;
        }

        if (frameID[0] == 'T' || std::strcmp(frameID, "COMM") == 0) {
            std::vector<uint8_t> frameData(frameSize);
            if (file.read(reinterpret_cast<char*>(frameData.data()), frameSize)) {
                std::string val;
                if (std::strcmp(frameID, "COMM") == 0 && frameSize > 4) {
                    // COMM frame: enc(1), lang(3), desc(var), text
                    uint8_t enc = frameData[0];
                    size_t commOff = 4;
                    if (enc == 1 || enc == 2) {
                        while (commOff + 1 < frameSize && !(frameData[commOff] == 0 && frameData[commOff + 1] == 0)) {
                            commOff += 2;
                        }
                        if (commOff + 1 < frameSize) commOff += 2;
                    } else {
                        while (commOff < frameSize && frameData[commOff] != 0) commOff++;
                        if (commOff < frameSize) commOff++;
                    }
                    if (commOff < frameSize) {
                        val = decodeId3Text(frameData.data() + commOff, frameSize - commOff);
                    }
                } else {
                    val = decodeId3Text(frameData.data(), frameSize);
                }

                if (!val.empty()) {
                    outTrack.rawTags[frameID] = val;
                    if (std::strcmp(frameID, "TIT2") == 0) outTrack.title = val;
                    else if (std::strcmp(frameID, "TPE1") == 0) outTrack.artist = val;
                    else if (std::strcmp(frameID, "TPE2") == 0) outTrack.albumArtist = val;
                    else if (std::strcmp(frameID, "TALB") == 0) outTrack.album = val;
                    else if (std::strcmp(frameID, "TCOM") == 0) outTrack.composer = val;
                    else if (std::strcmp(frameID, "TSRC") == 0) outTrack.isrc = val;
                    else if (std::strcmp(frameID, "COMM") == 0) outTrack.comment = val;
                    else if (std::strcmp(frameID, "TYER") == 0 || std::strcmp(frameID, "TDRC") == 0) {
                        try {
                            if (val.length() >= 4) outTrack.year = std::stoi(val.substr(0, 4));
                        } catch (...) {}
                    }
                    else if (std::strcmp(frameID, "TRCK") == 0) {
                        try {
                            size_t slashPos = val.find('/');
                            std::string numStr = (slashPos != std::string::npos) ? val.substr(0, slashPos) : val;
                            outTrack.trackNumber = std::stoi(numStr);
                            if (slashPos != std::string::npos) {
                                outTrack.trackTotal = std::stoi(val.substr(slashPos + 1));
                            }
                        } catch (...) {}
                    }
                    else if (std::strcmp(frameID, "TPOS") == 0) {
                        try {
                            size_t slashPos = val.find('/');
                            std::string numStr = (slashPos != std::string::npos) ? val.substr(0, slashPos) : val;
                            outTrack.discNumber = std::stoi(numStr);
                            if (slashPos != std::string::npos) {
                                outTrack.totalDiscs = std::stoi(val.substr(slashPos + 1));
                            }
                        } catch (...) {}
                    }
                    else if (std::strcmp(frameID, "TCON") == 0) outTrack.genre = val;
                }
            }
        } else if (std::strcmp(frameID, "APIC") == 0) {
            // Attached picture
            std::vector<uint8_t> apicData(frameSize);
            if (file.read(reinterpret_cast<char*>(apicData.data()), frameSize)) {
                if (frameSize > 5) {
                    uint8_t enc = apicData[0];
                    size_t offset = 1;
                    // MIME type
                    std::string mime;
                    while (offset < frameSize && apicData[offset] != 0) {
                        mime.push_back(static_cast<char>(apicData[offset++]));
                    }
                    if (offset < frameSize && apicData[offset] == 0) offset++; // skip null
                    if (offset < frameSize) {
                        uint8_t picType = apicData[offset++];
                        (void)picType;
                        // Description
                        if (enc == 1 || enc == 2) {
                            while (offset + 1 < frameSize && !(apicData[offset] == 0 && apicData[offset + 1] == 0)) {
                                offset += 2;
                            }
                            if (offset + 1 < frameSize) offset += 2;
                        } else {
                            while (offset < frameSize && apicData[offset] != 0) {
                                offset++;
                            }
                            if (offset < frameSize) offset++;
                        }
                        if (offset < frameSize) {
                            outTrack.hasEmbeddedArt = true;
                            outTrack.embeddedArtOffset = currentPos + 10 + offset;
                            outTrack.embeddedArtSize = static_cast<uint32_t>(frameSize - offset);
                        }
                    }
                }
            }
        }

        currentPos += 10 + frameSize;
    }

    return true;
}

bool TagReader::parseID3v1(std::ifstream& file, Track& outTrack) {
    file.seekg(-128, std::ios::end);
    char buf[128];
    if (!file.read(buf, 128)) return false;

    if (std::memcmp(buf, "TAG", 3) != 0) return false;

    if (outTrack.title.empty()) {
        outTrack.title = trim(std::string(buf + 3, 30));
    }
    if (outTrack.artist.empty()) {
        outTrack.artist = trim(std::string(buf + 33, 30));
    }
    if (outTrack.album.empty()) {
        outTrack.album = trim(std::string(buf + 63, 30));
    }
    if (outTrack.year == 0) {
        std::string yearStr = trim(std::string(buf + 93, 4));
        try {
            if (yearStr.length() == 4) outTrack.year = std::stoi(yearStr);
        } catch (...) {}
    }
    if (outTrack.trackNumber == 0 && buf[125] == 0 && buf[126] != 0) {
        outTrack.trackNumber = static_cast<uint8_t>(buf[126]);
    }
    return true;
}

bool TagReader::parseFLAC(std::ifstream& file, Track& outTrack) {
    file.seekg(0, std::ios::beg);
    char magic[4];
    if (!file.read(magic, 4) || std::memcmp(magic, "fLaC", 4) != 0) return false;

    bool isLast = false;
    while (!isLast && file.good()) {
        uint8_t blockHeader[4];
        if (!file.read(reinterpret_cast<char*>(blockHeader), 4)) break;

        isLast = (blockHeader[0] & 0x80) != 0;
        uint8_t blockType = blockHeader[0] & 0x7F;
        uint32_t blockSize = (blockHeader[1] << 16) | (blockHeader[2] << 8) | blockHeader[3];

        if (blockSize > 50 * 1024 * 1024) break; // 50MB sanity ceiling per block

        uint64_t blockStart = file.tellg();

        if (blockType == 4) { // VORBIS_COMMENT
            std::vector<uint8_t> data(blockSize);
            if (file.read(reinterpret_cast<char*>(data.data()), blockSize)) {
                size_t pos = 0;
                if (pos + 4 <= blockSize) {
                    uint32_t vendorLen = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24);
                    pos += 4;
                    if (vendorLen <= blockSize - pos) {
                        pos += vendorLen;
                    } else {
                        pos = blockSize;
                    }
                }
                if (pos + 4 <= blockSize) {
                    uint32_t commentCount = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24);
                    pos += 4;
                    for (uint32_t i = 0; i < commentCount && pos + 4 <= blockSize; ++i) {
                        uint32_t cLen = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24);
                        pos += 4;
                        if (cLen > blockSize - pos) {
                            break; // Corrupted length, terminate comment parsing safely
                        }
                        std::string comment(reinterpret_cast<char*>(&data[pos]), cLen);
                        pos += cLen;
                        size_t eqPos = comment.find('=');
                        if (eqPos != std::string::npos) {
                            std::string key = comment.substr(0, eqPos);
                            std::string val = comment.substr(eqPos + 1);
                            std::transform(key.begin(), key.end(), key.begin(), ::toupper);
                            std::string trimmedVal = trim(val);
                            outTrack.rawTags[key] = trimmedVal;

                            if (key == "TITLE") outTrack.title = trimmedVal;
                            else if (key == "ARTIST") outTrack.artist = trimmedVal;
                            else if (key == "ALBUMARTIST" || key == "ALBUM ARTIST") outTrack.albumArtist = trimmedVal;
                            else if (key == "ALBUM") outTrack.album = trimmedVal;
                            else if (key == "GENRE") outTrack.genre = trimmedVal;
                            else if (key == "COMPOSER") outTrack.composer = trimmedVal;
                            else if (key == "COMMENT" || key == "DESCRIPTION") outTrack.comment = trimmedVal;
                            else if (key == "ISRC") outTrack.isrc = trimmedVal;
                            else if (key == "DATE" || key == "YEAR") {
                                try {
                                    if (trimmedVal.length() >= 4) outTrack.year = std::stoi(trimmedVal.substr(0, 4));
                                } catch (...) {}
                            }
                            else if (key == "TRACKNUMBER") {
                                try {
                                    size_t slashPos = trimmedVal.find('/');
                                    std::string numStr = (slashPos != std::string::npos) ? trimmedVal.substr(0, slashPos) : trimmedVal;
                                    outTrack.trackNumber = std::stoi(numStr);
                                    if (slashPos != std::string::npos) {
                                        outTrack.trackTotal = std::stoi(trimmedVal.substr(slashPos + 1));
                                    }
                                } catch (...) {}
                            }
                            else if (key == "TRACKTOTAL" || key == "TOTALTRACKS") {
                                try { outTrack.trackTotal = std::stoi(trimmedVal); } catch (...) {}
                            }
                            else if (key == "DISCNUMBER" || key == "DISC") {
                                try {
                                    size_t slashPos = trimmedVal.find('/');
                                    std::string numStr = (slashPos != std::string::npos) ? trimmedVal.substr(0, slashPos) : trimmedVal;
                                    outTrack.discNumber = std::stoi(numStr);
                                    if (slashPos != std::string::npos) {
                                        outTrack.totalDiscs = std::stoi(trimmedVal.substr(slashPos + 1));
                                    }
                                } catch (...) {}
                            }
                            else if (key == "TOTALDISCS" || key == "DISCTOTAL") {
                                try { outTrack.totalDiscs = std::stoi(trimmedVal); } catch (...) {}
                            }
                        }
                    }
                }
            }
        } else if (blockType == 6) { // PICTURE
            std::vector<uint8_t> data(blockSize);
            if (file.read(reinterpret_cast<char*>(data.data()), blockSize)) {
                if (blockSize > 32) {
                    uint32_t picType = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
                    (void)picType;
                    size_t pos = 4;
                    if (pos + 4 <= blockSize) {
                        uint32_t mimeLen = (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
                        pos += 4;
                        if (mimeLen <= blockSize - pos) {
                            pos += mimeLen;
                            if (pos + 4 <= blockSize) {
                                uint32_t descLen = (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
                                pos += 4;
                                if (descLen <= blockSize - pos) {
                                    pos += descLen;
                                    if (pos + 20 <= blockSize) { // 16 bytes for width, height, depth, colors + 4 bytes picDataLen
                                        pos += 16;
                                        uint32_t picDataLen = (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
                                        pos += 4;
                                        if (picDataLen <= blockSize - pos) {
                                            outTrack.hasEmbeddedArt = true;
                                            outTrack.embeddedArtOffset = blockStart + pos;
                                            outTrack.embeddedArtSize = picDataLen;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            file.seekg(blockStart + blockSize, std::ios::beg);
        }
    }
    return true;
}

void TagReader::deduceFromPath(const std::string& filePath, Track& outTrack) {
    fs::path p(filePath);
    outTrack.fileName = p.filename().string();
    if (outTrack.title.empty()) {
        std::string stem = p.stem().string();
        // Remove leading numbers, e.g. "01 - ", "01. ", "1 "
        size_t idx = 0;
        while (idx < stem.size() && (std::isdigit(static_cast<unsigned char>(stem[idx])) || stem[idx] == ' ' || stem[idx] == '-' || stem[idx] == '.')) {
            idx++;
        }
        if (idx < stem.size() && idx > 0 && (stem[idx - 1] == '-' || stem[idx - 1] == '.' || stem[idx - 1] == ' ')) {
            outTrack.title = trim(stem.substr(idx));
        } else {
            outTrack.title = stem;
        }
    }

    // Try deducing Album and Artist from parent folders
    if (outTrack.album.empty() && p.has_parent_path()) {
        outTrack.album = p.parent_path().filename().string();
        if (outTrack.artist.empty() && p.parent_path().has_parent_path()) {
            std::string grandParent = p.parent_path().parent_path().filename().string();
            if (!grandParent.empty() && grandParent != "Music" && grandParent != "SlothPlayer" && grandParent != "Media") {
                outTrack.artist = grandParent;
            }
        }
    }
}

void TagReader::findExternalCoverArt(const std::string& filePath, Track& outTrack) {
    try {
        fs::path dir = fs::path(filePath).parent_path();
        const char* coverNames[] = {
            "cover.jpg", "cover.png", "folder.jpg", "folder.png",
            "albumart.jpg", "albumart.png", "front.jpg", "front.png"
        };
        for (const char* name : coverNames) {
            fs::path candidate = dir / name;
            if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
                outTrack.albumArtPath = candidate.string();
                return;
            }
        }
    } catch (...) {}
}

bool TagReader::readMetadata(const std::string& filePath, Track& outTrack) {
    outTrack.filePath = filePath;
    try {
        outTrack.fileSizeBytes = fs::file_size(filePath);
    } catch (...) {
        outTrack.fileSizeBytes = 0;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    // Check extension
    std::string ext = fs::path(filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".flac") {
        parseFLAC(file, outTrack);
    } else {
        parseID3v2(file, outTrack);
        parseID3v1(file, outTrack);
    }

    file.close();

    deduceFromPath(filePath, outTrack);
    findExternalCoverArt(filePath, outTrack);

    // Audio format probe using miniaudio decoder
    ma_decoder_config config = ma_decoder_config_init_default();
    ma_decoder decoder;
    if (ma_decoder_init_file(filePath.c_str(), &config, &decoder) == MA_SUCCESS) {
        outTrack.sampleRate = decoder.outputSampleRate;
        outTrack.channels = decoder.outputChannels;
        ma_uint64 totalFrames = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS && decoder.outputSampleRate > 0) {
            outTrack.duration = static_cast<double>(totalFrames) / static_cast<double>(decoder.outputSampleRate);
        }
        ma_decoder_uninit(&decoder);

        if (outTrack.duration > 0 && outTrack.fileSizeBytes > 0) {
            outTrack.bitrate = static_cast<int>((outTrack.fileSizeBytes * 8.0) / (outTrack.duration * 1000.0));
        }
    }

    return true;
}

bool TagReader::extractAlbumArt(const Track& track, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    constexpr size_t MAX_ART_SIZE = 50 * 1024 * 1024; // 50 MB safety ceiling

    if (track.hasEmbeddedArt && track.embeddedArtSize > 0 && track.embeddedArtSize <= MAX_ART_SIZE) {
        std::ifstream file(track.filePath, std::ios::binary);
        if (file.is_open()) {
            file.seekg(track.embeddedArtOffset, std::ios::beg);
            if (file.good()) {
                outBytes.resize(track.embeddedArtSize);
                if (file.read(reinterpret_cast<char*>(outBytes.data()), track.embeddedArtSize)) {
                    return true;
                }
            }
            outBytes.clear();
        }
    }

    if (!track.albumArtPath.empty()) {
        std::ifstream file(track.albumArtPath, std::ios::binary);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            std::streampos endPos = file.tellg();
            if (endPos > 0 && static_cast<uint64_t>(endPos) <= MAX_ART_SIZE) {
                size_t sz = static_cast<size_t>(endPos);
                file.seekg(0, std::ios::beg);
                outBytes.resize(sz);
                if (file.read(reinterpret_cast<char*>(outBytes.data()), sz)) {
                    return true;
                }
            }
            outBytes.clear();
        }
    }

    return false;
}
