cmake_minimum_required(VERSION 3.25)
project(RawSwingHost VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT DEFINED JUCE_SOURCE_DIR)
    message(FATAL_ERROR "Set JUCE_SOURCE_DIR to the isolated patched JUCE checkout")
endif()
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
add_subdirectory("${JUCE_SOURCE_DIR}" juce EXCLUDE_FROM_ALL)
juce_add_console_app(actual_vst3_host PRODUCT_NAME "Raw Swing Host")
target_sources(actual_vst3_host PRIVATE actual_vst3_host.cpp)
set_target_properties(actual_vst3_host PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(actual_vst3_host PRIVATE
    juce::juce_audio_formats juce::juce_audio_processors_headless
    juce::juce_cryptography juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(actual_vst3_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
