#include "SequencerPanel.h"

//==============================================================================
// Constructor / Destructor
//==============================================================================

SequencerPanel::SequencerPanel()
{
    // Create child components
    channelRack = std::make_unique<ChannelRackComponent>();
    pianoRoll = std::make_unique<PianoRollComponent>();

    addAndMakeVisible (channelRack.get());
    addAndMakeVisible (pianoRoll.get());

    // Setup callbacks
    setupChannelRackCallbacks();
    setupPianoRollCallbacks();
}

SequencerPanel::~SequencerPanel()
{
}

//==============================================================================
// Component Overrides
//==============================================================================

void SequencerPanel::paint (juce::Graphics& g)
{
    // Background
    g.fillAll (juce::Colour (0xFF1A1A1A));
}

void SequencerPanel::resized()
{
    // Split layout: channel rack on left, piano roll on right
    const int dividerWidth = 2;

    // Channel rack on the left
    channelRack->setBounds (0, 0, channelRackWidth, getHeight());

    // Piano roll on the right
    pianoRoll->setBounds (channelRackWidth + dividerWidth, 0,
                         getWidth() - channelRackWidth - dividerWidth, getHeight());
}

//==============================================================================
// Component Access
//==============================================================================

void SequencerPanel::setChannelRackWidth (int width)
{
    channelRackWidth = juce::jlimit (100, getWidth() - 200, width);
    resized();
}

//==============================================================================
// Playback Control
//==============================================================================

void SequencerPanel::setIsPlaying (bool shouldPlay)
{
    isPlaying = shouldPlay;
    pianoRoll->setIsPlaying (shouldPlay);
}

void SequencerPanel::setPlaybackStep (int step)
{
    pianoRoll->setPlaybackStep (step);
}

//==============================================================================
// Sequence Management
//==============================================================================

void SequencerPanel::clearSequence()
{
    pianoRoll->clearNotes();
    if (onSequenceChanged)
        onSequenceChanged();
}

std::vector<int> SequencerPanel::getSequenceAsNotes() const
{
    std::vector<int> noteSequence;
    for (const auto& note : pianoRoll->getNotes())
    {
        // Convert piano roll notes to simple MIDI note list
        // (In a real app, you'd want to handle durations and velocities)
        noteSequence.push_back (note.midiNote);
    }
    return noteSequence;
}

void SequencerPanel::loadSequence (const std::vector<PianoRollComponent::Note>& sequence)
{
    pianoRoll->clearNotes();
    for (const auto& note : sequence)
    {
        pianoRoll->addNote (note);
    }
}

//==============================================================================
// Internal Helpers
//==============================================================================

void SequencerPanel::setupChannelRackCallbacks()
{
    if (!channelRack)
        return;

    channelRack->onChannelSelected = [this] (int channelIndex)
    {
        // Update piano roll to show notes for this channel
        // In a multi-channel design, each channel would have its own sequence
        juce::ignoreUnused (channelIndex);
    };

    channelRack->onChannelMuteToggled = [this] (int channelIndex, bool muted)
    {
        juce::ignoreUnused (channelIndex, muted);
        // Could suppress MIDI output for muted channels here
    };

    channelRack->onChannelSoloToggled = [this] (int channelIndex, bool soloed)
    {
        juce::ignoreUnused (channelIndex, soloed);
        // Could mute all other channels when solo is engaged
    };
}

void SequencerPanel::setupPianoRollCallbacks()
{
    if (!pianoRoll)
        return;

    pianoRoll->onNoteAdded = [this] (const PianoRollComponent::Note& note)
    {
        juce::ignoreUnused (note);
        if (onSequenceChanged)
            onSequenceChanged();
    };

    pianoRoll->onNoteRemoved = [this] (int noteIndex)
    {
        juce::ignoreUnused (noteIndex);
        if (onSequenceChanged)
            onSequenceChanged();
    };

    pianoRoll->onNoteEdited = [this] (int noteIndex, const PianoRollComponent::Note& note)
    {
        juce::ignoreUnused (noteIndex, note);
        if (onSequenceChanged)
            onSequenceChanged();
    };
}
