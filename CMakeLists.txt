cmake_minimum_required(VERSION 3.16)
project(PocketEuclidean VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pocket_euclidean
    src/audio/AdEnvelope.cpp
    src/audio/AudioCallbackProcessor.cpp
    src/audio/AudioParameterQueue.cpp
    src/audio/BasicVaVoice.cpp
    src/audio/Mixer.cpp
    src/audio/OnePoleLowPass.cpp
    src/audio/SampleCache.cpp
    src/audio/SamplerMixerRenderer.cpp
    src/audio/SamplerVoicePool.cpp
    src/audio/SynthVoice.cpp
    src/audio/WavDecoder.cpp
    src/sequencer/EuclideanGenerator.cpp
    src/sequencer/Jitter.cpp
    src/sequencer/Modulation.cpp
    src/sequencer/Sequencer.cpp
    src/platform/SystemSettings.cpp
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
