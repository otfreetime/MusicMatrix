#include "PianoRollComponent.h"
#include "../debug/DebugLogger.h"

//==============================================================================
// Constructor / Destructor
//==============================================================================

PianoRollComponent::PianoRollComponent()
{
    DEBUG_LOG ("PianoRollComponent: Constructor started");
    
    // Create scroll bars
    DEBUG_LOG ("PianoRollComponent: Creating horizontal scroll...");
    horizontalScroll = std::make_unique<juce::ScrollBar> (true);  // true = horizontal
    DEBUG_LOG ("PianoRollComponent: Horizontal scroll created");
    
    DEBUG_LOG ("PianoRollComponent: Creating vertical scroll...");
    verticalScroll = std::make_unique<juce::ScrollBar> (false);   // false = vertical
    DEBUG_LOG ("PianoRollComponent: Vertical scroll created");
    
    DEBUG_LOG ("PianoRollComponent: Adding scroll listeners...");
    horizontalScroll->addListener (this);
    verticalScroll->addListener (this);
    
    DEBUG_LOG ("PianoRollComponent: Adding horizontal scroll to parent...");
    addAndMakeVisible (horizontalScroll.get());
    DEBUG_LOG ("PianoRollComponent: Horizontal scroll added");
    
    DEBUG_LOG ("PianoRollComponent: Adding vertical scroll to parent...");
    addAndMakeVisible (verticalScroll.get());
    DEBUG_LOG ("PianoRollComponent: Vertical scroll added");
    
    DEBUG_LOG ("PianoRollComponent: Constructor finished");
}

PianoRollComponent::~PianoRollComponent()
{
    if (horizontalScroll)
        horizontalScroll->removeListener (this);
    if (verticalScroll)
        verticalScroll->removeListener (this);
}

//==============================================================================
// Component Overrides
//==============================================================================

void PianoRollComponent::paint (juce::Graphics& g)
{
    // Background
    g.fillAll (juce::Colour (0xFF1A1A1A));

    drawPianoRoll (g);
    drawKeyLabels (g);
    drawTimeline (g);
    drawNotes (g);
}

void PianoRollComponent::resized()
{
    const int scrollBarHeight = 12;
    const int scrollBarWidth = 12;
    const int contentWidth = getWidth() - KEY_LABEL_WIDTH - scrollBarWidth;
    const int contentHeight = getHeight() - TIMELINE_HEIGHT - scrollBarHeight;

    // Horizontal scroll bar
    horizontalScroll->setBounds (KEY_LABEL_WIDTH, getHeight() - scrollBarHeight,
                                 contentWidth, scrollBarHeight);

    // Vertical scroll bar
    verticalScroll->setBounds (getWidth() - scrollBarWidth, TIMELINE_HEIGHT,
                              scrollBarWidth, contentHeight);

    // Update scroll ranges
    const int totalSteps = numSteps;
    const int visibleSteps = contentWidth / STEP_COLUMN_WIDTH;
    if (totalSteps > visibleSteps)
        horizontalScroll->setRangeLimits (0, (totalSteps - visibleSteps) * STEP_COLUMN_WIDTH);
    else
        horizontalScroll->setRangeLimits (0, 0);

    const int totalNoteRows = highestVisibleNote - lowestVisibleNote + 1;
    const int visibleNoteRows = contentHeight / NOTE_ROW_HEIGHT;
    if (totalNoteRows > visibleNoteRows)
        verticalScroll->setRangeLimits (0, (totalNoteRows - visibleNoteRows) * NOTE_ROW_HEIGHT);
    else
        verticalScroll->setRangeLimits (0, 0);
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    // Check if clicking on existing note (for selection/editing)
    int clickedNoteIdx = getNoteAtPosition (pos);
    if (clickedNoteIdx >= 0)
    {
        selectedNoteIndex = clickedNoteIdx;
        repaint();
        return;
    }

    // Otherwise, add a new note at this position
    if (pos.getX() > KEY_LABEL_WIDTH && pos.getY() > TIMELINE_HEIGHT)
    {
        const int midiNote = getNotePitchAtPosition (pos);
        const int startStep = getStepIndexAtPosition (pos);

        if (midiNote >= 0 && midiNote < 128 && startStep >= 0 && startStep < numSteps)
        {
            Note newNote;
            newNote.midiNote = midiNote;
            newNote.startStep = startStep;
            newNote.lengthSteps = 1;
            newNote.velocity = 100;

            addNote (newNote);
            if (onNoteAdded)
                onNoteAdded (newNote);
        }
    }
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (selectedNoteIndex < 0 || selectedNoteIndex >= static_cast<int> (notes.size()))
        return;

    // Allow resizing note length by dragging right edge
    const int pos = e.getPosition().getX();
    const Note& note = notes[selectedNoteIndex];
    const auto noteBounds = getNoteScreenBounds (note);
    const int distFromRight = noteBounds.getRight() - pos;

    if (distFromRight > -10 && distFromRight < 10)  // Near right edge
    {
        editMode = EditMode::RESIZE_END;
        const int currentStep = getStepIndexAtPosition (e.getPosition());
        int newLength = currentStep - note.startStep + 1;
        if (newLength < 1)
            newLength = 1;
        notes[selectedNoteIndex].lengthSteps = newLength;
        repaint();
    }
}

