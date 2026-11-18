# Switch Blade and MIDI

Switch Blade has four tracks. Each track plays one of eight machines: OB-Z, WAVE, ACID, DRIFT, JP-Z, CZ-Z, GW-Z and MICRO
(V2: JP-Z to MICRO are still placeholder voices).
Each track can play its own machine, drive an external synth, or do both.
The same CC numbers work in both directions. A controller turning CC74 on channel 1 moves track 1's CUT.
When track 1 is set to send, turning track 1's CUT knob sends CC74 on channel 1.

MIDI OUT goes over **USB only**. The TRS jack is a MIDI **input**.

## Channels

By default, track N uses channel N:

| Track | Machine (default) | Channel |
|---|---|---|
| T1 | OB-Z  | 1 |
| T2 | WAVE  | 2 |
| T3 | ACID  | 3 |
| T4 | DRIFT | 4 |

MENU > MIDI > MIDI IN chooses the block of channels: CH1-4 (the default), CH5-8, CH9-12 or CH13-16.
It can also be set to SEL, where every channel plays the selected track, or to TRACK, where you pick a channel for each track in MENU > MIDI > T1..T4 CHANNEL.
MIDI OUT uses the same channel that the track listens on.
With SEL, or with a track set to OMNI or OFF, the track sends on its own channel from CH1-4 (T1 on 1, T2 on 2, and so on).

## MIDI OUT per track: INT, EXT, BOTH

MENU > MIDI OUT > T1..T4 OUT:

| Setting | What it does |
|---|---|
| **INT** (default) | The track plays its machine only and sends nothing. |
| **EXT** | The track drives external gear only. Its own voice stays silent. |
| **BOTH** | The track plays its machine and sends to external gear at the same time. |

In EXT and BOTH, the track sends these on its channel:

- **Notes** from the sequencer, the arpeggiator, the keys and chords. Every note-on is followed by its note-off, even if you switch the track back to INT while the note is held.
- **The EDIT knobs, VOLUME and the other mapped controls**, as CCs from the table below. A CC is sent only when its value changes, at most once per audio block.
- **Bank Select (CC0 = the machine, 0..7) and then Program Change** when you load a factory preset on the track. A DAW that records them chooses the same sound again when it plays them back.

What comes **in** over MIDI (USB or TRS) is not passed through:

- Notes received are played, not sent out. If the track's arpeggiator is on, the notes it plays from them do go out, because the arpeggiator generates them.
- CCs received set their parameters, but are not sent out. The same is true when MIDI LEARN maps a received CC to a parameter.
- A Program Change received loads the preset, but sends no Program Change.
- While Switch Blade follows an external clock, it sends no clock.

A DAW that echoes Switch Blade's own output back to it can still double what it sent, for example by playing the echoed notes on the track's own voice in BOTH. Turn off the DAW's MIDI thru for Switch Blade's port.

When you load a factory preset, Switch Blade sends its bank and Program Change but not the preset's CUT, RES and other values. The external synth keeps the sound its own program chose. After that, only the changes you make are sent.

Switching a track to EXT or BOTH sends nothing until you change something.

## The CC map (the same in and out)

| Control | CC |
|---|---|
| VOLUME (track level) | 7 |
| CUT (EDIT knob 1) | 74 |
| RES (EDIT knob 2) | 71 |
| ENV, the filter envelope amount (FLT, EDIT knob 3) | 79 |
| MACHINE control (EDIT knob 4) | 70 |
| GLIDE | 5 |
| PAN | 10 |
| ATTACK | 73 |
| DECAY | 75 |
| RELEASE | 72 |
| REVERB send | 91 |
| CHORUS send | 93 |
| DELAY send | 94 |
| The machine's eight EDIT values E1..E8 | 102..109 |
| Machine | Bank Select CC0 0..7 (8: user presets) |
| Preset | Program Change 0..49 (user presets 0..31) |

CC 102..109 are the eight values of the track machine's EDIT pages, in order (E1 on 102, E8 on 109). They work on every machine.
Some of them are also one of the four EDIT knobs, such as CUT. Such a value is set by either CC, and Switch Blade sends it on the EDIT knob's CC only (CUT on 74, not also on its 102..109 number).

DRIFT's Juno chorus (CHOR: OFF, I, II, I+II; EDIT > JUNO) has no CC: 91, 93 and 94 are the sends, and the CHORUS send is the shared chorus bus, not this one. It can be mapped with MIDI LEARN (GLO + D4).

EDIT knob 4 is each machine's own control: DTN (detune) on OB-Z, POS (wavetable position) on WAVE, DECY (decay) on ACID and DRV (drive) on DRIFT.

CC values 0..127 span the parameter's whole range. For a bipolar value such as PAN, 64 is the middle.
## Choosing sounds: Bank Select and Program Change

