#include "OrientalScaleManager.h"
#include <cmath>

namespace myapp::music
{
// ---------------------------------------------------------------
// Interval tables  (semitone class 0=C ... 11=B)
// Cent deviations from 12-TET.
// Quarter-tone = +/-50 c, three-quarter-tone = +/-150 c.
//
// Theory references:
//   Arel-Ezgi-Uzdilek (AEU) system
//   Touma (1996) "The Music of the Arabs"
//   Marcus (1993) "Interface between Theory and Practice"
// ---------------------------------------------------------------
static const MaqamIntervalMap kMaps[] =
{
    //  Bayati on D: D  Eb- F  G  A  Bb  C
    //  Eb- = Eb lowered 50c (three-quarter tone from D)
    //  Bb- = Bb lowered 50c
    //    C     C#    D     Eb    E     F     F#    G     Ab    A     Bb    B
    { "Bayati",
      { 0.f,  0.f,  0.f, -50.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f, -50.f,  0.f } },

    //  Rast on C: C  D  E-  F  G  A  Bb-  C
    //  E-  = E lowered 50c; Bb- = Bb lowered 50c
    //    C     C#    D     Eb    E     F     F#    G     Ab    A     Bb    B
    { "Rast",
      { 0.f,  0.f,  0.f,  0.f, -50.f,  0.f,  0.f,  0.f,  0.f,  0.f, -50.f,  0.f } },

    //  Hijaz on D: D  Eb-  F#  G  A  Bb  C
    //  Augmented 2nd between Eb- and F#.
    //  Eb- = Eb lowered 50c; F# stays in 12-TET
    //    C     C#    D     Eb    E     F     F#    G     Ab    A     Bb    B
    { "Hijaz",
      { 0.f,  0.f,  0.f, -50.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f } },

    //  Sika on E-: E-  F  G  Ab-  Bb  C  D
    //  E-  = E lowered 50c; Ab- = Ab lowered 50c; B- = B lowered 50c
    //    C     C#    D     Eb    E     F     F#    G     Ab    A     Bb    B
    { "Sika",
      { 0.f,  0.f,  0.f,  0.f, -50.f,  0.f,  0.f,  0.f, -50.f,  0.f,  0.f, -50.f } },
};

static_assert ((int) MaqamPreset::count == 4,
               "kMaps[] must be updated when new MaqamPreset values are added");

// ---------------------------------------------------------------

void OrientalScaleManager::setMaqam (MaqamPreset preset)
{
    currentMaqam = preset;
}

MaqamPreset OrientalScaleManager::getMaqam() const
{
    return currentMaqam;
}

juce::String OrientalScaleManager::getMaqamName() const
{
    return juce::String (getIntervalMap (currentMaqam).name);
}

const MaqamIntervalMap& OrientalScaleManager::getIntervalMap (MaqamPreset preset)
{
    const int idx = juce::jlimit (0, (int) MaqamPreset::count - 1, (int) preset);
    return kMaps[idx];
}

float OrientalScaleManager::getCentsForNote (int midiNote) const
{
    const int semitone = midiNote % 12;
    return getIntervalMap (currentMaqam).centsOffset[(size_t) semitone];
}

void OrientalScaleManager::processMidiBuffer (juce::MidiBuffer& midi, int channel) const
{
    juce::MidiBuffer transformed;

    for (const auto metadata : midi)
    {
        const auto msg       = metadata.getMessage();
        const int  samplePos = metadata.samplePosition;

        if (msg.isNoteOn())
        {
            const float cents = getCentsForNote (msg.getNoteNumber());

            // Pitch-bend range assumed +/-200 cents (+/-2 semitones).
            // 8192 = centre; 0 = max flat; 16383 = max sharp.
            const float normalized = juce::jlimit (-1.0f, 1.0f, cents / 200.0f);
            const int   pitchVal   = juce::roundToInt (8192.0f + normalized * 8191.0f);
            transformed.addEvent (
                juce::MidiMessage::pitchWheel (channel, pitchVal), samplePos);
        }
        else if (msg.isNoteOff())
        {
            // Pass the note-off first, then reset pitch to centre
            transformed.addEvent (msg, samplePos);
            transformed.addEvent (
                juce::MidiMessage::pitchWheel (channel, 8192), samplePos + 1);
            continue;  // already added msg above
        }

        transformed.addEvent (msg, samplePos);
    }

    midi.swapWith (transformed);
}
} // namespace myapp::music
