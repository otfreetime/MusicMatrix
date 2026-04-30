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

    //==========================================================================
    // Audio Processing Helpers
    //==========================================================================
    void processDryWet (juce::AudioBuffer<float>& buffer, int numSamples);
    void updatePluginListUI();

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
