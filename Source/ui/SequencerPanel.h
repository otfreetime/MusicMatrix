#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ChannelRackComponent.h"
#include "PianoRollComponent.h"

/**
 * SequencerPanel
 * Main sequencer UI combining Channel Rack (left) and Piano Roll (right).
 * Manages layout and communication between components.
 */
class SequencerPanel : public juce::Component
{
public:
    //==========================================================================
    // Constructor / Destructor
    //==========================================================================
    SequencerPanel();
    ~SequencerPanel() override;

    //==========================================================================
    // Component Overrides
    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
    // Component Access
    //==========================================================================
    ChannelRackComponent* getChannelRack() { return channelRack.get(); }
    const ChannelRackComponent* getChannelRack() const { return channelRack.get(); }

    PianoRollComponent* getPianoRoll() { return pianoRoll.get(); }
    const PianoRollComponent* getPianoRoll() const { return pianoRoll.get(); }

    //==========================================================================
    // Layout Configuration
    //==========================================================================
    /** Set the split between channel rack and piano roll (0.0 - 1.0) */
    void setChannelRackWidth (int width);
    int getChannelRackWidth() const { return channelRackWidth; }

    //==========================================================================
    // Playback Control
    //==========================================================================
    void setIsPlaying (bool shouldPlay);
    bool getIsPlaying() const { return isPlaying; }

    void setPlaybackStep (int step);

    //==========================================================================
    // Sequence Management
    //==========================================================================
    /** Clear all notes from the piano roll */
    void clearSequence();

    /** Get the current sequence as MelodyPlayer steps */
    std::vector<int> getSequenceAsNotes() const;

    /** Load a sequence into the piano roll */
    void loadSequence (const std::vector<PianoRollComponent::Note>& sequence);

    //==========================================================================
    // Callbacks
    //==========================================================================
    /** Called when piano roll notes change */
    std::function<void()> onSequenceChanged = nullptr;

private:
    //==========================================================================
    // Internal Helpers
    //==========================================================================
    void setupChannelRackCallbacks();
    void setupPianoRollCallbacks();

    //==========================================================================
    // Members
    //==========================================================================
    std::unique_ptr<ChannelRackComponent> channelRack;
    std::unique_ptr<PianoRollComponent> pianoRoll;

    int channelRackWidth = 200;  // Default width for channel rack
    bool isPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerPanel)
};