void PianoRollComponent::mouseUp (const juce::MouseEvent& e)
{
    if (selectedNoteIndex >= 0 && selectedNoteIndex < static_cast<int> (notes.size()))
    {
        if (onNoteEdited)
            onNoteEdited (selectedNoteIndex, notes[selectedNoteIndex]);
    }
    editMode = EditMode::NONE;
}

void PianoRollComponent::mouseMove (const juce::MouseEvent& e)
{
    if (selectedNoteIndex < 0 || selectedNoteIndex >= static_cast<int> (notes.size()))
        return;

    const auto noteBounds = getNoteScreenBounds (notes[selectedNoteIndex]);
    const int distFromRight = noteBounds.getRight() - e.getPosition().getX();

    if (distFromRight > -10 && distFromRight < 10)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

//==============================================================================
// Note Management
//==============================================================================

void PianoRollComponent::addNote (const Note& note)
{
    notes.push_back (note);
    repaint();
}

void PianoRollComponent::removeNote (int noteIndex)
{
    if (noteIndex >= 0 && noteIndex < static_cast<int> (notes.size()))
    {
        notes.erase (notes.begin() + noteIndex);
        if (selectedNoteIndex == noteIndex)
            selectedNoteIndex = -1;
        repaint();
    }
}

void PianoRollComponent::clearNotes()
{
    notes.clear();
    selectedNoteIndex = -1;
    repaint();
}

int PianoRollComponent::getNoteAtPosition (juce::Point<int> pos) const
{
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (getNoteScreenBounds (notes[i]).contains (pos))
            return static_cast<int> (i);
    }
    return -1;
}

//==============================================================================
// Playback
//==============================================================================

void PianoRollComponent::setPlaybackStep (int step)
{
    playbackStep = step;
    repaint();
}

void PianoRollComponent::setIsPlaying (bool shouldPlay)
{
    isPlaying = shouldPlay;
    repaint();
}

//==============================================================================
// Grid Configuration
//==============================================================================

void PianoRollComponent::setVisibleNoteRange (int lowestNote, int highestNote)
{
    lowestVisibleNote = lowestNote;
    highestVisibleNote = highestNote;
    resized();
    repaint();
}

//==============================================================================
// ScrollBar::Listener
//==============================================================================

void PianoRollComponent::scrollBarMoved (juce::ScrollBar* scrollBarThatMoved, double newRangeStart)
{
    if (scrollBarThatMoved == horizontalScroll.get())
    {
        horizontalScrollPos = newRangeStart;
        repaint();
    }
    else if (scrollBarThatMoved == verticalScroll.get())
    {
        verticalScrollPos = newRangeStart;
        repaint();
    }
}

//==============================================================================
// Internal Helpers
//==============================================================================

juce::Rectangle<int> PianoRollComponent::getNoteScreenBounds (const Note& note) const
{
    const auto noteLane = getNoteLaneScreenBounds (note.midiNote);
    const auto startStepBounds = getStepColumnScreenBounds (note.startStep);
    const auto endStepBounds = getStepColumnScreenBounds (note.startStep + note.lengthSteps - 1);

    return juce::Rectangle<int> (startStepBounds.getX(), noteLane.getY(),
                                 endStepBounds.getRight() - startStepBounds.getX(),
                                 noteLane.getHeight());
}

juce::Rectangle<int> PianoRollComponent::getNoteLaneScreenBounds (int midiNote) const
{
    if (midiNote < lowestVisibleNote || midiNote > highestVisibleNote)
        return juce::Rectangle<int>();

    const int scrollBarWidth = 12;
    const int contentHeight = getHeight() - TIMELINE_HEIGHT - scrollBarWidth;
    const int noteOffset = midiNote - lowestVisibleNote;
    const int screenY = TIMELINE_HEIGHT + (noteOffset * NOTE_ROW_HEIGHT)
                        - static_cast<int> (verticalScrollPos);

    return juce::Rectangle<int> (KEY_LABEL_WIDTH, screenY, 
                                 getWidth() - KEY_LABEL_WIDTH - scrollBarWidth,
                                 NOTE_ROW_HEIGHT);
}

juce::Rectangle<int> PianoRollComponent::getStepColumnScreenBounds (int stepIndex) const
{
    const int scrollBarHeight = 12;
    const int screenX = KEY_LABEL_WIDTH + (stepIndex * STEP_COLUMN_WIDTH)
                        - static_cast<int> (horizontalScrollPos);

    return juce::Rectangle<int> (screenX, TIMELINE_HEIGHT,
                                 STEP_COLUMN_WIDTH,
                                 getHeight() - TIMELINE_HEIGHT - scrollBarHeight);
}

int PianoRollComponent::getNotePitchAtPosition (juce::Point<int> pos) const
{
    const int relativeY = pos.getY() - TIMELINE_HEIGHT + static_cast<int> (verticalScrollPos);
    const int noteOffset = relativeY / NOTE_ROW_HEIGHT;
    return lowestVisibleNote + noteOffset;
}

