#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "ClockFollower.h"
#include "Kit.h"
#include "Project.h"
#include "synth/StepSequencer.h"
#include "synth/v3/EngineV3.h"

/*
    CoolSynth 808's processor: the engine playing the 808 kit, the step sequencer playing the engine, the transport
    (its own clock, or an incoming MIDI clock) and MIDI clock out. A JUCE audio plugin built as a standalone app for
    now (D267): JUCE's standalone wrapper brings the audio and MIDI settings and keeps the last state.

    The project lives on the message thread. Each edit hands the audio thread a copy under a lock the audio thread
    only tries (it never waits): the kit, the playing settings, the bank with its lanes' notes. The audio thread
    hands back where it plays and which drums it struck, as atomics, and the hits recorded while it plays, through
    a FIFO.
*/
namespace cs808
{
    struct RecordedHit
    {
        int pattern = 0; // -1 the fill
        bool variationB = false;
        int lane = 0;
        int step = 0;
        bool alt = false;
    };

    class Processor final : public juce::AudioProcessor
    {
    public:
        Processor();
        ~Processor() override;

        // ---- juce::AudioProcessor ----------------------------------------------------------------------------
        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }
        const juce::String getName() const override { return "CoolSynth 808"; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 2.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock& destData) override;
        void setStateInformation(const void* data, int sizeInBytes) override;

        // ---- The project, message thread ---------------------------------------------------------------------
        [[nodiscard]] const Project& project() const noexcept { return model; }
        /** Changes the project through `edit`, then hands the audio thread what changed. Undoable unless asked not. */
        void editKit(const std::function<void(KitValues&)>& edit);
        void editPlay(const std::function<void(PlaySettings&)>& edit);
        void editBank(const std::function<void(SequencerBank&)>& edit, bool undoable = true);
        void replaceProject(const Project& p, bool undoable = true);
        /** Begins an undo step that later edits join (a knob's drag). */
        void beginGesture();
        void endGesture();
        bool undo();
        bool redo();
        [[nodiscard]] bool canUndo() const noexcept { return ! undoStack.empty(); }
        [[nodiscard]] bool canRedo() const noexcept { return ! redoStack.empty(); }

        void selectBank(int index);
        void addBank();

        [[nodiscard]] juce::File currentFile() const { return file; }
        void setCurrentFile(const juce::File& f) { file = f; }
        bool saveTo(const juce::File& f);
        juce::Result loadFrom(const juce::File& f);

        // ---- Transport, message thread -----------------------------------------------------------------------
        void setPlaying(bool shouldPlay) noexcept { playRequest.store(shouldPlay ? 1 : 0, std::memory_order_release); }
        [[nodiscard]] bool isPlaying() const noexcept { return playingNow.load(std::memory_order_acquire); }
        void requestFill() noexcept { fillRequest.store(true, std::memory_order_release); }
        void setChain(const std::vector<int>& patterns);
        [[nodiscard]] std::vector<int> chain() const { return chainOnUi; }
        void setRecording(bool on) noexcept { recording.store(on, std::memory_order_release); }
        [[nodiscard]] bool isRecording() const noexcept { return recording.load(std::memory_order_acquire); }
        /** A drum struck from the panel: played at once, and recorded when recording. */
        void strike(int lane, bool accent);

        // ---- What the audio thread reports, message thread ---------------------------------------------------
        [[nodiscard]] int playingPattern() const noexcept { return patternNow.load(std::memory_order_acquire); }
        [[nodiscard]] bool playingVariationB() const noexcept { return variationBNow.load(std::memory_order_acquire); }
        [[nodiscard]] std::int64_t playingSixteenth() const noexcept { return sixteenthNow.load(std::memory_order_acquire); }
        [[nodiscard]] bool followingClock() const noexcept { return clockFollowed.load(std::memory_order_acquire); }
        [[nodiscard]] double tempoNow() const noexcept { return bpmNow.load(std::memory_order_acquire); }
        /** The lanes struck since the last call, a bit each (bit 11 the accent). */
        [[nodiscard]] std::uint32_t takeStruckLanes() noexcept { return struck.exchange(0, std::memory_order_acq_rel); }
        /** Writes the hits recorded while playing into the project; returns whether any came. */
        bool applyRecordedHits();

        // ---- Rendering without a device (tests, the MIDI file) -----------------------------------------------
        [[nodiscard]] coolsynth::synth::v3::EngineV3& engineForTesting() noexcept { return engine; }

    private:
        struct AudioCopy
        {
            KitValues kit {};
            PlaySettings play {};
            std::unique_ptr<SequencerBank> bank = std::make_unique<SequencerBank>();
        };

        void pushKit();
        void pushPlay();
        void pushBank();
        void pushUndo();
        void pullFromUi() noexcept;
        void playEvents(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart);
        void sendClock(juce::MidiBuffer& out, double beat0, double beat1, int numSamples, bool started, bool stopped);

        // message thread
        Project model;
        juce::File file;
        std::vector<std::unique_ptr<Project>> undoStack, redoStack;
        bool inGesture = false;
        std::vector<int> chainOnUi { 0 };

        // handed over
        juce::SpinLock handover;
        AudioCopy pending;
        std::array<int, coolsynth::synth::StepSequencer::maxChain> pendingChain {};
        int pendingChainLength = 0;
        std::atomic<bool> kitDirty { true }, playDirty { true }, bankDirty { true }, chainDirty { false };
        std::atomic<int> playRequest { -1 };
        std::atomic<bool> fillRequest { false };
        std::atomic<bool> recording { false };
        juce::MidiMessageCollector panelNotes;

        // audio thread
        coolsynth::synth::v3::EngineV3 engine;
        coolsynth::synth::StepSequencer sequencer;
        coolsynth::synth::v3::EngineSettings engineSettings {};
        KitValues kitOnAudio {};
        PlaySettings playOnAudio {};
        ClockFollower clock;
        bool playing = false;
        bool playingByClock = false;
        double beat = 0.0;
        std::int64_t samplesSoFar = 0;
        double rate = 48000.0;
        std::vector<coolsynth::synth::EngineMidiEvent> events;
        std::vector<coolsynth::synth::EngineMidiEvent> sequencerEvents;

        // reported
        std::atomic<bool> playingNow { false }, variationBNow { false }, clockFollowed { false };
        std::atomic<int> patternNow { 0 };
        std::atomic<std::int64_t> sixteenthNow { 0 };
        std::atomic<double> bpmNow { 120.0 };
        std::atomic<std::uint32_t> struck { 0 };
        juce::AbstractFifo recordedFifo { 256 };
        std::array<RecordedHit, 256> recordedHits {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
    };
}
