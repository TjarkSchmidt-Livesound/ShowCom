cmake_minimum_required(VERSION 3.22)

project(ShowCom VERSION 0.7.0 LANGUAGES C CXX)

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

# Parakeet-Spracherkennung über sherpa-onnx (vorgebaute Bibliotheken von GitHub).
option(TBS_WITH_PARAKEET "Parakeet speech recognition via sherpa-onnx" ON)
set(TBS_SHERPA_DIR "" CACHE PATH "Optional: local prebuilt sherpa-onnx directory (include/ + lib/)")

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

# sherpa-onnx (Apache-2.0) inkl. ONNX Runtime (MIT), statisch gelinkt – keine DLLs neben der .exe
set(TBS_SHERPA_VERSION 1.13.8)
if(TBS_WITH_PARAKEET AND NOT TBS_SHERPA_DIR)
    if(APPLE)
        set(_sherpa_pkg osx-universal2-static)
        set(_sherpa_sha 57ef55a8fc8ef849402982ad878d0e3e71306ef221635ba50f9f34fdf15b483c)
    elseif(WIN32)
        set(_sherpa_pkg win-x64-static-MD-Release)
        set(_sherpa_sha c4547b0fdf93a266d241c4d28dbb55a70a8e64804e1d5eee77412bd0f3d93f6f)
    else()
        set(_sherpa_pkg linux-x64-shared)
        set(_sherpa_sha c0bdb7907d3a74bba1d55d22bf4d9fa75586cf1530614ebe88a27b9118e015c4)
    endif()
    FetchContent_Declare(sherpa_onnx
        URL https://github.com/k2-fsa/sherpa-onnx/releases/download/v${TBS_SHERPA_VERSION}/sherpa-onnx-v${TBS_SHERPA_VERSION}-${_sherpa_pkg}.tar.bz2
        URL_HASH SHA256=${_sherpa_sha}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(sherpa_onnx)
    set(TBS_SHERPA_DIR ${sherpa_onnx_SOURCE_DIR})
endif()

# Alle eigenen Quelltexte sind UTF-8 (Umlaute in Texten)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ------------------------------------------------------------------------------
# Core library (no GUI) – shared by the app and the tests
# ------------------------------------------------------------------------------
add_library(tbs_core STATIC
    src/core/KeywordMatcher.cpp
    src/core/SpeechSegmenter.cpp
    src/core/ShureParser.cpp
    src/core/Cutout.cpp)
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
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon_small.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ShowCom nimmt die Talkback-Kanäle deines Audio-Interfaces auf und transkribiert sie lokal."
    DOCUMENT_BROWSER_ENABLED FALSE)

juce_add_binary_data(ShowComAssets SOURCES
    resources/icon_small.png
    resources/silero_vad.onnx
    resources/web/start.html
    resources/web/app.html)

target_sources(ShowCom PRIVATE
    src/Main.cpp
    src/AppState.cpp
    src/AppController.cpp
    src/Wireless.cpp
    src/WebServer.cpp
    src/ui/BoardView.cpp
    src/AudioEngine.cpp
    src/Transcriber.cpp
    src/ParakeetEngine.cpp
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

if(TBS_WITH_PARAKEET)
    if(NOT EXISTS "${TBS_SHERPA_DIR}/include/sherpa-onnx/c-api/cxx-api.h")
        message(FATAL_ERROR "sherpa-onnx not found in ${TBS_SHERPA_DIR}")
    endif()
    target_include_directories(ShowCom PRIVATE "${TBS_SHERPA_DIR}/include")
    target_compile_definitions(ShowCom PRIVATE TBS_HAVE_SHERPA=1)

    set(_sherpa_names sherpa-onnx-cxx-api sherpa-onnx-c-api sherpa-onnx-core kaldi-decoder-core
        sherpa-onnx-kaldifst-core sherpa-onnx-fstfar sherpa-onnx-fst kaldi-native-fbank-core
        kissfft-float ssentencepiece_core piper_phonemize espeak-ng ucd onnxruntime)
    set(_sherpa_link_libs "")
    if(WIN32)
        foreach(l ${_sherpa_names})
            list(APPEND _sherpa_link_libs "${TBS_SHERPA_DIR}/lib/${l}.lib")
        endforeach()
    elseif(APPLE)
        foreach(l ${_sherpa_names})
            list(APPEND _sherpa_link_libs "${TBS_SHERPA_DIR}/lib/lib${l}.a")
        endforeach()
        list(APPEND _sherpa_link_libs "-framework Foundation")
    else()
        # Linux (nur Entwicklung/Tests): geteilte Bibliotheken
        set(_sherpa_link_libs "${TBS_SHERPA_DIR}/lib/libsherpa-onnx-cxx-api.so"
                              "${TBS_SHERPA_DIR}/lib/libsherpa-onnx-c-api.so"
                              "${TBS_SHERPA_DIR}/lib/libonnxruntime.so")
        set_target_properties(ShowCom PROPERTIES BUILD_RPATH "${TBS_SHERPA_DIR}/lib")
    endif()
    target_link_libraries(ShowCom PRIVATE ${_sherpa_link_libs})
endif()

if(APPLE)
    # Profilbilder freistellen mit Apple Vision (macOS 12+, ältere Systeme nutzen das eigene Verfahren)
    target_sources(ShowCom PRIVATE src/CutoutMac.mm)
    set_source_files_properties(src/CutoutMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(ShowCom PRIVATE "-framework Vision" "-framework CoreVideo")
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
        ShowComAssets
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
    target_sources(tbs_engine_test PRIVATE tests/EngineTest.cpp src/AudioEngine.cpp src/Wireless.cpp src/WebServer.cpp)
    target_include_directories(tbs_engine_test PRIVATE src)
    target_compile_definitions(tbs_engine_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0)
    target_link_libraries(tbs_engine_test PRIVATE tbs_core ShowComAssets
        juce::juce_audio_devices juce::juce_audio_formats juce::juce_gui_basics juce::juce_recommended_config_flags)
    add_test(NAME engine COMMAND tbs_engine_test)

    # Spracherkennung mit echten Audiodateien (nur lokal mit Modell, siehe tests/AsrTest.cpp)
    juce_add_console_app(tbs_asr_test PRODUCT_NAME "tbs_asr_test")
    target_sources(tbs_asr_test PRIVATE tests/AsrTest.cpp src/Transcriber.cpp src/ParakeetEngine.cpp)
    target_include_directories(tbs_asr_test PRIVATE src)
    target_compile_definitions(tbs_asr_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(tbs_asr_test PRIVATE whisper juce::juce_audio_formats juce::juce_events juce::juce_recommended_config_flags)
    if(TBS_WITH_PARAKEET)
        target_include_directories(tbs_asr_test PRIVATE "${TBS_SHERPA_DIR}/include")
        target_compile_definitions(tbs_asr_test PRIVATE TBS_HAVE_SHERPA=1)
        target_link_libraries(tbs_asr_test PRIVATE ${_sherpa_link_libs})
        if(UNIX AND NOT APPLE)
            set_target_properties(tbs_asr_test PROPERTIES BUILD_RPATH "${TBS_SHERPA_DIR}/lib")
        endif()
    endif()
endif()
