#include "MainComponent.h"

namespace
{
juce::String getBaseNoteLabel (int semitone)
{
    static constexpr const char* noteNames[12] = {
        "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
    };

    if (semitone < 0 || semitone >= 12)
        return {};

    return noteNames[semitone];
}

}

//==============================================================================
// MIDI Keyboard Implementation using JUCE MidiKeyboardComponent
//==============================================================================

void MainComponent::handleMidiNoteOn (int midiNoteNumber, float velocity)
{
    DEBUG_LOG ("MidiKeyboard: Note ON - note=" + juce::String (midiNoteNumber) 
              + " velocity=" + juce::String (velocity));
    
    // Track this note as active
    if (midiNoteNumber >= 0 && midiNoteNumber < 128)
        activeNotes[midiNoteNumber] = true;
    
    if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        // VST3 (local) - direct MIDI processing
        juce::MidiBuffer midiBuffer;
        midiBuffer.addEvent (juce::MidiMessage::noteOn (1, midiNoteNumber, velocity), 0);
        
        juce::AudioBuffer<float> tempBuffer (2, 512);
        tempBuffer.clear();
        plugin->processBlock (tempBuffer, midiBuffer);
    }
    else if (bridgePluginLoaded && bridgeManager.isAvailable())
    {
        // VST2 (bridge) - IPC MIDI with maqam pitch bend for Arabic quarter tones
        juce::MidiBuffer midiBuffer;
        midiBuffer.addEvent (juce::MidiMessage::noteOn (1, midiNoteNumber, velocity), 0);
        
        // Apply maqam pitch bend for Arabic quarter tones (critical for black keys!)
        maqamManager.processMidiBuffer (midiBuffer, 1);
        
        // Send all MIDI messages (note + pitch bend) to VST2 via bridge
        // IMPORTANT: Maintain strict ordering - pitch bend BEFORE note on
        for (const auto metadata : midiBuffer)
        {
            const auto msg = metadata.getMessage();
            const int status = msg.getRawData()[0] & 0xF0;
            const int note = msg.getNoteNumber();
            const int vel = msg.getVelocity();
            
            if (status == 0x90)  // Note On
            {
                const int midiMessage = (0x90 << 16) | ((note & 0x7F) << 8) | (vel & 0x7F);
                DEBUG_LOG ("MidiKeyboard: Sending note ON to VST2 - note=" + juce::String (note));
                bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::processMidi, juce::String (midiMessage) });
            }
            else if (status == 0x80)  // Note Off
            {
                const int midiMessage = (0x80 << 16) | ((note & 0x7F) << 8) | (vel & 0x7F);
                DEBUG_LOG ("MidiKeyboard: Sending note OFF to VST2 - note=" + juce::String (note));
                bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::processMidi, juce::String (midiMessage) });
            }
            else if (status == 0xE0)  // Pitch Bend
            {
                const int pitchValue = msg.getPitchWheelValue();
                const int lsb = pitchValue & 0x7F;
                const int msb = (pitchValue >> 7) & 0x7F;
                const int pitchMessage = (0xE0 << 16) | (lsb << 8) | msb;
                
                DEBUG_LOG ("MidiKeyboard: Sending pitch bend to VST2 - value=" + juce::String (pitchValue));
                bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::processMidi, juce::String (pitchMessage) });
            }
        }
    }
    else
    {
        DEBUG_LOG ("MidiKeyboard: No plugin loaded - bridgePluginLoaded=" 
                  + juce::String (bridgePluginLoaded ? "true" : "false") 
                  + " bridgeManager.isAvailable=" 
                  + juce::String (bridgeManager.isAvailable() ? "true" : "false"));
    }
}

