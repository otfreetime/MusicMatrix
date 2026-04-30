#include "MainComponent.h"

//==============================================================================
// Virtual Keyboard Implementation
//==============================================================================

MainComponent::VirtualKeyboard::VirtualKeyboard()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
}

void MainComponent::VirtualKeyboard::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    
    const int startNote = 48; // C3
    const int numKeys = 25;   // 2 octaves
    
    for (int i = 0; i < numKeys; ++i)
    {
        const int noteNumber = startNote + i;
        const bool isBlack = isBlackKey (noteNumber);
        const auto bounds = getKeyBounds (i).toFloat();
        
        if (isBlack)
        {
            g.setColour (activeNotes.contains (noteNumber) ? juce::Colours::grey : juce::Colours::darkgrey);
            g.fillRect (bounds.reduced (1, 0));
        }
        else
        {
            g.setColour (activeNotes.contains (noteNumber) ? juce::Colours::lightgrey : juce::Colours::white);
            g.fillRect (bounds.reduced (1, 1));
            g.setColour (juce::Colours::black);
            g.drawRect (bounds.reduced (1, 1), 1);
        }
    }
    
    // Draw note labels
    g.setColour (juce::Colours::white);
    g.setFont (12.0f);
    for (int i = 0; i < numKeys; ++i)
    {
        const int noteNumber = startNote + i;
        if (! isBlackKey (noteNumber))
        {
            const auto bounds = getKeyBounds (i);
            const juce::String noteName = juce::MidiMessage::getMidiNoteName (noteNumber, true, true, 3);
            g.drawFittedText (noteName, bounds.toNearestInt(), juce::Justification::centredBottom, 1);
        }
    }
}

void MainComponent::VirtualKeyboard::mouseDown (const juce::MouseEvent& event)
{
    const int noteNumber = getNoteFromX (event.x);
    if (noteNumber >= 0 && ! activeNotes.contains (noteNumber))
    {
        activeNotes.add (noteNumber);
        repaint();
        
        if (onNotePlayed)
            onNotePlayed (noteNumber, true);
    }
}

void MainComponent::VirtualKeyboard::mouseUp (const juce::MouseEvent& event)
{
    const int noteNumber = getNoteFromX (event.x);
    if (activeNotes.contains (noteNumber))
    {
        activeNotes.removeAllInstancesOf (noteNumber);
        repaint();
        
        if (onNotePlayed)
            onNotePlayed (noteNumber, false);
    }
}

void MainComponent::VirtualKeyboard::mouseDrag (const juce::MouseEvent& event)
{
    // Trigger note on for new key under mouse
    const int noteNumber = getNoteFromX (event.x);
    if (noteNumber >= 0 && ! activeNotes.contains (noteNumber))
    {
        // Turn off previous notes
        for (int i = activeNotes.size() - 1; i >= 0; --i)
        {
            if (onNotePlayed)
                onNotePlayed (activeNotes[i], false);
        }
        activeNotes.clear();
        
        // Turn on new note
        activeNotes.add (noteNumber);
        repaint();
        
        if (onNotePlayed)
            onNotePlayed (noteNumber, true);
    }
}

int MainComponent::VirtualKeyboard::getNoteFromX (float x) const
{
    const int startNote = 48; // C3
    const int numKeys = 25;   // 2 octaves
    
    for (int i = 0; i < numKeys; ++i)
    {
        const auto bounds = getKeyBounds (i);
        if (bounds.contains (static_cast<int> (x), 0))
            return startNote + i;
    }
    
    return -1;
}

juce::Rectangle<float> MainComponent::VirtualKeyboard::getKeyBounds (int noteIndex) const
{
    const int startNote = 48; // C3
    const int numKeys = 25;   // 2 octaves
    const float keyWidth = static_cast<float> (getWidth()) / numKeys;
    const float keyHeight = static_cast<float> (getHeight());
    
    const int noteNumber = startNote + noteIndex;
    const bool isBlack = isBlackKey (noteNumber);
    
    if (isBlack)
    {
        // Black keys are narrower and shorter, positioned between white keys
        const float blackKeyWidth = keyWidth * 0.6f;
        const float blackKeyHeight = keyHeight * 0.6f;
        const float x = (noteIndex * keyWidth) + (keyWidth * 0.7f);
        return juce::Rectangle<float> (x, 0, blackKeyWidth, blackKeyHeight);
    }
    else
    {
        return juce::Rectangle<float> (noteIndex * keyWidth, 0, keyWidth - 1, keyHeight);
    }
}

bool MainComponent::VirtualKeyboard::isBlackKey (int noteNumber) const
{
    const int noteInOctave = noteNumber % 12;
    return (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 || noteInOctave == 8 || noteInOctave == 10);
}
