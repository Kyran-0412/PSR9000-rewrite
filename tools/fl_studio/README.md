# FL Studio script for the KyranOS setlist

`device_KyranOS_Setlist.py` selects the drone pattern in FL Studio when a song is opened in
the KyranOS setlist on the PSR-9000.

Install:
1. Make the folder `Documents\Image-Line\FL Studio\Settings\Hardware\KyranOS Setlist\`
   and copy `device_KyranOS_Setlist.py` into it.
2. In FL Studio: Options > MIDI Settings. Pick the MIDI input the keyboard's MIDI OUT is
   connected to, enable it, and set Controller type to "KyranOS Setlist (PSR-9000)".
3. Name the drone patterns with sharps: C, C#, D, D#, E, F, F#, G, G#, A, A#, B.

What the keyboard sends (note-on velocity 127, then note-off):
- channel 16, notes 60-71: the song's key (C = 60 ... B = 71). The script picks the pattern.
- channel 15, note 0-127: the song's FL configuration ID. The script leaves it to FL, so it
  can be used with "Link to controller" mappings. FL's hint bar shows the number.

The keyboard shows flats (Eb, Ab, Bb); they arrive as the same note numbers as D#, G#, A#.
