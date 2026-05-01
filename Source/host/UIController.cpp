#include "UIController.h"

namespace myapp::host
{

UIController::UIController()
{
    waveformVisualiser.setColours (juce::Colour (0xFF1E1E1E), juce::Colour (0xFFFFBF00));
    waveformVisualiser.setSamplesPerBlock (256);
    waveformVisualiser.setRepaintRate (30);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));

    playButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF2A7A2A));
    stopButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF7A2A2A));
    stopButton.setEnabled (false);
    playButton.setEnabled (false);

    playBayatiDemoButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF7A5C2A));
    playBayatiDemoButton.setTooltip (juce::CharPointer_UTF8 ("\xd8\xb9\xd8\xb2\xd9\x81 \xd9\x84\xd8\xad\xd9\x86 \xd8\xa8\xd9\x8a\xd8\xa7\xd8\xaa\xd9\x8a \xd8\xaa\xd8\xb9\xd9\x84\xd9\x8a\xd9\x85\xd9\x8a"));

    maqamSelector.addItem ("Bayati",    1);
    maqamSelector.addItem ("Rast",      2);
    maqamSelector.addItem ("Hijaz",     3);
    maqamSelector.addItem ("Sika",      4);
    maqamSelector.addItem ("Ajam",      5);
    maqamSelector.addItem ("Nahawand",  6);
    maqamSelector.addItem ("Saba",      7);
    maqamSelector.addItem ("Kurd",      8);
    maqamSelector.setSelectedId (1, juce::dontSendNotification);

    pluginFilterSelector.addItem ("All", 1);
    pluginFilterSelector.addItem ("Loadable", 2);
    pluginFilterSelector.addItem ("Failed", 3);
    pluginFilterSelector.setSelectedId (1, juce::dontSendNotification);
}

UIController::~UIController() = default;

void UIController::createComponents (juce::AudioPluginFormatManager& formatManager,
                                     juce::KnownPluginList& pluginList,
                                     const juce::File& deadMansPedalFile,
                                     juce::PropertiesFile* appProperties)
{
    pluginListComponent = std::make_unique<juce::PluginListComponent> (
        formatManager,
        pluginList,
        deadMansPedalFile,
        appProperties,
        true);
}

void UIController::addComponentsTo (juce::Component& parent)
{
    parent.addAndMakeVisible (pluginListComponent.get());
    parent.addAndMakeVisible (scanButton);
    parent.addAndMakeVisible (loadButton);
    parent.addAndMakeVisible (unloadButton);
    parent.addAndMakeVisible (openAudioFileButton);
    parent.addAndMakeVisible (playButton);
    parent.addAndMakeVisible (stopButton);
    parent.addAndMakeVisible (playBayatiDemoButton);
    parent.addAndMakeVisible (statusLabel);
    parent.addAndMakeVisible (maqamLabel);
    parent.addAndMakeVisible (maqamSelector);
    parent.addAndMakeVisible (pluginFilterSelector);
    parent.addAndMakeVisible (pluginSelector);
    parent.addAndMakeVisible (waveformVisualiser);
}

void UIController::resized (juce::Component& parent, juce::Component* pluginSubWindowContainer)
{
    // Note: This method is now deprecated in favor of the new layout in MainComponent_UI.cpp::resized()
    // It's kept for backward compatibility but should not be called in the new layout flow
    auto bounds = parent.getLocalBounds().reduced (8);

    if (pluginSubWindowContainer != nullptr)
    {
        pluginListComponent->setBounds (bounds);
        pluginSubWindowContainer->setBounds (bounds);
    }
    else
    {
        pluginListComponent->setBounds (bounds);
    }
}

