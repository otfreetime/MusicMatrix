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

    maqamSelector.addItem ("Bayati",  1);
    maqamSelector.addItem ("Rast",    2);
    maqamSelector.addItem ("Hijaz",   3);
    maqamSelector.addItem ("Sika",    4);
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
    parent.addAndMakeVisible (statusLabel);
    parent.addAndMakeVisible (maqamLabel);
    parent.addAndMakeVisible (maqamSelector);
    parent.addAndMakeVisible (pluginFilterSelector);
    parent.addAndMakeVisible (pluginSelector);
    parent.addAndMakeVisible (waveformVisualiser);
}

void UIController::resized (juce::Component& parent, juce::Component* pluginSubWindowContainer)
{
    auto bounds = parent.getLocalBounds().reduced (8);

    auto buttonRow = bounds.removeFromTop (36);
    scanButton.setBounds (buttonRow.removeFromLeft (140).reduced (2, 0));
    loadButton.setBounds (buttonRow.removeFromLeft (140).reduced (2, 0));
    unloadButton.setBounds (buttonRow.removeFromLeft (140).reduced (2, 0));

    auto audioRow = bounds.removeFromTop (36);
    openAudioFileButton.setBounds (audioRow.removeFromLeft (140).reduced (2, 0));
    playButton.setBounds (audioRow.removeFromLeft (100).reduced (2, 0));
    stopButton.setBounds (audioRow.removeFromLeft (100).reduced (2, 0));

    auto statusRow = bounds.removeFromTop (28);
    statusLabel.setBounds (statusRow.reduced (4, 4));

    auto controlRow = bounds.removeFromTop (32);
    maqamLabel.setBounds (controlRow.removeFromLeft (80));
    maqamSelector.setBounds (controlRow.removeFromLeft (200).reduced (2, 0));
    controlRow.removeFromLeft (12);
    pluginFilterSelector.setBounds (controlRow.removeFromLeft (150).reduced (2, 0));
    pluginSelector.setBounds (controlRow.removeFromLeft (300).reduced (2, 0));

    auto visRow = bounds.removeFromTop (80);
    waveformVisualiser.setBounds (visRow.reduced (4, 4));

    if (pluginSubWindowContainer != nullptr)
    {
        auto contentArea = bounds;
        pluginListComponent->setBounds (contentArea);
        pluginSubWindowContainer->setBounds (contentArea);
    }
    else
    {
        pluginListComponent->setBounds (bounds);
    }
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
