#pragma once

#include <JuceHeader.h>

namespace myapp::host
{

/**
    Manages UI components and layout.
    Extracted from MainComponent for better maintainability.
*/
class UIController
{
public:
    UIController();
    ~UIController();

    // Component Creation
    void createComponents (juce::AudioPluginFormatManager& formatManager,
                          juce::KnownPluginList& pluginList,
                          const juce::File& deadMansPedalFile,
                          juce::PropertiesFile* appProperties);

    void addComponentsTo (juce::Component& parent);
    void resized (juce::Component& parent, juce::Component* pluginSubWindowContainer);

    // UI Updates
    void updatePluginList (const juce::KnownPluginList& pluginList,
                          juce::ComboBox& pluginSelector,
                          juce::Array<int>& pluginSelectorToKnownIndex,
                          int filterType);

    void setStatusMessage (const juce::String& message);
    void setMaqamSelection (int index);

    // Component Access
    juce::PluginListComponent* getPluginListComponent() { return pluginListComponent.get(); }
    juce::TextButton* getScanButton() { return &scanButton; }
    juce::TextButton* getLoadButton() { return &loadButton; }
    juce::TextButton* getUnloadButton() { return &unloadButton; }
    juce::TextButton* getOpenAudioFileButton() { return &openAudioFileButton; }
    juce::TextButton* getPlayButton()  { return &playButton; }
    juce::TextButton* getStopButton()  { return &stopButton; }
    juce::Label* getStatusLabel() { return &statusLabel; }
    juce::ComboBox* getMaqamSelector() { return &maqamSelector; }
    juce::ComboBox* getPluginFilterSelector() { return &pluginFilterSelector; }
    juce::ComboBox* getPluginSelector() { return &pluginSelector; }
    juce::AudioVisualiserComponent* getWaveformVisualiser() { return &waveformVisualiser; }

private:
    std::unique_ptr<juce::PluginListComponent> pluginListComponent;
    juce::TextButton scanButton        { "Scan for Plugins" };
    juce::TextButton loadButton        { "Load Selected" };
    juce::TextButton unloadButton      { "Unload Plugin" };
    juce::TextButton openAudioFileButton { "Open Audio File" };
    juce::TextButton playButton        { "Play" };
    juce::TextButton stopButton        { "Stop" };
    juce::Label      statusLabel;
    juce::Label      maqamLabel   { {}, "Maqam:" };
    juce::ComboBox   maqamSelector;
    juce::ComboBox   pluginFilterSelector;
    juce::ComboBox   pluginSelector;
    juce::AudioVisualiserComponent waveformVisualiser { 2 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UIController)
};

} // namespace myapp::host
