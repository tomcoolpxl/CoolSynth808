"""How much current flows in each drum's circuit after a strike, from its ngspice recording (resources/lid), for the
etched board of CoolSynth 808's panel (CoolSynth's D298): the board lights each drum's column as its recording says,
not as a guessed decay. Run from the repository root:

    python tools/board_envelopes.py

It writes resources/lid/board_envelopes.json: for each drum, the swing of its output (what is heard) at every
millisecond of its recording, 0..1 (1 its loudest moment after the trigger; the trigger's millisecond a full flash).
"""
import base64
import json
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LID = os.path.join(ROOT, "resources", "lid")
DRUMS = {"BD": "bassdrum_currents", "SD": "snare_currents", "LT": "tom_currents", "MT": "board_mt", "HT": "board_ht",
         "RS": "rimshot_currents", "CP": "clap_currents", "CB": "cowbell_currents", "CY": "cymbal_currents",
         "OH": "board_oh", "CH": "board_ch"}


def series(rec):
    raw = base64.b64decode(rec["data"])
    n = len(rec["t"])
    values = struct.unpack("<{}h".format(len(raw) // 2), raw)
    for k, name in enumerate(rec["names"]):
        yield name, [v * rec["scales"][k] for v in values[k * n:(k + 1) * n]]


TRIGGER_MS = 1.0  # the trigger: a full flash, whatever the output does in it
# Each drum's output node in its recording: what is heard, so the board lights with the sound.
OUTPUT = {"BD": "vout", "SD": "vout", "LT": "vout", "MT": "vout", "HT": "vout", "RS": "vout", "CP": "vout",
          "CB": "ncbo", "CY": "nhpa", "OH": "voh", "CH": "vch"}  # jcb and jcy are only the 5 V select lines


def envelope(rec, node):
    t = rec["t"]
    x = dict(series(rec))[node]
    rest = sorted(x[len(x) * 3 // 4:])[len(x) // 8]  # where the output settles: the median of its last quarter
    # every millisecond, the output's largest swing from rest in it: the envelope of what is heard
    out, i = [], 0
    for ms in range(int(t[-1]) + 1):
        best = 0.0
        while i < len(t) and t[i] < ms + 1:
            best = max(best, abs(x[i] - rest))
            i += 1
        out.append(best)
    # a ring's swing holds between its peaks: carry each peak 2 ms on so the light does not flicker at audio rate
    held = [max(out[max(0, k - 2):k + 1]) for k in range(len(out))]
    # the scale from after the first 5 ms: a spike in the strike itself (the metal voices have one) would else
    # make the whole ring look faint; anything above it is a full light
    top = max(held[5:], default=0.0) or 1.0
    return [1.0 if ms <= TRIGGER_MS else round(min(1.0, v / top), 4) for ms, v in enumerate(held)]


def main():
    result = {}
    for drum, name in DRUMS.items():
        with open(os.path.join(LID, name + ".json"), encoding="utf-8") as f:
            result[drum] = envelope(json.load(f), OUTPUT[drum])
        e = result[drum]
        top = max(range(3, len(e)), key=lambda i: e[i])
        half = next((i for i in range(top, len(e)) if e[i] < 0.42), len(e))
        tenth = next((i for i in range(top, len(e)) if e[i] < 0.085), len(e))
        print("{}: {} ms, ring peaks at {} ms, half at {} ms, a tenth at {} ms".format(drum, len(e), top, half, tenth))
    with open(os.path.join(LID, "board_envelopes.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(result, f, separators=(",", ":"))


if __name__ == "__main__":
    main()