int PianoRollComponent::getStepIndexAtPosition (juce::Point<int> pos) const
{
    const int relativeX = pos.getX() - KEY_LABEL_WIDTH + static_cast<int> (horizontalScrollPos);
    return relativeX / STEP_COLUMN_WIDTH;
}

void PianoRollComponent::drawPianoRoll (juce::Graphics& g)
{
    const int scrollBarWidth = 12;
    const int scrollBarHeight = 12;
    const int contentWidth = getWidth() - KEY_LABEL_WIDTH - scrollBarWidth;
    const int contentHeight = getHeight() - TIMELINE_HEIGHT - scrollBarHeight;

    // Draw grid
    g.setColour (juce::Colour (0xFF333333));

    // Vertical lines (steps)
    for (int step = 0; step <= numSteps; ++step)
    {
        const auto bounds = getStepColumnScreenBounds (step);
        if (bounds.getX() >= KEY_LABEL_WIDTH && bounds.getX() < getWidth() - scrollBarWidth)
        {
            // Darker lines every 4 steps (beat)
            if (step % 4 == 0)
                g.setColour (juce::Colour (0xFF555555));
            else
                g.setColour (juce::Colour (0xFF333333));
            g.drawVerticalLine (bounds.getX(), TIMELINE_HEIGHT, getHeight() - scrollBarHeight);
        }
    }

    // Horizontal lines (notes)
    for (int note = lowestVisibleNote; note <= highestVisibleNote; ++note)
    {
        const auto bounds = getNoteLaneScreenBounds (note);
        if (!bounds.isEmpty() && bounds.getY() >= TIMELINE_HEIGHT)
        {
            g.setColour (juce::Colour (0xFF333333));
            g.drawHorizontalLine (bounds.getBottom() - 1, KEY_LABEL_WIDTH, getWidth() - scrollBarWidth);
        }
    }
}

void PianoRollComponent::drawTimeline (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xFF2A2A2A));
    g.fillRect (0, 0, getWidth(), TIMELINE_HEIGHT);

    g.setColour (juce::Colour (0xFF666666));
    g.drawHorizontalLine (TIMELINE_HEIGHT - 1, 0, getWidth());

    // Draw step numbers
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::Font (10.0f));

    for (int step = 0; step < numSteps; step += 4)
    {
        const auto bounds = getStepColumnScreenBounds (step);
        if (bounds.getX() >= KEY_LABEL_WIDTH && bounds.getX() < getWidth())
        {
            g.drawText (juce::String (step), bounds.withHeight (TIMELINE_HEIGHT),
                       juce::Justification::centred);
        }
    }
}

void PianoRollComponent::drawKeyLabels (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xFF2A2A2A));
    g.fillRect (0, 0, KEY_LABEL_WIDTH, getHeight());

    g.setColour (juce::Colour (0xFF666666));
    g.drawVerticalLine (KEY_LABEL_WIDTH - 1, TIMELINE_HEIGHT, getHeight());

    // Draw note names
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (10.0f));

    for (int note = lowestVisibleNote; note <= highestVisibleNote; note += 2)
    {
        const auto bounds = getNoteLaneScreenBounds (note);
        if (!bounds.isEmpty())
        {
            g.drawText (getMidiNoteName (note), 
                       juce::Rectangle<int> (0, bounds.getY(), KEY_LABEL_WIDTH, NOTE_ROW_HEIGHT),
                       juce::Justification::centred);
        }
    }
}

void PianoRollComponent::drawNotes (juce::Graphics& g)
{
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& note = notes[i];
        const auto noteBounds = getNoteScreenBounds (note);

        if (noteBounds.isEmpty() || !noteBounds.intersects (getLocalBounds()))
            continue;

        // Highlight selected note
        if (static_cast<int> (i) == selectedNoteIndex)
            g.setColour (juce::Colours::yellow);
        else
            g.setColour (juce::Colour (0xFF6B9BD1));  // Light blue

        g.fillRect (noteBounds);

        // Draw border
        g.setColour (juce::Colours::white);
        g.drawRect (noteBounds, 1);

        // Draw velocity indicator (height of bottom bar)
        const int velocityBarHeight = juce::jmax (1, (note.velocity * noteBounds.getHeight()) / 127);
        g.setColour (juce::Colours::green);
        auto velocityBounds = noteBounds;
        g.fillRect (velocityBounds.removeFromBottom (velocityBarHeight));
    }

    // Draw playback cursor
    if (isPlaying)
    {
        const auto cursorBounds = getStepColumnScreenBounds (playbackStep);
        g.setColour (juce::Colours::red);
        g.drawVerticalLine (cursorBounds.getX(), TIMELINE_HEIGHT, getHeight() - 12);
    }
}

juce::String PianoRollComponent::getMidiNoteName (int noteNumber)
{
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = (noteNumber / 12) - 1;
    const int note = noteNumber % 12;
    return juce::String (noteNames[note]) + juce::String (octave);
}
