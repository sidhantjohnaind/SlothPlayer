@echo off
set PATH=D:\w64devkit\bin;%PATH%
echo ==============================================
echo Building SlothPlayer in C++
echo ==============================================
if not exist build mkdir build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b %ERRORLEVEL%
)
ninja -C build -j 2
if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)
echo ==============================================
echo Build succeeded! Output: build\SlothPlayer.exe
echo ==============================================