void MainComponent::handleMidiNoteOff (int midiNoteNumber, float velocity)
{
    DEBUG_LOG ("MidiKeyboard: Note OFF - note=" + juce::String (midiNoteNumber));
    
    // Mark this note as inactive
    if (midiNoteNumber >= 0 && midiNoteNumber < 128)
        activeNotes[midiNoteNumber] = false;
    
    if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        // VST3 (local) - direct MIDI processing
        juce::MidiBuffer midiBuffer;
        midiBuffer.addEvent (juce::MidiMessage::noteOff (1, midiNoteNumber, velocity), 0);
        
        juce::AudioBuffer<float> tempBuffer (2, 512);
        tempBuffer.clear();
        plugin->processBlock (tempBuffer, midiBuffer);
    }
    else if (bridgePluginLoaded && bridgeManager.isAvailable())
    {
        // VST2 (bridge) - IPC MIDI
        const int midiMessage = (0x80 << 16) | ((midiNoteNumber & 0x7F) << 8) | (static_cast<int>(velocity) & 0x7F);
        bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::processMidi, juce::String (midiMessage) });
    }
}

void MainComponent::allNotesOff()
{
    DEBUG_LOG ("MidiKeyboard: Clearing all active notes before program change");
    
    // Send Note-OFF for all currently playing notes
    for (int noteNum = 0; noteNum < 128; ++noteNum)
    {
        if (activeNotes[noteNum])
        {
            if (auto* plugin = pluginManager.getLoadedPlugin())
            {
                // VST3 (local)
                juce::MidiBuffer midiBuffer;
                midiBuffer.addEvent (juce::MidiMessage::noteOff (1, noteNum, 0.0f), 0);
                
                juce::AudioBuffer<float> tempBuffer (2, 512);
                tempBuffer.clear();
                plugin->processBlock (tempBuffer, midiBuffer);
            }
            else if (bridgePluginLoaded && bridgeManager.isAvailable())
            {
                // VST2 (bridge) - IPC MIDI
                const int midiMessage = (0x80 << 16) | ((noteNum & 0x7F) << 8) | 0x00;
                bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::processMidi, juce::String (midiMessage) });
            }
            
            // Mark as inactive
            activeNotes[noteNum] = false;
        }
    }

    if (bridgePluginLoaded && bridgeManager.isAvailable())
    {
        // MIDI CC 123 (All Notes Off), CC 120 (All Sound Off), then center pitch bend
        const int allNotesOffMessage = (0xB0 << 16) | (123 << 8) | 0;
        const int allSoundOffMessage = (0xB0 << 16) | (120 << 8) | 0;
        const int pitchCenterMessage = (0xE0 << 16) | (0x00 << 8) | 0x40; // 8192 center

        bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::processMidi, juce::String (allNotesOffMessage) });
        bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::processMidi, juce::String (allSoundOffMessage) });
        bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::processMidi, juce::String (pitchCenterMessage) });
    }
}

void MainComponent::refreshKeyboardProgramNoteLabels()
{
    if (midiKeyboard == nullptr)
        return;

    CustomMidiKeyboardComponent::NoteLabelArray labels;

    const auto& intervalMap = myapp::music::OrientalScaleManager::getIntervalMap (maqamManager.getMaqam());
    for (int semitone = 0; semitone < 12; ++semitone)
    {
        auto label = getBaseNoteLabel (semitone);
        const float cents = intervalMap.centsOffset[(size_t) semitone];

        if (std::abs (cents + 50.0f) < 0.1f)
            label += "-";
        else if (std::abs (cents - 50.0f) < 0.1f)
            label += "+";

        labels[(size_t) semitone] = label;
    }

    midiKeyboard->setNoteClassLabels (labels);
}

