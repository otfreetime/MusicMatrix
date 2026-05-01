#!/usr/bin/env python3
"""
Convert "Salalem El-Nashh" Arabic melody to MIDI format.
Based on musical notation with Bayati maqam characteristics.
"""

from midiutil import MIDIFile
from pathlib import Path

# Salalem El-Nashh melody in Bayati maqam
# Extracted from the sheet music notation
# Format: (note_name, octave, duration_in_beats, is_microtonal)
# is_microtonal=True means apply -50 cents pitch bend for quarter-tone flats

melody_top = [
    # Line 1: Opening phrase
    ('D', 4, 0.5, False),
    ('D', 4, 0.5, False),
    ('D', 4, 0.5, False),
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),   # Quarter-tone flat (Bayati character)
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    
    # Line 1 continuation
    ('C', 4, 0.5, False),
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    ('F', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('F', 4, 1.0, False),
    ('G', 4, 0.5, False),
    
    # Line 2
    ('F', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    ('F', 4, 0.5, False),
    ('G', 4, 1.0, False),
    ('A', 4, 0.5, False),
    
    # Line 3
    ('G', 4, 0.5, False),
    ('F', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('F', 4, 0.5, False),
    ('G', 4, 1.0, False),
    ('A', 4, 0.5, False),
    
    # Line 4
    ('Bb', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('F', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 1.0, False),
    
    # Line 5
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('F', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('G', 4, 1.0, False),
    ('Bb', 4, 0.5, False),
    
    # Line 6
    ('C', 5, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('F', 4, 1.0, False),
    ('G', 4, 0.5, False),
    
    # Line 7
    ('A', 4, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('C', 5, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('F', 4, 1.0, False),
    ('G', 4, 0.5, False),
    
    # Line 8: Conclusion
    ('A', 4, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 2.0, False),  # Final note, held longer
]

# Second part (lower section in image)
melody_bottom = [
    # Lower section opening
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    ('F', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('G', 4, 1.0, False),
    ('F', 4, 0.5, False),
    
    # Continuation
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('F', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('Bb', 4, 1.0, False),
    ('C', 5, 0.5, False),
    
    # Section 3
    ('Bb', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('F', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('F', 4, 1.0, False),
    
    # Section 4
    ('G', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('F', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    ('C', 4, 1.0, False),
    ('D', 4, 0.5, False),
    
    # Section 5
    ('Eb', 4, 0.5, True),
    ('F', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('G', 4, 1.0, False),
    ('F', 4, 0.5, False),
    
    # Section 6
    ('G', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('C', 5, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('G', 4, 1.0, False),
    ('F', 4, 0.5, False),
    
    # Section 7
    ('Eb', 4, 0.5, True),
    ('F', 4, 0.5, False),
    ('G', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('Bb', 4, 0.5, False),
    ('A', 4, 0.5, False),
    ('G', 4, 1.0, False),
    ('Eb', 4, 0.5, True),
    
    # Final section
    ('D', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 0.5, False),
    ('F', 4, 0.5, False),
    ('Eb', 4, 0.5, True),
    ('D', 4, 2.0, False),  # Final resolution
]

def note_to_midi(note_name, octave):
    """Convert note name and octave to MIDI note number."""
    note_map = {
        'C': 0, 'D': 2, 'Eb': 3, 'E': 4, 'F': 5,
        'Gb': 6, 'G': 7, 'Ab': 8, 'A': 9, 'Bb': 10, 'B': 11
    }
    return 12 + (octave * 12) + note_map[note_name]

def create_midi_file(melody, output_path, bpm=120, title="Salalem El-Nashh"):
    """Create MIDI file from melody notes."""
    
    # Create MIDI object
    midi = MIDIFile(1)  # 1 track
    track = 0
    channel = 0
    volume = 100
    
    # Add track name and tempo
    midi.addTrackName(track, 0, title)
    midi.addTempo(track, 0, bpm)
    
    # Convert beat duration to time in MIDI ticks
    time = 0
    
    for note_name, octave, duration, is_microtonal in melody:
        midi_note = note_to_midi(note_name, octave)
        
        # Add note
        midi.addNote(track, channel, midi_note, time, duration, volume)
        
        # If microtonal, add pitch bend for -50 cents (quarter-tone flat)
        if is_microtonal:
            # Pitch bend value: 0-16383, center at 8192
            # -50 cents ≈ -4096 (half of ±8192 range)
            pitch_bend_value = 8192 - 4096
            midi.addPitchWheelEvent(track, channel, time, pitch_bend_value)
            # Reset pitch bend after note
            midi.addPitchWheelEvent(track, channel, time + duration, 8192)
        
        time += duration
    
    # Write to file
    with open(output_path, 'wb') as f:
        midi.writeFile(f)
    
    return output_path

if __name__ == '__main__':
    # Combine both sections
    full_melody = melody_top + melody_bottom
    
    # Create output directory if needed
    output_dir = Path(__file__).parent.parent / 'melodies'
    output_dir.mkdir(exist_ok=True)
    
    # Generate MIDI file
    output_file = output_dir / 'Salalem_El_Nashh.mid'
    result = create_midi_file(full_melody, output_file)
    
    print(f"✓ MIDI file created: {result}")
    print(f"  Total notes: {len(full_melody)}")
    print(f"  Duration: ~{len(full_melody) * 0.5 / 2:.1f} seconds (at 120 BPM)")
