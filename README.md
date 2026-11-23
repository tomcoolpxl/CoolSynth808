# CoolSynth 808

A drum machine for Windows based on the circuits of the Roland TR-808: its sixteen sounds from its eleven circuits,
each modelled part by part, played by a step sequencer. It is CoolSynth's 808, as an app of its own.

TR-808 is a trademark of Roland Corporation. CoolSynth 808 is not made, approved or endorsed by Roland; it is based
on the published service notes of the machine and checked against recordings of real units.

## Playing it

The panel has a column of knobs per drum, as on the machine: the bass drum's Level, Tone and Decay, the snare's
Snappy, the toms' Tuning and so on, and the Accent on the left. Click a drum's name to hear it.

- The row of sixteen keys shows the steps of the drum you selected: click a step to switch it on or off.
- Drag a step down to make it play less often; its height shows the chance.
- Right-click a step to select it; the line at the bottom then sets its chance and a roll of two or three hits.
- Open the lanes to see every drum at once, each with its own length (a shorter lane runs on across the bars).
- Space starts and stops; Swing delays every second sixteenth, as Roger Linn's drum machines do.
- A, AB and B play the pattern's two halves: A, B, or A then B.
- Pattern turns the sixteen keys into the bank's sixteen patterns: click one to play it, shift-click another to play
  the ones in between in a row (a chain).
- Fill plays the bank's fill pattern once, in the next bar; shift-click it to edit the fill.
- Rec writes the drums you play (on MIDI, or by clicking their names) on the nearest step while it runs.
- Drag MIDI drags the pattern into your music software as a MIDI file.
- Under the Lid shows the circuits as they are struck.
- Controller sets up a MIDI controller: the main knob chooses the drum, knobs follow it, pads play the drums or, after
  a press of the main knob, enter its steps. Click a place and move the control to link it.

It follows a MIDI clock when one comes in (its start, stop and tempo), and sends its own when none does. The audio
and MIDI settings are in the Options menu of the window.

## Patterns and projects

- File saves and opens projects (`.cs808`): the kit, every bank of patterns and the tempo. The app opens the last
  state again by itself.
- Library holds classic beats the 808 made famous, and the patterns you keep with Keep.
- Undo and Redo (Ctrl+Z, Ctrl+Y) take back any edit.

## Building from source

```powershell
git clone --recurse-submodules https://github.com/tomcoolpxl/CoolSynth808.git
cmake -S CoolSynth808 -B CoolSynth808/build -G "Visual Studio 17 2022" -A x64
cmake --build CoolSynth808/build --config Debug --parallel
ctest --test-dir CoolSynth808/build -C Debug --output-on-failure
```

The sound engine is its own repository, [coolsynth-engine](https://github.com/tomcoolpxl/coolsynth-engine), a
submodule here. The design and its decisions are in CoolSynth's repository: `docs/design/COOLSYNTH_808.md` and
`DECISIONS_LOG.md` (D263 onwards).
