cmake_minimum_required(VERSION 3.22)
project(Yoink VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# En macOS: binario universal (Apple Silicon + Intel)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# JUCE: carpeta ./JUCE (git clone --branch 8.0.4 https://github.com/juce-framework/JUCE.git)
add_subdirectory(JUCE)

option(YOINK_COPY_AFTER_BUILD "Copiar el plugin a la carpeta de plugins del sistema" ON)

set(YOINK_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND YOINK_FORMATS AU)
endif()

juce_add_plugin(Yoink
    COMPANY_NAME            "0600musica"
    BUNDLE_ID               "com.0600musica.yoink"
    PLUGIN_MANUFACTURER_CODE O6mu
    PLUGIN_CODE             Yonk
    PRODUCT_NAME            "Yoink"
    FORMATS                 ${YOINK_FORMATS}

    # Instrumento: toca el clip recortado por MIDI
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    AU_MAIN_TYPE            kAudioUnitType_MusicDevice
    VST3_CATEGORIES         Instrument Sampler

    COPY_PLUGIN_AFTER_BUILD ${YOINK_COPY_AFTER_BUILD}
)

target_sources(Yoink PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(Yoink PRIVATE Source)

target_compile_definitions(Yoink PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Yoink
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Tests del motor (búsqueda, parseo, nombres): sin red
juce_add_console_app(YoinkTests PRODUCT_NAME "YoinkTests")
target_sources(YoinkTests PRIVATE Tests/EngineTest.cpp)
target_include_directories(YoinkTests PRIVATE Source)
target_compile_definitions(YoinkTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(YoinkTests PRIVATE juce::juce_core juce::juce_events
                      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
