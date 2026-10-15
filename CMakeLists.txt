cmake_minimum_required(VERSION 3.22)
project(UnofficialRipples2Plugin VERSION 0.1.0)   # raise this with every build that changes the plug-in's inputs/outputs, so Logic re-reads it

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  # The GitHub workflow passes -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal build.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(UnofficialRipples2
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE URv2
  FORMATS ${FORMATS}
  PRODUCT_NAME "Unofficial Ripples V2"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(UnofficialRipples2 PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(UnofficialRipples2 PRIVATE src)
target_compile_definitions(UnofficialRipples2 PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(UnofficialRipples2 PRIVATE
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# --- Test of the filter model (no JUCE):  cmake --build build --target ripples2_test && build/ripples2_test ---
add_executable(ripples2_test EXCLUDE_FROM_ALL tests/ripples2_test.cpp)
target_include_directories(ripples2_test PRIVATE src)

# --- Speed check (no JUCE) ---
add_executable(ripples2_bench EXCLUDE_FROM_ALL tools/bench.cpp)
target_include_directories(ripples2_bench PRIVATE src)

# --- Picture of the panel (used by the build) ---
juce_add_console_app(ripples2_snapshot PRODUCT_NAME "ripples2_snapshot")
set_target_properties(ripples2_snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(ripples2_snapshot PRIVATE tools/snapshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(ripples2_snapshot PRIVATE src)
target_compile_definitions(ripples2_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(ripples2_snapshot PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- Whole-plug-in check (sidechain, FM sources, ping, outputs) ---
juce_add_console_app(ripples2_proc_test PRODUCT_NAME "ripples2_proc_test")
set_target_properties(ripples2_proc_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(ripples2_proc_test PRIVATE tools/proc_test.cpp src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(ripples2_proc_test PRIVATE src)
target_compile_definitions(ripples2_proc_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(ripples2_proc_test PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
