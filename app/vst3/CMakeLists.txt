cmake_minimum_required(VERSION 3.22)
project(MegaTune VERSION 0.7.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(JUCE_DIR "/private/tmp/juce" CACHE PATH "JUCE source directory")
set(MEGATUNE_JUCE_TAG "8.0.15" CACHE STRING "JUCE git tag fetched when JUCE_DIR is absent")
# createEditorAndMakeActive (used by the test suite and editor API) landed in 8.0.13,
# so the build needs at least that. Use a local JUCE_DIR when it exists; otherwise
# fetch a known-good tag so a fresh checkout builds without a manual JUCE clone.
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" juce)
else()
    message(STATUS "JUCE not found at ${JUCE_DIR}; fetching ${MEGATUNE_JUCE_TAG} via FetchContent")
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${MEGATUNE_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()
juce_add_plugin(MegaTune COMPANY_NAME "ademusic" PLUGIN_MANUFACTURER_CODE AdMs PLUGIN_CODE Mgtn FORMATS VST3 AU PRODUCT_NAME "MegaTune" BUNDLE_ID "com.ademusic.megatune" IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(MegaTune PRIVATE Plugin.cpp)
target_include_directories(MegaTune PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-linear/include")
target_compile_definitions(MegaTune PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MegaTune PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
juce_add_console_app(MegaTuneTests PRODUCT_NAME "MegaTuneTests")
target_sources(MegaTuneTests PRIVATE Tests.cpp)
target_link_libraries(MegaTuneTests PRIVATE MegaTune juce::juce_audio_utils)
target_compile_definitions(MegaTuneTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
enable_testing()
add_test(NAME MegaTuneStreaming COMMAND MegaTuneTests)
# Offline render bench used by verify/bench.py (not part of ctest).
juce_add_console_app(MegaTuneBench PRODUCT_NAME "MegaTuneBench")
target_sources(MegaTuneBench PRIVATE verify/Bench.cpp)
target_link_libraries(MegaTuneBench PRIVATE MegaTune juce::juce_audio_utils)
target_compile_definitions(MegaTuneBench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
