"""Write THIRD_PARTY_NOTICES.txt (CoolSynth's D363): the notices of everything the app and the VST3 carry.

What ships, found in the Release binaries (their marker strings and symbols), as CoolSynth 303's: JUCE 9 and, compiled
into it, zlib, libpng, libjpeg, libwebp, HarfBuzz, SheenBidi, LunaSVG with PlutoVG, FLAC, Ogg Vorbis and Opus; the VST3
SDK; Open303's generator, rewritten in the engine and linked with it; the fonts Archivo and IBM Plex Mono. Not ASIO: a
release is built without it (COOLSYNTH808_ASIO=OFF). Each licence's text is read from its own file in the JUCE
submodule or resources/fonts, so running this again after a JUCE update brings them up to date.

usage: python make_notices.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
JUCE = os.path.join(ROOT, "external", "JUCE", "modules")
RULE = "-" * 120


def read(*parts):
    with open(os.path.join(*parts), encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r\n", "\n").strip("\n")


def ijg_legal():
    # libjpeg's terms are the README's LEGAL ISSUES section, up to REFERENCES.
    lines = read(JUCE, "juce_graphics", "image_formats", "jpglib", "README").split("\n")
    start = next(i for i, l in enumerate(lines) if l.strip() == "LEGAL ISSUES")
    end = next(i for i, l in enumerate(lines) if i > start and l.strip() == "REFERENCES")
    return "\n".join(lines[start:end]).strip("\n")


OPEN303 = """The MIT License

Copyright (c) 2009 Robin Schmidt (www.rs-met.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE."""

sections = [
    ("JUCE (https://juce.com)",
     "CoolSynth 808 is built with JUCE 9, copyright Raw Material Software Limited, under the Starter licence of the\n"
     "JUCE 9 End User Licence Agreement (https://juce.com/legal/juce-9-licence/).\n"
     "JUCE carries the libraries below, compiled into the app and the VST3, each under its own licence.",
     None),
    ("Open303 (https://github.com/RobinSchmidt/Open303)",
     "The TB-303 generator in the sound engine the 808 is built on (the engine's src/synth/v3/Tb303Generator.h) is\n"
     "rewritten from Open303's engine.",
     OPEN303),
    ("Archivo (https://github.com/Omnibus-Type/Archivo)", "The panel's typeface for every label.",
     read(ROOT, "resources", "fonts", "Archivo-OFL.txt")),
    ("IBM Plex Mono (https://github.com/IBM/plex)", "The panel's typeface for numbers.",
     read(ROOT, "resources", "fonts", "IBMPlexMono-OFL.txt")),
    ("VST 3 SDK (https://github.com/steinbergmedia/vst3sdk)", "In the VST3 plugin.",
     read(JUCE, "juce_audio_processors_headless", "format_types", "VST3_SDK", "LICENSE.txt").strip("/-\n")),
    ("zlib (https://zlib.net)", None, read(JUCE, "juce_core", "zip", "zlib", "LICENSE")),
    ("libpng (http://www.libpng.org)", None, read(JUCE, "juce_graphics", "image_formats", "pnglib", "LICENSE")),
    ("libjpeg (https://ijg.org)", "This software is based in part on the work of the Independent JPEG Group.", ijg_legal()),
    ("libwebp (https://chromium.googlesource.com/webm/libwebp)", None,
     read(JUCE, "juce_graphics", "image_formats", "libwebp", "COPYING")),
    ("HarfBuzz (https://github.com/harfbuzz/harfbuzz)", None, read(JUCE, "juce_graphics", "fonts", "harfbuzz", "COPYING")),
    ("SheenBidi (https://github.com/Tehreer/SheenBidi)", None, read(JUCE, "juce_graphics", "unicode", "sheenbidi", "LICENSE")),
    ("LunaSVG (https://github.com/sammycage/lunasvg)", None, read(JUCE, "juce_graphics", "drawables", "lunasvg", "LICENSE")),
    ("PlutoVG (https://github.com/sammycage/plutovg)", None,
     read(JUCE, "juce_graphics", "drawables", "lunasvg", "plutovg", "LICENSE")),
    ("FLAC (https://xiph.org/flac)", None, read(JUCE, "juce_audio_formats", "codecs", "flac", "Flac Licence.txt")),
    ("libogg (https://xiph.org/ogg)", None, read(JUCE, "juce_audio_formats", "codecs", "ogg", "COPYING")),
    ("libvorbis (https://xiph.org/vorbis)", None, read(JUCE, "juce_audio_formats", "codecs", "vorbis", "COPYING")),
    ("Opus (https://opus-codec.org)", None,
     read(JUCE, "juce_audio_formats", "codecs", "opus", "opus", "COPYING") + "\n\n"
     + read(JUCE, "juce_audio_formats", "codecs", "opus", "opus", "LICENSE_PLEASE_READ.txt")),
    ("opusfile (https://opus-codec.org)", None, read(JUCE, "juce_audio_formats", "codecs", "opus", "opusfile", "COPYING")),
    ("libopusenc (https://opus-codec.org)", None, read(JUCE, "juce_audio_formats", "codecs", "opus", "libopusenc", "COPYING")),
]

out = ["CoolSynth 808 includes work from the following projects, under their licences. CoolSynth 808's own code is",
       "MIT licensed (LICENSE.txt)."]
for title, note, text in sections:
    out += ["", RULE, title]
    if note:
        out.append(note)
    if text:
        out += ["", text]
with open(os.path.join(ROOT, "THIRD_PARTY_NOTICES.txt"), "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(out) + "\n")
print("THIRD_PARTY_NOTICES.txt:", len(sections), "sections")
