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
project(GILLRESTORATION VERSION 0.6.0 LANGUAGES C CXX)
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
if(GILL_JUCE_EXPORT)
    include("${GILL_JUCE_EXPORT}")
else()
    add_subdirectory("${GILL_JUCE_SOURCE_DIR}" JUCE)
endif()

# A neutral runtime is shared by the processor, editor and native test hosts.
set(runtime_defs
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WIN_WEBVIEW2=0
    JUCE_REPORT_APP_USAGE=0 JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
if(GILL_PREBUILT_RUNTIME)
    add_library(GillRestorationRuntime STATIC IMPORTED)
    foreach(m audio_basics audio_devices audio_formats audio_processors audio_processors_headless audio_utils core data_structures events graphics gui_basics gui_extra)
        list(APPEND runtime_defs JUCE_MODULE_AVAILABLE_juce_${m}=1)
        if(TARGET juce_${m})
            set_property(TARGET juce_${m} PROPERTY INTERFACE_SOURCES "")
        endif()
    endforeach()
    set_target_properties(GillRestorationRuntime PROPERTIES IMPORTED_LOCATION "${GILL_PREBUILT_RUNTIME}"
        INCLUDE_DIRECTORIES "${JUCE_MODULES_DIR}" COMPILE_DEFINITIONS "${runtime_defs}")
    target_include_directories(GillRestorationRuntime INTERFACE "${JUCE_MODULES_DIR}")
    target_compile_definitions(GillRestorationRuntime INTERFACE ${runtime_defs})
    target_link_libraries(GillRestorationRuntime INTERFACE juce::juce_recommended_config_flags winmm version shlwapi Imm32 comdlg32 ole32 oleaut32 uuid wininet ws2_32)
else()
    add_library(GillRestorationRuntime STATIC Source/JuceRuntime.cpp)
    target_compile_definitions(GillRestorationRuntime PUBLIC ${runtime_defs})
    target_link_libraries(GillRestorationRuntime PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
endif()
juce_add_binary_data(GillRestorationAssets SOURCES Assets/core_reference.png)
add_library(GillRestorationShared STATIC Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_link_libraries(GillRestorationShared PRIVATE GillRestorationRuntime GillRestorationAssets)
target_include_directories(GillRestorationShared PUBLIC Source "$<TARGET_PROPERTY:GillRestorationRuntime,INCLUDE_DIRECTORIES>")
target_compile_definitions(GillRestorationShared PUBLIC "$<TARGET_PROPERTY:GillRestorationRuntime,COMPILE_DEFINITIONS>")

function(add_gill_restoration product code kind)
    string(TOLOWER "${product}" product_lower)
    juce_add_plugin(${product}
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "GILLPRODUCTION"
        BUNDLE_ID "com.gillproduction.${product_lower}"
        PLUGIN_MANUFACTURER_CODE Gill PLUGIN_CODE ${code}
        PRODUCT_NAME "${product}" DESCRIPTION "${product} Oak Sage Audio Restoration"
        FORMATS VST3 VST3_CATEGORIES Fx Restoration VST3_AUTO_MANIFEST FALSE
        IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE COPY_PLUGIN_AFTER_BUILD FALSE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        get_target_property(generated ${product} JUCE_GENERATED_SOURCES_DIRECTORY)
        get_target_property(info ${product} JUCE_INFO_FILE)
        add_custom_command(OUTPUT "${generated}/${product}_resources.rc" APPEND DEPENDS "${info}")
    endif()
    target_sources(${product} PRIVATE Source/Factory.cpp)
    target_include_directories(${product} PRIVATE Source "$<TARGET_PROPERTY:GillRestorationRuntime,INCLUDE_DIRECTORIES>")
    target_compile_definitions(${product} PUBLIC GILL_RESTORATION_KIND=${kind} JUCE_VST3_CAN_REPLACE_VST2=0
        "$<TARGET_PROPERTY:GillRestorationRuntime,COMPILE_DEFINITIONS>")
    target_link_libraries(${product} PRIVATE GillRestorationShared GillRestorationAssets PUBLIC juce::juce_recommended_config_flags)
endfunction()
add_gill_restoration(GILLDECLICK Gdc1 0)
add_gill_restoration(GILLDECRACKLE Gcr1 1)

enable_testing()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/RestorationTests.cpp")
    add_executable(GillRestorationDspTests Tests/RestorationTests.cpp)
    target_include_directories(GillRestorationDspTests PRIVATE Source)
    add_test(NAME RESTORATION_DSP COMMAND GillRestorationDspTests)
    set_tests_properties(RESTORATION_DSP PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
endif()

add_executable(GillRestorationIntegrationTests Tests/PluginTests.cpp)
target_link_libraries(GillRestorationIntegrationTests PRIVATE GillRestorationShared GillRestorationAssets)
add_test(NAME RESTORATION_INTEGRATION COMMAND GillRestorationIntegrationTests)
set_tests_properties(RESTORATION_INTEGRATION PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Tests")

# This legacy binary host harness embeds Windows JUCE host objects.
if(WIN32)
add_executable(GillSuiteVst3HostTests Tests/Vst3HostTests.cpp)
target_link_libraries(GillSuiteVst3HostTests PRIVATE GillRestorationRuntime)
target_include_directories(GillSuiteVst3HostTests PRIVATE "$<TARGET_PROPERTY:GillRestorationRuntime,INCLUDE_DIRECTORIES>"
    "${JUCE_MODULES_DIR}/juce_audio_processors_headless/format_types/VST3_SDK")
target_compile_definitions(GillSuiteVst3HostTests PRIVATE JUCE_PLUGINHOST_VST3=1 "$<TARGET_PROPERTY:GillRestorationRuntime,COMPILE_DEFINITIONS>")
target_sources(GillSuiteVst3HostTests PRIVATE
    "${JUCE_MODULES_DIR}/juce_audio_processors/juce_audio_processors.cpp"
    "${JUCE_MODULES_DIR}/juce_audio_processors_headless/juce_audio_processors_headless.cpp")

endif()
