cmake_minimum_required(VERSION 3.22)
# Universal macOS defaults are set before compiler detection; CI may request
# one architecture explicitly and combine independently tested slices later.
if(CMAKE_HOST_APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "")
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS")
    endif()
endif()
project(GILLDEREVERB VERSION 0.10.0 LANGUAGES C CXX)
# Keep each Mach-O image's JUCE implementation private, including custom
# static runtimes and Objective-C++ units. VST3 entry points stay explicit exports.
if(APPLE)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_OBJC_VISIBILITY_PRESET hidden)
    set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Apple Clang contracts float multiply/add by default. JUCE's decimal
# parameter-grid snapping must retain the same rounding steps as Windows:
# e.g. -12 + 0.01f * 1200 must be exactly 0 dB, not an FMA residual.
# Keep IEEE rounding boundaries for control/state and DSP code consistently.
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-ffp-contract=off)
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
# These two optional shortcuts reference the local Windows JUCE tools/runtime.
# macOS always compiles the pinned JUCE sources for the requested architecture.
if(NOT WIN32 AND (GILL_JUCE_EXPORT OR GILL_PREBUILT_RUNTIME))
    message(FATAL_ERROR "GILL_JUCE_EXPORT and GILL_PREBUILT_RUNTIME are Windows-only. Use a clean macOS build directory without these options.")
endif()
set(GILL_JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/JUCE" CACHE PATH "Pinned JUCE source directory")

set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Optional reuse of JUCE's officially exported host tool configuration.
# The normal source distribution builds JUCE directly from ../dependencies/JUCE.
if(GILL_JUCE_EXPORT)
    include("${GILL_JUCE_EXPORT}")
else()
    add_subdirectory("${GILL_JUCE_SOURCE_DIR}" JUCE)
endif()
juce_add_binary_data(GillDereverbAssets SOURCES Assets/oak_sage.png Assets/core_reference.png)
# Optional local runtime reuse; ordinary source builds remain unchanged.
if(GILL_PREBUILT_RUNTIME)
    add_library(GillPrebuiltRuntime STATIC IMPORTED)
    set_target_properties(GillPrebuiltRuntime PROPERTIES IMPORTED_LOCATION "${GILL_PREBUILT_RUNTIME}")
    target_include_directories(GillPrebuiltRuntime INTERFACE "${JUCE_MODULES_DIR}")
    target_compile_definitions(GillPrebuiltRuntime INTERFACE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(GillPrebuiltRuntime INTERFACE winmm version shlwapi Imm32 comdlg32 ole32 oleaut32 uuid wininet ws2_32)
    foreach(m audio_basics audio_devices audio_formats audio_processors audio_processors_headless audio_utils core data_structures events graphics gui_basics gui_extra)
        target_compile_definitions(GillPrebuiltRuntime INTERFACE JUCE_MODULE_AVAILABLE_juce_${m}=1)
        if(TARGET juce_${m})
            set_property(TARGET juce_${m} PROPERTY INTERFACE_SOURCES "")
        endif()
    endforeach()
endif()
juce_add_plugin(GILLDEREVERB
    COMPANY_NAME "GILLPRODUCTION"
    BUNDLE_ID "com.gillproduction.gilldereverb"
    PLUGIN_MANUFACTURER_CODE Gill
    PLUGIN_CODE Gdr1
    PRODUCT_NAME "GILLDEREVERB"
    DESCRIPTION "GILLDEREVERB Oak Sage Voice Dereverberator"
    FORMATS VST3 Standalone
    VST3_CATEGORIES Fx Restoration
    VST3_AUTO_MANIFEST FALSE
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE COPY_PLUGIN_AFTER_BUILD FALSE)
# JUCE 8.0.12's rcfile command reads JUCE_INFO_FILE but does not depend on it.
# Append the missing file dependency so version/metadata changes regenerate
# the resource and relink both Windows formats, including incremental builds.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    get_target_property(gill_generated_sources GILLDEREVERB JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(gill_info_file GILLDEREVERB JUCE_INFO_FILE)
    add_custom_command(
        OUTPUT "${gill_generated_sources}/GILLDEREVERB_resources.rc"
        APPEND
        DEPENDS "${gill_info_file}")
endif()
juce_generate_juce_header(GILLDEREVERB)
target_sources(GILLDEREVERB PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(GILLDEREVERB PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_WIN_WEBVIEW2=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(GILLDEREVERB PRIVATE GillDereverbAssets juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
if(GILL_PREBUILT_RUNTIME)
    target_link_libraries(GILLDEREVERB PRIVATE GillPrebuiltRuntime)
endif()
add_executable(GillDereverbDspTests Tests/DspTests.cpp)
target_include_directories(GillDereverbDspTests PRIVATE Source)
add_executable(GillDereverbAutoQualityTests Tests/AutoQualityTests.cpp)
target_include_directories(GillDereverbAutoQualityTests PRIVATE Source)
add_executable(GillDereverbIntegrationTests Tests/PluginTests.cpp)
# Link the already compiled shared code once; do not compile JUCE a second time.
target_link_libraries(GillDereverbIntegrationTests PRIVATE GILLDEREVERB GillDereverbAssets)
target_include_directories(GillDereverbIntegrationTests PRIVATE Source "$<TARGET_PROPERTY:GILLDEREVERB,INCLUDE_DIRECTORIES>")
target_compile_definitions(GillDereverbIntegrationTests PRIVATE "$<TARGET_PROPERTY:GILLDEREVERB,COMPILE_DEFINITIONS>")
# macOS bundle validation is performed with the native pluginval host.
if(WIN32)
add_executable(GillDereverbVst3HostTests Tests/Vst3HostTests.cpp)
target_link_libraries(GillDereverbVst3HostTests PRIVATE GILLDEREVERB GillDereverbAssets)
target_include_directories(GillDereverbVst3HostTests PRIVATE "$<TARGET_PROPERTY:GILLDEREVERB,INCLUDE_DIRECTORIES>")
target_compile_definitions(GillDereverbVst3HostTests PRIVATE "$<TARGET_PROPERTY:GILLDEREVERB,COMPILE_DEFINITIONS>")
# Only this validation executable needs JUCE's VST3 hosting implementation.
# Its explicit processor objects supersede the corresponding static-library
# members; the shipped plugin is built with hosting disabled.
target_compile_definitions(GillDereverbVst3HostTests PRIVATE JUCE_PLUGINHOST_VST3=1)
target_sources(GillDereverbVst3HostTests PRIVATE
    "${JUCE_MODULES_DIR}/juce_audio_processors/juce_audio_processors.cpp"
    "${JUCE_MODULES_DIR}/juce_audio_processors_headless/juce_audio_processors_headless.cpp")
endif()

enable_testing()
add_test(NAME DSP COMMAND GillDereverbDspTests "${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")
add_test(NAME PLUGIN_INTEGRATION COMMAND GillDereverbIntegrationTests)
add_test(NAME AUTO_QUALITY COMMAND GillDereverbAutoQualityTests "${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")
if(TARGET GillDereverbVst3HostTests)
    add_test(NAME VST3_HOST COMMAND GillDereverbVst3HostTests "$<TARGET_FILE_DIR:GILLDEREVERB_VST3>/../..")
    set_tests_properties(VST3_HOST PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
endif()
set_tests_properties(DSP PLUGIN_INTEGRATION PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
set_tests_properties(AUTO_QUALITY PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
