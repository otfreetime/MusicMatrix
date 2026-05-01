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

void MainComponent::initialiseKeyboardMapping()
{
    if (midiKeyboard == nullptr)
        return;
    
    // Clear any existing mappings
    midiKeyboard->clearKeyMappings();

    // JUCE computes: actualMidiNote = keyMappingOctave * 12 + midiNoteOffsetFromC.
    // UI labels use C0 at MIDI 12, so UI octave Cn maps to JUCE base octave (n + 1).
    midiKeyboard->setKeyPressBaseOctave (keyboardBaseOctave + 1);
    
    DEBUG_LOG ("Keyboard: setKeyPressBaseOctave called with value=" + juce::String (keyboardBaseOctave + 1) + " (expected MIDI C note=" + juce::String ((keyboardBaseOctave + 1) * 12) + ")");

    // White keys - home row (semitone offsets from C, 0-12)
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('a'), 0);   // C
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('s'), 2);   // D
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('d'), 4);   // E
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('f'), 5);   // F
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('g'), 7);   // G
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('h'), 9);   // A
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('j'), 11);  // B
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('k'), 12);  // C (next octave)

    // Black keys - top row (semitone offsets from C)
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('w'), 1);   // C#/Db
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('e'), 3);   // D#/Eb
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('t'), 6);   // F#/Gb
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('y'), 8);   // G#/Ab
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('u'), 10);  // A#/Bb
    
    DEBUG_LOG ("Keyboard: PC keyboard mapping initialized (base octave " + juce::String (keyboardBaseOctave) + ")");
    DEBUG_LOG ("Keyboard: White keys: A=C, S=D, D=E, F=F, G=G, H=A, J=B, K=C");
    DEBUG_LOG ("Keyboard: Black keys: W=C#, E=D#, T=F#, Y=G#, U=A#");
}

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

    using BayatiPlayMode = MelodyPlayer::BayatiPlayMode;

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

    BayatiPlayMode selectedMode = BayatiPlayMode::upAndDownScale;
    if (auto* modeSelector = uiController.getBayatiPlayModeSelector())
    {
        const int modeId = modeSelector->getSelectedId();
        selectedMode = (modeId == 1) ? BayatiPlayMode::upScale : BayatiPlayMode::upAndDownScale;
    }

    melodyPlayer.playBayatiDemo (selectedMode);

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

//==============================================================================
// Play "Tairi Ya Tayyara" Melody (G Bayati)
//==============================================================================

