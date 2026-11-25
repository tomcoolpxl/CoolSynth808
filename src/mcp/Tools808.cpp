#include "Tools808.h"

#include <algorithm>
#include <cmath>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Editor.h"
#include "Kit.h"
#include "Processor.h"
#include "ui/LidView.h"

namespace cs808
{
    namespace
    {
        using coolsynth::mcp::imageResult;
        using coolsynth::mcp::objectSchema;
        using coolsynth::mcp::textResult;
        using coolsynth::mcp::Tool;
        using coolsynth::mcp::typeSchema;

        juce::String db(float gain)
        {
            return gain > 0.0f ? juce::String(juce::Decibels::gainToDecibels(gain), 1) + " dBFS" : juce::String("silence");
        }

        /** A lane by its code (BD .. CH, or a second instrument's: LC, MC, HC, CL, MA), or -1. */
        int laneOf(const juce::String& code, bool& alt)
        {
            alt = false;
            for (int l = 0; l < laneCount; ++l)
            {
                const auto& inst = instruments[static_cast<std::size_t>(l)];
                if (code.equalsIgnoreCase(inst.code))
                    return l;
                if (inst.altCode != nullptr && code.equalsIgnoreCase(inst.altCode))
                {
                    alt = true;
                    return l;
                }
            }
            return -1;
        }

        /** The panel and the transport as the screen shows them: every value by the name set_values takes. */
        juce::var describe(Processor& p)
        {
            const auto& project = p.project();
            auto* knobsObj = new juce::DynamicObject();
            for (int i = 0; i < knobCount; ++i)
                knobsObj->setProperty(knobs[static_cast<std::size_t>(i)].id, project.kit.knob[static_cast<std::size_t>(i)]);
            auto* switches = new juce::DynamicObject();
            for (int l = 0; l < laneCount; ++l)
                if (instruments[static_cast<std::size_t>(l)].altCode != nullptr)
                    switches->setProperty(instruments[static_cast<std::size_t>(l)].code,
                                          project.kit.alt[static_cast<std::size_t>(l)] ? instruments[static_cast<std::size_t>(l)].altCode
                                                                                         : instruments[static_cast<std::size_t>(l)].code);
            juce::Array<juce::var> notes;
            for (const int n : p.drumNotes())
                notes.add(n);
            auto* o = new juce::DynamicObject();
            o->setProperty("knobs", juce::var(knobsObj));
            o->setProperty("switches", juce::var(switches));
            o->setProperty("volumeDb", project.kit.volumeDb);
            o->setProperty("tempo", project.play.tempo);
            o->setProperty("swing", project.play.swing);
            o->setProperty("playing", p.isPlaying());
            o->setProperty("pattern", p.playingPattern() + 1);
            o->setProperty("step", static_cast<int>(p.playingSixteenth() % 16) + 1);
            o->setProperty("drumNotes", notes);
            o->setProperty("followHostPlay", p.followsHostPlay());
            if (p.hasBeatSync())
            {
                // the beat sync (D361): on, the other peers in its session, Play and Stop shared, leading the beat
                auto* sync = new juce::DynamicObject();
                sync->setProperty("on", p.beatSyncOn());
                sync->setProperty("peers", p.syncPeers());
                sync->setProperty("startStopSharing", p.startStopSharing());
                sync->setProperty("leads", p.followingSync());
                sync->setProperty("countingIn", p.countingIn());
                sync->setProperty("tempoNow", p.tempoNow());
                o->setProperty("sync", juce::var(sync));
            }
            o->setProperty("outputLevel", db(p.outputLevel()));
            if (auto* editor = dynamic_cast<Editor*>(p.getActiveEditor()))
            {
                o->setProperty("lidLifted", editor->isLidLifted());
                o->setProperty("sheetOpen", editor->lidView);
                o->setProperty("optionsOpen", editor->optionsOpen());
            }
            return juce::var(o);
        }

        void runOn(const coolsynth::mcp::Server::Runner& runner, std::function<void()> job)
        {
            if (runner)
                runner(std::move(job));
            else
                job();
        }
    }

