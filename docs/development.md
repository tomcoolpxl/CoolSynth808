# CoolSynth 808 development

## How the circuits are modelled

The sixteen voices are component-level models of the TR-808's eleven drum circuits, solved in real time as wave digital filters with their nonlinear transistors and diodes (after Kurt James Werner's 2016 thesis) and checked stage by stage against ngspice.

- Each drum is its circuit, modelled part by part (transistors, diodes, op-amps, resistors, capacitors and pots) and run at 96 kHz.
- Each circuit's stages are compared with ngspice renders of the schematic in the 1981 service notes; the drums are then compared with recordings of two real 808s and with the notes' own measurements.
- Where the service notes are unclear or both real units differ from the drawing, a part's value was fitted to them, within the circuit: no extra controls or curves.
- The accent is the trigger's voltage into each circuit, as on the machine, its range read from three real units' recordings.
- The devices use SPICE-style models, the same as the ngspice references: the op-amps with their bandwidth and output swing, the transistors and diodes by their junctions, the metal voices' oscillators by their thresholds, the noise at the service notes' level. Each circuit's file in the engine lists them. Fuller models (a measured transistor card, the op-amps' slew) were rendered in ngspice and compared with both real units; today's matched them as well or better, so they stay.
- Every part runs at 25 °C, as SPICE assumes: the circuits never warm up, drift or age forty years.

## Building

ASIO comes in when Steinberg's ASIO SDK is found: set `ASIO_SDK_DIR` to its `ASIOSDK` folder, or unpack it to `SDKs/asio/sdk/ASIOSDK` beside the repository's parent folder. Without it the app offers the Windows drivers only.

## Design and sources

- The sound engine is its own repository, [coolsynth-engine](https://github.com/tomcoolpxl/coolsynth-engine), a submodule here; the 808's circuits are in [`src/synth/v3`](https://github.com/tomcoolpxl/coolsynth-engine/tree/main/src/synth/v3) (`CircuitWdf.h` and the `Tr808` files), their ngspice checks in its `tests/V3Tr808*Tests.cpp`.
- The app's design: [COOLSYNTH_808.md](https://github.com/tomcoolpxl/CoolSynth/blob/main/docs/design/COOLSYNTH_808.md) and the sequencer's CPU section, [COOLSYNTH_808_CPU.md](https://github.com/tomcoolpxl/CoolSynth/blob/main/docs/design/COOLSYNTH_808_CPU.md).
- Every decision, with what was measured: CoolSynth's [DECISIONS_LOG.md](https://github.com/tomcoolpxl/CoolSynth/blob/main/DECISIONS_LOG.md), the circuits from D235 and D250, the app from D263.
- The research on each voice and the accent: [docs/research](https://github.com/tomcoolpxl/CoolSynth/tree/main/docs/research) in CoolSynth (`tr-808-voices.md`, `808-accent.md` and one file per family).
- What fuller device models change, against both units: [808-device-models.md](https://github.com/tomcoolpxl/CoolSynth/blob/main/docs/research/808-device-models.md).
- Kurt James Werner, [Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters](https://purl.stanford.edu/jy057cz8322), Stanford University, 2016.
- The code's layout: [GEMINI.md](../GEMINI.md).
