#pragma once

#include <JuceHeader.h>
#include <array>

namespace myapp::music
{
enum class MaqamPreset
{
    bayati = 0,
    rast,
    hijaz,
    sika,
    ajam,
    nahawand,
    saba,
    kurd,
    count   // sentinel -- keep last
};

/** Per-semitone-class cent offsets (index 0=C .. 11=B).
    Positive = sharper, negative = flatter vs 12-TET.
    Quarter-tone = +/-50 cents. */
struct MaqamIntervalMap
{
    const char*           name;
    std::array<float, 12> centsOffset;
};

class OrientalScaleManager
{
public:
    void           setMaqam      (MaqamPreset preset);
    MaqamPreset    getMaqam      () const;
    juce::String   getMaqamName  () const;

    /** Injects pitch-bend before note-on and resets it after note-off. */
    void processMidiBuffer (juce::MidiBuffer& midi, int channel = 1) const;

    static const MaqamIntervalMap& getIntervalMap (MaqamPreset preset);

    /** Returns a 12-element bool array: true = semitone belongs to the maqam scale. */
    static std::array<bool, 12> getScaleNotes (MaqamPreset preset);

    /** Returns the signature UI colour for this maqam (saturated root colour). */
    static juce::Colour getMaqamRootColour (MaqamPreset preset);

private:
    float getCentsForNote (int midiNote) const;

    MaqamPreset currentMaqam { MaqamPreset::bayati };
};
} // namespace myapp::music
