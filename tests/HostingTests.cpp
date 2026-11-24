// The VST3 as a DAW loads it (D325): found in its bundle, made, prepared, played from the host's transport, its state
// saved and given back, stopping with the host, and the host's notes playing the drums. (The switch that keeps the
// pattern to its own Play key is tested on the processor: ProcessorTests.) Skipped when the VST3 has not been built
// (the CI builds the tests only). After CoolSynth 303's.

#include <cmath>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

namespace
{
    class HostingTests final : public juce::UnitTest
    {
    public:
        HostingTests() : juce::UnitTest("Hosting", "CoolSynth808") {}

        struct Head final : juce::AudioPlayHead
        {
            double ppq = 0.0;
            bool playing = true;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                info.setIsPlaying(playing);
                info.setPpqPosition(ppq);
                info.setBpm(125.0);
                return info;
            }
        };

        /** Two seconds under the host; a note at the first block when `note` >= 0. The level, dBFS rms. */
        static double play(juce::AudioPluginInstance& plugin, Head& head, int note)
        {
            juce::AudioBuffer<float> buffer(2, 480);
            double energy = 0.0;
            for (int b = 0; b < 200; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0 && note >= 0)
                    midi.addEvent(juce::MidiMessage::noteOn(10, note, static_cast<juce::uint8>(100)), 0);
                buffer.clear();
                plugin.processBlock(buffer, midi);
                head.ppq += 480.0 / 48000.0 * 125.0 / 60.0;
                for (int i = 0; i < 480; ++i)
                    energy += static_cast<double>(buffer.getSample(0, i)) * buffer.getSample(0, i);
            }
            return juce::Decibels::gainToDecibels(std::sqrt(energy / (200.0 * 480.0)), -200.0);
        }

        void runTest() override
        {
            beginTest("the_vst3_loads_plays_with_the_host_and_keeps_its_state");
            const juce::File bundle = juce::File(COOLSYNTH808_BINARY_DIR).getChildFile("CoolSynth808_artefacts/Debug/VST3/CoolSynth 808.vst3");
            if (! bundle.exists())
            {
                logMessage("no VST3 built at " + bundle.getFullPathName() + ": skipped");
                expect(true);
                return;
            }
            juce::VST3PluginFormat format;
            juce::OwnedArray<juce::PluginDescription> found;
            format.findAllTypesForFile(found, bundle.getFullPathName());
            expectEquals(found.size(), 1);
            if (found.isEmpty())
                return;
            expectEquals(found[0]->name, juce::String("CoolSynth 808"));
            expect(found[0]->isInstrument);

            juce::String error;
            auto plugin = format.createInstanceFromDescription(*found[0], 48000.0, 480, error);
            expect(plugin != nullptr, error);
            if (plugin == nullptr)
                return;
            Head head;
            plugin->setPlayHead(&head);
            plugin->setPlayConfigDetails(0, 2, 48000.0, 480);
            plugin->prepareToPlay(48000.0, 480);

            // The host plays: its first pattern plays with it.
            const double withHost = play(*plugin, head, -1);
            logMessage("the VST3 under a playing host: " + juce::String(withHost, 1) + " dBFS rms");
            expect(withHost > -40.0, "it plays with the host's transport");

            // Its state holds the project and the switch; a second instance takes it as it was.
            juce::MemoryBlock state;
            plugin->getStateInformation(state);
            expect(state.getSize() > 1000, "its state holds the project: " + juce::String(static_cast<int>(state.getSize())) + " bytes");
            auto second = format.createInstanceFromDescription(*found[0], 48000.0, 480, error);
            expect(second != nullptr);
            if (second != nullptr)
            {
                second->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
                juce::MemoryBlock back;
                second->getStateInformation(back);
                expect(back == state, "a second instance takes the state as it was");
                second->releaseResources();
            }

            // The host stops: the pattern stops with it; a note from the host still plays its drum.
            head.playing = false;
            const double stoppedLevel = [&] { (void) play(*plugin, head, -1); return play(*plugin, head, -1); }(); // its ring out first
            logMessage("the host stopped: " + juce::String(stoppedLevel, 1) + " dBFS rms");
            expect(stoppedLevel < -80.0, "the pattern stops with the host");
            const double note = play(*plugin, head, 36);
            logMessage("the host's note 36, stopped: " + juce::String(note, 1) + " dBFS rms");
            expect(note > -50.0, "the host's note plays its drum");

            plugin->releaseResources();
            plugin->setPlayHead(nullptr);
        }
    };

    static HostingTests hostingTests;
}
