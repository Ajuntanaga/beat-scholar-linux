cmake_minimum_required(VERSION 3.25)

project(BeatScholarLinux VERSION 0.1.0 LANGUAGES C CXX)

option(BSL_BUILD_TESTS "Build Beat Scholar Linux tests" ON)
option(BSL_BUILD_PLUGIN "Build the JUCE VST3 and standalone application" OFF)
option(BSL_ENABLE_TSAN "Build the scheduler stress test with ThreadSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(beat_scholar_sanitizers INTERFACE)
if(BSL_ENABLE_TSAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(beat_scholar_sanitizers INTERFACE
        -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(beat_scholar_sanitizers INTERFACE -fsanitize=thread)
endif()

add_library(beat_scholar_core
    src/content/FactoryResourceScanner.cpp
    src/core/Model.cpp
    src/core/PatternScheduler.cpp
    src/core/ProjectEditor.cpp
    src/core/Randomizer.cpp
    src/core/Sequencer.cpp
    src/core/SequencerPlayback.cpp
    src/audio/AudioEngine.cpp
    src/audio/AuxDistortion.cpp
)

# The core library is linked into the VST3 shared object. Make the requirement
# explicit so clean builds do not depend on toolchain-specific PIC defaults.
set_target_properties(beat_scholar_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(beat_scholar_core PUBLIC src)
target_compile_features(beat_scholar_core PUBLIC cxx_std_20)
target_link_libraries(beat_scholar_core PUBLIC beat_scholar_sanitizers)
target_compile_options(beat_scholar_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

if(BSL_BUILD_TESTS)
    enable_testing()
    add_executable(beat_scholar_core_tests tests/core_tests.cpp)
    target_link_libraries(beat_scholar_core_tests PRIVATE beat_scholar_core)
    add_test(NAME beat_scholar_core_tests COMMAND beat_scholar_core_tests)

    add_executable(beat_scholar_audio_tests tests/audio_tests.cpp)
    target_link_libraries(beat_scholar_audio_tests PRIVATE beat_scholar_core)
    add_test(NAME beat_scholar_audio_tests COMMAND beat_scholar_audio_tests)

    add_executable(beat_scholar_scheduler_tests tests/scheduler_tests.cpp)
    target_link_libraries(beat_scholar_scheduler_tests PRIVATE beat_scholar_core)
    add_test(NAME beat_scholar_scheduler_tests COMMAND beat_scholar_scheduler_tests)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME beat_scholar_factory_migration_tests
             COMMAND ${Python3_EXECUTABLE} -m unittest tests/test_migrate_factory_content.py)
    add_test(NAME beat_scholar_effect_parity_unit_tests
             COMMAND ${Python3_EXECUTABLE} -m unittest tests/test_measure_effect_parity.py)
    set_tests_properties(beat_scholar_factory_migration_tests
                         PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(beat_scholar_effect_parity_unit_tests
                         PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(BSL_BUILD_PLUGIN)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
        message(FATAL_ERROR "JUCE 8.0.16-security is required at third_party/JUCE")
    endif()

    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)

    juce_add_plugin(BeatScholarLinux
        COMPANY_NAME "Ajuntanaga"
        BUNDLE_ID "com.ajuntanaga.beatscholarlinux"
        PLUGIN_MANUFACTURER_CODE Ajun
        PLUGIN_CODE Bslx
        FORMATS VST3 Standalone
        PRODUCT_NAME "Beat Scholar Linux"
        DESCRIPTION "Native Linux beat slicer and drum sampler"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Drum
    )

    target_sources(BeatScholarLinux PRIVATE
        src/plugin/AmbienceConvolution.cpp
        src/plugin/AmbienceConvolution.h
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginProcessor.h
        src/plugin/BeatScholarLookAndFeel.cpp
        src/plugin/BeatScholarLookAndFeel.h
        src/plugin/PluginEditor.cpp
        src/plugin/PluginEditor.h
        src/plugin/StateCodec.cpp
        src/plugin/StateCodec.h
    )
    target_link_libraries(BeatScholarLinux PRIVATE
        beat_scholar_core
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    target_compile_definitions(BeatScholarLinux PUBLIC
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=1
    )

    if(BSL_ENABLE_TSAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_executable(beat_scholar_processor_tsan_tests tests/scheduler_stress_tests.cpp)
        target_link_libraries(beat_scholar_processor_tsan_tests PRIVATE
            BeatScholarLinux
            juce::juce_audio_utils
            juce::juce_cryptography
            juce::juce_dsp
        )
        target_include_directories(beat_scholar_processor_tsan_tests PRIVATE src)
        target_compile_definitions(beat_scholar_processor_tsan_tests PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=1
        )
        add_test(NAME beat_scholar_processor_tsan_tests COMMAND beat_scholar_processor_tsan_tests)
        set_tests_properties(beat_scholar_processor_tsan_tests PROPERTIES
            ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/tsan.supp")
    endif()

    if(BSL_BUILD_TESTS)
        add_executable(beat_scholar_ambience_tests tests/ambience_tests.cpp)
        target_link_libraries(beat_scholar_ambience_tests PRIVATE BeatScholarLinux)
        target_include_directories(beat_scholar_ambience_tests PRIVATE src third_party/JUCE/modules)
        target_compile_definitions(beat_scholar_ambience_tests PRIVATE
            JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        add_test(NAME beat_scholar_ambience_tests COMMAND beat_scholar_ambience_tests)
        add_executable(beat_scholar_factory_playback_tests tests/factory_playback_tests.cpp)
        target_link_libraries(beat_scholar_factory_playback_tests PRIVATE BeatScholarLinux)
        target_include_directories(beat_scholar_factory_playback_tests PRIVATE src third_party/JUCE/modules)
        target_compile_definitions(beat_scholar_factory_playback_tests PRIVATE
            JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        add_test(NAME beat_scholar_factory_playback_tests COMMAND beat_scholar_factory_playback_tests
            ${CMAKE_BINARY_DIR}/factory-playback)
        set_tests_properties(beat_scholar_factory_playback_tests PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(beat_scholar_editor_tests tests/editor_tests.cpp)
        target_link_libraries(beat_scholar_editor_tests PRIVATE BeatScholarLinux)
        target_include_directories(beat_scholar_editor_tests PRIVATE src third_party/JUCE/modules)
        target_compile_definitions(beat_scholar_editor_tests PRIVATE
            JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        find_program(BSL_XVFB_RUN xvfb-run)
        if(BSL_XVFB_RUN)
            add_test(NAME beat_scholar_editor_tests
                COMMAND ${BSL_XVFB_RUN} -a --server-args=-noreset ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_editor_tests.py
                    $<TARGET_FILE:beat_scholar_editor_tests>
                    ${CMAKE_BINARY_DIR}/editor-regression)
        else()
            add_test(NAME beat_scholar_editor_tests COMMAND beat_scholar_editor_tests
                ${CMAKE_BINARY_DIR}/editor-regression)
        endif()
        add_executable(beat_scholar_state_tests
            tests/state_tests.cpp
            src/plugin/StateCodec.cpp
            src/plugin/StateCodec.h
        )
        target_link_libraries(beat_scholar_state_tests PRIVATE
            beat_scholar_core
            juce::juce_core
            juce::juce_data_structures
            juce::juce_cryptography
        )
        target_compile_definitions(beat_scholar_state_tests PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
        )
        add_test(NAME beat_scholar_state_tests COMMAND beat_scholar_state_tests)
    endif()

    add_executable(beat_scholar_extract_factory_samples
        tools/extract_factory_samples.cpp
    )
    target_link_libraries(beat_scholar_extract_factory_samples PRIVATE
        beat_scholar_core
        juce::juce_audio_formats
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    target_compile_definitions(beat_scholar_extract_factory_samples PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    # This executable deliberately does not link BeatScholarLinux or any of its
    # processor sources. It scans and instantiates the built bundle through the
    # same VST3 ABI boundary used by an external host.
    juce_add_console_app(actual_vst3_host
        PRODUCT_NAME "Beat Scholar Linux Actual VST3 Host"
    )
    set_target_properties(actual_vst3_host PROPERTIES
        OUTPUT_NAME actual_vst3_host
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    )
    target_sources(actual_vst3_host PRIVATE tools/actual_vst3_host.cpp)
    target_link_libraries(actual_vst3_host PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_processors_headless
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    target_compile_definitions(actual_vst3_host PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    if(BSL_BUILD_TESTS)
        add_test(NAME beat_scholar_actual_vst3_smoke
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/measure_effect_parity.py
                --mode smoke
                --plugin "${CMAKE_BINARY_DIR}/BeatScholarLinux_artefacts/$<CONFIG>/VST3/Beat Scholar Linux.vst3"
                --host $<TARGET_FILE:actual_vst3_host>
                --artifacts-dir "${CMAKE_BINARY_DIR}/actual-vst3-smoke"
                --output "${CMAKE_BINARY_DIR}/actual-vst3-smoke/report.json"
        )
        add_test(NAME beat_scholar_actual_vst3_midi
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_vst3_midi_output.py
                --plugin "${CMAKE_BINARY_DIR}/BeatScholarLinux_artefacts/$<CONFIG>/VST3/Beat Scholar Linux.vst3"
                --host $<TARGET_FILE:actual_vst3_host>
                --artifacts-dir "${CMAKE_BINARY_DIR}/actual-vst3-midi"
        )
        add_test(NAME beat_scholar_actual_vst3_panic
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_vst3_panic.py
                --plugin "${CMAKE_BINARY_DIR}/BeatScholarLinux_artefacts/$<CONFIG>/VST3/Beat Scholar Linux.vst3"
                --host $<TARGET_FILE:actual_vst3_host>
                --artifacts-dir "${CMAKE_BINARY_DIR}/actual-vst3-panic"
        )
        set_tests_properties(beat_scholar_actual_vst3_smoke beat_scholar_actual_vst3_midi beat_scholar_actual_vst3_panic PROPERTIES
            ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=")
    endif()
endif()
