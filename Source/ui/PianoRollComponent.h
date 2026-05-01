#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include <memory>
#include <map>

/**
 * PianoRollComponent
 * Grid-based piano roll editor for composing melodies.
 * - Rows represent MIDI notes (C0-G9, 128 notes)
 * - Columns represent time steps (16th notes)
 * - Users can click to add notes, drag to adjust length
 */
class PianoRollComponent : public juce::Component,
                           public juce::ScrollBar::Listener
{
public:
    //==========================================================================
    // Note Representation
    //==========================================================================
    struct Note
    {
        int midiNote = 60;       // 0-127
        int startStep = 0;       // Column index
        int lengthSteps = 1;     // Duration in steps
        int velocity = 100;      // 0-127
        float pitchBendCents = 0.0f;  // For microtonals (-50 to +50)
    };

    //==========================================================================
    // Constructor / Destructor
    //==========================================================================
    PianoRollComponent();
    ~PianoRollComponent() override;

    //==========================================================================
    // Component Overrides
    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

    //==========================================================================
    // Note Management
    //==========================================================================
    /** Add a note to the current sequence */
    void addNote (const Note& note);

    /** Remove a note by index */
    void removeNote (int noteIndex);

    /** Clear all notes */
    void clearNotes();

    /** Get all notes in the sequence */
    const std::vector<Note>& getNotes() const { return notes; }
    std::vector<Note>& getNotes() { return notes; }

    /** Get note at position, or -1 if none */
    int getNoteAtPosition (juce::Point<int> pos) const;

    //==========================================================================
    // Playback
    //==========================================================================
    /** Set current playback step (for visual feedback) */
    void setPlaybackStep (int step);
    int getPlaybackStep() const { return playbackStep; }

    /** Start/stop playback */
    void setIsPlaying (bool shouldPlay);
    bool getIsPlaying() const { return isPlaying; }

    //==========================================================================
    // Grid Configuration
    //==========================================================================
    void setNumSteps (int numSteps) { this->numSteps = numSteps; resized(); repaint(); }
    int getNumSteps() const { return numSteps; }

    void setStepLengthMs (int ms) { stepLengthMs = ms; }
    int getStepLengthMs() const { return stepLengthMs; }

    /** Set visible MIDI note range */
    void setVisibleNoteRange (int lowestNote, int highestNote);
    int getLowestVisibleNote() const { return lowestVisibleNote; }
    int getHighestVisibleNote() const { return highestVisibleNote; }

    //==========================================================================
    // Callbacks
    //==========================================================================
    /** Called when user adds a note */
    std::function<void (const Note&)> onNoteAdded = nullptr;

    /** Called when user removes a note */
    std::function<void (int noteIndex)> onNoteRemoved = nullptr;

    /** Called when user edits a note */
    std::function<void (int noteIndex, const Note&)> onNoteEdited = nullptr;

    //==========================================================================
    // ScrollBar::Listener override
    //==========================================================================
    void scrollBarMoved (juce::ScrollBar* scrollBarThatMoved, double newRangeStart) override;

    //==========================================================================
    // Layout Properties
    //==========================================================================
    static constexpr int NOTE_ROW_HEIGHT = 16;     // Height of each note row
    static constexpr int STEP_COLUMN_WIDTH = 20;   // Width of each time step
    static constexpr int KEY_LABEL_WIDTH = 50;     // Width for note labels on left
    static constexpr int TIMELINE_HEIGHT = 25;     // Height of timeline at top

private:
    //==========================================================================
    // Internal Helpers
    //==========================================================================
    /** Get screen bounds for a note */
    juce::Rectangle<int> getNoteScreenBounds (const Note& note) const;

    /** Get screen bounds for a MIDI note row */
    juce::Rectangle<int> getNoteLaneScreenBounds (int midiNote) const;

    /** Get screen bounds for a time step column */
    juce::Rectangle<int> getStepColumnScreenBounds (int stepIndex) const;

    /** Convert screen position to MIDI note number */
    int getNotePitchAtPosition (juce::Point<int> pos) const;

    /** Convert screen position to step index */
    int getStepIndexAtPosition (juce::Point<int> pos) const;

    /** Draw the piano roll grid and notes */
    void drawPianoRoll (juce::Graphics& g);

    /** Draw the timeline (step numbers) */
    void drawTimeline (juce::Graphics& g);

    /** Draw the note key labels (left side) */
    void drawKeyLabels (juce::Graphics& g);

    /** Draw individual notes as rectangles */
    void drawNotes (juce::Graphics& g);

    /** Get MIDI note name from number */
    static juce::String getMidiNoteName (int noteNumber);

    //==========================================================================
    // Members
    //==========================================================================
    std::vector<Note> notes;
    int numSteps = 32;              // Default to 32 steps (2 bars at 16th notes)
    int stepLengthMs = 125;         // 120 BPM = 500ms per quarter, 125ms per 16th
    int lowestVisibleNote = 36;     // C2
    int highestVisibleNote = 84;    // C6
    int playbackStep = 0;
    bool isPlaying = false;

    // Scroll management
    std::unique_ptr<juce::ScrollBar> horizontalScroll;  // For steps
    std::unique_ptr<juce::ScrollBar> verticalScroll;    // For notes
    double horizontalScrollPos = 0.0;
    double verticalScrollPos = 0.0;

    // Editing state
    int selectedNoteIndex = -1;
    enum class EditMode { NONE, RESIZE_END };
    EditMode editMode = EditMode::NONE;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
