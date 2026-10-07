cmake_minimum_required(VERSION 3.22)

project(ShowCom VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
# Windows: Pfad zum Steinberg ASIO SDK (Ordner, der "common/iasiodrv.h" enthält).
set(TBS_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (Windows only)")

# Nur für Compile-Checks auf Linux-Build-Servern ohne Audio-/X11-Extras.
option(TBS_LINUX_MINIMAL "Build on Linux without ALSA/JACK/curl/Xrandr (compile check only)" OFF)

option(TBS_BUILD_TESTS "Build unit tests" ON)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)

# whisper.cpp: lokale Spracherkennung.
# GPU: macOS nutzt automatisch Metal. Unter Windows z. B. -DGGML_VULKAN=ON oder -DGGML_CUDA=ON.
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG        v1.9.5
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(juce whisper)

# ------------------------------------------------------------------------------
# Core library (no GUI) – shared by the app and the tests
# ------------------------------------------------------------------------------
add_library(tbs_core STATIC
    src/core/KeywordMatcher.cpp
    src/core/SpeechSegmenter.cpp)
target_include_directories(tbs_core PUBLIC src/core)
target_compile_features(tbs_core PUBLIC cxx_std_17)
set_target_properties(tbs_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------
# App
# ------------------------------------------------------------------------------
juce_add_gui_app(ShowCom
    PRODUCT_NAME "ShowCom"
    COMPANY_NAME "ShowCom"
    BUNDLE_ID "de.showcom.app"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ShowCom nimmt die Talkback-Kanäle deines Audio-Interfaces auf und transkribiert sie lokal."
    DOCUMENT_BROWSER_ENABLED FALSE)

target_sources(ShowCom PRIVATE
    src/Main.cpp
    src/AppState.cpp
    src/AppController.cpp
    src/AudioEngine.cpp
    src/Transcriber.cpp
    src/TranscriptModel.cpp
    src/ui/MainComponent.cpp
    src/ui/TranscriptView.cpp
    src/ui/UsersPanel.cpp
    src/ui/SettingsPanel.cpp
    src/ui/Theme.cpp)

target_include_directories(ShowCom PRIVATE src src/ui)

target_compile_definitions(ShowCom PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CAMERA=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ShowCom,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ShowCom,JUCE_VERSION>")

if(MSVC)
    target_compile_options(ShowCom PRIVATE /utf-8)
    target_compile_options(tbs_core PRIVATE /utf-8)
endif()

if(WIN32)
    if(TBS_ASIO_SDK_DIR)
        if(NOT EXISTS "${TBS_ASIO_SDK_DIR}/common/iasiodrv.h")
            message(FATAL_ERROR "TBS_ASIO_SDK_DIR does not contain common/iasiodrv.h: ${TBS_ASIO_SDK_DIR}")
        endif()
        target_include_directories(ShowCom PRIVATE "${TBS_ASIO_SDK_DIR}/common")
        target_compile_definitions(ShowCom PRIVATE JUCE_ASIO=1)
        message(STATUS "ASIO support: ON (${TBS_ASIO_SDK_DIR})")
    else()
        message(WARNING "ASIO support: OFF. Set -DTBS_ASIO_SDK_DIR=<path to asiosdk> to enable ASIO.")
    endif()
    target_compile_definitions(ShowCom PRIVATE JUCE_WASAPI=1 JUCE_DIRECTSOUND=0)
endif()

if(UNIX AND NOT APPLE)
    if(TBS_LINUX_MINIMAL)
        target_compile_definitions(ShowCom PRIVATE
            JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_CURL=0
            JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
    else()
        target_compile_definitions(ShowCom PRIVATE JUCE_ALSA=1 JUCE_JACK=1)
    endif()
endif()

target_link_libraries(ShowCom
    PRIVATE
        tbs_core
        whisper
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(TBS_BUILD_TESTS)
    enable_testing()
    add_executable(tbs_tests tests/CoreTests.cpp)
    target_link_libraries(tbs_tests PRIVATE tbs_core)
    add_test(NAME core COMMAND tbs_tests)

    # Integrationstest der Audio-Engine mit simuliertem Interface
    juce_add_console_app(tbs_engine_test PRODUCT_NAME "tbs_engine_test")
    target_sources(tbs_engine_test PRIVATE tests/EngineTest.cpp src/AudioEngine.cpp)
    target_include_directories(tbs_engine_test PRIVATE src)
    target_compile_definitions(tbs_engine_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0)
    target_link_libraries(tbs_engine_test PRIVATE tbs_core
        juce::juce_audio_devices juce::juce_audio_formats juce::juce_recommended_config_flags)
    add_test(NAME engine COMMAND tbs_engine_test)
endif()
