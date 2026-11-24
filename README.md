# CoolSynth 808

CoolSynth 808 is a drum machine for Windows and macOS whose sixteen voices are component-level models of the Roland
TR-808's eleven drum circuits, solved in real time as wave digital filters with their nonlinear transistors and diodes
(after Kurt James Werner's 2016 thesis) and checked stage by stage against ngspice.

- No samples and no sound-alike DSP: each drum is its circuit, modelled part by part (transistors, diodes, op-amps,
  resistors, capacitors and pots) and run at 96 kHz.
- Each knob turns the part it turns on the machine, so the drums respond as the circuit does.
- Each circuit's stages are compared with ngspice renders of the schematic in the 1981 service notes; the drums are
  then compared with recordings of two real 808s and with the notes' own measurements.
- Where the service notes are unclear or both real units differ from the drawing, a part's value was fitted to them,
  within the circuit: no extra controls or curves.
- The accent is the trigger's voltage into each circuit, as on the machine, its range read from three real units'
  recordings.
- The devices use SPICE-style models, the same as the ngspice references: the op-amps with their bandwidth and output
  swing, the transistors and diodes by their junctions, the metal voices' oscillators by their thresholds, the noise
  at the service notes' level. Each circuit's file in the engine lists them. Fuller models (a measured transistor
  card, the op-amps' slew) were rendered in ngspice and compared with both real units; today's matched them as well
  or better, so they stay.

TR-808 is a trademark of Roland Corporation. CoolSynth 808 is not made, approved or endorsed by Roland.

## Screenshots

<!-- Screenshots and animated GIFs go here: the panel in each theme, a beat playing, Under the Lid. -->

To come.

## Playing it

- The panel is the machine's circuit board, etched into the metal: each drum's knobs on its own column of copper,
  which lights as its circuit rings. Slide switches pick a circuit's second instrument: conga, claves or maracas.
- The sequencer sits on smoked glass below it:
  - banks A to D of sixteen patterns, each with A and B halves and a fill
  - a chain of patterns that loops or plays once
  - a lane per drum with its own length; on each step a chance, a roll and the accent
- The strip at the top right: Play, Rec, the tempo, Tap, Swing, Lid, Library, File and Options. Space starts and
  stops.
- Lid lifts the lid: the knobs and the glass go, the panel's circuit stays with its electricity, and the 808's CPU
  board is drawn below it. The drums' copper and the whole board light green as a node rises and red as it falls, as
  the engine plays. Each column that opens is framed; a click opens its schematic, MASTER's the whole board. A
  drum's shows the engine's own voltages and currents from its last hit, with the ngspice recording until it is
  struck. A click on the sheet's empty space, or Escape, goes back.
- Options opens a window of its own: the audio driver (Windows Audio, or ASIO), the MIDI ports, the theme (Roland,
  Silver, Steel, Olive), the panel's circuit (full, faint or hidden) and its electricity (animated or still), the
  window's size and a MIDI controller's setup.
- It follows an incoming MIDI clock and sends its own when none comes in; both can be switched off.
- Drag MIDI drags the pattern into your music software as a MIDI file.
- The library holds starter beats for 22 modern genres, classic 808 beats and your own patterns.
- File saves projects (`.cs808`): the kit, every bank and the tempo. The app reopens where you left off; Ctrl+Z and
  Ctrl+Y undo and redo any edit.

## Building from source

```powershell
git clone --recurse-submodules https://github.com/tomcoolpxl/CoolSynth808.git
cmake -S CoolSynth808 -B CoolSynth808/build -G "Visual Studio 17 2022" -A x64
cmake --build CoolSynth808/build --config Debug --parallel
ctest --test-dir CoolSynth808/build -C Debug --output-on-failure
```

ASIO comes in when Steinberg's ASIO SDK is found: set `ASIO_SDK_DIR` to its `ASIOSDK` folder, or unpack it to
`SDKs/asio/sdk/ASIOSDK` beside the repository's parent folder. Without it the app offers the Windows drivers only.

## Design and sources

- The sound engine is its own repository, [coolsynth-engine](https://github.com/tomcoolpxl/coolsynth-engine), a
  submodule here; the 808's circuits are in
  [`src/synth/v3`](https://github.com/tomcoolpxl/coolsynth-engine/tree/main/src/synth/v3) (`CircuitWdf.h` and the
  `Tr808` files), their ngspice checks in its `tests/V3Tr808*Tests.cpp`.
- The app's design: [COOLSYNTH_808.md](https://github.com/tomcoolpxl/CoolSynth/blob/main/docs/design/COOLSYNTH_808.md)
  and the sequencer's CPU section,
  [COOLSYNTH_808_CPU.md](https://github.com/tomcoolpxl/CoolSynth/blob/main/docs/design/COOLSYNTH_808_CPU.md).
- Every decision, with what was measured: CoolSynth's
  [DECISIONS_LOG.md](https://github.com/tomcoolpxl/CoolSynth/blob/main/DECISIONS_LOG.md), the circuits from D235
  and D250, the app from D263.
- The research on each voice and the accent:
  [docs/research](https://github.com/tomcoolpxl/CoolSynth/tree/main/docs/research) in CoolSynth (`tr-808-voices.md`,
  `808-accent.md` and one file per family).
- What fuller device models change, against both units:
  [808-device-models.md](https://github.com/tomcoolpxl/CoolSynth/blob/main/docs/research/808-device-models.md).
- Kurt James Werner, [Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters](https://purl.stanford.edu/jy057cz8322),
  Stanford University, 2016.
- The code's layout: [GEMINI.md](GEMINI.md).
