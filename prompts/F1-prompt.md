# JUCE Hybrid Host Blueprint + Copilot Master Prompt

## 1) Project Context

- **Workspace**: `E:\Maqam Classification\MyApp`
- **Framework**: JUCE (C++20)
- **Start file**: `E:\Maqam Classification\MyApp\Source\MainComponent.cpp`
- **Primary implementation order**:
  1. CMake setup
  2. IPC / Bridge layer
  3. VST2 sub-window embedding
  4. UI / LookAndFeel
  5. Maqam microtonal engine
- **VST2 SDK path**: `E:\Maqam Classification\vstsdk3612_03_12_2018_build_67\VST_SDK\VST2_SDK`
- **ARA limitation**: ARA 2.0 is VST3 (64-bit) only; for VST2 hosting use `MidiBuffer` interception for microtonal behavior.

---

## 2) Must-Use Links (Curated)

1. https://docs.juce.com/master/
2. https://docs.juce.com/master/classes.html
3. https://juce.com/learn/tutorials/
4. https://juce.com/learn/course/
5. https://github.com/juce-framework/JUCE
6. https://github.com/sudara/awesome-juce
7. https://github.com/danielraffel/JUCE-Plugin-Starter
8. https://juce.com/features/

---

## 3) Execution Map (Concrete Files)

- **Build and feature flags**: `CMakeLists.txt`
- **Main app shell / host UI entry**: `Source/MainComponent.h`, `Source/MainComponent.cpp`
- **Bridge process classes (new)**:
  - `Source/bridge/PluginBridgeMaster.h/.cpp`
  - `Source/bridge/PluginBridgeWorker.h/.cpp`
  - `Source/bridge/IPCManager.h/.cpp`
- **Sub-window embedding (new)**:
  - `Source/ui/PluginSubWindowContainer.h/.cpp`
- **Custom style and widgets (new)**:
  - `Source/ui/CustomLookAndFeel.h/.cpp`
- **Maqam/microtonal logic (new)**:
  - `Source/music/OrientalScaleManager.h/.cpp`

---

## 4) Technical Blueprint (Implementation Brief)

### A) Architecture

Build a **single codebase** with a **multi-process bridge**:

- **Master (64-bit)**: Main app, UI, plugin management, orchestration.
- **Worker (32-bit)**: Legacy VST2 loader/runner to support Win32 plugins.

Goal: host VST3 natively and host VST2 Win32 via process isolation, similar to FL Studio bridge behavior.

### B) Build Strategy

Use CMake with platform/architecture-aware targets.

- x64 target: Main host + VST3 path.
- x86 target: Worker executable for VST2 Win32.
- Enable relevant JUCE host flags (`JUCE_PLUGINHOST_VST`, `JUCE_PLUGINHOST_VST3`, optional `JUCE_PLUGINHOST_ARA` where applicable).
- Use Direct2D where suitable for Windows UI performance (`JUCE_DIRECT2D=1`).

### C) Bridge + IPC

Implement bridge communication for:

- Audio buffer exchange
- MIDI message exchange
- Parameter synchronization
- Plugin window lifecycle and resize commands

Recommended JUCE primitives:

- `juce::ChildProcess`
- `juce::InterprocessConnection`
- shared-memory strategy for low-latency transfer where needed

### D) VST2 GUI Sub-Window Embedding

Embed VST2 editor window into a host container component:

- Acquire worker plugin `HWND`
- Attach to host placeholder with Win32 parenting
- Handle style flags, resize, focus, repaint, and DPI scaling
- Keep host controls active around the embedded plugin UI

### E) UI Direction (Reference-Matched)

Target style inspired by the provided Studio One / DynAssist screenshot:

- Dark-mode base (`#121212` / deep gray)
- Amber/Gold accents for active controls
- Electric/Sea blue waveform area
- Thin header bar with compact controls
- Flat-modern sliders and iOS-style toggles
- Vector-first drawing (`juce::Path`) for high DPI

### F) Maqam & Microtonal Engine

Core behavior:

- Intercept incoming MIDI in processing path
- Apply quarter-tone logic using pitch bend / mapping rules
- Keep profiles for maqamat (e.g., Bayati, Rast, Hijaz, Sika)
- Expose selection in host UI (combobox/selector)

