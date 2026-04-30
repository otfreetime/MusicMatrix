# Copilot Instructions for MyApp

This repository is a JUCE-based VST plugin host built with CMake and a host/bridge process design. The AI should focus on `Source/MainComponent.cpp`, `Source/bridge/*`, and the bridge worker launch logic when changing plugin loading, IPC, or audio routing.

## What matters most
- `MainComponent` is the central app component. It owns:
  - `juce::AudioPluginFormatManager` and `juce::KnownPluginList`
  - async scanning via `scanForPluginsAsync()` and `PluginDirectoryScanner`
  - plugin loading via `loadPluginAsync()`
  - VST3 in-process loading, VST2 via the bridge worker
  - **Virtual piano keyboard** (25 keys, C3-C5, MIDI output)
  - **Instrument selector** (ComboBox for VST2 program changes)
- `Source/bridge/PluginBridgeMaster.*` launches and communicates with the worker.
- `Source/bridge/PluginBridgeWorker.*` hosts VST2 plugins in a separate process.
- `Source/bridge/IPCManager.*` builds on `juce::InterprocessConnection` and serializes commands as `type|payload`.
- `Source/ui/PluginSubWindowContainer.*` handles embedded plugin editor windows.
- `tests/PluginHostingTests.cpp` models the main test style and expected behavior.
- **MIDI routing:** Keyboard → IPC `processMidi` → worker's `midiBuffer` → plugin's `processBlock()`

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
- **Keyboard MIDI:** Uses IPC `processMidi` command to send notes to VST2 worker
- **MIDI buffer:** Worker accumulates MIDI in `midiBuffer`, passes to `processBlock()` (NOT empty buffer!)
- **Audio thread:** Worker uses high-priority `juce::Thread` (not timer) for real-time processing

## Key files to inspect for any change
- `CMakeLists.txt` — FetchContent-based JUCE/Catch2 setup, optional VST2 SDK path logic
- `Source/MainComponent.cpp` / `Source/MainComponent.h`
- `Source/bridge/PluginBridgeMaster.*`
- `Source/bridge/PluginBridgeWorker.*`
- `Source/bridge/IPCManager.*`
- `Source/ui/PluginSubWindowContainer.*`
- `tests/PluginHostingTests.cpp`
- `scripts/build_worker_x86.ps1`
- **NEW:** `Source/MainComponent_Keyboard.cpp` — Virtual piano keyboard implementation
- **NEW:** `Source/MainComponent_UI.cpp` — Keyboard MIDI callback, instrument selector

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

## Global Development Guidelines
- **Modularity & Separation of Concerns:** Keep classes focused. `MainComponent` should delegate to managers (e.g., `PluginManager`, `BridgeManager`, `UIController`) rather than implementing all logic itself. Avoid monolithic classes.
- **Asynchronous Operations:** Never block the message thread. Use `juce::MessageManager::callAsync()` or `juce::Thread::launch()` for heavy operations. Always guard async callbacks with `juce::Component::SafePointer` or atomic flags (like `destroyed`) to prevent use-after-free when components are torn down.
- **Audio Thread Safety:** Code running in `processBlock` or `getNextAudioBlock` must be wait-free and lock-free. Do not allocate memory, log to the terminal, or call heavy UI functions from the audio thread.
- **Resource Management (RAII):** Use smart pointers (`std::unique_ptr`, `std::unique_ptr`) to manage ownership. Raw pointers should only be used as non-owning, observing pointers.
- **Cross-Platform Compatibility:** Write path handling, threading, and UI logic using JUCE primitives rather than OS-specific APIs unless absolutely necessary (like the specific Windows `HWND` embedding).

## Debugging, Fixing, and Development Updates
To prevent trial-and-error loops from happening again, we need to fundamentally change our process from making educated guesses to relying on hard evidence. This applies to any development update, new feature, or fixing any issue (not just specific crashes):
- **Require Stack Traces & Evidence:** Never guess the cause of an `abort()` or crash. If a crash occurs, execution must be attached to a debugger to capture the exact Call Stack and failing line. Validate behavior mathematically or logically instead of guessing.
- **Verify Framework Fundamentals First:** Always reference core JUCE documentation based on the Recommended JUCE resources above (e.g., subclass requirements like calling `shutdownAudio()` in `~AudioAppComponent()`) before assuming complex custom logic (like IPC or Bridge architecture) is at fault.
- **Component Isolation:** When narrowing down an issue, isolate features by disabling subsystems (like deferring PluginManager or BridgeManager startup) to cleanly separate core lifecycles from feature issues.
- **Strict Teardown Reviews:** Any new feature that allocates resources or registers listener callbacks must have its cleanup sequence explicitly reviewed in the relevant class destructor to prevent use-after-free and memory leaks.

## What to avoid
- Do not assume all plugins are loaded in-process.
- Do not remove the bridge worker fallback or IPC command contract without updating both host and worker.
- Do not change plugin cache logic without also updating `loadPluginCache()` / `savePluginCache()`.
- **CRITICAL:** Never pass empty MIDI buffer to `processBlock()` — must use accumulated `midiBuffer` with notes

If any section is unclear or incomplete, please point to the exact behavior you want encoded so I can refine this guidance.

## Global Development Guidelines
- **Modularity & Separation of Concerns:** Keep classes focused. `MainComponent` should delegate to managers (e.g., `PluginManager`, `BridgeManager`, `UIController`) rather than implementing all logic itself. Avoid monolithic classes.
- **Asynchronous Operations:** Never block the message thread. Use `juce::MessageManager::callAsync()` or `juce::Thread::launch()` for heavy operations. Always guard async callbacks with `juce::Component::SafePointer` or atomic flags (like `destroyed`) to prevent use-after-free when components are torn down.
- **Audio Thread Safety:** Code running in `processBlock` or `getNextAudioBlock` must be wait-free and lock-free. Do not allocate memory, log to the terminal, or call heavy UI functions from the audio thread.
- **Resource Management (RAII):** Use smart pointers (`std::unique_ptr`, `std::unique_ptr`) to manage ownership. Raw pointers should only be used as non-owning, observing pointers.
- **Cross-Platform Compatibility:** Write path handling, threading, and UI logic using JUCE primitives rather than OS-specific APIs unless absolutely necessary (like the specific Windows `HWND` embedding).

## Debugging, Fixing, and Development Updates
To prevent trial-and-error loops from happening again, we need to fundamentally change our process from making educated guesses to relying on hard evidence. This applies to any development update, new feature, or fixing any issue (not just specific crashes):
- **Require Stack Traces & Evidence:** Never guess the cause of an `abort()` or crash. If a crash occurs, execution must be attached to a debugger to capture the exact Call Stack and failing line. Validate behavior mathematically or logically instead of guessing.
- **Verify Framework Fundamentals First:** Always reference core JUCE documentation based on the Recommended JUCE resources above (e.g., subclass requirements like calling `shutdownAudio()` in `~AudioAppComponent()`) before assuming complex custom logic (like IPC or Bridge architecture) is at fault.
- **Component Isolation:** When narrowing down an issue, isolate features by disabling subsystems (like deferring PluginManager or BridgeManager startup) to cleanly separate core lifecycles from feature issues.
- **Strict Teardown Reviews:** Any new feature that allocates resources or registers listener callbacks must have its cleanup sequence explicitly reviewed in the relevant class destructor to prevent use-after-free and memory leaks.