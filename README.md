# SlothPlayer - Modern Desktop Audio Player in C++

A high-performance, native Windows C++ desktop music player and audio library manager.

Built with **DirectX 11**, **Dear ImGui**, and a low-latency WASAPI **miniaudio** engine.

---

## Features

### 1. Multi-Panel Dark UI
- **Top Menu & Global Instant Search**: Filter tracks across title, artist, album, and genre in real-time.
- **Left Panel (Library Navigator & Sources)**:
  - Library categories: *All Tracks*, *Favorites*, *Recently Added*.
  - Playlists manager: Create, rename, delete custom playlists, export to `.m3u`.
  - Folder Explorer: Monitored local music folders with directory tree.
  - Background scanner progress bar and animated status.
- **Center Panel (Main Views)**:
  - **Tracks View**: Multi-column sortable table with Status icons, Track #, Title, Artist, Album, Duration, Genre, and Year. Double-click to play, right-click context menu (Play, Play Next, Queue, Favorite, Playlist, Properties).
  - **Album Covers Grid**: Responsive grid of album art cards with metadata and track count.
  - **Artists View**: Artist discography browser.
  - **Folder Browser**: Direct file browsing and playback.
- **Right Panel (Track Info, Queue & Visualizer)**:
  - High-resolution album artwork display (with procedural vinyl disk fallback if no cover art exists).
  - Track metadata & format badge (bitrate, sample rate, channels).
  - **Live Real-Time Audio Visualizer**: 32-band animated spectrum analyzer and stereo L/R VU meters driven directly by the audio output stream.
  - Now Playing Queue with drag/click to play.
- **Bottom Transport Bar**:
  - Mini album art cover, track title, artist, album, and favorite star toggle.
  - Playback controls: Shuffle, Previous, Play/Pause, Stop, Next, Repeat mode (Off / All / One).
  - Interactive Scrubber: Elapsed time, draggable progress slider, total duration.
  - Volume control with mute toggle.
  - Quick **EQ** launcher button.

### 2. 10-Band Graphic DSP Equalizer
- **10 Frequency Bands**: 31Hz, 63Hz, 125Hz, 250Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz, 16kHz.
- **Preamp Gain**: -12dB to +12dB.
- **Biquad Filters**: Real-time peaking IIR filters applied in the playback audio stream.
- **Presets**: Flat, Rock, Pop, Jazz, Classical, Bass Boost, Treble Boost, Vocal Boost, Electronic, Acoustic.

### 3. Audio & Tag Engine
- **WASAPI Playback**: Low-latency native Windows audio output via `miniaudio`.
- **Formats**: MP3, FLAC, WAV, OGG, AAC/M4A.
- **Tag Extraction**: ID3v1, ID3v2.3/ID3v2.4, and FLAC Vorbis Comments + APIC embedded cover art extraction.
- **Library Persistence**: Fast JSON database cache (`slothplayer_library.json`).

---

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| **Space** | Play / Pause |
| **Left / Right** | Seek backward / forward 5 seconds |
| **Ctrl + Left** | Previous Track |
| **Ctrl + Right** | Next Track |
| **Up / Down** | Volume Up / Down (+5% / -5%) |
| **M** | Toggle Mute |
| **Ctrl + E** | Toggle 10-Band Equalizer |

---

## Building and Running

### Requirements
- Windows 10/11 64-bit
- MinGW-w64 GCC or MSVC
- CMake 3.20+
- Ninja

### One-Click Build
```cmd
build.bat
```

### Running
```cmd
.\build\SlothPlayer.exe
```

### Running the Subsystem Test Suite
```cmd
ninja -C build test_audio_library
.\build\test_audio_library.exe
```
