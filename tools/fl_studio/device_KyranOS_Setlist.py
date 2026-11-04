# name=KyranOS Setlist (PSR-9000)
# url=https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/midi_scripting.htm
#
# FL Studio MIDI controller script for the KyranOS setlist on the PSR-9000.
# Put this file in  Documents\Image-Line\FL Studio\Settings\Hardware\KyranOS Setlist\
# and pick "KyranOS Setlist (PSR-9000)" as the controller type for the MIDI input the
# keyboard's MIDI OUT is connected to.
#
# What the keyboard sends when a song is opened (KyranOS setlist):
#   channel 16, note-on 60-71  = the song's root key (drone)  -> handled here
#   channel 15, note-on 0-127  = the song's FL configuration ID -> left for FL / Fruity
#                                 controller mappings (not handled here, see CONFIG_* below)
# Both are sent as note-on (velocity 127) followed by note-off.
#
# Drone patterns are found by name, sharps only, exactly like the SZ Mini Drone Selector:
# "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B".
# (The keyboard shows flats such as Eb / Ab / Bb; they arrive as the same note numbers.)

import patterns
import ui

KEY_CHANNEL = 16        # 1-16, as shown in FL / on the keyboard
CONFIG_CHANNEL = 15
KEY_BASE_NOTE = 60      # C = 60 ... B = 71

NOTE_TO_PATTERN = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def find_pattern(name):
    # Search pattern slots by exact name.
    for index in range(1, patterns.patternMax() + 1):
        try:
            if patterns.getPatternName(index) == name:
                return index
        except Exception:
            pass
    return None


def select_drone(name):
    index = find_pattern(name)
    if index is None:
        message = "Drone pattern not found: " + name
        print(message)
        ui.setHintMsg(message)
        return
    patterns.jumpToPattern(index)
    message = "Drone key: " + name
    print(message)
    ui.setHintMsg(message)


def OnInit():
    print("KyranOS Setlist script loaded.")
    print("Key (drone): note-on 60-71 on channel %d. Config ID: channel %d (passed through)."
          % (KEY_CHANNEL, CONFIG_CHANNEL))


def OnMidiMsg(event):
    status = event.status & 0xF0
    channel = (event.status & 0x0F) + 1          # 1-16

    if channel == KEY_CHANNEL:
        # Only note-on with velocity > 0 selects; everything else on this channel is
        # swallowed so it cannot play or control anything by accident.
        if status == 0x90 and event.data2 > 0:
            n = event.data1 - KEY_BASE_NOTE
            if 0 <= n < 12:
                select_drone(NOTE_TO_PATTERN[n])
        event.handled = True
        return

    if channel == CONFIG_CHANNEL and status == 0x90 and event.data2 > 0:
        # Config ID = note number. Not handled here: leave the event to FL so it can be
        # used by "Link to controller" / Fruity controller mappings. Show it for checking.
        ui.setHintMsg("KyranOS config ID: %d" % event.data1)
