cmake_minimum_required(VERSION 3.15)
project(BanglaVoiceTyping VERSION 1.0.0 LANGUAGES CXX RC)

# ============================================================================
# C++ Standard
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Unicode Support (required for Bangla text handling)
# ============================================================================
add_definitions(-DUNICODE -D_UNICODE)

# ============================================================================
# Source Files
# ============================================================================
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/ui.cpp
    src/recorder.cpp
    src/transcriber.cpp
    src/paster.cpp
    src/config.cpp
    src/indicator.cpp
    src/updater.cpp
)

set(HEADERS
    src/app.h
    src/ui.h
    src/recorder.h
    src/transcriber.h
    src/paster.h
    src/config.h
    src/wav_utils.h
    src/indicator.h
)

# ============================================================================
# Resource Files
# ============================================================================
set(RESOURCES
    resources/app.rc
)

# ============================================================================
# Executable (WIN32 = subsystem:windows, no console window)
# ============================================================================
add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# ============================================================================
# Include Directories
# ============================================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# ============================================================================
# Link Windows System Libraries
# ============================================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    winhttp         # HTTP/HTTPS requests (WinHTTP API)
    winmm           # Windows Multimedia (audio, waveIn)
    gdiplus         # GDI+ (modern 2D graphics rendering)
    user32          # Window management, input, hotkeys
    shell32         # Shell functions (system tray, Shell_NotifyIcon)
    ole32           # COM initialization (required by some Win32 APIs)
    comctl32        # Common controls
    gdi32           # GDI (device contexts, fonts)
    dwmapi          # Desktop Window Manager (rounded corners)
)

# ============================================================================
# Static CRT Linking (no external DLL dependencies)
# ============================================================================
set_property(TARGET ${PROJECT_NAME} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# ============================================================================
# Compiler Options
# ============================================================================
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4                     # Warning level 4
        /WX-                    # Don't treat warnings as errors (for now)
        /utf-8                  # Source files are UTF-8 encoded
        /permissive-            # Strict standard conformance
        $<$<CONFIG:Release>:/O2 /GL>    # Optimize for speed in Release
        $<$<CONFIG:Debug>:/Od /Zi>      # No optimization + debug info in Debug
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/LTCG>      # Link-time code generation in Release
    )
endif()

# ============================================================================
# Output Directory
# ============================================================================
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/Release"
)

# ============================================================================
# Post-build: Copy config.txt to output directory
# ============================================================================
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.txt"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/voice bangla.png"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/VoiceBangla.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/fonts/HindSiliguri-Regular.ttf"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/HindSiliguri-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/fonts/HindSiliguri-Bold.ttf"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/HindSiliguri-Bold.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/fonts/Inter-Regular.ttf"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Inter-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/fonts/Inter-Bold.ttf"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Inter-Bold.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/fonts/Inter-SemiBold.ttf"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Inter-SemiBold.ttf"
    COMMENT "Copying config, logo, and fonts to output directory"
)
