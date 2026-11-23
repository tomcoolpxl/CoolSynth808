# CoolSynth 808 Project Rules

The shared project memory for every AI tool (`CLAUDE.md` points here).

## What it is

- A standalone drum machine app for Windows (JUCE 9.0.5, C++20, CMake): a JUCE audio plugin built only as a
  Standalone for now (D267), so VST3 and CLAP are one line in `CMakeLists.txt` later.
- The sound is CoolSynth's engine, the submodule `external/coolsynth-engine` (its own repository,
  `tomcoolpxl/coolsynth-engine`): the three 808 generators in one layer in the Kit mode, set as CoolSynth's stock
  808 Kit (`src/Kit.cpp`). The step sequencer is the engine's (`src/synth/StepSequencer`).
- The design, the user's decisions and the history live in CoolSynth's repository (`E:\github\CoolSynth`):
  `docs/design/COOLSYNTH_808.md`, `DECISIONS_LOG.md` from D263, and `docs/design/mockups/coolsynth-808.html`, the
  look's reference. Log new decisions there, in the same numbering.

## Where things are

- `src/Processor`: the engine, the sequencer, the transport, MIDI clock in and out, recording; the project lives on
  the message thread and reaches the audio thread through a lock the audio thread only tries.
- `src/Project`: the project, pattern files and the library's compact form, as JSON. `src/Library`: the built-in
  beats (`resources/library/patterns.json`, compiled in) and the user's (`%APPDATA%\CoolSynth808\Patterns`).
- `src/Editor` and `src/ui`: the panel, its widgets and themes, Under the Lid.
- `tests/`: the app's tests (`CoolSynth808Tests`); the engine's run in the engine's repository.

## Rules

- The rules of CoolSynth's `GEMINI.md` hold here: pick lists for the user's choices, research online first, plain
  words for anything a player sees, no heredocs, commit on `main` and push after each verified chunk.
- An engine change goes to the engine repository (its `main`), then the new submodule pointer is committed here and
  in CoolSynth.
- Required checks: the Debug build (compiled optimised) and `ctest --test-dir build -C Debug --output-on-failure`.