    std::vector<Tool> tools808(Processor& processor, coolsynth::mcp::Server::Runner runner)
    {
        std::vector<Tool> tools;

        tools.push_back({ "get_state",
                          "The kit and the transport as the panel shows them: every knob 0..1 by its id (accent, volume, bdLevel, "
                          "bdTone, bdDecay, ...), each switched drum's choice (LT: LT or LC, RS: RS or CL, CP: CP or MA, ...), "
                          "the volume in dB, tempo, swing, whether it plays, the pattern and step, the drums' notes from outside "
                          "(BD..CH, LC, MC, HC, CL, MA), the host switch, the beat sync (on, other peers, Play and Stop shared, "
                          "whether it leads, counting in to the next bar line, the tempo heard), the output's level now, and the "
                          "views (lid lifted, a sheet open, Options open).",
                          objectSchema({}),
                          [&processor](const juce::var&) { return textResult(juce::JSON::toString(describe(processor), false)); } });

        tools.push_back({ "set_values",
                          "Sets knobs by the ids get_state gives (0..1), switches by drum code ({\"RS\": \"CL\"} for claves), "
                          "volumeDb, tempo and swing, as the panel would; out of range is held to the range; sync and "
                          "syncSharing (true or false) switch the beat sync and its shared Play and Stop. Answers with the "
                          "state afterwards.",
                          objectSchema({ { "values", typeSchema("object", "e.g. {\"bdDecay\": 0.8, \"RS\": \"CL\", \"tempo\": 125}") } }, { "values" }),
                          [&processor](const juce::var& args)
                          {
                              const auto* values = args["values"].getDynamicObject();
                              if (values == nullptr)
                                  return textResult("No values given.", true);
                              juce::StringArray unknown;
                              for (const auto& nv : values->getProperties())
                              {
                                  const auto name = nv.name.toString();
                                  int knob = -1;
                                  for (int i = 0; i < knobCount; ++i)
                                      if (name == knobs[static_cast<std::size_t>(i)].id)
                                          knob = i;
                                  bool alt = false;
                                  const int lane = laneOf(name, alt);
                                  if (knob >= 0)
                                      processor.editKit([knob, v = static_cast<float>(nv.value)](KitValues& k) { k.knob[static_cast<std::size_t>(knob)] = juce::jlimit(0.0f, 1.0f, v); });
                                  else if (name == "volumeDb")
                                      processor.editKit([v = static_cast<float>(nv.value)](KitValues& k) { k.volumeDb = juce::jlimit(-60.0f, 6.0f, v); });
                                  else if (name == "tempo")
                                      processor.editPlay([v = static_cast<double>(nv.value)](PlaySettings& s) { s.tempo = juce::jlimit(30.0, 300.0, v); });
                                  else if (name == "swing")
                                      processor.editPlay([v = static_cast<float>(nv.value)](PlaySettings& s) { s.swing = juce::jlimit(0.5f, 0.75f, v); });
                                  else if (name == "sync" && processor.hasBeatSync())
                                      processor.setBeatSyncOn(static_cast<bool>(nv.value)); // as the SYNC key (D361)
                                  else if (name == "syncSharing" && processor.hasBeatSync())
                                      processor.setStartStopSharing(static_cast<bool>(nv.value)); // as Options' switch
                                  else if (lane >= 0 && ! alt && instruments[static_cast<std::size_t>(lane)].altCode != nullptr)
                                  {
                                      bool toAlt = false;
                                      (void) laneOf(nv.value.toString(), toAlt);
                                      processor.editKit([lane, toAlt](KitValues& k) { k.alt[static_cast<std::size_t>(lane)] = toAlt; });
                                  }
                                  else
                                      unknown.add(name);
                              }
                              if (! unknown.isEmpty())
                                  return textResult("Not on the panel: " + unknown.joinIntoString(", "), true);
                              return textResult(juce::JSON::toString(describe(processor), false));
                          } });

        tools.push_back({ "transport",
                          "Starts or stops the sequencer, and picks the pattern it plays (1 to 16).",
                          objectSchema({ { "play", typeSchema("boolean", "true starts, false stops") },
                                         { "pattern", typeSchema("integer", "1 to 16, optional") } }),
                          [&processor](const juce::var& args)
                          {
                              if (args.hasProperty("pattern"))
                                  processor.setChain({ juce::jlimit(1, 16, static_cast<int>(args["pattern"])) - 1 });
                              if (args.hasProperty("play"))
                                  processor.setPlaying(static_cast<bool>(args["play"]));
                              return textResult(juce::String("Asked: ") + (args.hasProperty("play") ? (static_cast<bool>(args["play"]) ? "play" : "stop") : "no change")
                                                + (args.hasProperty("pattern") ? ", pattern " + args["pattern"].toString() : juce::String()) + ".");
                          } });

        tools.push_back({ "strike",
                          "Strikes drums in turn, as the panel's drum keys do (BD, SD, LT, MT, HT, RS, CP, CB, CY, OH, CH; a "
                          "switched drum plays as its switch is set), and tells the loudest output during and after.",
                          objectSchema({ { "drums", typeSchema("array", "Drum codes, struck one after another") },
                                         { "accent", typeSchema("boolean", "Accented") },
                                         { "gap", typeSchema("number", "Seconds between strikes, 0.3 by default") },
                                         { "release", typeSchema("number", "Seconds listened to after the last, 1 by default") } },
                                       { "drums" }),
                          [&processor, runner](const juce::var& args)
                          {
                              const auto* drums = args["drums"].getArray();
                              if (drums == nullptr || drums->isEmpty())
                                  return textResult("No drums given.", true);
                              const double gap = juce::jlimit(0.02, 5.0, args.hasProperty("gap") ? static_cast<double>(args["gap"]) : 0.3);
                              const double after = juce::jlimit(0.0, 10.0, args.hasProperty("release") ? static_cast<double>(args["release"]) : 1.0);
                              const bool accent = static_cast<bool>(args["accent"]);
                              (void) processor.takeLoudest();
                              juce::StringArray unknown;
                              for (const auto& d : *drums)
                              {
                                  bool alt = false;
                                  const int lane = laneOf(d.toString(), alt);
                                  if (lane < 0)
                                  {
                                      unknown.add(d.toString());
                                      continue;
                                  }
                                  runOn(runner, [&processor, lane, accent] { processor.strike(lane, accent); });
                                  juce::Thread::sleep(static_cast<int>(gap * 1000.0));
                              }
                              const float during = processor.takeLoudest();
                              juce::Thread::sleep(static_cast<int>(after * 1000.0));
                              const float later = processor.takeLoudest();
                              return textResult("Struck " + juce::String(drums->size() - unknown.size()) + " drums: loudest " + db(during) + ", after "
                                                + db(later) + (unknown.isEmpty() ? juce::String(".") : ". Not drums: " + unknown.joinIntoString(", ")));
                          },
                          true });

        tools.push_back({ "record",
                          "Records the app's output (its left channel, at the audio device's rate) for so many seconds into a "
                          "WAV file and gives its path, loudest sample and level. With play, the sequencer starts with the "
                          "recording and stops after it; with drums, those are struck in turn instead.",
                          objectSchema({ { "seconds", typeSchema("number", "How long, 0.1 to 60") },
                                         { "play", typeSchema("boolean", "Start the sequencer for the recording") },
                                         { "drums", typeSchema("array", "Or: drum codes struck in turn, every gap seconds") },
                                         { "gap", typeSchema("number", "With drums: seconds between strikes, 0.5 by default") },
                                         { "file", typeSchema("string", "Optional path for the WAV; a temporary file by default") } },
                                       { "seconds" }),
                          [&processor, runner](const juce::var& args)
                          {
                              const double seconds = juce::jlimit(0.1, 60.0, static_cast<double>(args["seconds"]));
                              const bool play = static_cast<bool>(args["play"]);
                              const auto* drums = args["drums"].getArray();
                              const double gap = juce::jlimit(0.02, 5.0, args.hasProperty("gap") ? static_cast<double>(args["gap"]) : 0.5);
                              runOn(runner, [&processor, seconds, play]
                              {
                                  processor.startCapture(seconds);
                                  if (play)
                                      processor.setPlaying(true);
                              });
                              const double started = juce::Time::getMillisecondCounterHiRes() * 0.001;
                              if (drums != nullptr)
                                  for (const auto& d : *drums)
                                  {
                                      bool alt = false;
                                      if (const int lane = laneOf(d.toString(), alt); lane >= 0)
                                          runOn(runner, [&processor, lane] { processor.strike(lane, false); });
                                      juce::Thread::sleep(static_cast<int>(gap * 1000.0));
                                      if (juce::Time::getMillisecondCounterHiRes() * 0.001 - started > seconds)
                                          break;
                                  }
                              const int wanted = static_cast<int>(processor.capture().size());
                              while (processor.captured() < wanted && juce::Time::getMillisecondCounterHiRes() * 0.001 - started < seconds + 5.0)
                                  juce::Thread::sleep(20);
                              if (play)
                                  runOn(runner, [&processor] { processor.setPlaying(false); });
                              const int n = processor.captured();
                              if (n <= 0)
                                  return textResult("Nothing came out: is the audio device running?", true);
                              auto file = args["file"].toString().isNotEmpty()
                                              ? juce::File(args["file"].toString())
                                              : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CoolSynth808")
                                                    .getChildFile("record-" + juce::Time::getCurrentTime().formatted("%H%M%S") + ".wav");
                              file.getParentDirectory().createDirectory();
                              file.deleteFile();
                              const auto& data = processor.capture();
                              float peak = 0.0f;
                              double sum = 0.0;
                              for (int i = 0; i < n; ++i)
                              {
                                  peak = std::max(peak, std::abs(data[static_cast<std::size_t>(i)]));
                                  sum += static_cast<double>(data[static_cast<std::size_t>(i)]) * data[static_cast<std::size_t>(i)];
                              }
                              {
                                  auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
                                  if (stream == nullptr)
                                      return textResult("Cannot write " + file.getFullPathName(), true);
                                  juce::WavAudioFormat wav;
                                  auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}
                                                                                .withSampleRate(processor.captureRate())
                                                                                .withNumChannels(1)
                                                                                .withBitsPerSample(24));
                                  if (writer == nullptr)
                                      return textResult("Cannot write a WAV to " + file.getFullPathName(), true);
                                  const float* channels[] = { data.data() };
                                  writer->writeFromFloatArrays(channels, 1, n);
                              }
                              return textResult("Recorded " + juce::String(n / processor.captureRate(), 2) + " s at " + juce::String(processor.captureRate(), 0)
                                                + " Hz into " + file.getFullPathName() + ": loudest " + db(peak) + ", rms "
                                                + db(static_cast<float>(std::sqrt(sum / n))) + ".");
                          },
                          true });

        tools.push_back({ "screenshot",
                          "A picture of CoolSynth 808's window as the player sees it now, drawn at a scale: 2 (the default) twice "
                          "the window's own size, sharp enough for a README; 1 the window's size, up to 4. With file, the "
                          "PNG is also written there at full resolution.",
                          objectSchema({ { "scale", typeSchema("number", "Pixels per point, 1 to 4; 2 by default") },
                                         { "file", typeSchema("string", "Optional: a full path to write the PNG to as well") } }),
                          [&processor](const juce::var& args)
                          {
                              auto* editor = processor.getActiveEditor();
                              if (editor == nullptr || editor->getWidth() <= 0)
                                  return textResult("No window to picture.", true);
                              const float scale = args.hasProperty("scale")
                                                      ? juce::jlimit(1.0f, 4.0f, static_cast<float>(static_cast<double>(args["scale"])))
                                                      : 2.0f;
                              const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, scale);
                              juce::MemoryOutputStream png;
                              juce::PNGImageFormat().writeImageToStream(image, png);
                              juce::String saved;
                              if (const auto path = args["file"].toString(); path.isNotEmpty())
                              {
                                  if (! juce::File::isAbsolutePath(path))
                                      return textResult("file must be a full path.", true);
                                  const juce::File file(path);
                                  (void) file.getParentDirectory().createDirectory();
                                  if (! file.replaceWithData(png.getData(), png.getDataSize()))
                                      return textResult("Cannot write " + file.getFullPathName() + ".", true);
                                  saved = " Written to " + file.getFullPathName() + ".";
                              }
                              return imageResult(png.getMemoryBlock(), "CoolSynth 808's window, " + juce::String(image.getWidth()) + " x "
                                                                           + juce::String(image.getHeight()) + " pixels, at "
                                                                           + juce::String(scale, 1) + " times its size." + saved);
                          } });

        tools.push_back({ "view",
                          "Changes what the window shows, as its keys do: lid lifts or lowers the lid; sheet opens a schematic "
                          "(0 the whole board, 1 bass drum, 2 snare, 3 low tom, 4 rim shot, 5 hand clap, 6 cowbell, 7 metal "
                          "oscillators, 8 cymbal, 9 hi-hats, 10 the CPU section; the lid is lifted for it) and closeSheet goes "
                          "back; options opens or closes the Options window.",
                          objectSchema({ { "lid", typeSchema("boolean", "Lift (true) or lower (false) the lid") },
                                         { "sheet", typeSchema("integer", "0 to 10: open this sheet") },
                                         { "closeSheet", typeSchema("boolean", "Back from a sheet to the lifted lid") },
                                         { "options", typeSchema("boolean", "Open (true) or close (false) the Options window") } }),
                          [&processor](const juce::var& args)
                          {
                              auto* editor = dynamic_cast<Editor*>(processor.getActiveEditor());
                              if (editor == nullptr)
                                  return textResult("No window open.", true);
                              if (args.hasProperty("lid"))
                                  editor->liftLid(static_cast<bool>(args["lid"]));
                              if (args.hasProperty("sheet"))
                              {
                                  if (! editor->isLidLifted())
                                      editor->liftLid(true);
                                  editor->lidForTesting().showSheet(juce::jlimit(0, 10, static_cast<int>(args["sheet"])));
                                  editor->showLid(true);
                              }
                              if (static_cast<bool>(args["closeSheet"]))
                                  editor->showLid(false);
                              if (args.hasProperty("options") && static_cast<bool>(args["options"]) != editor->optionsOpen())
                                  editor->toggleOptions();
                              return textResult(juce::JSON::toString(describe(processor), false));
                          } });

        return tools;
    }
}
