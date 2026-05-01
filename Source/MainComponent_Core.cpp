#include "MainComponent.h"

//==============================================================================
// Arch detection
//==============================================================================

MainComponent::PluginBinaryArch MainComponent::detectWindowsBinaryArch (const juce::String& filePath) const
{
#if JUCE_WINDOWS
    juce::File file (filePath);
    if (! file.existsAsFile())
        return PluginBinaryArch::unknown;

    juce::FileInputStream stream (file);
    if (! stream.openedOk())
        return PluginBinaryArch::unknown;

    if (! stream.setPosition (0x3C))
        return PluginBinaryArch::unknown;

    const auto peOffset = stream.readInt();
    if (peOffset <= 0 || ! stream.setPosition ((juce::int64) peOffset + 4))
        return PluginBinaryArch::unknown;

    const auto machine = (juce::uint16) stream.readShort();
    if (machine == 0x14c)
        return PluginBinaryArch::x86;
    if (machine == 0x8664)
        return PluginBinaryArch::x64;
#endif
    return PluginBinaryArch::unknown;
}


//==============================================================================

MainComponent::MainComponent()
    : pluginManager (juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("MyApp_Plugins.deadmanspedalfile"))
{
    setLookAndFeel (&customLookAndFeel);

    // Set up dead man's pedal file (already initialized in member init list)
    // deadMansPedalFile is available for UIController

    // Initialize UI
    initialiseUI();

    // Defer bridge startup
    juce::MessageManager::callAsync ([this]
    {
        initialiseBridge();
    });
}

MainComponent::~MainComponent()
{
    DEBUG_LOG ("MainComponent: DESTRUCTOR CALLED");
    
    // Remove the mouse listener to prevent lingering pointers referencing 'this' on teardown
    if (uiController.getPluginListComponent() != nullptr)
        uiController.getPluginListComponent()->getTableListBox().removeMouseListener (this);

    // Clean up MIDI keyboard listener
    if (keyboardListener != nullptr)
        keyboardState.removeListener (keyboardListener.get());

    if (midiKeyboard != nullptr)
        midiKeyboard->removeChangeListener (this);

    setLookAndFeel (nullptr);
    DEBUG_LOG ("MainComponent: Look and feel set to null");
    
    // Save plugin list before shutdown
    savePluginCache();

    unloadPlugin();
    DEBUG_LOG ("MainComponent: Plugin unloaded");
    
    bridgeManager.shutdown();
    DEBUG_LOG ("MainComponent: Bridge shut down");
    
    // Shut down audio to stop callbacks and prevent abort() on exit
    shutdownAudio();

    DEBUG_LOG ("MainComponent: Destructor finished");
}

//==============================================================================
// Plugin Editor Window
//==============================================================================

MainComponent::PluginEditorWindow::PluginEditorWindow (juce::AudioProcessorEditor* editor)
    : juce::DocumentWindow (editor->getName(), 
                            juce::Colours::lightgrey, 
                            juce::DocumentWindow::allButtons)
{
    setResizable (true, true);
    setContentOwned (editor, true);
    setUsingNativeTitleBar (true);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void MainComponent::PluginEditorWindow::closeButtonPressed()
{
    clearContentComponent();
}
