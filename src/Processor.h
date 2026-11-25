#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "mcp/McpServer.h"
#include "ClockFollower.h"
#include "Kit.h"
#include "midi/ControllerSetup.h"
#include "Project.h"
#include "synth/StepSequencer.h"
#include "synth/v3/CircuitWatch.h"
#include "synth/v3/EngineV3.h"
#include "sync/BeatSync.h"
#include "sync/BlockClock.h"
#include "sync/SyncClock.h"

/*
    CoolSynth 808's processor: the engine playing the 808 kit, the step sequencer playing the engine, the transport
    (its own clock, an incoming MIDI clock, a DAW's, or the standalone app's beat sync, D361) and MIDI clock out. A
    JUCE audio plugin built as a standalone app for now (D267): JUCE's standalone wrapper brings the audio and MIDI
    settings and keeps the last state.

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

    /** The beat sync as last chosen (CoolSynth's D361): on, and Play and Stop shared; both on until switched off. Kept
        in the app's settings. */
    struct SyncChoice
    {
        bool on = coolsynth::sync::BeatSync::kDefaultEnabled;
        bool sharing = coolsynth::sync::BeatSync::kDefaultStartStopSharing;
    };
    [[nodiscard]] SyncChoice syncChoiceFrom(const juce::PropertySet& settings);
    void keepSyncChoice(juce::PropertySet& settings, const SyncChoice& choice);
    /** The SYNC key's word: the other peers' count after it, none while alone ("SYNC", "SYNC 2"). */
    [[nodiscard]] juce::String syncKeyText(int peers);

    class Processor final : public juce::AudioProcessor, private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater
    {
    public:
        Processor();
        /** A processor that renders offline (the WAV export, D331): no MCP server, no controller setup, no MIDI
            ports, no settings read, whatever wraps the app. */
        struct Offline {};
        explicit Processor(Offline);
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
        /** The transport as it will be once a request the audio thread has not yet taken is: what to decide on right
            after asking (CoolSynth's D354: an audition read isPlaying() just after a stop and kept the transport going). */
        [[nodiscard]] bool willPlay() const noexcept
        {
            const int r = playRequest.load(std::memory_order_acquire);
            return r >= 0 ? r == 1 : playingNow.load(std::memory_order_acquire);
        }
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
        /** The beat sync leads the beat, with other peers in its session (D361). */
        [[nodiscard]] bool followingSync() const noexcept { return syncFollowed.load(std::memory_order_acquire); }
        /** Play pressed with other peers: waiting for the session's next bar line (D361). */
        [[nodiscard]] bool countingIn() const noexcept { return countingInNow.load(std::memory_order_acquire); }
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
        /** In a DAW (D311, D325): the pattern plays while the host plays, from where it is, at its tempo; off, it plays
            only on its own Play key, at the host's tempo still (a hardware unit's Position Lock). On by default; kept
            with the DAW's project. */
        void setFollowHostPlay(bool on) noexcept { followHostPlayOn.store(on, std::memory_order_relaxed); }
        [[nodiscard]] bool followsHostPlay() const noexcept { return followHostPlayOn.load(std::memory_order_relaxed); }
        // ---- Claude's way in (D333): the standalone app's MCP server, its output captured and measured --------------
        /** Starts capturing the output's left channel for so many seconds (message thread); captured() counts it. */
        void startCapture(double seconds);
        [[nodiscard]] int captured() const noexcept { return captureCount.load(std::memory_order_acquire); }
        [[nodiscard]] double captureRate() const noexcept { return rate; }
        [[nodiscard]] const std::vector<float>& capture() const noexcept { return captureBuffer; }
        /** The loudest output sample since the last take, and a fresh start; the level of the last block. */
        [[nodiscard]] float takeLoudest() noexcept { return loudest.exchange(0.0f, std::memory_order_acq_rel); }
        [[nodiscard]] float outputLevel() const noexcept { return lastLevel.load(std::memory_order_relaxed); }
        [[nodiscard]] juce::String mcpStatus() const { return mcpStatusText; }

        // ---- The drums' notes from outside (D329) ----------------------------------------------------------------
        /** The notes a DAW or a MIDI input plays the drums with (Kit.h's slots); any thread. */
        void setDrumNotes(const DrumNotes& notes) noexcept;
        [[nodiscard]] DrumNotes drumNotes() const noexcept;
        /** The next note that comes in becomes this slot's (another slot on that note loses it); -1 stops learning. */
        void learnDrumNote(int slot) noexcept { learningSlot.store(slot, std::memory_order_release); }
        [[nodiscard]] int learningDrumNote() const noexcept { return learningSlot.load(std::memory_order_acquire); }
        /** Counts every change of the notes, learned ones too: the editor keeps them when it moves. */
        [[nodiscard]] unsigned int drumNotesVersion() const noexcept { return drumNotesChanged.load(std::memory_order_acquire); }
        /** Whether the host's transport leads the pattern now. */
        [[nodiscard]] bool followingHost() const noexcept { return hostFollowed.load(std::memory_order_acquire); }
        /** A plugin in a DAW, not the standalone app nor the tests. */
        [[nodiscard]] bool isPlugin() const noexcept { return wrapperType != wrapperType_Standalone && wrapperType != wrapperType_Undefined; }
        [[nodiscard]] bool sendsClock() const noexcept { return sendClockOn.load(std::memory_order_relaxed); }
        [[nodiscard]] bool inputsRoutedForTesting() const noexcept { return inputsRouted.load(std::memory_order_relaxed); }
        /** MIDI threads: a linked control's message from the controller's input, for the next block. A note or a
            controller; anything else is not taken. */
        void takeControllerMessage(const juce::MidiMessage& message) noexcept;

        /** The circuits' accuracy (D339): 0 Best; 1 Lighter, the metal drums' VCAs only while their drum sounds and a
            looser Newton; 2 Lightest, and the voices at the host's own rate, not near 96 kHz. Kept in the app's
            settings; message thread (a change of rate prepares the engine again, the sound paused for it). An export
            renders at Best whatever this says. */
        void setAccuracy(int level);
        [[nodiscard]] int accuracy() const noexcept { return accuracyLevel; }

        /** The transport to this beat position at the next block while it plays, as a DAW's loop jumps: what rings
            rings on, every random number from its seed again. The WAV export's seamless loop (D331). Any thread. */
        void jumpTo(double beatPosition) noexcept { jumpRequest.store(beatPosition, std::memory_order_release); }

        // ---- The beat sync (CoolSynth's D361), the standalone app's -----------------------------------------------
        /** One tempo, beat grid and play state with the other programs on the network that speak the beat sync: the
            standalone app has one (a plugin follows its DAW, an export renders alone). It feeds the transport through
            a play head of its own, as a DAW's does: Play and Stop and the tempo go through it, a start waits for the
            session's next bar line of 4 beats, and while other peers are in the session an incoming MIDI clock is
            not followed. */
        [[nodiscard]] bool hasBeatSync() const noexcept { return beatSync != nullptr; }
        /** Message thread: the SYNC key and Options. Kept in the app's settings by the editor (syncChoiceVersion). */
        void setBeatSyncOn(bool on);
        [[nodiscard]] bool beatSyncOn() const noexcept { return syncOn.load(std::memory_order_acquire); }
        void setStartStopSharing(bool on);
        [[nodiscard]] bool startStopSharing() const noexcept { return beatSync != nullptr && beatSync->startStopSharing(); }
        /** The other peers in the session; 0 while off. Message thread. */
        [[nodiscard]] int syncPeers() const noexcept { return beatSync != nullptr ? beatSync->peerCount() : 0; }
        /** Grows with every change of the two switches. */
        [[nodiscard]] unsigned int syncChoiceVersion() const noexcept { return syncChoiceChanged.load(std::memory_order_acquire); }
        /** Message thread, ten times a second: the output's latency from the audio device, and a tempo another peer
            set becomes the panel's. */
        void syncTick();
        /** For the tests: a beat sync of its own, its network on the loopback only, or none (then alone, never on a
            socket); the host clock in microseconds the audio thread reads; each sequencer hit's sample since the
            start, written into `log` (audio thread, allocating: the tests only). */
        void useBeatSyncForTesting(bool network);
        void setHostClockForTesting(std::function<std::int64_t()> micros) { hostClockForTesting = std::move(micros); }
        void logSequencerHitsForTesting(std::vector<std::int64_t>* log) noexcept { hitLog = log; }

        // ---- The host's parameters (D343) -----------------------------------------------------------------------
        /** The panel's five voice switches as parameters: the lanes whose switch each is (tom or conga on the three
            toms, rim shot or claves, clap or maracas). */
        static constexpr int switchCount = 5;
        static constexpr std::array<int, switchCount> switchLanes { 2, 3, 4, 5, 6 };
        /** A drum on its own output (D343): in a DAW, each drum may leave the main mix for its own mono output (outputs
            2 to 12, BD to CH), while the host has that output on. Kept with the DAW's project. Any thread. */
        void setOwnOutput(int lane, bool on) noexcept
        {
            if (lane >= 0 && lane < laneCount)
                ownOutputs.fetch_xor((ownOutput(lane) != on ? 1u : 0u) << lane, std::memory_order_acq_rel);
        }
        [[nodiscard]] bool ownOutput(int lane) const noexcept { return ((ownOutputs.load(std::memory_order_acquire) >> lane) & 1u) != 0u; }
        /** For the tests: the host's moves taken into the model now, as the message thread would. */
        void takeParametersForTesting() { handleUpdateNowIfNeeded(); }

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
        // The host's parameters (D343): the 22 knobs, the Volume and the five voice switches. The audio thread plays what
        // they say; the panel's edits set them (each its own gesture); the host's automation moves the panel through the
        // message thread, writing no undo steps.
        std::array<juce::AudioParameterFloat*, knobCount> knobParams {};
        juce::AudioParameterFloat* volumeParam = nullptr;
        std::array<juce::AudioParameterBool*, switchCount> switchParams {};
        void createParameters();
        void paramsFromModel();                                                 // message thread
        [[nodiscard]] KitValues kitFromParams(const KitValues& base) const noexcept; // any thread
        void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
        void parameterGestureChanged(int, bool) override {}
        void handleAsyncUpdate() override;                                      // the host's moves into the model
        std::unique_ptr<SequencerBank> notesBank = std::make_unique<SequencerBank>(); // audio thread: a switch's new notes
        // The drums' own outputs (D343): which lanes leave the main, and the engine's taps for them (audio thread).
        std::atomic<std::uint32_t> ownOutputs { 0 };
        std::array<std::vector<float>, coolsynth::synth::v3::CircuitWatch::lightCount> taps;
        std::array<float*, coolsynth::synth::v3::CircuitWatch::lightCount> tapPointers {};
        static BusesProperties buses();
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

        explicit Processor(bool offlineRender);
        const bool offline; // the WAV export's: none of the app's ports, setups or settings

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
        std::atomic<double> jumpRequest { -1.0 };
        int accuracyLevel = 0;                 // message thread (D339)
        int preparedBlock = 512;               // as prepareToPlay last set it
        bool prepared = false;
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
        bool playingByClock = false, playingByHost = false;
        double beat = 0.0;
        std::int64_t samplesSoFar = 0;
        double rate = 48000.0;
        std::vector<coolsynth::synth::EngineMidiEvent> events;
        std::vector<coolsynth::synth::EngineMidiEvent> sequencerEvents;

        // reported
        std::atomic<bool> playingNow { false }, variationBNow { false }, clockFollowed { false }, hostFollowed { false };
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
        std::atomic<bool> followClockOn { true }, sendClockOn { true }, followHostPlayOn { true };
        // the drums' notes from outside (D329): each slot's, and for each note from outside the drum's own note
        std::array<std::atomic<int>, drumSlots> drumNoteOf {};
        std::array<std::atomic<int>, 128> engineNoteFrom {};
        std::atomic<int> learningSlot { -1 };
        // Claude's way in (D333)
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
        std::unique_ptr<coolsynth::mcp::Server> mcp;
        juce::String mcpStatusText = "Off";
        std::vector<float> captureBuffer;
        std::atomic<bool> captureArmed { false };
        std::atomic<int> captureCount { 0 };
        std::atomic<float> loudest { 0.0f }, lastLevel { 0.0f };
        void startMcpServer();
        std::atomic<unsigned int> drumNotesChanged { 0 };
        void rebuildDrumNotes() noexcept;
        juce::SpinLock routedWriters; // one writer at a time: each input may have its own thread
        juce::AbstractFifo routedFifo { 256 };
        std::array<RoutedMessage, 256> routedMessages {};
        double lastBlockMilliseconds = 0.0; // audio thread

        // The beat sync (D361). Its play head gives the block's beats as a DAW's gives its transport: the tempo the
        // block plays at, the beat at its first sample, and playing once the count-in reaches beat 0 in the block.
        struct SyncPlayHead final : juce::AudioPlayHead
        {
            coolsynth::sync::BlockBeats beats {};
            double sampleRate = 48000.0;
            int samples = 0;
            juce::Optional<PositionInfo> getPosition() const override;
        };
        std::unique_ptr<coolsynth::sync::BeatSync> beatSync; // the standalone's, or a test's
        bool syncNetwork = false;                            // message thread: its network switched with it
        std::atomic<bool> syncOn { false };
        std::atomic<unsigned int> syncChoiceChanged { 0 };
        std::atomic<std::int64_t> outputLatencyMicros { 0 };
        coolsynth::sync::BlockClock blockClock;  // audio thread
        SyncPlayHead syncHead;                   // audio thread
        bool syncInUse = false;                  // audio thread: the last block was the sync's
        bool syncRunning = false;                // audio thread: the last block had the sync on
        double syncPanelTempo = -1.0;            // audio thread: the panel's tempo as last given to the sync
        std::atomic<double> syncPanelTempoNow { -1.0 }, syncBpmNow { 0.0 };
        std::atomic<int> syncPeersNow { 0 };
        std::atomic<bool> syncFollowed { false }, countingInNow { false };
        std::function<std::int64_t()> hostClockForTesting;
        std::vector<std::int64_t>* hitLog = nullptr;
        std::unique_ptr<juce::Timer> syncTicker;
        void startBeatSync(const SyncChoice& choice, bool network, coolsynth::sync::BeatSync::Options options);

        std::unique_ptr<juce::Timer> midiInRouter; // last: it is gone before anything it writes to

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
    };
}
