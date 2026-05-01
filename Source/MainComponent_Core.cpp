#include "MainComponent.h"

namespace
{
int getSemitoneForComputerKey (juce::juce_wchar keyChar)
{
    switch (juce::CharacterFunctions::toLowerCase (keyChar))
    {
        case 'a': return 0;
        case 'w': return 1;
        case 's': return 2;
        case 'e': return 3;
        case 'd': return 4;
        case 'f': return 5;
        case 't': return 6;
        case 'g': return 7;
        case 'y': return 8;
        case 'h': return 9;
        case 'u': return 10;
        case 'j': return 11;
        case 'k': return 12;
        default:  return -1;
    }
}

bool isComputerKeyCurrentlyDown (juce::juce_wchar lowerKey)
{
    const auto upperKey = juce::CharacterFunctions::toUpperCase (lowerKey);
    return juce::KeyPress::isKeyCurrentlyDown ((int) lowerKey)
        || juce::KeyPress::isKeyCurrentlyDown ((int) upperKey);
}
}

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
// Keyboard Input Handling
//==============================================================================

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (! computerKeyboardMappingEnabled)
        return false;

    const auto keyChar = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const auto semitone = getSemitoneForComputerKey (keyChar);
    if (semitone < 0)
        return false;

    if (! hasKeyboardFocus (true))
        grabKeyboardFocus();

    DEBUG_LOG ("Keyboard: keyPressed " + juce::String::charToString (keyChar));
    return refreshComputerKeyboardNotesFromKeyState();
}

bool MainComponent::keyStateChanged (bool isKeyDown)
{
    if (! computerKeyboardMappingEnabled)
        return false;

    if (! hasKeyboardFocus (true))
        grabKeyboardFocus();

    DEBUG_LOG ("Keyboard: keyStateChanged - isKeyDown=" + juce::String (isKeyDown ? "true" : "false"));
    return refreshComputerKeyboardNotesFromKeyState();
}

bool MainComponent::refreshComputerKeyboardNotesFromKeyState()
{
    bool handledAny = false;

    static constexpr juce::juce_wchar mappedKeys[] = {
        'a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'y', 'h', 'u', 'j', 'k'
    };

    for (auto keyChar : mappedKeys)
    {
        const auto lowerKey = juce::CharacterFunctions::toLowerCase (keyChar);
        const bool isDown = isComputerKeyCurrentlyDown (lowerKey);
        const auto existing = heldComputerKeysToMidiNotes.find (lowerKey);

        if (isDown && existing == heldComputerKeysToMidiNotes.end())
        {
            const auto semitone = getSemitoneForComputerKey (lowerKey);
            if (semitone >= 0)
            {
                const int noteNumber = juce::jlimit (0, 127, (keyboardBaseOctave + 1) * 12 + semitone);
                keyboardState.noteOn (1, noteNumber, 0.8f);
                heldComputerKeysToMidiNotes[lowerKey] = noteNumber;
                handledAny = true;
            }
        }
        else if (! isDown && existing != heldComputerKeysToMidiNotes.end())
        {
            keyboardState.noteOff (1, existing->second, 0.0f);
            heldComputerKeysToMidiNotes.erase (existing);
            handledAny = true;
        }
    }

    DEBUG_LOG (handledAny ? "Keyboard: key state reconciliation handled" : "Keyboard: key state reconciliation no-op");
    return handledAny;
}

void MainComponent::releaseAllHeldComputerKeyboardNotes()
{
    for (const auto& [keyChar, noteNumber] : heldComputerKeysToMidiNotes)
        keyboardState.noteOff (1, noteNumber, 0.0f);

    heldComputerKeysToMidiNotes.clear();
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