Program Change 0..49 picks a preset by its number, in the order the PRESETS list shows them.
Send Bank Select (CC0) first to choose the machine as well:

| CC0 | Program Change picks |
|---|---|
| 0 OB-Z, 1 WAVE, 2 ACID, 3 DRIFT, 4 JP-Z, 5 CZ-Z, 6 GW-Z, 7 MICRO | that machine's preset 0..49. The track switches machine if it must. |
| 8 | user preset slot 0..31 |
| none | a preset of the machine the track already plays |

- A bank applies to the next Program Change on its channel only. A Program Change with no CC0 just before it keeps the track's machine.
- CC32 (bank LSB) is accepted and ignored.
- A program number past the last preset, an empty user slot, or a bank above 8 is ignored. Nothing changes.
- A run of Program Changes is one undo.
- **MENU > MIDI > PC IN** (default ON). Set it to OFF to ignore every Bank Select and Program Change received. Use this when a sequencer sends them on every pattern change (Elektron MIDI tracks do) and you want to keep the sounds you have. It is a device setting, not saved in the project.

## When MIDI comes in too fast

The input holds 256 messages.
- **USB:** when the queue is three quarters full, the computer is asked to wait. Nothing is lost.
- **TRS:** the input cannot wait, so it has the last quarter of the queue to itself.
  - Above three quarters, controllers (CC, aftertouch, pitch bend) are dropped first. A later message carries their value again.
  - Notes, sustain (CC64), CC120..127, Bank Select and Program Change are never dropped.
  - Only a completely full queue stops every note, so that no note can hang.

Other controllers received: CC1 (mod wheel), CC11 (expression), CC64 (sustain), pitch bend with RPN 0 bend range, channel aftertouch, and CC120, CC121 and CC123 (sound off, reset controllers, notes off).
With MENU > CONTROL > SELECT set to MODW, turning SELECT on a track set to EXT or BOTH sends CC1 on that track's channel.

## Clock

MENU > MIDI OUT > CLOCK OUT (default ON) sends MIDI clock over USB while Switch Blade runs on its own clock (CLK INT):

- **Start (FA)** when you press PLAY. A restart (GLO + PLAY) sends Stop and then Start.
- **Clock (F8)** at 24 pulses per quarter note, locked to the sequencer.
- **Stop (FC)** when you press STOP, when you turn CLOCK OUT off while playing, and when you switch CLK to an external clock.

When Switch Blade follows an external clock (CLK USB or CLK TRS), it sends no clock. This prevents clock loops. If you turn CLOCK OUT on in the middle of a song, the clock starts with the next PLAY.
Clock goes out for the whole device, whatever each track's OUT setting is.

## MIDI LEARN

MIDI LEARN works as before (GLO + D4). A learned CC is yours: on input it overrides the fixed map above.
MIDI OUT always sends the fixed map, not learned CCs.

## Recipe: drive an external synth from track N

1. Connect Switch Blade to your computer or USB MIDI host. On the external synth, set its MIDI input to receive **channel N** from Switch Blade's USB port. If MENU > MIDI > MIDI IN is not CH1-4, use the channel shown for track N.
2. Go to **MENU > MIDI OUT > TN OUT** and choose **EXT**, or **BOTH** if you also want Switch Blade's machine to play.
3. Play the keys or press PLAY. Track N's notes now play the external synth, and EDIT knobs 1..4 move its filter and more on **CC 74, 71, 79 and 70**. Load a preset on track N to send a Program Change. If the synth should follow Switch Blade's tempo, leave CLOCK OUT on and set the synth to external clock.

## Recipe: play Switch Blade from an Elektron Syntakt

A Syntakt MIDI track sends notes, 8 assignable CCs, and bank and program, all on one channel. Connect the Syntakt's MIDI OUT to Switch Blade's TRS MIDI IN.

1. On the Syntakt, set up to four tracks as MIDI tracks, on **channels 1..4**. Each one plays the Switch Blade track with the same number. Leave MENU > MIDI > MIDI IN at CH1-4.
2. On each MIDI track, set the 8 CC slots to **102, 103, 104, 105, 106, 107, 108, 109**. The Syntakt's 8 knobs then move that track machine's E1..E8, the values of its EDIT pages in order. Add **74** (CUT) or **7** (VOLUME) in place of one if you prefer.
3. To choose the sound from the Syntakt, set the MIDI track's **BANK** to the machine (0 OB-Z .. 7 MICRO, 8 your user presets) and **PROG** to the preset number. The Syntakt sends them when a pattern starts.
4. To keep the sounds you chose on Switch Blade instead, leave BANK and PROG off on the Syntakt, or set **MENU > MIDI > PC IN** to **OFF**.
5. To sync tempo, set the Syntakt to send clock and Switch Blade's CLK to TRS.
