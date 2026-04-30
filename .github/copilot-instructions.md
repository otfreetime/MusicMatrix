# Copilot Instructions for MyApp

This repository is a JUCE-based VST plugin host built with CMake and a host/bridge process design. The AI should focus on `Source/MainComponent.cpp`, `Source/bridge/*`, and the bridge worker launch logic when changing plugin loading, IPC, or audio routing.

## What matters most
- `MainComponent` is the central app component. It owns:
  - `juce::AudioPluginFormatManager` and `juce::KnownPluginList`
  - async scanning via `scanForPluginsAsync()` and `PluginDirectoryScanner`
  - plugin loading via `loadPluginAsync()`
  - VST3 in-process loading, VST2 via the bridge worker
- `Source/bridge/PluginBridgeMaster.*` launches and communicates with the worker.
- `Source/bridge/PluginBridgeWorker.*` hosts VST2 plugins in a separate process.
- `Source/bridge/IPCManager.*` builds on `juce::InterprocessConnection` and serializes commands as `type|payload`.
- `Source/ui/PluginSubWindowContainer.*` handles embedded plugin editor windows.
- `tests/PluginHostingTests.cpp` models the main test style and expected behavior.

## Build & test workflow
- Create a root build folder and configure with CMake:
  - `cmake -G "Visual Studio 18 2026" -A x64 ..`
  - optionally use Ninja: `cmake -G "Ninja" ..`
- Build the host:
  - `cmake --build . --config Debug`
- Run tests:
  - `ctest --verbose`
- Build the x86 bridge worker using the provided script:
  - `.	oolsuild_worker_x86.ps1 -Config Debug`
- Use these env vars when the worker binary is not in the default CMake layout:
  - `MYAPP_BRIDGE_WORKER_PATH`
  - `MYAPP_BRIDGE_WORKER_X86_PATH`
  - `MYAPP_BRIDGE_WORKER_X64_PATH`

## Important project-specific conventions
- VST2 plugins are expected to be isolated in `MyAppBridgeWorker`. Do not load them in the host process.
- VST3 plugins remain local and use `formatManager.createPluginInstanceAsync()`.
- Plugin cache persistence is stored in a `juce::PropertiesFile` under user app data, not a repo file.
- Failed plugin scan entries are mirrored into the UI as `Failed to load` items via `syncFailedPluginsIntoList()`.
- UI changes from background threads should use `juce::MessageManager::callAsync()`.
- The bridge is started lazily in `initialiseBridge()` so UI creation is not blocked.
- Windows-specific editor crash protection uses `tryCreateEditor_SEH()` and fallback editor UI.

## Key files to inspect for any change
- `CMakeLists.txt` — FetchContent-based JUCE/Catch2 setup, optional VST2 SDK path logic
- `Source/MainComponent.cpp` / `Source/MainComponent.h`
- `Source/bridge/PluginBridgeMaster.*`
- `Source/bridge/PluginBridgeWorker.*`
- `Source/bridge/IPCManager.*`
- `Source/ui/PluginSubWindowContainer.*`
- `tests/PluginHostingTests.cpp`
- `scripts/build_worker_x86.ps1`

## External dependencies and integration notes
- JUCE 8.0.12 is fetched automatically via CMake FetchContent.
- Catch2 v3.4.0 is used for unit tests.
- Optional legacy VST2 host support is enabled by `MYAPP_ENABLE_VST2_HOST` and requires a valid `MYAPP_VST2_SDK_PATH`.
- The bridge worker must be available for x86 VST2 plugins; the host looks in:
  - env override paths
  - sibling executable next to the host
  - `build-x86/MyAppBridgeWorker_artefacts/<Config>/MyAppBridgeWorker.exe`

## Recommended JUCE resources
- Official JUCE repo: https://github.com/juce-framework/JUCE
- JUCE API docs: https://docs.juce.com/develop/index.html
- Latest JUCE docs: https://docs.juce.com/master/index.html
- JUCE class browser: https://docs.juce.com/master/classes.html
- JUCE learning course: https://juce.com/learn/course/
- JUCE tutorials: https://juce.com/learn/tutorials/
- JUCE resources and features: https://juce.com/resources/ and https://juce.com/features/
- Community examples: https://github.com/sudara/awesome-juce
- Plugin starter reference: https://github.com/danielraffel/JUCE-Plugin-Starter

Use the official JUCE documentation when modifying framework integration or host/bridge interactions. The source here follows JUCE best-practice patterns for plugin hosting, `KnownPluginList`, and `AudioProcessorPlayer`.

## What to avoid
- Do not assume all plugins are loaded in-process.
- Do not remove the bridge worker fallback or IPC command contract without updating both host and worker.
- Do not change plugin cache logic without also updating `loadPluginCache()` / `savePluginCache()`.

If any section is unclear or incomplete, please point to the exact behavior you want encoded so I can refine this guidance.