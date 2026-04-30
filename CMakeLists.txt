cmake_minimum_required(VERSION 3.19)

project(MyApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MYAPP_ENABLE_BRIDGE_SCaffold "Enable bridge scaffolding classes" ON)
option(MYAPP_ENABLE_VST2_HOST "Enable legacy VST2 host support (requires VST2 SDK)" ON)
set(MYAPP_VST2_SDK_PATH "E:/Maqam Classification/vstsdk3610_11_06_2018_build_37/VST_SDK/VST2_SDK"
    CACHE PATH "Path to VST2 SDK root or directly to a folder containing aeffect.h/aeffectx.h")

# ============================================================================
# JUCE Framework Setup (FetchContent)
# ============================================================================
string(MAKE_C_IDENTIFIER "${CMAKE_GENERATOR}" FETCHCONTENT_GENERATOR_ID)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps_${FETCHCONTENT_GENERATOR_ID}")

include(FetchContent)

# Use FetchContent to automatically download JUCE
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
)

# Use FetchContent to automatically download Catch2 for testing
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(JUCE Catch2)

# Set up optional legacy VST2 SDK after JUCE CMake functions are available.
if(MYAPP_ENABLE_VST2_HOST)
    set(_myapp_vst2_juce_root "")

    # Layout A: path already points to SDK root with pluginterfaces/vst2.x/*.h
    if(EXISTS "${MYAPP_VST2_SDK_PATH}/pluginterfaces/vst2.x/aeffect.h"
       AND EXISTS "${MYAPP_VST2_SDK_PATH}/pluginterfaces/vst2.x/aeffectx.h")
        set(_myapp_vst2_juce_root "${MYAPP_VST2_SDK_PATH}")
    endif()

    # Layout B: path points directly to folder containing aeffect.h/aeffectx.h
    if(_myapp_vst2_juce_root STREQUAL ""
       AND EXISTS "${MYAPP_VST2_SDK_PATH}/aeffect.h"
       AND EXISTS "${MYAPP_VST2_SDK_PATH}/aeffectx.h")
        set(_myapp_vst2_compat_root "${CMAKE_BINARY_DIR}/vst2_sdk_compat")
        file(MAKE_DIRECTORY "${_myapp_vst2_compat_root}/pluginterfaces/vst2.x")
        file(COPY "${MYAPP_VST2_SDK_PATH}/"
             DESTINATION "${_myapp_vst2_compat_root}/pluginterfaces/vst2.x")
        set(_myapp_vst2_juce_root "${_myapp_vst2_compat_root}")
    endif()

    if(NOT _myapp_vst2_juce_root STREQUAL "")
        juce_set_vst2_sdk_path("${_myapp_vst2_juce_root}")
        set(MYAPP_PLUGINHOST_VST 1)
    else()
        message(WARNING "MYAPP_ENABLE_VST2_HOST=ON but JUCE-required VST2 headers are missing. Expected either: (1) ${MYAPP_VST2_SDK_PATH}/pluginterfaces/vst2.x/{aeffect.h,aeffectx.h} or (2) ${MYAPP_VST2_SDK_PATH}/{aeffect.h,aeffectx.h}. Building with VST3-only host.")
        set(MYAPP_PLUGINHOST_VST 0)
    endif()
else()
    set(MYAPP_PLUGINHOST_VST 0)
endif()

# ============================================================================
# Main App Target
# ============================================================================
juce_add_gui_app(MyApp
    PRODUCT_NAME "MyApp"
    COMPANY_NAME "Maqam"
    NEEDS_CURL FALSE
    NEEDS_CURL_STATIC FALSE
)

juce_generate_juce_header(MyApp)

# Add user source files using target_sources() for better JUCE compatibility
target_sources(MyApp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/MainComponent.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/MainComponent_Core.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/MainComponent_UI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/MainComponent_PluginLoading.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/MainComponent_Bridge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/MainComponent_Audio.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/MainComponent_Scanning.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/MainComponent_Events.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/MainComponent_Keyboard.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/host/PluginManager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/host/PluginManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/host/BridgeManager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/host/BridgeManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/host/UIController.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/host/UIController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/IPCManager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/IPCManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/PluginBridgeMaster.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/PluginBridgeMaster.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/PluginBridgeWorker.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/PluginBridgeWorker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/PluginSubWindowContainer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/PluginSubWindowContainer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/CustomLookAndFeel.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/CustomLookAndFeel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/music/OrientalScaleManager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/music/OrientalScaleManager.cpp"
)

# Dedicated bridge worker executable (can be built as Win32 in a separate build directory)
juce_add_gui_app(MyAppBridgeWorker
    PRODUCT_NAME "MyAppBridgeWorker"
    COMPANY_NAME "Maqam"
    NEEDS_CURL FALSE
    NEEDS_CURL_STATIC FALSE
)

juce_generate_juce_header(MyAppBridgeWorker)

target_sources(MyAppBridgeWorker PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/BridgeWorkerMain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/IPCManager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/IPCManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/PluginBridgeWorker.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge/PluginBridgeWorker.cpp"
)

target_include_directories(MyAppBridgeWorker PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge"
)

# Add include directories
target_include_directories(MyApp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/bridge"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/music"
)

# ============================================================================
# JUCE Modules (VST Plugin Hosting)
# ============================================================================
target_link_libraries(MyApp PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors      # Required for plugin hosting
    juce::juce_audio_processors_headless  # Required for VST3 support
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

target_link_libraries(MyAppBridgeWorker PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_processors_headless
    juce::juce_dsp
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

# ============================================================================
# Plugin Format Support
# ============================================================================
# Enable VST3 plugin hosting, optionally VST2 hosting when SDK is configured.
target_compile_definitions(MyApp PRIVATE
    JUCE_PLUGINHOST_VST=${MYAPP_PLUGINHOST_VST}
    JUCE_PLUGINHOST_VST3=1
)

target_compile_definitions(MyAppBridgeWorker PRIVATE
    JUCE_PLUGINHOST_VST=${MYAPP_PLUGINHOST_VST}
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

# VST3 SDK headers (bundled inside juce_audio_processors_headless)
target_include_directories(MyApp PRIVATE
    "${JUCE_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
)

target_include_directories(MyAppBridgeWorker PRIVATE
    "${JUCE_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
)

# ============================================================================
# Compiler-Specific Settings
# ============================================================================
if(MSVC)
    # Visual Studio
    target_compile_options(MyApp PRIVATE /W4 /fp:fast)
    # Enable multi-processor compilation
    target_compile_options(MyApp PRIVATE /MP)
    target_compile_options(MyAppBridgeWorker PRIVATE /W4 /fp:fast /MP)
elseif(APPLE)
    # macOS Clang
    target_compile_options(MyApp PRIVATE -Wall -Wextra -Wno-unknown-pragmas)
    set_target_properties(MyApp PROPERTIES
        XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL "3"
        MACOSX_DEPLOYMENT_TARGET "10.11"
    )
elseif(UNIX)
    # Linux Clang/GCC
    target_compile_options(MyApp PRIVATE -Wall -Wextra -Wno-unknown-pragmas)
endif()

# ============================================================================
# Unit Tests with Catch2
# ============================================================================
enable_testing()

add_executable(PluginHostingTests
    tests/PluginHostingTests.cpp
)

target_link_libraries(PluginHostingTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    Catch2::Catch2WithMain
)

target_compile_definitions(PluginHostingTests PRIVATE
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
)

add_test(NAME PluginHostingTests COMMAND PluginHostingTests)
