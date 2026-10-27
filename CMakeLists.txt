cmake_minimum_required(VERSION 3.16)
project(PocketEuclidean VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(POCKET_BUILD_APP "Build the desktop app shell target" ON)
option(PORTMASTER_BUILD "Build the handheld PortMaster binary (Release, no tests, SDL2 required)" OFF)

if(PORTMASTER_BUILD)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
    set(BUILD_TESTING OFF CACHE BOOL "Tests are not built for PortMaster" FORCE)
    set(POCKET_BUILD_APP ON CACHE BOOL "" FORCE)
endif()

add_library(pocket_euclidean
    src/audio/AdEnvelope.cpp
    src/audio/AudioCallbackProcessor.cpp
    src/audio/AudioParameterQueue.cpp
    src/audio/BasicVaVoice.cpp
    src/audio/PlaitsVoice.cpp
    src/audio/Mixer.cpp
    src/audio/OnePoleLowPass.cpp
    src/audio/ReverbEffect.cpp
    src/audio/SampleCache.cpp
    src/audio/SamplerMixerRenderer.cpp
    src/audio/SamplerVoicePool.cpp
    src/audio/SynthVoice.cpp
    src/audio/WavDecoder.cpp
    src/sequencer/EuclideanGenerator.cpp
    src/sequencer/Jitter.cpp
    src/sequencer/MidiClock.cpp
    src/sequencer/Modulation.cpp
    src/sequencer/Sequencer.cpp
    src/platform/SystemSettings.cpp
    src/platform/SettingsStore.cpp
    src/project/ProjectStore.cpp
    src/ui/InputMapping.cpp
    src/ui/MainScreenDrawList.cpp
    src/ui/MainScreenLayout.cpp
    src/ui/MainScreenModel.cpp
    src/ui/StepLightBank.cpp
    src/ui/StepLightEnvelope.cpp
    src/ui/ThemePalette.cpp
    src/ui/UiController.cpp
)
target_include_directories(pocket_euclidean PUBLIC src)
set_target_properties(pocket_euclidean PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(POCKET_BUILD_APP)
    add_executable(pocket_euclidean_app
        src/app/main.cpp
        src/app/SystemMidiBackend.cpp
    )
    target_link_libraries(pocket_euclidean_app PRIVATE pocket_euclidean)
    if(WIN32)
        target_link_libraries(pocket_euclidean_app PRIVATE winmm)
    endif()

    find_package(SDL2 QUIET CONFIG)
    if(NOT SDL2_FOUND)
        find_package(SDL2 QUIET)
    endif()
    if(PORTMASTER_BUILD AND NOT SDL2_FOUND)
        message(FATAL_ERROR
            "PORTMASTER_BUILD needs SDL2 development files for the target "
            "(set CMAKE_PREFIX_PATH / POCKET_SYSROOT).")
    endif()

    if(PORTMASTER_BUILD)
        # PortMaster images differ in libstdc++ version; link it statically.
        target_link_options(pocket_euclidean_app PRIVATE
            -static-libstdc++ -static-libgcc -s)
        set_target_properties(pocket_euclidean_app PROPERTIES
            OUTPUT_NAME "pocketeuclidean.${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(SDL2_FOUND)
        target_compile_definitions(pocket_euclidean_app PRIVATE POCKET_HAVE_SDL2=1)
        if(TARGET SDL2::SDL2)
            target_link_libraries(pocket_euclidean_app PRIVATE SDL2::SDL2)
        endif()
        if(TARGET SDL2::SDL2main)
            target_link_libraries(pocket_euclidean_app PRIVATE SDL2::SDL2main)
        endif()
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(pocket_euclidean_tests
        tests/test_sequencer.cpp
    )
    target_link_libraries(pocket_euclidean_tests PRIVATE pocket_euclidean)
    add_test(NAME pocket_euclidean_tests COMMAND pocket_euclidean_tests)

    add_executable(pocket_euclidean_ui_tests
        tests/test_ui.cpp
    )
    target_link_libraries(pocket_euclidean_ui_tests PRIVATE pocket_euclidean)
    add_test(NAME pocket_euclidean_ui_tests COMMAND pocket_euclidean_ui_tests)
endif()