void MainComponent::playTairiYaTayyara()
{
    if (midiKeyboard == nullptr)
        return;

    // Switch to G Bayati (preset Bayati, will use G root via pitch transposition)
    using myapp::music::MaqamPreset;
    if (maqamManager.getMaqam() != MaqamPreset::bayati)
    {
        maqamManager.setMaqam (MaqamPreset::bayati);
        uiController.getMaqamSelector()->setSelectedId (1, juce::sendNotification);
    }

    // Scroll keyboard to show the melody range (G2 to C4, MIDI 67 to 72)
    midiKeyboard->setLowestVisibleKey (60); // C3 as reference
    midiKeyboard->clearDemoNotes();

    const auto melodyColour = juce::Colour (0xFFFFA500);  // Orange for Arabic melody

    melodyPlayer.setCallbacks (
        [this, melodyColour] (int note, float vel)
        {
            // Fire note visually on keyboard
            if (midiKeyboard != nullptr)
            {
                midiKeyboard->setDemoNoteActive (note, true, melodyColour);
            }

            keyboardState.noteOn (1, note, vel);
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
            stopTairiYaTayyara();
        }
    );

    // Tairi Ya Tayyara in G Bayati (transposed from D Bayati root)
    // G Bayati = D Bayati transposed up 5 semitones
    // MIDI notes: G=67, C=60, Bb=70, A=69, F=65, D=62, Ed=64 (with -50 cent pitch bend)
    // Based on the musical notation provided
    std::vector<MelodyPlayer::Step> tairiSequence;

    // Line 1: Main Theme Introduction
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 60, 300 }); // C (next octave)
    tairiSequence.push_back ({ 60, 300 }); // C

    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 60, 300 }); // C
    tairiSequence.push_back ({ 60, 300 }); // C

    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 65, 250 }); // F
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 70, 300 }); // Bb

    tairiSequence.push_back ({ 60, 250 }); // C
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 67, 400 }); // G (longer note)

    // Lines 2 & 3: Development
    tairiSequence.push_back ({ 60, 250 }); // C
    tairiSequence.push_back ({ 60, 250 }); // C
    tairiSequence.push_back ({ 62, 250 }); // D
    tairiSequence.push_back ({ 64, 250 }); // Ed (microtonal E-half-flat, pitch bend -50c)
    tairiSequence.push_back ({ 62, 250 }); // D
    tairiSequence.push_back ({ 60, 300 }); // C

    tairiSequence.push_back ({ 60, 250 }); // C
    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 65, 300 }); // F

    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 60, 300 }); // C
    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 67, 400 }); // G (conclusion)

    // Lines 4 & 5: Musical Bridge
    tairiSequence.push_back ({ 62, 250 }); // D
    tairiSequence.push_back ({ 62, 250 }); // D
    tairiSequence.push_back ({ 62, 250 }); // D
    tairiSequence.push_back ({ 62, 250 }); // D
    tairiSequence.push_back ({ 64, 250 }); // Ed
    tairiSequence.push_back ({ 65, 300 }); // F

    tairiSequence.push_back ({ 64, 250 }); // Ed
    tairiSequence.push_back ({ 62, 250 }); // D
    tairiSequence.push_back ({ 60, 250 }); // C
    tairiSequence.push_back ({ 60, 250 }); // C
    tairiSequence.push_back ({ 62, 250 }); // D
    tairiSequence.push_back ({ 64, 300 }); // Ed

    tairiSequence.push_back ({ 62, 250 }); // D
    tairiSequence.push_back ({ 60, 250 }); // C
    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 60, 250 }); // C
    tairiSequence.push_back ({ 62, 300 }); // D

    tairiSequence.push_back ({ 60, 250 }); // C
    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 60, 300 }); // C

    // Lines 6 & 7: Coda and Return
    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 65, 250 }); // F
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 70, 300 }); // Bb

    tairiSequence.push_back ({ 70, 250 }); // Bb
    tairiSequence.push_back ({ 69, 250 }); // A
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 65, 250 }); // F
    tairiSequence.push_back ({ 67, 250 }); // G
    tairiSequence.push_back ({ 69, 500 }); // A (longer)

    tairiSequence.push_back ({ 67, 300 }); // G (final note)
    tairiSequence.push_back ({ 67, 300 }); // G
    tairiSequence.push_back ({ 67, 800 }); // G (sustained final)

    // Store the sequence and start playback
    melodyPlayer.sequence = tairiSequence;
    melodyPlayer.stepIndex = 0;
    melodyPlayer.noteOnPhase = true;
    melodyPlayer.startTimer (10);

    DEBUG_LOG ("Keyboard: playTairiYaTayyara started - " + juce::String ((int) tairiSequence.size()) + " notes");

    // Update button label
    if (tairiYaTayyaraButton != nullptr)
        tairiYaTayyaraButton->setButtonText ("Stop Tairi Ya Tayyara");
}

void MainComponent::stopTairiYaTayyara()
{
    melodyPlayer.stop();

    if (midiKeyboard != nullptr)
        midiKeyboard->clearDemoNotes();

    if (tairiYaTayyaraButton != nullptr)
        tairiYaTayyaraButton->setButtonText ("Play Tairi Ya Tayyara");

    DEBUG_LOG ("Keyboard: playTairiYaTayyara stopped");
}

//==============================================================================
// Play "Salalem El-Nashh" Melody (D Bayati)
//==============================================================================

