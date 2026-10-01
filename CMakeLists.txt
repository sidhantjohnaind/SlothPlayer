cmake_minimum_required(VERSION 3.20)
project(SlothPlayer C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Architecture and Platform Identification (AMD, ARM, RISC-V)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    set(SLOTH_ARCH "AMD64 / x86_64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(SLOTH_ARCH "ARM64 / AArch64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "arm.*")
    set(SLOTH_ARCH "ARM 32-bit (armhf)")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    set(SLOTH_ARCH "RISC-V 64-bit (riscv64)")
else()
    set(SLOTH_ARCH "${CMAKE_SYSTEM_PROCESSOR} (Generic)")
endif()

message(STATUS "SlothPlayer Target OS: ${CMAKE_SYSTEM_NAME}, Architecture: ${SLOTH_ARCH}")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
)

set(IMGUI_CORE_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
)

if (WIN32)
    set(IMGUI_BACKEND_SOURCES
        third_party/imgui/backends/imgui_impl_win32.cpp
        third_party/imgui/backends/imgui_impl_dx11.cpp
    )
    set(PLATFORM_RESOURCES
        resources/SlothPlayer.rc
    )
else()
    set(IMGUI_BACKEND_SOURCES
        third_party/imgui/backends/imgui_impl_glfw.cpp
        third_party/imgui/backends/imgui_impl_opengl3.cpp
    )
    set(PLATFORM_RESOURCES "")
endif()

set(APP_SOURCES
    src/main.cpp
    src/platform/Platform.cpp
    src/platform/SMTCManager.cpp
    src/platform/TaskbarManager.cpp
    src/platform/SettingsManager.cpp
    src/network/HttpClient.cpp
    src/audio/miniaudio_impl.cpp
    src/audio/AudioEngine.cpp
    src/audio/EqualizerDSP.cpp
    src/library/TagReader.cpp
    src/library/FolderWatcher.cpp
    src/library/CueSheetParser.cpp
    src/library/OnlineMetadataFetcher.cpp
    src/library/LibraryManager.cpp
    src/library/LyricsManager.cpp
    src/library/FileOrganizer.cpp
    src/library/AudioConverter.cpp
    src/library/PodcastManager.cpp
    src/library/VolumeScanner.cpp
    src/library/ScrobbleManager.cpp
    src/graphics/TextureManager.cpp
    src/ui/Theme.cpp
    src/ui/SkinManager.cpp
    src/ui/MainWindow.cpp
    ${PLATFORM_RESOURCES}
)

add_executable(SlothPlayer
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
    ${APP_SOURCES}
)

add_executable(test_audio_library
    tests/test_audio_library.cpp
    src/platform/Platform.cpp
    src/platform/SettingsManager.cpp
    src/network/HttpClient.cpp
    src/audio/miniaudio_impl.cpp
    src/audio/AudioEngine.cpp
    src/audio/EqualizerDSP.cpp
    src/library/TagReader.cpp
    src/library/FolderWatcher.cpp
    src/library/CueSheetParser.cpp
    src/library/OnlineMetadataFetcher.cpp
    src/library/LibraryManager.cpp
    src/library/FileOrganizer.cpp
    src/library/AudioConverter.cpp
    src/library/PodcastManager.cpp
    src/library/VolumeScanner.cpp
    src/library/ScrobbleManager.cpp
)

if (WIN32)
    if (MINGW)
        target_compile_options(SlothPlayer PRIVATE -O3 -DNDEBUG -Wall -Wextra -Wno-unused-parameter)
        target_link_libraries(SlothPlayer PRIVATE
            d3d11
            d3dcompiler
            dxgi
            dwmapi
            winhttp
            ole32
            shlwapi
            shell32
            comdlg32
            uuid
            gdi32
            winmm
            mfplat
            mfreadwrite
            mfuuid
        )
        target_compile_options(test_audio_library PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
        target_link_libraries(test_audio_library PRIVATE
            winhttp
            ole32
            shlwapi
            shell32
            comdlg32
            uuid
            winmm
            dwmapi
            mfplat
            mfreadwrite
            mfuuid
        )
    elseif (MSVC)
        target_compile_options(SlothPlayer PRIVATE /O2 /W3 /utf-8)
        target_link_libraries(SlothPlayer PRIVATE
            d3d11.lib
            d3dcompiler.lib
            dxgi.lib
            dwmapi.lib
            winhttp.lib
            ole32.lib
            shlwapi.lib
            shell32.lib
            comdlg32.lib
            gdi32.lib
            winmm.lib
        )
        target_compile_options(test_audio_library PRIVATE /O2 /W3 /utf-8)
        target_link_libraries(test_audio_library PRIVATE
            winhttp.lib
            ole32.lib
            shlwapi.lib
            shell32.lib
            comdlg32.lib
            winmm.lib
            dwmapi.lib
        )
    endif()
elseif (APPLE)
    # macOS (Apple Silicon ARM64 & Intel x86_64)
    find_package(Threads REQUIRED)
    find_package(OpenGL REQUIRED)

    find_package(glfw3 3.3 QUIET)
    if (glfw3_FOUND)
        set(GLFW_LIBS glfw)
    else()
        find_library(GLFW_LIB NAMES glfw glfw3)
        if (GLFW_LIB)
            set(GLFW_LIBS ${GLFW_LIB})
        else()
            set(GLFW_LIBS glfw)
        endif()
    endif()

    target_compile_options(SlothPlayer PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(SlothPlayer PRIVATE
        ${GLFW_LIBS}
        "-framework OpenGL"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
    )

    target_compile_options(test_audio_library PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(test_audio_library PRIVATE
        Threads::Threads
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        ${CMAKE_DL_LIBS}
        m
    )
else()
    # Linux (x86_64 / AMD, aarch64 / ARM, riscv64 / RISC-V)
    find_package(Threads REQUIRED)
    find_package(OpenGL REQUIRED)

    find_package(glfw3 3.3 QUIET)
    if (glfw3_FOUND)
        set(GLFW_LIBS glfw)
    else()
        find_library(GLFW_LIB NAMES glfw glfw3)
        if (GLFW_LIB)
            set(GLFW_LIBS ${GLFW_LIB})
        else()
            set(GLFW_LIBS glfw)
        endif()
    endif()

    find_library(ASOUND_LIB NAMES asound)
    if (NOT ASOUND_LIB)
        set(ASOUND_LIB "-lasound")
    endif()

    target_compile_options(SlothPlayer PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(SlothPlayer PRIVATE
        ${GLFW_LIBS}
        OpenGL::GL
        Threads::Threads
        ${ASOUND_LIB}
        ${CMAKE_DL_LIBS}
        m
    )

    target_compile_options(test_audio_library PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(test_audio_library PRIVATE
        Threads::Threads
        ${ASOUND_LIB}
        ${CMAKE_DL_LIBS}
        m
    )
endif()