For VST2 path, microtonal behavior must be applied at host level before forwarding MIDI to worker/plugin.

---

## 5) Master Copilot Prompt (Single Authoritative Prompt)

Use the following prompt directly with Copilot Chat:

```text
Role: Lead Audio Software Architect, JUCE/C++20 specialist, and DSP engineer.

Project:
I am building a JUCE-based Windows 11 hybrid plugin host at:
E:\Maqam Classification\MyApp

Start from:
E:\Maqam Classification\MyApp\Source\MainComponent.cpp

Key constraints:
1) Implement using JUCE classes/modules and clean C++20.
2) Support VST3 natively in 64-bit host.
3) Support VST2 Win32 through a separate 32-bit worker process (bridge).
4) VST2 SDK path:
   E:\Maqam Classification\vstsdk3612_03_12_2018_build_67\VST_SDK\VST2_SDK
5) ARA note: ARA 2.0 is VST3 (64-bit) only. For VST2, apply microtonal behavior via MidiBuffer interception.

Implementation order:
Step 1: CMake setup
Step 2: IPC/Bridge
Step 3: VST2 sub-window embedding
Step 4: UI/LookAndFeel
Step 5: Maqam microtonal engine

Requirements:
- Build a robust multi-process architecture:
  - 64-bit master host process
  - 32-bit worker process for VST2 Win32
- Implement IPC for:
  - audio buffers
  - MIDI events
  - parameter sync
  - editor window commands
- Embed the VST2 editor as a sub-window in host UI (Win32 HWND parenting).
- Keep host features active while plugin window is embedded.
- Implement a modern dark UI inspired by Studio One/DynAssist:
  - dark charcoal background
  - amber/gold active states
  - blue waveform view
  - compact header
  - thin, modern sliders and iOS-like toggles
- Use a custom LookAndFeel class and vector drawing via juce::Path.
- Add a maqam microtonal module:
  - quarter-tone handling
  - maqam presets (Bayati, Rast, Hijaz, Sika)
  - host-side MIDI transformation before plugin dispatch

Code quality expectations:
- Thread-safe audio path
- Avoid blocking message thread
- Maintain clean class boundaries and file organization
- Use JUCE naming/style conventions

Deliverables:
1) Updated CMakeLists with required flags/targets
2) Master bridge classes + worker bridge classes
3) Sub-window container component
4) Custom LookAndFeel and UI scaffold
5) OrientalScaleManager (microtonal mapping)
6) Integration points in MainComponent

When generating code:
- Produce complete .h/.cpp pairs
- Include minimal compile-ready stubs where needed
- Explain where each file should be added in this project
```

---

## 6) Arabic Glossary (Quick Reference)

- **Maqam (مقام)**: Arabic melodic mode / scale system.
- **Quarter-tone (ربع تون)**: Interval smaller than a semitone used in Arabic music.
- **Bridge (جسر العمليات)**: Separate worker process allowing 32-bit plugin hosting from 64-bit app.
- **Sub-window (نافذة فرعية)**: Embedded plugin editor window inside host UI.
- **Microtonal mapping (تعيين ميكروتونال)**: Rule-based pitch transformation to match maqam intervals.
- **Plugin host (مضيف البلجنات)**: Application that scans, loads, and runs audio plugins.

---

## 7) Out of Scope / Backlog

The following ideas are intentionally excluded from the active implementation brief and kept as backlog notes:

- General non-project topics (banking, projector shopping, unrelated poetry/songwriting threads)
- Generic “AI from YouTube videos” discussion not tied to this JUCE host architecture
- Non-essential mixed-language brainstorming lines that do not affect build/runtime design

---

## 8) Next Action (Immediate)

Begin coding from `Source/MainComponent.cpp` with this sequence:

1. Add minimal host skeleton + plugin manager entry points.
2. Wire CMake flags and architecture-aware targets.
3. Introduce bridge interfaces (`PluginBridgeMaster`, `PluginBridgeWorker`, `IPCManager`).
4. Add `PluginSubWindowContainer` placeholder in UI.
5. Add `OrientalScaleManager` and route MIDI through it before dispatch.
