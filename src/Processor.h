#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "ClockFollower.h"
#include "Kit.h"
#include "midi/ControllerSetup.h"
#include "Project.h"
#include "synth/StepSequencer.h"
#include "synth/v3/CircuitWatch.h"
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
        void editKit(const std::function<void(KitValues&)>& edit, bool undoable = true);
        void editPlay(const std::function<void(PlaySettings&)>& edit, bool undoable = true);
        void editBank(const std::function<void(SequencerBank&)>& edit, bool undoable = true);
        void replaceProject(const Project& p, bool undoable = true);
        /** Begins an undo step that later edits join (a knob's drag); taken at the first edit, so a gesture that
            changes nothing leaves none. */
        void beginGesture();
        void endGesture();
        /** No undo or redo from here: a project opened or begun anew. */
        void forgetHistory();
        /** The edits `make` makes reach the audio thread together on the next bar line while it plays, the
            chain beginning there from its start; stopped, at once (CoolSynth's D274: a beat auditioned in time). Edits
            made while they wait go with them. */
        void atNextBar(const std::function<void()>& make);
        [[nodiscard]] bool waitingForBar() const noexcept { return cueWaiting.load(std::memory_order_acquire); }
        /** The project as it is becomes one undo step back to `before` (an auditioned beat loaded as it plays). */
        void keepAsOneStep(const Project& before);
        /** Grows with every change of the project, the panel's sign to draw again. */
        [[nodiscard]] std::uint64_t editCount() const noexcept { return edits; }
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
        void setRecording(bool on) noexcept
        {
            recordingStepTaken = false; // each pass its own undo step
            recording.store(on, std::memory_order_release);
        }
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
        /** Each drum's level as its circuit plays it, the highest since the last take, by the panel's lane (1 the bass
            drum to 11 the closed hat; 0 unused): the engine's watch on the 808's circuits (D313). Message thread. */
        void takeLights(std::array<float, 12>& byLane) noexcept;
        /** How many blocks the 808's generators have reported: still the same, and the engine is not playing. */
        [[nodiscard]] unsigned int lightReports() const noexcept { return watch.reportCount(); }
        /** The engine's watch on the circuits: a sheet of Under the Lid captures its drum's circuit there (D317). */
        [[nodiscard]] coolsynth::synth::v3::CircuitWatch& circuitWatch() noexcept { return watch; }
        /** The board's nodes as the drums play them (D320), by the panel's lane (0 the bass drum to 10 the closed hat):
            each lane's hit count, and each node's largest swing from its rest since the last take. Message thread. */
        void takeBoard(std::array<unsigned int, 11>& hits, std::array<std::array<float, 9>, 11>& swings) noexcept;
        /** The board's nodes' rests, by lane, from their recordings: the engine's swings are measured from them. */
        void setBoardRests(const std::array<std::array<float, 9>, 11>& rests) noexcept;
        /** Writes the hits recorded while playing into the project; returns whether any came. */
        bool applyRecordedHits();

        // ---- A MIDI controller (D268), message thread -----------------------------------------------------------
        /** The note each controller pad strikes, by padIndex (-1 none), with Shift up and held: the audio thread plays
            it at once. Shift's own controls are marked shiftMark in `plain`; the audio thread follows Shift itself, so a
            pad struck just after it takes its second drum (the review of D267). */
        static constexpr int padSlots = 2 * 16 * 128;
        static constexpr std::int8_t shiftMark = -2;
        using PadNotes = std::array<std::int8_t, padSlots>;
        [[nodiscard]] static int padIndex(const coolsynth::midi::ControlId& control) noexcept;
        void setPadNotes(const PadNotes& plain, const PadNotes& shifted) noexcept;
        /** The controller's messages the setup links (or hears while learning), as they came. */
        void takeControllerEvents(const std::function<void(const coolsynth::midi::ControllerEvent&)>& use);

        // ---- The controller's input (CoolSynth's D271) ----------------------------------------------------------
        /** The standalone app sorts its MIDI inputs (StandaloneMidiIn): true, the sound's MIDI holds only what is not
            the controller's, and the controller's linked controls come by takeControllerMessage. False (a DAW, the
            tests, Any input): every message may be the controller's, as before. Any thread. */
        void setInputsRouted(bool routed) noexcept { inputsRouted.store(routed, std::memory_order_relaxed); }
        /** The options window's clock switches (D311): follow a MIDI clock that comes in, and send our own while we
            lead. Both on by default, as before. */
        void setFollowClock(bool on) noexcept { followClockOn.store(on, std::memory_order_relaxed); }
        void setSendClock(bool on) noexcept { sendClockOn.store(on, std::memory_order_relaxed); }
        [[nodiscard]] bool followsClock() const noexcept { return followClockOn.load(std::memory_order_relaxed); }
        [[nodiscard]] bool sendsClock() const noexcept { return sendClockOn.load(std::memory_order_relaxed); }
        [[nodiscard]] bool inputsRoutedForTesting() const noexcept { return inputsRouted.load(std::memory_order_relaxed); }
        /** MIDI threads: a linked control's message from the controller's input, for the next block. A note or a
            controller; anything else is not taken. */
        void takeControllerMessage(const juce::MidiMessage& message) noexcept;

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
        void markCue() noexcept; // under the handover's lock: an edit inside atNextBar waits for the bar line
        /** What the message thread handed over. Edits waiting for a bar line are taken only when `barDue`; their chain
            then begins at `bar`. */
        void pullFromUi(bool barDue = false, std::int64_t bar = 0) noexcept;
        void playEvents(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart);
        /** A controller's message: to the panel's queue, and a pad's drum struck into keptBlock. True if struck. */
        bool takeControl(const juce::MidiMessage& m, int samplePosition, int numSamples) noexcept;
        void sendClock(juce::MidiBuffer& out, double beat0, double beat1, int numSamples, bool started, bool stopped,
                       int offsetInBlock = 0);

        // message thread
        Project model;
        juce::File file;
        std::vector<std::unique_ptr<Project>> undoStack, redoStack;
        bool inGesture = false;
        bool gestureStepTaken = false;
        bool recordingStepTaken = false;
        std::uint64_t edits = 0;
        std::vector<int> chainOnUi { 0 };

        // handed over
        juce::SpinLock handover;
        AudioCopy pending;
        std::array<int, coolsynth::synth::StepSequencer::maxChain> pendingChain {};
        int pendingChainLength = 0;
        std::atomic<bool> kitDirty { true }, playDirty { true }, bankDirty { true }, chainDirty { false };
        bool cueing = false;              // message thread: inside atNextBar
        bool pendingChainFromBar = false; // under the handover's lock
        std::atomic<bool> cueWaiting { false }, cueOpen { false };
        std::atomic<int> playRequest { -1 };
        std::atomic<bool> fillRequest { false };
        std::atomic<bool> recording { false };
        struct Strike
        {
            int note = 0;
            bool accent = false;
        };
        juce::AbstractFifo strikeFifo { 128 };
        std::array<Strike, 128> strikes {};

        // audio thread
        // One layer, no effects, one sound (CoolSynth's D270): about 35 MB instead of 610, the same samples.
        coolsynth::synth::v3::CircuitWatch watch; // the circuits' levels for the panel (D313); outlives the engine's use
        coolsynth::synth::v3::EngineV3 engine { coolsynth::synth::v3::EngineShape::singleLayer() };
        coolsynth::synth::StepSequencer sequencer;
        coolsynth::synth::v3::EngineSettings engineSettings {};
        coolsynth::synth::v3::EngineSettings settingsBeforeSplit {}; // a block split on a bar line: its first part's
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
        juce::AbstractFifo controllerFifo { 512 };
        std::array<coolsynth::midi::ControllerEvent, 512> controllerEvents {};
        std::array<std::atomic<std::int8_t>, padSlots> padNotes, padNotesShifted;
        bool shiftDown = false; // audio thread: Shift as the controller's messages last said
        juce::MidiBuffer keptBlock; // the audio thread's, sized in prepareToPlay
        bool startedSetup = false;
        std::unique_ptr<juce::Timer> midiOutKeeper;

        // The controller's input, sorted by the standalone app (D271): its MIDI threads write, the audio thread reads.
        struct RoutedMessage
        {
            std::array<juce::uint8, 3> bytes {};
            double milliseconds = 0.0; // when it came, on the hi-res millisecond counter
        };
        std::atomic<bool> inputsRouted { false };
        std::atomic<bool> followClockOn { true }, sendClockOn { true };
        juce::SpinLock routedWriters; // one writer at a time: each input may have its own thread
        juce::AbstractFifo routedFifo { 256 };
        std::array<RoutedMessage, 256> routedMessages {};
        double lastBlockMilliseconds = 0.0; // audio thread
        std::unique_ptr<juce::Timer> midiInRouter; // last: it is gone before anything it writes to

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
    };
}
