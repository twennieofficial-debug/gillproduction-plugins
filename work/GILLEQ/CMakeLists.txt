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
project(GILLEQ VERSION 0.4.0 LANGUAGES C CXX)
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
# Optional local runtime reuse; the ordinary source build is unchanged.
# juce_dsp remains compiled for this product.
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
juce_add_binary_data(GilleqAssets SOURCES Assets/oak_sage.png)
juce_add_plugin(GILLEQ
    COMPANY_NAME "GILLPRODUCTION"
    BUNDLE_ID "com.gillproduction.gilleq"
    PLUGIN_MANUFACTURER_CODE Gill
    PLUGIN_CODE Geq1
    PRODUCT_NAME "GILLEQ"
    DESCRIPTION "GILLEQ Oak Sage Parametric Equalizer"
    FORMATS VST3 Standalone
    VST3_CATEGORIES Fx EQ
    VST3_AUTO_MANIFEST FALSE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    get_target_property(generated GILLEQ JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(info GILLEQ JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${generated}/GILLEQ_resources.rc" APPEND DEPENDS "${info}")
endif()
juce_generate_juce_header(GILLEQ)
target_sources(GILLEQ PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(GILLEQ PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_WIN_WEBVIEW2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(GILLEQ PRIVATE GilleqAssets juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
if(GILL_PREBUILT_RUNTIME)
    target_link_libraries(GILLEQ PRIVATE GillPrebuiltRuntime)
endif()
if(MINGW)
    target_link_options(GILLEQ_VST3 PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(GILLEQ_Standalone PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_executable(GilleqDspTests Tests/DspTests.cpp)
target_include_directories(GilleqDspTests PRIVATE Source)
if(MINGW)
    target_link_options(GilleqDspTests PRIVATE -static)
endif()
enable_testing()
add_test(NAME DSP COMMAND GilleqDspTests)
add_executable(GilleqIntegrationTests)
target_sources(GilleqIntegrationTests PRIVATE Tests/PluginTests.cpp)
if(APPLE)
    enable_language(OBJCXX)
    # Console tests bypass JUCEApplicationBase::main(). Create NSApplication
    # before ScopedJuceInitialiser_GUI so the real AppKit loop delivers the
    # existing asynchronous automation and live editor timer assertions.
    target_sources(GilleqIntegrationTests PRIVATE Tests/MacTestApplication.mm)
    target_link_libraries(GilleqIntegrationTests PRIVATE "-framework AppKit")
endif()
target_include_directories(GilleqIntegrationTests PRIVATE Source "$<TARGET_PROPERTY:GILLEQ,INCLUDE_DIRECTORIES>")
target_link_libraries(GilleqIntegrationTests PRIVATE GILLEQ GilleqAssets)
target_compile_definitions(GilleqIntegrationTests PRIVATE "$<TARGET_PROPERTY:GILLEQ,COMPILE_DEFINITIONS>")
add_test(NAME PLUGIN_INTEGRATION COMMAND GilleqIntegrationTests)
set_tests_properties(DSP PLUGIN_INTEGRATION PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
add_executable(GilleqDynamicTests Tests/DynamicTests.cpp)
target_include_directories(GilleqDynamicTests PRIVATE Source)
add_test(NAME DYNAMIC_DSP COMMAND GilleqDynamicTests)
set_tests_properties(DYNAMIC_DSP PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
