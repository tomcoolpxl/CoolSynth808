# CoolSynth 808 Project Rules

The shared project memory for every AI tool (`CLAUDE.md` points here).

## What it is

- A drum machine for Windows (JUCE 9.0.5, C++20, CMake): a JUCE audio plugin built as a Standalone (D267) and a VST3
  (D325), linked into `C:\Program Files\Common Files\VST3`.
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
- `src/Editor` and `src/ui`: the panel, its widgets and themes, Under the Lid, the controller setup, the library's
  shelf (`ui/LibraryShelf`, D273).
- `src/midi`: CoolSynth's controller layer, ported (D268), with its own `%APPDATA%\CoolSynth808\controller.json`;
  `src/Controls` says what each place does in the 808. Pads strike on the audio thread (`Processor::setPadNotes`).
  `src/StandaloneMidiIn` sorts the standalone's MIDI inputs: the controller's linked controls apart from the sound's
  notes (D271); `src/StandaloneMidiOut` keeps the clock's output thread running.
- `src/AudioExport` and `ui/ExportPanel`: the WAV export (D331, D334), rendered by an offline `Processor`; with drum
  files, one render puts each drum heard on its own output (D343, D357), the mix the main plus those outputs, so the
  files add up to it.
- The VST3's host parameters (D343) are the 28 panel controls; the audio thread plays what they say, the panel's
  edits set them (`Processor::paramsFromModel`), the host's moves reach the model without undo. A drum on its own
  output leaves the main through the engine's tap (`CircuitWatch::setSeparate`), outputs 2 to 12 mono.
- `tests/`: the app's tests (`CoolSynth808Tests`); the engine's run in the engine's repository.
- `src/mcp`: the standalone app's MCP server (D333, CoolSynth's and the 303's), on `http://127.0.0.1:47608/mcp`,
  registered with `claude mcp add --transport http coolsynth808 http://127.0.0.1:47608/mcp`. Its tools read and set
  the kit, run the transport, strike drums, record the output to WAV, picture the window and open the lid, a sheet or
  Options: check the running app through it, and ask the user only for ears and hands. It is off until switched on in
  Options, MIDI (CoolSynth's D363), kept as `mcpServer` in the app's settings; `COOLSYNTH808_NO_MCP` keeps it off.
- The release (CoolSynth's D363): `installer/CoolSynth808.iss` (Inno Setup), `scripts/ci` (build, test, package,
  without ASIO), `.github/workflows/release.yml` on a version tag; `tools/release/make_notices.py` writes
  `THIRD_PARTY_NOTICES.txt`.
- `tools/RenderLibrary`: `CoolSynth808Render <folder> [id ...]` writes the built-in beats as WAV files, for listening
  passes (D276); `listening/` holds them and is not committed.

## Rules

- The rules of CoolSynth's `GEMINI.md` hold here: pick lists for the user's choices, research online first, plain
  words for anything a player sees, no heredocs, commit on `main` and push after each verified chunk.
- An engine change goes to the engine repository (its `main`), then the new submodule pointer is committed here and
  in CoolSynth.
- Required checks: the Debug build (compiled optimised) and `ctest --test-dir build -C Debug --output-on-failure`.
