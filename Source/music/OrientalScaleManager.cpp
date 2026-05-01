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

    //  Ajam on C: C  D  E  F  G  A  B  C  (pure major scale, no microtonals)
    //    C     C#    D     Eb    E     F     F#    G     Ab    A     Bb    B
    { "Ajam",
      { 0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f } },

    //  Nahawand on C: C  D  Eb  F  G  Ab  B  C  (harmonic minor)
    //    C     C#    D     Eb    E     F     F#    G     Ab    A     Bb    B
    { "Nahawand",
      { 0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f } },

    //  Saba on D: D  Eb-  F  Gb  A  Bb  C  (Gb = F#, all standard 12-TET except Eb-)
    //  Eb- = Eb lowered 50c
    //    C     C#    D     Eb    E     F     F#    G     Ab    A     Bb    B
    { "Saba",
      { 0.f,  0.f,  0.f, -50.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f } },

    //  Kurd on D: D  Eb  F  G  A  Bb  C  D  (standard Phrygian, no microtonals)
    //    C     C#    D     Eb    E     F     F#    G     Ab    A     Bb    B
    { "Kurd",
      { 0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f } },
};

static_assert ((int) MaqamPreset::count == 8,
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

// ---------------------------------------------------------------
// Scale membership: true = semitone is part of the maqam scale
// Order matches MaqamPreset enum: bayati, rast, hijaz, sika, ajam, nahawand, saba, kurd
// Semitone index:                  C   C#    D    Eb    E     F    F#    G    Ab    A    Bb    B
// ---------------------------------------------------------------
static const bool kScaleNotes[8][12] =
{
    // Bayati on D:   D  Eb- F  G  A  Bb  C
    /* bayati   */ { false,false, true, true,false, true,false, true,false, true, true, false },
    // Rast on C:     C  D  E-  F  G  A  Bb-
    /* rast     */ { true, false, true,false, true, true,false, true,false, true, true, false },
    // Hijaz on D:    D  Eb-  F#  G  A  Bb  C
    /* hijaz    */ { true, false, true, true,false,false, true, true,false, true, true, false },
    // Sika on E-:    E-  F  G  Ab-  Bb  C  D
    /* sika     */ { true, false, true,false, true, true,false, true, true,false, true, false },
    // Ajam on C:     C  D  E  F  G  A  B
    /* ajam     */ { true, false, true,false, true, true,false, true,false, true,false, true  },
    // Nahawand on C: C  D  Eb  F  G  Ab  B
    /* nahawand */ { true, false, true, true,false, true,false, true, true,false,false, true  },
    // Saba on D:     D  Eb-  F  Gb  A  Bb  C
    /* saba     */ { true,  true, true, true,false, true, true,false,false, true, true, false },
    // Kurd on D:     D  Eb  F  G  A  Bb  C
    /* kurd     */ { true, false, true, true,false, true,false, true,false, true, true, false },
};

// Signature root colours per maqam
static const juce::uint32 kRootColours[8] =
{
    0xFF8B5E3C,  // Bayati  — Oud Brown
    0xFF2E7D32,  // Rast    — Royal Green
    0xFFFFAB00,  // Hijaz   — Amber/Gold
    0xFFE65100,  // Sika    — Sunset Orange
    0xFF1E88E5,  // Ajam    — Sky Blue
    0xFF6A1B9A,  // Nahawand— Deep Purple
    0xFF546E7A,  // Saba    — Slate Gray
    0xFFC62828,  // Kurd    — Crimson Red
};

std::array<bool, 12> OrientalScaleManager::getScaleNotes (MaqamPreset preset)
{
    const int idx = juce::jlimit (0, (int) MaqamPreset::count - 1, (int) preset);
    std::array<bool, 12> result;
    for (int i = 0; i < 12; ++i)
        result[(size_t) i] = kScaleNotes[idx][i];
    return result;
}

juce::Colour OrientalScaleManager::getMaqamRootColour (MaqamPreset preset)
{
    const int idx = juce::jlimit (0, (int) MaqamPreset::count - 1, (int) preset);
    return juce::Colour (kRootColours[idx]);
}

} // namespace myapp::music
