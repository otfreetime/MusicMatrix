#pragma once

#include <JuceHeader.h>
#include "host/PluginManager.h"
#include "host/BridgeManager.h"
#include "host/UIController.h"
#include "music/OrientalScaleManager.h"
#include "ui/CustomLookAndFeel.h"
#include "ui/PluginSubWindowContainer.h"
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
        
        void drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                           juce::Rectangle<float> area, bool isDown,
                           bool isOver, juce::Colour lineColour,
                           juce::Colour textColour) override
        {
            // Check if this is a C note (start of octave)
            const bool isCNote = (midiNoteNumber % 12 == 0);
            
            // Draw C notes with gray background for octave visualization
            if (isCNote)
            {
                g.setColour (findColour (whiteNoteColourId).interpolatedWith (juce::Colours::grey, 0.3f));
            }
            else
            {
                g.setColour (findColour (whiteNoteColourId));
            }
            
            g.fillRect (area);
            
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
            g.setFont (12.0f);

            const int labelHeight = 14;
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

        NoteLabelArray noteClassLabels;
        
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
    
    juce::MidiKeyboardState keyboardState;
    std::unique_ptr<KeyboardStateListener> keyboardListener;
    std::unique_ptr<CustomMidiKeyboardComponent> midiKeyboard;
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
    int keyboardBaseOctave { 3 };  // QWERTY row maps to this octave

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
