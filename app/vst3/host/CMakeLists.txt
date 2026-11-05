cmake_minimum_required(VERSION 3.22)
project(MegaTuneHostCheck VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(JUCE_DIR "/private/tmp/juce" CACHE PATH "JUCE source directory")
add_subdirectory("${JUCE_DIR}" juce)
juce_add_console_app(MegaTuneHostCheck PRODUCT_NAME "MegaTuneHostCheck")
target_sources(MegaTuneHostCheck PRIVATE HostCheck.cpp)
target_link_libraries(MegaTuneHostCheck PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra juce::juce_recommended_config_flags)
target_compile_definitions(MegaTuneHostCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_AU=0 JUCE_MODAL_LOOPS_PERMITTED=1)

juce_add_console_app(MegaTuneRender PRODUCT_NAME "MegaTuneRender")
target_sources(MegaTuneRender PRIVATE RenderCheck.cpp)
target_link_libraries(MegaTuneRender PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics juce::juce_gui_extra juce::juce_recommended_config_flags)
target_compile_definitions(MegaTuneRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_AU=0 JUCE_MODAL_LOOPS_PERMITTED=1)
