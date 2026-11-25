# CoolSynth 808 manual

## The panel

The panel is the machine's circuit board, etched into the metal: each drum's knobs on its own column of copper, which lights as its circuit rings. Slide switches pick a circuit's second instrument: conga, claves or maracas.

## The sequencer

The sequencer sits on smoked glass below the panel:

- banks A to D of sixteen patterns, each with A and B halves and a fill
- a chain of patterns that loops or plays once
- a lane per drum with its own length; on each step a chance, a roll and the accent

## The top strip

Play, Rec, the tempo, SYNC, Tap, Swing, Lid, Library, File and Options. Space starts and stops.

## Under the lid

Lid lifts the lid: the knobs and the glass go, the panel's circuit stays with its electricity, and the 808's CPU board is drawn below it. The drums' copper and the whole board light green as a node rises and red as it falls, as the engine plays. Each column that opens is framed; a click opens its schematic, MAIN's the whole board. A drum's schematic shows the engine's own voltages and currents from its last hit, with the circuit simulation's recording until it is struck. The CPU board opens the CPU section's: our sequencer's step as the service notes describe it. A click on the sheet's empty space, or Escape, goes back.

## Options

Options opens a window of its own, its pages listed down the left:

- Audio: the driver (Windows Audio; ASIO too when built from source with Steinberg's ASIO SDK, not in the release) and the circuits' accuracy: Best, Lighter or Lightest for a busy computer; an export always renders at Best.
- MIDI: the ports, the clock, the beat sync's Play and Stop, and Claude's MCP server: it lets Claude Code read and set the panel and play the 808 while it helps you, off until you switch it on, and only this computer can reach it.
- Drum notes: which note plays which drum from a DAW or a MIDI input: General MIDI, in a row from 36 or from 60, or each learned from a note played. Drag MIDI writes the same notes.
- Panel: the theme, the window's size, and the circuit full, faint or hidden, its electricity animated or still.
- A MIDI controller's setup.

## The beat sync and MIDI clock

The beat sync plays the 808 in time with CoolSynth 303 and other apps and DAWs that have it, on this computer or the network. It is on from the start. SYNC beside the tempo lights while it's on and counts the others ("SYNC 2"); a click switches it. With others, the tempo is shared and a Play starts on their next bar line. Play and Stop go to all of them, unless **Options > MIDI > Share Play and Stop** is off. The first time, Windows asks about its firewall: allow private networks to sync with other computers.

For hardware it follows an incoming MIDI clock and sends its own, also while the beat sync leads; both can be switched off. While the beat sync has others with it, an incoming clock is ignored.

The 808 and the 303 together take some two and a half processor cores; a busy program beside them can make the 808 stutter.

## Files, export and the library

Drag MIDI drags the pattern into your music software as a MIDI file.

File, Export audio writes the pattern shown or the chain as a 24-bit WAV, played a number of times: cut at the end, ringing on, or as a seamless loop. A file per drum too if you like, each as long as the mix.

The library holds starter beats for 22 modern genres, classic 808 beats and your own patterns.

File saves projects (`.cs808`): the kit, every bank and the tempo. The app reopens where you left off; Ctrl+Z and Ctrl+Y undo and redo any edit.

## In a DAW

The VST3 plays the pattern with the host's transport and tempo, and the host's notes play the drums. Options has "Follow the host's Play": off, the pattern waits for its own Play key, at the host's tempo. Every knob, the Volume and the five voice switches can be automated in the DAW. Each drum can leave the main mix for its own mono output (outputs 2 to 12, BD to CH): switch it on in **Options > Host**, and turn the output on in the DAW.