void UIController::positionToolbarBelowPluginList (int yPosition, int parentWidth)
{
    const int margin = 20;
    const int buttonHeight = 36;
    const int spacing = 8;
    
    // Position scan/load/unload buttons row
    auto buttonRow = juce::Rectangle<int> (margin, yPosition, 
                                           parentWidth - (margin * 2), buttonHeight);
    scanButton.setBounds (buttonRow.removeFromLeft (140).reduced (2, 0));
    loadButton.setBounds (buttonRow.removeFromLeft (140).reduced (2, 0));
    unloadButton.setBounds (buttonRow.removeFromLeft (140).reduced (2, 0));
    
    // Position audio file buttons row
    auto audioRow = juce::Rectangle<int> (margin, yPosition + buttonHeight + spacing,
                                          parentWidth - (margin * 2), buttonHeight);
    openAudioFileButton.setBounds (audioRow.removeFromLeft (140).reduced (2, 0));
    playButton.setBounds (audioRow.removeFromLeft (100).reduced (2, 0));
    stopButton.setBounds (audioRow.removeFromLeft (100).reduced (2, 0));
    
    // Position status label
    auto statusRow = juce::Rectangle<int> (margin, yPosition + (buttonHeight + spacing) * 2,
                                           parentWidth - (margin * 2), 28);
    statusLabel.setBounds (statusRow.reduced (4, 4));
    
    // Position control row (Maqam, filter, plugin selector)
    auto controlRow = juce::Rectangle<int> (margin, yPosition + (buttonHeight + spacing) * 2 + 36,
                                            parentWidth - (margin * 2), 32);
    maqamLabel.setBounds (controlRow.removeFromLeft (80));
    maqamSelector.setBounds (controlRow.removeFromLeft (200).reduced (2, 0));
    controlRow.removeFromLeft (12);
    pluginFilterSelector.setBounds (controlRow.removeFromLeft (150).reduced (2, 0));
    pluginSelector.setBounds (controlRow.removeFromLeft (300).reduced (2, 0));
    
    // Position waveform visualizer
    auto visRow = juce::Rectangle<int> (margin, yPosition + (buttonHeight + spacing) * 2 + 72,
                                        parentWidth - (margin * 2), 80);
    waveformVisualiser.setBounds (visRow.reduced (4, 4));
}

void UIController::updatePluginList (const juce::KnownPluginList& pluginList,
                                     juce::ComboBox& pluginSelectorCombo,
                                     juce::Array<int>& pluginSelectorToKnownIndex,
                                     int filterType)
{
    pluginSelectorCombo.clear();
    pluginSelectorToKnownIndex.clear();

    const auto& types = pluginList.getTypes();
    int itemId = 1;

    for (int i = 0; i < types.size(); ++i)
    {
        const auto& desc = types[i];
        bool shouldInclude = false;

        if (filterType == 1)
            shouldInclude = true;
        else if (filterType == 2)
            shouldInclude = ! desc.category.equalsIgnoreCase ("Failed to load");
        else if (filterType == 3)
            shouldInclude = desc.category.equalsIgnoreCase ("Failed to load");

        if (shouldInclude)
        {
            juce::String displayName = desc.name;
            if (desc.category.equalsIgnoreCase ("Failed to load"))
                displayName += " [FAILED]";

            pluginSelectorCombo.addItem (displayName, itemId);
            pluginSelectorToKnownIndex.add (i);
            ++itemId;
        }
    }

    if (pluginSelectorCombo.getNumItems() > 0)
        pluginSelectorCombo.setSelectedId (1, juce::dontSendNotification);
}

void UIController::setStatusMessage (const juce::String& message)
{
    statusLabel.setText (message, juce::dontSendNotification);
    juce::Logger::writeToLog (message);
    
    // Also log to file on desktop
    juce::File logFile = juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("MyApp_log.txt");
    logFile.appendText (juce::Time::getCurrentTime().toString (true, true) + " - " + message + "\n");
}

void UIController::setMaqamSelection (int index)
{
    maqamSelector.setSelectedId (index, juce::dontSendNotification);
}

} // namespace myapp::host
