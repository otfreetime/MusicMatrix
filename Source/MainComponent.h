#pragma once

#include <JuceHeader.h>
#include <functional>
#include <unordered_map>
#include "host/PluginManager.h"
#include "host/BridgeManager.h"
#include "host/UIController.h"
#include "music/OrientalScaleManager.h"
#include "ui/CustomLookAndFeel.h"
#include "ui/PluginSubWindowContainer.h"
#include "ui/SequencerPanel.h"
#include "debug/DebugLogger.h"

//==============================================================================
/**
    Main audio component with VST2/VST3 plugin hosting capability.
    
    Modular architecture:
    - PluginManager: Plugin scanning, loading, cache management
    - BridgeManager: VST2 bridge worker lifecycle and IPC
    - UIController: UI components and layout
    - MainComponent: Audio I/O, orchestration, Maqam management
    
    Features:
    - Async plugin loading (non-blocking audio thread)
    - XML-based plugin cache with modification tracking
    - Built-in PluginListComponent for UI
    - Error handling with blacklist support
    - Dry/wet mixing with optional double-precision processing
*/
class MainComponent : public juce::AudioAppComponent,
                      public juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    //==========================================================================
    // Audio Processing
    //==========================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==========================================================================
    // GUI Rendering
    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    
    //==========================================================================
    // Keyboard Focus & Input
    //==========================================================================
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;

    //==========================================================================
    // Plugin Management (delegated to managers)
    //==========================================================================
    void scanForPluginsAsync();
    void loadPluginAsync (int pluginIndex);
    void unloadPlugin();
    bool isPluginLoaded() const;
    juce::String getLoadedPluginName() const;
    int getNumDiscoveredPlugins() const;
    juce::PluginDescription getPluginDescription (int index) const;

    //==========================================================================
    // MIDI Keyboard & Instrument Selection
    //==========================================================================
    void updateProgramSelector();
    void setCurrentProgram (int programIndex);
    void handleMidiNoteOn (int midiNoteNumber, float velocity);
    void handleMidiNoteOff (int midiNoteNumber, float velocity);
    void refreshKeyboardProgramNoteLabels();
    void refreshKeyboardHighlights();
    void playDemoMelody();
    void stopDemoMelody();
    void playTairiYaTayyara();
    void stopTairiYaTayyara();
    void playSalalemElNashh();
    void stopSalalemElNashh();

    //==========================================================================
    // Event Handlers
    //==========================================================================
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    //==========================================================================
    // Initialization Helpers
    //==========================================================================
    void initialiseUI();
    void initialiseBridge();
    void loadPluginCache();
    void savePluginCache();
    void restoreMaqamPreset();
    void clampKeyboardViewportToRange();

    //==========================================================================
    // Audio Processing Helpers
    //==========================================================================
    void processDryWet (juce::AudioBuffer<float>& buffer, int numSamples);
    void updatePluginListUI();
    void rebuildProgramSelectorsFromProgramNames();
    int getActualProgramIndexForDisplayIndex (int displayIndex) const;
    int getDisplayProgramIndexForActualIndex (int actualIndex) const;

    //==========================================================================
    // Plugin Loading Helpers
    //==========================================================================
    void onPluginLoadComplete (juce::AudioPluginInstance* plugin, const juce::String& error);
    void handleBridgeCommand (const myapp::bridge::IPCCommand& command);
    void syncFailedPluginsIntoList();

    enum class PluginBinaryArch { unknown, x86, x64 };
    PluginBinaryArch detectWindowsBinaryArch (const juce::String& filePath) const;

    bool openBridgeAudioFiles (const juce::String& inputPath, const juce::String& outputPath);
    void closeBridgeAudioFiles();
    void resetBridgePluginState();

    //==========================================================================
    // Managers (modular architecture)
    //==========================================================================
    myapp::host::PluginManager pluginManager;
    myapp::host::BridgeManager bridgeManager;
    myapp::host::UIController uiController;

    std::unique_ptr<juce::PropertiesFile> appProperties;
    juce::AudioProcessorPlayer processorPlayer;
    juce::File deadMansPedalFile;
    juce::Array<int> pluginSelectorToKnownIndex;
    bool bridgeAvailable = false;

    //==========================================================================
    // Virtual MIDI Keyboard & Instrument Selector
    //==========================================================================
    // Custom MIDI keyboard component with octave markers
    class CustomMidiKeyboardComponent : public juce::MidiKeyboardComponent
    {
    public:
        using NoteLabelArray = std::array<juce::String, 12>;

        CustomMidiKeyboardComponent (juce::MidiKeyboardState& state, Orientation orientation)
            : MidiKeyboardComponent (state, orientation)
        {
            static constexpr const char* defaultNoteNames[12] = {
                "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
            };

            for (int semitone = 0; semitone < 12; ++semitone)
                noteClassLabels[(size_t) semitone] = defaultNoteNames[semitone];
        }

        void setProgramLabel (juce::String label)
        {
            juce::ignoreUnused (label);
        }

        void setNoteClassLabels (const NoteLabelArray& labels)
        {
            noteClassLabels = labels;
            repaint();
        }

        using NoteColourArray = std::array<juce::Colour, 12>;

        void setNoteHighlightColours (const NoteColourArray& colours)
        {
            noteHighlightColours = colours;
            repaint();
        }

        void setDemoNoteActive (int midiNoteNumber, bool isActive, juce::Colour accentColour = {})
        {
            if (midiNoteNumber < 0 || midiNoteNumber >= (int) demoActiveNotes.size())
                return;

            const auto noteIndex = (size_t) midiNoteNumber;
            const auto resolvedAccent = accentColour.getAlpha() > 0
                ? accentColour
                : juce::Colour (0xFF9FD8B5);

            if (demoActiveNotes[noteIndex] == isActive
                && (! isActive || demoNoteAccentColours[noteIndex] == resolvedAccent))
                return;

            demoActiveNotes[noteIndex] = isActive;
            demoNoteAccentColours[noteIndex] = isActive ? resolvedAccent : juce::Colours::transparentBlack;
            repaint();
        }

        void clearDemoNotes()
        {
            demoActiveNotes.fill (false);
            demoNoteAccentColours.fill (juce::Colours::transparentBlack);
            repaint();
        }
        
        void drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                           juce::Rectangle<float> area, bool isDown,
                           bool isOver, juce::Colour lineColour,
                           juce::Colour textColour) override
        {
            // Check if this is a C note (start of octave)
            const bool isCNote = (midiNoteNumber % 12 == 0);

            // Determine base fill – C notes get a subtle grey tint for octave markers
            juce::Colour baseFill = isCNote
                ? findColour (whiteNoteColourId).interpolatedWith (juce::Colours::grey, 0.3f)
                : findColour (whiteNoteColourId);

            // Blend maqam highlight colour over white key (alpha-blended for subtlety)
            const auto highlight = noteHighlightColours[(size_t)(midiNoteNumber % 12)];
            if (highlight.getAlpha() > 0)
                baseFill = baseFill.interpolatedWith (highlight, 0.45f);

            const bool demoNoteActive = demoActiveNotes[(size_t) midiNoteNumber];
            const auto demoHighlight = demoNoteActive
                ? demoNoteAccentColours[(size_t) midiNoteNumber]
                : juce::Colour (0xFF9FD8B5);
            if (demoNoteActive)
                baseFill = baseFill.interpolatedWith (demoHighlight, 0.68f);

            g.setColour (baseFill);
            g.fillRect (area);

            if (demoNoteActive)
            {
                auto glowArea = area.reduced (1.5f, 3.0f);
                auto accentArea = glowArea.removeFromTop (area.getHeight() * 0.14f);
                g.setColour (demoHighlight.withAlpha (0.92f));
                g.fillRoundedRectangle (accentArea, 2.5f);
            }
            
            // Draw separator lines
            g.setColour (findColour (keySeparatorLineColourId));
            g.drawRect (area, 0.5f);
            
            // Draw note name only for C notes (C0, C1, C2, ..., C10)
            if (isCNote)
            {
                const int octave = (midiNoteNumber / 12) - 1;
                const juce::String noteName = "C" + juce::String (octave);
                
                g.setColour (findColour (textLabelColourId));
                g.setFont (20.0f);
                
                // Keep label fully inside white key body to avoid clipping/overlap
                const auto keyBounds = area.toNearestInt();
                const int labelHeight = 24;
                const int bottomInset = 8;
                auto labelArea = juce::Rectangle<int> (
                    keyBounds.getX() + 2,
                    keyBounds.getBottom() - labelHeight - bottomInset,
                    juce::jmax (0, keyBounds.getWidth() - 4),
                    labelHeight);
                
                g.drawFittedText (noteName, labelArea, juce::Justification::centred, 1);

            }

            drawNoteClassLabel (midiNoteNumber, g, area, juce::Colours::black.withAlpha (0.72f));
        }
        
        void drawBlackNote (int midiNoteNumber, juce::Graphics& g,
                           juce::Rectangle<float> area, bool isDown,
                           bool isOver, juce::Colour noteFillColour) override
        {
            g.setColour (findColour (blackNoteColourId));
            g.fillRect (area);

            // Draw a coloured top-band strip for maqam highlighting (keeps label readable)
            const auto highlight = noteHighlightColours[(size_t)(midiNoteNumber % 12)];
            if (highlight.getAlpha() > 0)
            {
                auto band = area.withHeight (area.getHeight() * 0.28f);
                g.setColour (highlight.withAlpha (0.85f));
                g.fillRect (band);
            }

            if (demoActiveNotes[(size_t) midiNoteNumber])
            {
                const auto demoHighlight = demoNoteAccentColours[(size_t) midiNoteNumber];
                auto band = area.withHeight (area.getHeight() * 0.34f);
                g.setColour (demoHighlight.withAlpha (0.94f));
                g.fillRect (band);

                auto outline = area.reduced (0.75f, 1.5f);
                g.setColour (demoHighlight.brighter (0.15f).withAlpha (0.85f));
                g.drawRoundedRectangle (outline, 2.0f, 1.2f);
            }

            g.setColour (noteFillColour);
            g.drawRect (area, 0.5f);

            drawNoteClassLabel (midiNoteNumber, g, area, juce::Colours::white.withAlpha (0.88f));
        }

    private:
        void drawNoteClassLabel (int midiNoteNumber,
                                 juce::Graphics& g,
                                 juce::Rectangle<float> area,
                                 juce::Colour colour) const
        {
            const int semitone = midiNoteNumber % 12;
            if (semitone < 0 || semitone >= 12)
                return;

            const bool isBlackKey = (semitone == 1 || semitone == 3 || semitone == 6
                                     || semitone == 8 || semitone == 10);

            // C white keys already show octave labels (C0, C1, ...), so skip duplicate class text.
            if (! isBlackKey && semitone == 0)
                return;

            const auto noteClass = noteClassLabels[(size_t) semitone];
            if (noteClass.isEmpty())
                return;

            const auto keyBounds = area.toNearestInt();
            g.setColour (colour);
            g.setFont (15.0f);

            const int labelHeight = 18;
            const int labelY = isBlackKey
                ? (keyBounds.getY() + 18)
                : (keyBounds.getBottom() - 34);

            auto labelArea = juce::Rectangle<int> (
                keyBounds.getX() + 2,
                juce::jlimit (keyBounds.getY() + 18,
                              keyBounds.getBottom() - labelHeight - 4,
                              labelY),
                juce::jmax (0, keyBounds.getWidth() - 4),
                labelHeight);

            g.drawFittedText (noteClass, labelArea, juce::Justification::centred, 1);
        }

        NoteLabelArray  noteClassLabels;
        NoteColourArray noteHighlightColours {}; // all transparent by default
        std::array<bool, 128> demoActiveNotes {};
        std::array<juce::Colour, 128> demoNoteAccentColours {};
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustomMidiKeyboardComponent)
    };
    
    // Custom keyboard listener to handle note events
    class KeyboardStateListener : public juce::MidiKeyboardState::Listener
    {
    public:
        KeyboardStateListener (MainComponent& owner) : mainComponent (owner) {}
        
        void handleNoteOn (juce::MidiKeyboardState* source, int midiChannel,
                          int midiNoteNumber, float velocity) override
        {
            mainComponent.handleMidiNoteOn (midiNoteNumber, velocity);
        }
        
        void handleNoteOff (juce::MidiKeyboardState* source, int midiChannel,
                           int midiNoteNumber, float velocity) override
        {
            mainComponent.handleMidiNoteOff (midiNoteNumber, velocity);
        }
        
    private:
        MainComponent& mainComponent;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardStateListener)
    };

    //==========================================================================
    // Melody Player -- timer-driven note sequencer for maqam teaching demos
    //==========================================================================
    class MelodyPlayer : private juce::Timer
    {
    public:
        using NoteCallback = std::function<void (int midiNote, float velocity)>;

        enum class BayatiPlayMode
        {
            upScale,
            upAndDownScale
        };

        MelodyPlayer() = default;
        ~MelodyPlayer() override { stopTimer(); }

        void setCallbacks (NoteCallback onOn, NoteCallback onOff, std::function<void()> onFinished)
        {
            noteOnCb     = std::move (onOn);
            noteOffCb    = std::move (onOff);
            finishedCb   = std::move (onFinished);
        }

        /** Play Bayati demo melody according to selected mode. */
        void playBayatiDemo (BayatiPlayMode mode)
        {
            stop();
            // Bayati on D3 (MIDI 62).
            // Eb3 (MIDI 63) is the character quarter-tone note;
            // OrientalScaleManager injects pitch-bend when routed through the bridge.
            // Format: { midiNote, durationMs }
            if (mode == BayatiPlayMode::upScale)
            {
                sequence = {
                    { 62, 420 }, // D3
                    { 63, 360 }, // Eb3-
                    { 65, 360 }, // F3
                    { 67, 360 }, // G3
                    { 69, 360 }, // A3
                    { 70, 360 }, // Bb3
                    { 72, 380 }, // C4
                    { 74, 950 }, // D4 cadence
                };
            }
            else
            {
                sequence = {
                    { 62, 350 }, // D3  -- root (tonic of Bayati)
                    { 63, 350 }, // Eb3 -- maqam character note (quarter-tone)
                    { 65, 350 }, // F3
                    { 67, 350 }, // G3
                    { 69, 500 }, // A3  -- upper neighbour
                    { 67, 250 }, // G3
                    { 69, 250 }, // A3
                    { 70, 350 }, // Bb3
                    { 72, 350 }, // C4
                    { 74, 600 }, // D4  -- octave climax
                    { 72, 300 }, // C4  -- descent begins
                    { 70, 300 }, // Bb3
                    { 69, 300 }, // A3
                    { 67, 300 }, // G3
                    { 65, 300 }, // F3
                    { 63, 300 }, // Eb3
                    { 62, 800 }, // D3  -- first cadence
                    { 62, 250 }, // D3  -- second phrase opens
                    { 63, 250 }, // Eb3
                    { 65, 250 }, // F3
                    { 67, 500 }, // G3
                    { 65, 250 }, // F3
                    { 63, 250 }, // Eb3
                    { 62, 900 }, // D3  -- final resolution
                };
            }
            stepIndex   = 0;
            noteOnPhase = true;
            startTimer (10);
        }

        void stop()
        {
            stopTimer();
            if (lastNote >= 0 && noteOffCb)
                noteOffCb (lastNote, 0.f);
            lastNote = -1;
            sequence.clear();
        }

        bool isPlaying() const { return isTimerRunning(); }

        // Public access for custom melody playback sequences
        struct Step { int midiNote; int durationMs; };
        std::vector<Step> sequence;
        int stepIndex = 0;
        bool noteOnPhase = true;
        void startTimer (int intervalMs) { juce::Timer::startTimer (intervalMs); }
        void stopTimer() { juce::Timer::stopTimer(); }

    private:

        void timerCallback() override
        {
            stopTimer();
            if (stepIndex >= (int) sequence.size())
            {
                if (lastNote >= 0 && noteOffCb) noteOffCb (lastNote, 0.f);
                lastNote = -1;
                if (finishedCb) finishedCb();
                return;
            }
            const auto& step = sequence[(size_t) stepIndex];
            if (noteOnPhase)
            {
                if (lastNote >= 0 && noteOffCb) noteOffCb (lastNote, 0.f);
                if (noteOnCb) noteOnCb (step.midiNote, 0.75f);
                lastNote    = step.midiNote;
                noteOnPhase = false;
                startTimer (juce::jmax (80, step.durationMs - 40));
            }
            else
            {
                noteOnPhase = true;
                ++stepIndex;
                startTimer (40); // 40 ms silence between notes
            }
        }

        NoteCallback noteOnCb, noteOffCb;
        std::function<void()> finishedCb;
        int  lastNote    = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyPlayer)
    };

    juce::MidiKeyboardState keyboardState;
    std::unique_ptr<KeyboardStateListener> keyboardListener;
    std::unique_ptr<CustomMidiKeyboardComponent> midiKeyboard;
    MelodyPlayer melodyPlayer;
    juce::ComboBox programSelector;
    int currentProgramIndex { 0 };
    int numPrograms { 0 };
    juce::StringArray programNames;
    juce::Array<int> displayedProgramToActualIndex;
    
    // MIDI note tracking: tracks which notes (0-127) are currently playing
    // Used to send Note-OFF messages before program changes
    bool activeNotes[128] = {};  // All notes initially off
    
    // Helper function to stop all playing notes (sends Note-OFF to plugin)
    void allNotesOff();
    
    // Computer keyboard mapping support
    bool computerKeyboardMappingEnabled { true };
    int keyboardBaseOctave { 0 };  // QWERTY row maps to this octave (default C0)
    std::unordered_map<juce::juce_wchar, int> heldComputerKeysToMidiNotes;
    juce::ComboBox octaveSelector;   // C0–C9 octave selector for PC keyboard mapping
    juce::Label  octaveSelectorLabel;
    juce::OwnedArray<juce::TextButton> octaveButtons;  // C0-C9 octave selection buttons
    std::unique_ptr<juce::TextButton> tairiYaTayyaraButton;  // Play Tairi Ya Tayyara button
    std::unique_ptr<juce::TextButton> salalemElNashhButton;  // Play Salalem El-Nashh button
    std::unique_ptr<juce::TextButton> sequencerToggleButton;  // Toggle sequencer UI
    std::unique_ptr<SequencerPanel> sequencerPanel;   // Channel Rack + Piano Roll

    // Initialize PC keyboard to musical note mapping
    void initialiseKeyboardMapping();
    bool refreshComputerKeyboardNotesFromKeyState();
    void releaseAllHeldComputerKeyboardNotes();

    //==========================================================================
    // Plugin Editor Window
    //==========================================================================
    class PluginEditorWindow : public juce::DocumentWindow
    {
    public:
        PluginEditorWindow (juce::AudioProcessorEditor* editor);
        void closeButtonPressed() override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorWindow)
    };

    std::unique_ptr<PluginEditorWindow> pluginEditorWindow;

    //==========================================================================
    // Audio File Player
    //==========================================================================
    void openAudioFile();
    void playAudioFile();
    void stopAudioFile();

    juce::AudioFormatManager audioFormatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;

    //==========================================================================
    // Audio & Maqam
    //==========================================================================
    float dryWetMix = 0.5f;
    float dryGainLinear = 0.5f;
    juce::dsp::Gain<float> dryGain;
    myapp::ui::PluginSubWindowContainer pluginSubWindowContainer;
    myapp::music::OrientalScaleManager maqamManager;
    myapp::ui::CustomLookAndFeel customLookAndFeel;

    double currentSampleRate = 44100.0;
    int blockSize = 512;
    int sampleIndex = 0;
    bool bridgePluginLoaded { false };
    std::atomic<bool> bridgeAudioProcessed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