void MainComponent::playSalalemElNashh()
{
    if (midiKeyboard == nullptr)
        return;

    // Switch to Bayati maqam (D root)
    using myapp::music::MaqamPreset;
    if (maqamManager.getMaqam() != MaqamPreset::bayati)
    {
        maqamManager.setMaqam (MaqamPreset::bayati);
        uiController.getMaqamSelector()->setSelectedId (1, juce::sendNotification);
    }

    // Scroll keyboard to show the melody range (D3 to C5, MIDI 62 to 84)
    midiKeyboard->setLowestVisibleKey (55); // G2 as reference
    midiKeyboard->clearDemoNotes();

    const auto melodyColour = juce::Colour (0xFFFF6B6B);  // Coral/red for Salalem

    melodyPlayer.setCallbacks (
        [this, melodyColour] (int note, float vel)
        {
            if (midiKeyboard != nullptr)
                midiKeyboard->setDemoNoteActive (note, true, melodyColour);

            keyboardState.noteOn (1, note, vel);
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
            stopSalalemElNashh();
        }
    );

    // Salalem El-Nashh full melody - 132 notes from the sheet music
    // D Bayati with quarter-tone flats (Eb- and other microtonals)
    std::vector<MelodyPlayer::Step> salalemSequence;

    // Top section (8 lines)
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 63, 250 }); // Eb- (microtonal)
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D

    salalemSequence.push_back ({ 60, 250 }); // C
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 65, 500 }); // F (longer)
    salalemSequence.push_back ({ 67, 250 }); // G

    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 67, 500 }); // G (longer)
    salalemSequence.push_back ({ 69, 250 }); // A

    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 67, 500 }); // G (longer)
    salalemSequence.push_back ({ 69, 250 }); // A

    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 500 }); // D (longer)

    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 67, 500 }); // G (longer)
    salalemSequence.push_back ({ 70, 250 }); // Bb

    salalemSequence.push_back ({ 72, 250 }); // C
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 65, 500 }); // F (longer)
    salalemSequence.push_back ({ 67, 250 }); // G

    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 72, 250 }); // C
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 65, 500 }); // F (longer)
    salalemSequence.push_back ({ 67, 250 }); // G

    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 800 }); // D (final, held longer)

    // Bottom section (8 lines)
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 67, 500 }); // G (longer)
    salalemSequence.push_back ({ 65, 250 }); // F

    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 70, 500 }); // Bb (longer)
    salalemSequence.push_back ({ 72, 250 }); // C

    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 65, 500 }); // F (longer)

    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 60, 500 }); // C (longer)
    salalemSequence.push_back ({ 62, 250 }); // D

    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 67, 500 }); // G (longer)
    salalemSequence.push_back ({ 65, 250 }); // F

    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 72, 250 }); // C
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 67, 500 }); // G (longer)
    salalemSequence.push_back ({ 65, 250 }); // F

    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 67, 250 }); // G
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 70, 250 }); // Bb
    salalemSequence.push_back ({ 69, 250 }); // A
    salalemSequence.push_back ({ 67, 500 }); // G (longer)
    salalemSequence.push_back ({ 63, 250 }); // Eb-

    salalemSequence.push_back ({ 62, 250 }); // D (opening)
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 250 }); // D
    salalemSequence.push_back ({ 65, 250 }); // F
    salalemSequence.push_back ({ 63, 250 }); // Eb-
    salalemSequence.push_back ({ 62, 800 }); // D (final resolution, sustained)

    // Store and play the sequence
    melodyPlayer.sequence = salalemSequence;
    melodyPlayer.stepIndex = 0;
    melodyPlayer.noteOnPhase = true;
    melodyPlayer.startTimer (10);

    DEBUG_LOG ("Keyboard: playSalalemElNashh started - " + juce::String ((int) salalemSequence.size()) + " notes");

    if (salalemElNashhButton != nullptr)
        salalemElNashhButton->setButtonText ("Stop Salalem El-Nashh");
}

void MainComponent::stopSalalemElNashh()
{
    melodyPlayer.stop();

    if (midiKeyboard != nullptr)
        midiKeyboard->clearDemoNotes();

    if (salalemElNashhButton != nullptr)
        salalemElNashhButton->setButtonText ("Play Salalem El-Nashh");

    DEBUG_LOG ("Keyboard: playSalalemElNashh stopped");
}

