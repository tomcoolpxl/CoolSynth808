# CoolSynth 808

A drum machine for Windows based on the circuits of the Roland TR-808: its sixteen sounds from its eleven circuits,
each modelled part by part, played by a step sequencer. It is CoolSynth's 808, as an app of its own.

TR-808 is a trademark of Roland Corporation. CoolSynth 808 is not made, approved or endorsed by Roland; it is based
on the published service notes of the machine and checked against recordings of real units.

## Playing it

The top of the panel is the machine's circuit board, etched into the metal, with each drum's knobs mounted on its own
column of copper: the bass drum's Level, Tone and Decay, the snare's Snappy, the toms' Tuning and so on, and the
Accent and Volume under Master. Slide switches choose the second instrument of a circuit: conga for tom, claves for
rim shot, maracas for clap. As the beat plays, each drum's column lights as its circuit rings,
following the knobs and the accent; under the glass the sequencer's CPU and memory light with the clock and your
edits.

The sequencer sits on smoked glass below it, held over the board by four pins.

- The patterns row: banks A to D, the sixteen patterns, F for the fill and Fill to play it once in the next bar; A,
  AB and B play the pattern's two halves. Click a pattern to play and edit it; right-click it for copy, paste, clear,
  save to the library and add to the chain.
- The chain: the patterns in the order they play, a block with a repeat count for each; click a block for more
  repeats, right-click to remove it, + adds the pattern you edit. Loop plays it round and round, or once and then
  stops. Change says when a pattern you pick while playing comes in: at the next bar, or at the end of the pattern.
- The lanes, one per drum, each with its own length (a shorter lane runs on across the bars): click a step to switch
  it on or off and pick its lane, drag it down to make it play less often, right-click it for its chance, a roll of
  two or three hits and its accent. A row of lights above shows the step that plays.
- On the right, the picked lane's length, Every 4, Every 2, Clear and a shift, and the pattern's tools.
- The strip at the top right: Play, Rec (writes the drums you play onto the nearest step), the tempo (drag it), Tap,
  Swing (delays every second sixteenth, as Roger Linn's drum machines do), Lid (Under the Lid: the circuits with
  their measured currents, green above zero and red below), Library, File and Options. Space starts and stops.
- File also holds the banks, Undo and Redo.
- Options opens a window of its own, kept above the panel:
  - the audio driver (Windows Audio, its Low Latency mode, or ASIO when built with Steinberg's ASIO SDK), output,
    sample rate and buffer, with the latency and a test tone
  - the MIDI inputs and output, and whether to follow an incoming clock and send one
  - the theme (Roland, the 808's own charcoal and colours, the default; Silver; Steel; Olive) and the window's size
  - Controller setup (a MIDI controller: the main knob chooses the drum, knobs follow it, pads play the drums or,
    after a press of the main knob, enter its steps)
- Drag MIDI drags the pattern into your music software as a MIDI file.

It follows a MIDI clock when one comes in (its start, stop and tempo), and sends its own when none does; both can be
switched off in Options.

## Patterns and projects

- File saves and opens projects (`.cs808`): the kit, every bank of patterns and the tempo. The app opens the last
  state again by itself.
- Library holds starter beats for 22 modern genres, from house and trap to amapiano and drum and bass, and classic beats the 808 made famous, by family and decade, and the patterns you save to it.
  Audition plays a beat in place of your pattern until you load it or stop, coming in on the next bar line while
  the transport plays; your own patterns can be renamed or sent
  to the Recycle Bin, and Keep a copy puts a classic among them.
- Undo and Redo (Ctrl+Z, Ctrl+Y) take back any edit.

## Building from source

```powershell
git clone --recurse-submodules https://github.com/tomcoolpxl/CoolSynth808.git
cmake -S CoolSynth808 -B CoolSynth808/build -G "Visual Studio 17 2022" -A x64
cmake --build CoolSynth808/build --config Debug --parallel
ctest --test-dir CoolSynth808/build -C Debug --output-on-failure
```

ASIO comes in when Steinberg's ASIO SDK is found: set `ASIO_SDK_DIR` to its `ASIOSDK` folder, or unpack it to
`SDKs/asio/sdk/ASIOSDK` beside the repository's parent folder. Without it the app offers the Windows drivers only.

The sound engine is its own repository, [coolsynth-engine](https://github.com/tomcoolpxl/coolsynth-engine), a
submodule here. The design and its decisions are in CoolSynth's repository: `docs/design/COOLSYNTH_808.md` and
`DECISIONS_LOG.md` (D263 onwards).
