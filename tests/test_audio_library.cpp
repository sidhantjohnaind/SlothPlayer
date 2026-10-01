#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cmath>
#include <filesystem>
#include "../src/audio/AudioEngine.h"
#include "../src/audio/EqualizerDSP.h"
#include "../src/library/TagReader.h"
#include "../src/library/LibraryManager.h"
#include "../src/library/FileOrganizer.h"
#include "../src/library/AudioConverter.h"
#include "../src/library/PodcastManager.h"
#include "../src/library/VolumeScanner.h"
#include "../src/library/ScrobbleManager.h"
#include "../src/library/CueSheetParser.h"
#include "../src/library/OnlineMetadataFetcher.h"

namespace fs = std::filesystem;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting SlothPlayer Subsystem Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    // Test 1: TagReader on sample FLAC
    std::string testFlac = "D:\\Programming\\VIDPLAYER\\test_51_ident.flac";
    if (fs::exists(testFlac)) {
        std::cout << "[TEST 1] Testing TagReader on FLAC..." << std::endl;
        Track track;
        bool ok = TagReader::readMetadata(testFlac, track);
        assert(ok);
        std::cout << "  Track Title: " << track.getDisplayTitle() << std::endl;
        std::cout << "  Duration: " << track.formatDuration() << " (" << track.duration << "s)" << std::endl;
        std::cout << "  Sample Rate: " << track.sampleRate << " Hz" << std::endl;
        std::cout << "  Channels: " << track.channels << std::endl;
        assert(track.duration > 0.0);
        assert(track.sampleRate > 0);
        std::cout << "  -> TagReader test PASSED!" << std::endl;
    } else {
        std::cout << "[TEST 1] Skipped (sample flac not found)" << std::endl;
    }

    // Test 2: EqualizerDSP
    std::cout << "[TEST 2] Testing 10-Band EqualizerDSP..." << std::endl;
    {
        EqualizerDSP eq;
        eq.setSampleRate(44100.0f);
        eq.loadPreset("Bass Boost");
        assert(eq.getCurrentPresetName() == "Bass Boost");
        assert(eq.getBandGain(0) > 0.0f); // 31 Hz boosted

        // Process dummy buffer
        std::vector<float> buffer(1024 * 2, 0.5f);
        eq.process(buffer.data(), 1024, 2);

        for (float s : buffer) {
            assert(!std::isnan(s));
            assert(!std::isinf(s));
        }
        std::cout << "  -> EqualizerDSP test PASSED!" << std::endl;
    }

    // Test 3: AudioEngine Playback & Visualizer
    if (fs::exists(testFlac)) {
        std::cout << "[TEST 3] Testing AudioEngine Playback & Real-time Visualizer..." << std::endl;
        AudioEngine audio;
        bool initOk = audio.init();
        assert(initOk);

        bool playOk = audio.loadAndPlay(testFlac);
        assert(playOk);
        assert(audio.isPlaying());
        std::cout << "  Audio file loaded and playing..." << std::endl;

        // Play for 300ms to generate audio frames and visualizer data
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::vector<float> spectrum;
        audio.getSpectrum(spectrum, 32);
        assert(spectrum.size() == 32);

        float vuL = 0.0f, vuR = 0.0f;
        audio.getVUMeters(vuL, vuR);
        std::cout << "  32-band spectrum computed. VU Left: " << vuL << ", Right: " << vuR << std::endl;

        // Test seeking
        audio.seekTo(0.5);
        std::cout << "  Seeked to 0.5s." << std::endl;

        // Test volume
        audio.setVolume(0.5f);
        assert(audio.getVolume() == 0.5f);

        // Test pause/resume
        audio.pause();
        assert(audio.isPaused());
        audio.resume();
        assert(!audio.isPaused());

        audio.stop();
        assert(!audio.isPlaying());
        audio.shutdown();
        std::cout << "  -> AudioEngine test PASSED!" << std::endl;
    }

    // Test 4: LibraryManager & Playlists
    std::cout << "[TEST 4] Testing LibraryManager & Playlists..." << std::endl;
    {
        LibraryManager lib;
        lib.createPlaylist("Favorites Mix");
        assert(lib.getPlaylists().find("Favorites Mix") != lib.getPlaylists().end());

        lib.createPlaylist("Chill Vibez");
        lib.addToPlaylist("Chill Vibez", 1);
        lib.addToPlaylist("Chill Vibez", 2);
        assert(lib.getPlaylists().at("Chill Vibez").size() == 2);

        // M3U export
        bool m3uOk = lib.exportM3U("Chill Vibez", "test_chill.m3u");
        assert(m3uOk);
        assert(fs::exists("test_chill.m3u"));
        fs::remove("test_chill.m3u");

        // Library persistence
        bool saveOk = lib.saveLibrary("test_library.json");
        assert(saveOk);
        assert(fs::exists("test_library.json"));

        LibraryManager lib2;
        bool loadOk = lib2.loadLibrary("test_library.json");
        assert(loadOk);
        assert(lib2.getPlaylists().find("Chill Vibez") != lib2.getPlaylists().end());
        fs::remove("test_library.json");

        std::cout << "  -> LibraryManager test PASSED!" << std::endl;
    }

    // Test 5: AudioEngine Playback Speed & Fade Multiplier
    std::cout << "[TEST 5] Testing AudioEngine Playback Speed & Fade Multiplier..." << std::endl;
    {
        AudioEngine audio;
        audio.init();

        audio.setPlaybackSpeed(1.5f);
        assert(std::abs(audio.getPlaybackSpeed() - 1.5f) < 0.001f);

        // Clamp test: speed below min 0.5f clamps to 0.5f
        audio.setPlaybackSpeed(0.1f);
        assert(std::abs(audio.getPlaybackSpeed() - 0.5f) < 0.001f);

        // Clamp test: speed above max 2.0f clamps to 2.0f
        audio.setPlaybackSpeed(3.0f);
        assert(std::abs(audio.getPlaybackSpeed() - 2.0f) < 0.001f);

        // Fade Multiplier
        audio.setFadeMultiplier(0.75f);
        assert(std::abs(audio.getFadeMultiplier() - 0.75f) < 0.001f);

        audio.setFadeMultiplier(-0.5f);
        assert(audio.getFadeMultiplier() == 0.0f);

        audio.setFadeMultiplier(1.5f);
        assert(audio.getFadeMultiplier() == 1.0f);

        audio.shutdown();
        std::cout << "  -> AudioEngine Speed & Fade test PASSED!" << std::endl;
    }

    // Test 6: Auto-DJ Configuration & Selection
    std::cout << "[TEST 6] Testing Auto-DJ Configuration..." << std::endl;
    {
        LibraryManager lib;
        AutoDJConfig cfg;
        cfg.enabled = true;
        cfg.minRating = 3;
        cfg.favoritesOnly = true;
        cfg.artistSeparationTracks = 5;
        cfg.trackSeparationTracks = 15;
        cfg.genreFilter = "Rock";

        lib.setAutoDJConfig(cfg);
        const auto& retrieved = lib.getAutoDJConfig();
        assert(retrieved.enabled == true);
        assert(retrieved.minRating == 3);
        assert(retrieved.favoritesOnly == true);
        assert(retrieved.artistSeparationTracks == 5);
        assert(retrieved.trackSeparationTracks == 15);
        assert(retrieved.genreFilter == "Rock");

        // When library is empty, selectAutoDJTrack gracefully returns 0
        uint64_t trackId = lib.selectAutoDJTrack({1, 2, 3});
        assert(trackId == 0);

        std::cout << "  -> Auto-DJ Configuration test PASSED!" << std::endl;
    }

    // Test 7: Radio Streams Management & Persistence
    std::cout << "[TEST 7] Testing Radio Streams Management..." << std::endl;
    {
        LibraryManager lib;
        size_t initialCount = lib.getRadioStreams().size();
        assert(initialCount > 0); // Default curated stations exist

        lib.addRadioStream("SomaFM Suburbs of Goa", "http://ice1.somafm.com/suburbsofgoa-128-mp3", "World / Downtempo", "Desi-influenced Asian World Beats");
        assert(lib.getRadioStreams().size() == initialCount + 1);

        const auto& streams = lib.getRadioStreams();
        uint64_t addedId = streams.back().id;
        assert(streams.back().name == "SomaFM Suburbs of Goa");

        // Remove stream
        bool removed = lib.removeRadioStream(addedId);
        assert(removed);
        assert(lib.getRadioStreams().size() == initialCount);

        std::cout << "  -> Radio Streams test PASSED!" << std::endl;
    }

    // Test 8: Case Conversion Helpers
    std::cout << "[TEST 8] Testing Case Conversion Helpers..." << std::endl;
    {
        std::string title = "the dark side of the moon";
        std::string upper = convertStringToCase(title, CaseConversion::UpperCase);
        assert(upper == "THE DARK SIDE OF THE MOON");

        std::string lower = convertStringToCase(upper, CaseConversion::LowerCase);
        assert(lower == "the dark side of the moon");

        std::string titleCased = convertStringToCase("welcome to the jungle (live)", CaseConversion::TitleCase);
        assert(titleCased == "Welcome To The Jungle (Live)");

        std::cout << "  -> Case Conversion test PASSED!" << std::endl;
    }

    // Test 9: Library Statistics & Duplicate Detection logic
    std::cout << "[TEST 9] Testing Library Statistics & Duplicate Finder..." << std::endl;
    {
        LibraryManager lib;
        LibraryStats stats = lib.calculateStats();
        // With empty library
        assert(stats.totalTracks == 0);
        assert(stats.totalDurationSeconds == 0.0);

        auto dupes = lib.findDuplicates();
        assert(dupes.empty());

        std::cout << "  -> Library Stats & Duplicate Finder test PASSED!" << std::endl;
    }

    // Test 10: AudioEngine Stereo Balance & Pan Control
    std::cout << "[TEST 10] Testing AudioEngine Stereo Balance..." << std::endl;
    {
        AudioEngine audio;
        audio.init();

        audio.setStereoBalance(-0.75f);
        assert(std::abs(audio.getStereoBalance() - (-0.75f)) < 0.001f);

        // Clamping test: beyond -1.0 clamps to -1.0
        audio.setStereoBalance(-1.5f);
        assert(audio.getStereoBalance() == -1.0f);

        // Clamping test: beyond +1.0 clamps to +1.0
        audio.setStereoBalance(2.0f);
        assert(audio.getStereoBalance() == 1.0f);

        audio.shutdown();
        std::cout << "  -> Stereo Balance test PASSED!" << std::endl;
    }

    // Test 11: AudioEngine ReplayGain Preamp & Crossfade settings
    std::cout << "[TEST 11] Testing ReplayGain Preamp & Crossfade..." << std::endl;
    {
        AudioEngine audio;
        audio.init();

        audio.setReplayGainPreamp(3.5f);
        assert(std::abs(audio.getReplayGainPreamp() - 3.5f) < 0.001f);

        audio.setReplayGainPreamp(-20.0f);
        assert(audio.getReplayGainPreamp() == -12.0f);

        audio.setCrossfadeDuration(4.0f);
        assert(std::abs(audio.getCrossfadeDuration() - 4.0f) < 0.001f);

        audio.setSilenceSkipping(true);
        assert(audio.isSilenceSkipping() == true);

        audio.shutdown();
        std::cout << "  -> ReplayGain Preamp & Crossfade test PASSED!" << std::endl;
    }

    // Test 12: Waveform Peak Generation
    std::cout << "[TEST 12] Testing Waveform Peak Generation..." << std::endl;
    {
        std::vector<float> peaks = AudioEngine::generateWaveformPeaks(testFlac, 100);
        assert(peaks.size() == 100);
        for (float p : peaks) {
            assert(p >= 0.05f && p <= 1.0f);
        }
        std::cout << "  -> Waveform Peak Generation test PASSED!" << std::endl;
    }

    // Test 13: FileOrganizer Path Expansion & Sanitization
    std::cout << "[TEST 13] Testing FileOrganizer Path Expansion..." << std::endl;
    {
        Track t;
        t.artist = "Led Zeppelin";
        t.album = "Led Zeppelin IV";
        t.title = "Stairway to Heaven";
        t.trackNumber = 4;
        t.filePath = "C:/Music/sample.flac";

        std::string expanded = FileOrganizer::expandPattern("<Artist>/<Album>/<Track#> - <Title>", t, "D:/Organized");
        assert(expanded.find("Led Zeppelin") != std::string::npos);
        assert(expanded.find("Led Zeppelin IV") != std::string::npos);
        assert(expanded.find("04 - Stairway to Heaven") != std::string::npos);
        assert(expanded.find(".flac") != std::string::npos);

        std::string sanitized = FileOrganizer::sanitizePathComponent("Bad:Name?*Test/");
        assert(sanitized.find(':') == std::string::npos);
        assert(sanitized.find('?') == std::string::npos);
        assert(sanitized.find('*') == std::string::npos);

        std::cout << "  -> FileOrganizer test PASSED!" << std::endl;
    }

    // Test 14: Custom Smart Playlist Multi-Rule Evaluation
    std::cout << "[TEST 14] Testing Custom Smart Playlist Rules..." << std::endl;
    {
        LibraryManager lib;
        CustomSmartPlaylist spl;
        spl.name = "90s Heavy Rock";
        spl.matchAll = true;
        spl.limitTracks = 25;

        SmartPlaylistRule r1;
        r1.field = RuleField::Genre;
        r1.comparison = RuleComparison::Contains;
        r1.value = "Rock";
        spl.rules.push_back(r1);

        lib.addCustomSmartPlaylist(spl);
        assert(lib.getCustomSmartPlaylists().size() == 1);
        assert(lib.getCustomSmartPlaylists()[0].name == "90s Heavy Rock");

        std::vector<uint64_t> matched = lib.evaluateSmartPlaylist(spl);
        assert(matched.empty());

        bool delOk = lib.deleteCustomSmartPlaylist("90s Heavy Rock");
        assert(delOk);
        assert(lib.getCustomSmartPlaylists().empty());

        std::cout << "  -> Custom Smart Playlist test PASSED!" << std::endl;
    }

    // Test 15: Podcast Channel and Episode Management & Persistence
    std::cout << "[TEST 15] Testing Podcast Channel & Episode Management..." << std::endl;
    {
        PodcastManager pm;
        assert(!pm.getChannels().empty());
        size_t initialCount = pm.getChannels().size();

        pm.addChannel("Test Channel", "https://example.com/rss", "Host", "Tech", "Test podcast");
        assert(pm.getChannels().size() == initialCount + 1);

        uint64_t addedId = pm.getChannels().back().id;
        PodcastEpisode ep;
        ep.title = "Episode 1: The Beginning";
        ep.durationStr = "12:34";
        ep.audioUrl = "https://example.com/ep1.mp3";
        pm.addEpisode(addedId, ep);

        const PodcastChannel* ch = pm.getChannelById(addedId);
        assert(ch != nullptr);
        assert(!ch->episodes.empty());
        assert(ch->episodes.back().title == "Episode 1: The Beginning");

        bool saveOk = pm.saveToFile("test_podcasts.json");
        assert(saveOk);
        assert(fs::exists("test_podcasts.json"));

        PodcastManager pm2;
        bool loadOk = pm2.loadFromFile("test_podcasts.json");
        assert(loadOk);
        assert(pm2.getChannels().size() == initialCount + 1);
        fs::remove("test_podcasts.json");

        std::cout << "  -> Podcast Manager test PASSED!" << std::endl;
    }

    // Test 16: VolumeScanner ReplayGain calculation & Peak detection
    std::cout << "[TEST 16] Testing VolumeScanner ReplayGain Analyzer..." << std::endl;
    {
        Track t;
        t.id = 101;
        t.filePath = testFlac;
        t.duration = 6.0;

        VolumeScanResult res = VolumeScanner::analyzeTrack(t);
        if (fs::exists(testFlac)) {
            assert(res.success);
            assert(res.peak > 0.0f);
            assert(res.rms > 0.0f);
            assert(res.replayGainDb >= -15.0f && res.replayGainDb <= 15.0f);
        }

        std::cout << "  -> VolumeScanner test PASSED!" << std::endl;
    }

    // Test 17: ScrobbleManager threshold triggers & JSON persistence
    std::cout << "[TEST 17] Testing ScrobbleManager Subsystem..." << std::endl;
    {
        ScrobbleManager sm;
        sm.clearHistory();
        assert(sm.getScrobbleCount() == 0);

        Track t;
        t.id = 501;
        t.title = "Stairway to Heaven";
        t.artist = "Led Zeppelin";
        t.album = "Led Zeppelin IV";
        t.duration = 480.0; // 8 minutes

        sm.onTrackStarted(t);

        // At 100 seconds (under 50% and under 240s) -> should NOT scrobble yet
        sm.onTrackProgress(100.0, 480.0);
        assert(sm.getScrobbleCount() == 0);

        // At 240 seconds (reached 4-minute rule) -> triggers scrobble!
        sm.onTrackProgress(240.0, 480.0);
        assert(sm.getScrobbleCount() == 1);
        auto hist = sm.getHistory();
        assert(hist[0].title == "Stairway to Heaven");
        assert(hist[0].artist == "Led Zeppelin");

        // Further progress within same track should not duplicate
        sm.onTrackProgress(300.0, 480.0);
        assert(sm.getScrobbleCount() == 1);

        sm.onTrackEnded();

        // Save and Load test
        bool saveOk = sm.saveToFile("test_scrobbles.json");
        assert(saveOk);
        assert(fs::exists("test_scrobbles.json"));

        ScrobbleManager sm2;
        bool loadOk = sm2.loadFromFile("test_scrobbles.json");
        assert(loadOk);
        assert(sm2.getScrobbleCount() == 1);
        fs::remove("test_scrobbles.json");

        std::cout << "  -> ScrobbleManager test PASSED!" << std::endl;
    }

    // Test 18: AudioEngine Oscilloscope waveform extraction
    std::cout << "[TEST 18] Testing AudioEngine Oscilloscope Wave Extraction..." << std::endl;
    {
        AudioEngine audio;
        audio.init();

        std::vector<float> oscSamples;
        audio.getOscilloscope(oscSamples, 64);
        assert(oscSamples.size() == 64);

        audio.shutdown();
        std::cout << "  -> Oscilloscope extraction test PASSED!" << std::endl;
    }

    // Test 19: Track JSON Serialization for ReplayGain, Codec, BitsPerSample
    std::cout << "[TEST 19] Testing Track ReplayGain & Codec Serialization..." << std::endl;
    {
        Track t;
        t.id = 999;
        t.title = "Bohemian Rhapsody";
        t.codec = "FLAC";
        t.bitsPerSample = 24;
        t.replayGainTrackGain = -4.25f;
        t.replayGainTrackPeak = 0.985f;

        nlohmann::json j = t;
        assert(j["codec"] == "FLAC");
        assert(j["bitsPerSample"] == 24);
        assert(std::abs(j["replayGainTrackGain"].get<float>() - (-4.25f)) < 0.001f);
        assert(std::abs(j["replayGainTrackPeak"].get<float>() - 0.985f) < 0.001f);

        Track t2 = j.get<Track>();
        assert(t2.codec == "FLAC");
        assert(t2.bitsPerSample == 24);
        assert(std::abs(t2.replayGainTrackGain - (-4.25f)) < 0.001f);

        std::cout << "  -> Track Extended Metadata Serialization test PASSED!" << std::endl;
    }

    // Test 20: CueSheetParser Sub-track Extraction
    std::cout << "[TEST 20] Testing CueSheetParser Sub-track Extraction..." << std::endl;
    {
        std::string sampleCue = 
            "REM GENRE Electronic\n"
            "REM DATE 2001\n"
            "REM DISCNUMBER 1\n"
            "PERFORMER \"Daft Punk\"\n"
            "TITLE \"Discovery\"\n"
            "FILE \"Daft Punk - Discovery.flac\" WAVE\n"
            "  TRACK 01 AUDIO\n"
            "    TITLE \"One More Time\"\n"
            "    PERFORMER \"Daft Punk\"\n"
            "    INDEX 01 00:00:00\n"
            "  TRACK 02 AUDIO\n"
            "    TITLE \"Aerodynamic\"\n"
            "    PERFORMER \"Daft Punk\"\n"
            "    INDEX 01 05:20:00\n"
            "  TRACK 03 AUDIO\n"
            "    TITLE \"Digital Love\"\n"
            "    PERFORMER \"Daft Punk\"\n"
            "    INDEX 01 08:52:50\n";

        auto tracks = CueSheetParser::parseCueContent(sampleCue, "C:\\Music", "C:\\Music\\Discovery.cue");
        assert(tracks.size() == 3);
        assert(tracks[0].title == "One More Time");
        assert(tracks[0].trackNumber == 1);
        assert(tracks[0].isCueSubtrack == true);
        assert(std::abs(tracks[0].cueStartSeconds - 0.0) < 0.001);
        assert(std::abs(tracks[0].cueDurationSeconds - 320.0) < 0.001); // 5m20s = 320s

        assert(tracks[1].title == "Aerodynamic");
        assert(tracks[1].trackNumber == 2);
        assert(std::abs(tracks[1].cueStartSeconds - 320.0) < 0.001);

        assert(tracks[2].title == "Digital Love");
        assert(tracks[2].trackNumber == 3);

        std::cout << "  -> CueSheetParser test PASSED!" << std::endl;
    }

    // Test 21: Extended Metadata & Raw Tags Serialization
    std::cout << "[TEST 21] Testing Extended Metadata & Raw Tags..." << std::endl;
    {
        Track t;
        t.id = 555;
        t.title = "Paranoid Android";
        t.artist = "Radiohead";
        t.albumArtist = "Radiohead";
        t.album = "OK Computer";
        t.composer = "Thom Yorke";
        t.comment = "Masterpiece";
        t.discNumber = 1;
        t.totalDiscs = 2;
        t.trackNumber = 2;
        t.trackTotal = 12;
        t.rawTags["ENCODER"] = "Lavf58.76.100";
        t.rawTags["ISRC"] = "GBAYE9700073";

        nlohmann::json j = t;
        assert(j["albumArtist"] == "Radiohead");
        assert(j["composer"] == "Thom Yorke");
        assert(j["discNumber"] == 1);
        assert(j["totalDiscs"] == 2);
        assert(j["trackTotal"] == 12);
        assert(j["rawTags"]["ISRC"] == "GBAYE9700073");

        Track t2 = j.get<Track>();
        assert(t2.albumArtist == "Radiohead");
        assert(t2.composer == "Thom Yorke");
        assert(t2.discNumber == 1);
        assert(t2.totalDiscs == 2);
        assert(t2.rawTags["ISRC"] == "GBAYE9700073");

        std::cout << "  -> Extended Metadata & Raw Tags test PASSED!" << std::endl;
    }

    // Test 22: ScrobbleManager Last.fm Auth Configuration
    std::cout << "[TEST 22] Testing ScrobbleManager Last.fm Auth & Persistence..." << std::endl;
    {
        ScrobbleManager sm;
        assert(!sm.hasLastFmAuth());
        sm.setLastFmCredentials("audiophile_user", "session_key_secret_123", "api_key_456");
        assert(sm.hasLastFmAuth());
        assert(sm.getLastFmUsername() == "audiophile_user");
        assert(sm.getLastFmSessionKey() == "session_key_secret_123");

        sm.saveToFile("test_scrobble_auth.json");

        ScrobbleManager sm2;
        sm2.loadFromFile("test_scrobble_auth.json");
        assert(sm2.hasLastFmAuth());
        assert(sm2.getLastFmUsername() == "audiophile_user");
        assert(sm2.getLastFmSessionKey() == "session_key_secret_123");

        fs::remove("test_scrobble_auth.json");
        std::cout << "  -> ScrobbleManager Last.fm Auth test PASSED!" << std::endl;
    }

    // Test 23: Audio Driver Release When Paused & Stopped
    if (fs::exists(testFlac)) {
        std::cout << "[TEST 23] Testing Audio Driver Release When Paused & Stopped..." << std::endl;
        AudioEngine audio;
        audio.init();

        // 1. By default, releaseDriverWhenPaused is true
        assert(audio.isReleaseDriverWhenPaused());

        // Because it was idle on init, device should be released
        assert(!audio.isDeviceInitialized());

        // 2. Play track -> device should be acquired and started
        bool playOk = audio.loadAndPlay(testFlac);
        assert(playOk);
        assert(audio.isPlaying());
        assert(audio.isDeviceInitialized());

        // 3. Pause track -> device should be immediately released
        audio.pause();
        assert(audio.isPaused());
        assert(!audio.isDeviceInitialized());

        // 4. Resume track -> device should be reacquired seamlessly
        audio.resume();
        assert(!audio.isPaused());
        assert(audio.isPlaying());
        assert(audio.isDeviceInitialized());

        // 5. Stop track -> device should be released
        audio.stop();
        assert(!audio.isPlaying());
        assert(!audio.isDeviceInitialized());

        // 6. Disable releaseDriverWhenPaused
        audio.setReleaseDriverWhenPaused(false);
        assert(!audio.isReleaseDriverWhenPaused());

        audio.loadAndPlay(testFlac);
        assert(audio.isPlaying());
        assert(audio.isDeviceInitialized());

        // With setting false, pause keeps device initialized
        audio.pause();
        assert(audio.isPaused());
        assert(audio.isDeviceInitialized());

        // Enabling release while paused immediately releases device
        audio.setReleaseDriverWhenPaused(true);
        assert(audio.isReleaseDriverWhenPaused());
        assert(!audio.isDeviceInitialized());

        audio.shutdown();
        std::cout << "  -> Audio Driver Release When Paused test PASSED!" << std::endl;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "ALL 23 SUBSYSTEM TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