void MainComponent::refreshKeyboardHighlights()
{
    if (midiKeyboard == nullptr)
        return;

    using myapp::music::OrientalScaleManager;
    using myapp::music::MaqamPreset;

    const MaqamPreset preset   = maqamManager.getMaqam();
    const auto scaleNotes      = OrientalScaleManager::getScaleNotes (preset);
    const juce::Colour rootCol = OrientalScaleManager::getMaqamRootColour (preset);

    // Root semitone per maqam: Bayati=D(2), Rast=C(0), Hijaz=D(2), Sika=E(4),
    //                          Ajam=C(0), Nahawand=C(0), Saba=D(2), Kurd=D(2)
    static constexpr int kRootSemitone[(int) MaqamPreset::count] = { 2, 0, 2, 4, 0, 0, 2, 2 };
    const int rootSemitone = kRootSemitone[(int) preset];

    const auto& intervalMap = OrientalScaleManager::getIntervalMap (preset);

    CustomMidiKeyboardComponent::NoteColourArray colours;
    colours.fill (juce::Colours::transparentBlack);

    for (int s = 0; s < 12; ++s)
    {
        if (s == rootSemitone)
        {
            // Root: full saturated signature colour
            colours[(size_t) s] = rootCol;
        }
        else if (scaleNotes[(size_t) s])
        {
            const float cents = intervalMap.centsOffset[(size_t) s];
            if (std::abs (cents + 50.0f) < 0.1f)
            {
                // Microtonal (quarter-tone flat) note: warm orange tint
                colours[(size_t) s] = juce::Colour (0xFFFF8C00).interpolatedWith (rootCol, 0.25f);
            }
            else
            {
                // Regular in-scale note: light tint of root colour
                colours[(size_t) s] = rootCol.withSaturation (rootCol.getSaturation() * 0.6f)
                                              .withBrightness (0.85f)
                                              .withAlpha (0.75f);
            }
        }
        // Out-of-scale remains transparentBlack (no highlight)
    }

    midiKeyboard->setNoteHighlightColours (colours);
}

void MainComponent::playDemoMelody()
{
    if (midiKeyboard == nullptr)
        return;

    // Ensure Bayati is selected so highlights + pitch-bend are correct
    using myapp::music::MaqamPreset;
    if (maqamManager.getMaqam() != MaqamPreset::bayati)
    {
        maqamManager.setMaqam (MaqamPreset::bayati);
        uiController.getMaqamSelector()->setSelectedId (1, juce::sendNotification);
    }

    // Scroll keyboard to show D3 (MIDI 62)
    midiKeyboard->setLowestVisibleKey (55); // G2 — a few keys before D3
    midiKeyboard->clearDemoNotes();

    const auto tonicAccentColour = juce::Colour (0xFFE2C46B);
    const auto defaultDemoColour = juce::Colour (0xFF9FD8B5);

    melodyPlayer.setCallbacks (
        [this, tonicAccentColour, defaultDemoColour] (int note, float vel)
        {
            // Fire note visually on keyboard
            if (midiKeyboard != nullptr)
            {
                const auto accent = (note % 12 == 2) ? tonicAccentColour : defaultDemoColour;
                midiKeyboard->setDemoNoteActive (note, true, accent);
            }

            keyboardState.noteOn (1, note, vel);
            // Route through normal MIDI path (includes maqam pitch-bend)
            handleMidiNoteOn (note, vel);
        },
        [this] (int note, float /*vel*/)
        {
            if (midiKeyboard != nullptr)
                midiKeyboard->setDemoNoteActive (note, false);

            keyboardState.noteOff (1, note, 0.f);
            handleMidiNoteOff (note, 0.f);
        },
        [this]
        {
            stopDemoMelody();
        }
    );

    melodyPlayer.playBayatiDemo();

    // Toggle button label while playing
    if (auto* btn = uiController.getDemoMelodyButton())
        btn->setButtonText ("Stop Play Byati");
}

void MainComponent::stopDemoMelody()
{
    melodyPlayer.stop();

    if (midiKeyboard != nullptr)
        midiKeyboard->clearDemoNotes();

    if (auto* btn = uiController.getDemoMelodyButton())
        btn->setButtonText ("Play Bayati Demo");
}

