#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace Platform {
    // Returns platform-specific user config / app data directory
    // Windows: %APPDATA%/SlothPlayer
    // Linux: $XDG_CONFIG_HOME/SlothPlayer or $HOME/.config/SlothPlayer
    std::string getAppDataDir();

    // Locates a file in system file manager (Explorer / xdg-open)
    void openInFileManager(const std::string& filePath);

    // Opens a directory in system file manager
    void openDirectory(const std::string& dirPath);

    // Displays open file dialog
    std::string openFileDialog(const std::string& filterTitle, const std::string& filterSpec);

    // Displays open folder dialog
    std::string openFolderDialog(const std::string& title = "Select Folder");

    // Requests application shutdown
    void requestQuit();

    // Register quit flag pointer for cross-platform window loop
    void setQuitFlag(bool* pFlag);

    // Resizes native window for compact mini-player mode
    void setMiniPlayerWindow(void* windowHandle, bool isMini, int targetWidth = 520, int targetHeight = 175, bool alwaysOnTop = true);
    void resizeNativeWindow(void* windowHandle, int targetWidth, int targetHeight, bool alwaysOnTop = true);
    void setWindowAlwaysOnTop(void* windowHandle, bool alwaysOnTop);
    void setWindowOpacity(void* windowHandle, float opacity);
}
