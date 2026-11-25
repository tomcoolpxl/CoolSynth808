#include "Processor.h"
#include "mcp/Tools808.h"

#include "Editor.h"
#include "StandaloneMidiIn.h"
#include "StandaloneMidiOut.h"

namespace cs808
{
    namespace
    {
        constexpr std::size_t maxUndo = 64;
        constexpr int maxEvents = 1024;
        constexpr int midiChannel = 10;
        constexpr std::uint32_t accentBit = 1u << laneCount;
        std::atomic<int> liveProcessors { 0 }; // the controller setup runs while one plays (D268)
    }

    Processor::Processor() : Processor(false) {}

    Processor::Processor(Offline) : Processor(true) {}

    Processor::Processor(bool offlineRender)
        : juce::AudioProcessor(buses()),
          offline(offlineRender)
    {
        events.reserve(maxEvents);
        sequencerEvents.resize(512);
        createParameters();
        // A beat to start with, until a saved state or a project replaces it: kick, snare, closed hat.
        auto& first = model.currentBank().pattern[0];
        for (const int s : { 0, 8, 10 })
            first.lanes[0].a[static_cast<std::size_t>(s)].on = true;
        for (const int s : { 4, 12 })
            first.lanes[1].a[static_cast<std::size_t>(s)].on = true;
        for (int s = 0; s < 16; s += 2)
            first.lanes[10].a[static_cast<std::size_t>(s)].on = true;
        first.accent.a[0].on = true;
        pushKit();
        pushPlay();
        pushBank();
        for (auto* table : { &padNotes, &padNotesShifted })
            for (auto& note : *table)
                note.store(-1, std::memory_order_relaxed);
        // The drums' notes from outside (D329): the app's, from its settings; General MIDI in the tests.
        {
            DrumNotes notes = drumNoteSet(0);
            if (wrapperType != wrapperType_Undefined && ! offline)
            {
                juce::PropertiesFile::Options options;
                options.applicationName = "CoolSynth808";
                options.filenameSuffix = ".settings";
                options.folderName = "CoolSynth808";
                options.osxLibrarySubFolder = "Application Support";
                juce::PropertiesFile file(options);
                if (file.containsKey("drumNotes"))
                    notes = drumNotesFromString(file.getValue("drumNotes"));
                setAccuracy(file.getIntValue("accuracy", 0)); // D339
            }
            setDrumNotes(notes);
        }
        // Claude's way in, in the standalone app only (a plugin in a DAW opens no port): D333, as the 303's.
        if (wrapperType == wrapperType_Standalone && ! offline)
            startMcpServer();
        // The computer's controller setup (D211, D268): read once for the program. Tests (no wrapper) set their own.
        if (wrapperType == wrapperType_Standalone && ! offline)
        {
            midiOutKeeper = makeStandaloneMidiOutKeeper(); // MIDI clock out at its own samples (the review of D267)
            midiInRouter = makeStandaloneMidiInRouter(*this); // the controller's input apart from the sound's (D271)
        }
        if (wrapperType != wrapperType_Undefined && ! offline)
        {
            startedSetup = true;
            if (liveProcessors.fetch_add(1, std::memory_order_relaxed) == 0)
                coolsynth::midi::ControllerSetup::instance().start();
        }
    }

    Processor::~Processor()
    {
        alive->store(false); // a tool's job still queued finds the processor gone and does nothing (D333)
        mcp = nullptr;
        midiInRouter.reset(); // gives the inputs back to the standalone's player before anything here goes
        // The last one: the setup's timers go while JUCE still runs (the review of D211).
        if (startedSetup && liveProcessors.fetch_sub(1, std::memory_order_relaxed) == 1)
            coolsynth::midi::ControllerSetup::instance().shutDown();
    }

    juce::AudioProcessor::BusesProperties Processor::buses()
    {
        // The main mix in stereo, then each drum's own mono output (D343), off until the host turns it on.
        auto b = BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true);
        for (const auto& in : instruments)
            b = b.withOutput(in.altCode != nullptr ? juce::String(in.code) + "/" + in.altCode : juce::String(in.code), juce::AudioChannelSet::mono(), false);
        return b;
    }

    bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
    {
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo() || ! layouts.getMainInputChannelSet().isDisabled())
            return false;
        for (int b = 1; b < layouts.outputBuses.size(); ++b)
            if (const auto& set = layouts.outputBuses.getReference(b); ! set.isDisabled() && set != juce::AudioChannelSet::mono())
                return false;
        return true;
    }

    void Processor::takeLights(std::array<float, 12>& byLane) noexcept
    {
        // The watch's instruments to the panel's lanes: BD SD LT MT HT RS CP CB CY OH CH (a tom's light is its
        // conga's too, the rim shot's the claves').
        using W = coolsynth::synth::v3::CircuitWatch;
        static constexpr std::array<int, 12> lightOfLane { -1, W::bassDrum, W::snare, W::lowTom, W::midTom, W::highTom, W::rimShot,
                                                           W::clap, W::cowbell, W::cymbal, W::openHat, W::closedHat };
        for (std::size_t lane = 0; lane < byLane.size(); ++lane)
            byLane[lane] = lightOfLane[lane] < 0 ? 0.0f : watch.take(lightOfLane[lane]);
    }

    void Processor::setBoardRests(const std::array<std::array<float, 9>, 11>& rests) noexcept
    {
        using W = coolsynth::synth::v3::CircuitWatch;
        static constexpr std::array<int, 11> lightOfLane { W::bassDrum, W::snare, W::lowTom, W::midTom, W::highTom, W::rimShot,
                                                           W::clap, W::cowbell, W::cymbal, W::openHat, W::closedHat };
        for (std::size_t lane = 0; lane < rests.size(); ++lane)
            for (int k = 0; k < W::boardSlots; ++k)
                watch.setBoardRest(lightOfLane[lane], k, rests[lane][static_cast<std::size_t>(k)]);
    }

    void Processor::takeBoard(std::array<unsigned int, 11>& hits, std::array<std::array<float, 9>, 11>& swings) noexcept
    {
        using W = coolsynth::synth::v3::CircuitWatch;
        static_assert(W::boardSlots == 9, "the panel takes nine nodes a drum");
        static constexpr std::array<int, 11> lightOfLane { W::bassDrum, W::snare, W::lowTom, W::midTom, W::highTom, W::rimShot,
                                                           W::clap, W::cowbell, W::cymbal, W::openHat, W::closedHat };
        for (std::size_t lane = 0; lane < hits.size(); ++lane)
        {
            hits[lane] = watch.boardHitCount(lightOfLane[lane]);
            for (int k = 0; k < W::boardSlots; ++k)
                swings[lane][static_cast<std::size_t>(k)] = watch.takeBoardSwing(lightOfLane[lane], k);
        }
    }

    void Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        preparedBlock = std::max(1, samplesPerBlock);
        for (std::size_t l = 0; l < taps.size(); ++l)
        {
            taps[l].assign(static_cast<std::size_t>(preparedBlock), 0.0f);
            tapPointers[l] = taps[l].data();
        }
        prepared = true;
        engine.setCircuitWatch(&watch); // the panel's lights follow the circuits as they play (D313)
        engine.setOversampling(accuracyLevel < 2); // the Lightest accuracy runs the voices at the host's rate (D339)
        engine.prepare(rate, std::max(1, samplesPerBlock), std::max(1, getTotalNumOutputChannels()));
        clock.prepare(rate);
        clock.setBlockLate(wrapperType == wrapperType_Standalone); // the standalone's MIDI comes a block late (D359)
        keptBlock.ensureSize(8192);
        sequencer.reset();
        playing = false;
        beat = 0.0;
        kitDirty = playDirty = bankDirty = true;
        chainDirty = true;
    }

    // ---- The project ------------------------------------------------------------------------------------------

    namespace
    {
        bool sameKit(const KitValues& a, const KitValues& b) noexcept
        {
            for (std::size_t k = 0; k < a.knob.size(); ++k)
                if (std::abs(a.knob[k] - b.knob[k]) > 1.0e-6f)
                    return false;
            return a.alt == b.alt && std::abs(a.volumeDb - b.volumeDb) < 1.0e-4f;
        }
    }

    void Processor::createParameters()
    {
        // Named as the panel prints them: the drum's code and the knob's name ("BD Decay"), the Accent alone.
        for (int k = 0; k < knobCount; ++k)
        {
            const auto& spec = knobs[static_cast<std::size_t>(k)];
            const juce::String name = spec.lane < 0 ? juce::String(spec.label)
                                                    : juce::String(instruments[static_cast<std::size_t>(spec.lane)].code) + " " + spec.label;
            auto p = std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { spec.id, 1 }, name, 0.0f, 1.0f, knobDefault(k));
            knobParams[static_cast<std::size_t>(k)] = p.get();
            addParameter(p.release());
        }
        auto volume = std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "volume", 1 }, "Volume", 0.0f, 1.0f,
                                                                  volumePosition(KitValues {}.volumeDb));
        volumeParam = volume.get();
        addParameter(volume.release());
        static constexpr std::array<const char*, switchCount> ids { "ltConga", "mtConga", "htConga", "rsClaves", "cpMaracas" };
        for (int s = 0; s < switchCount; ++s)
        {
            static constexpr std::array<const char*, switchCount> names { "LT Conga", "MT Conga", "HT Conga", "RS Claves", "CP Maracas" };
            auto p = std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ids[static_cast<std::size_t>(s)], 1 },
                                                                names[static_cast<std::size_t>(s)], false);
            switchParams[static_cast<std::size_t>(s)] = p.get();
            addParameter(p.release());
        }
        if (! offline)
            for (auto* p : getParameters())
                p->addListener(this);
    }

    KitValues Processor::kitFromParams(const KitValues& base) const noexcept
    {
        KitValues k = base;
        for (std::size_t i = 0; i < knobParams.size(); ++i)
            k.knob[i] = knobParams[i]->get();
        k.volumeDb = volumeDbAt(volumeParam->get());
        for (std::size_t s = 0; s < switchParams.size(); ++s)
            k.alt[static_cast<std::size_t>(switchLanes[s])] = switchParams[s]->get();
        return k;
    }

    void Processor::paramsFromModel()
    {
        if (volumeParam == nullptr)
            return;
        const auto set = [](juce::RangedAudioParameter& p, float plain)
        {
            if (std::abs(p.convertTo0to1(plain) - p.getValue()) < 1.0e-6f)
                return;
            p.beginChangeGesture();
            p.setValueNotifyingHost(p.convertTo0to1(plain));
            p.endChangeGesture();
        };
        for (std::size_t k = 0; k < knobParams.size(); ++k)
            set(*knobParams[k], model.kit.knob[k]);
        if (std::abs(volumeDbAt(volumeParam->get()) - model.kit.volumeDb) > 1.0e-4f)
            set(*volumeParam, volumePosition(model.kit.volumeDb));
        for (std::size_t s = 0; s < switchParams.size(); ++s)
            set(*switchParams[s], model.kit.alt[static_cast<std::size_t>(switchLanes[s])] ? 1.0f : 0.0f);
    }

    void Processor::handleAsyncUpdate()
    {
        // The host moved a parameter: the panel follows, no undo step (D343). A change the model made itself is
        // already there.
        const auto fromParams = kitFromParams(model.kit);
        if (sameKit(fromParams, model.kit))
            return;
        model.kit = fromParams;
        ++edits;
    }

    void Processor::pushKit()
    {
        paramsFromModel(); // the host's parameters say what the model says (D343)
        const juce::SpinLock::ScopedLockType lock(handover);
        pending.kit = model.kit;
        kitDirty.store(true, std::memory_order_release);
        markCue();
    }

    void Processor::pushPlay()
    {
        const juce::SpinLock::ScopedLockType lock(handover);
        pending.play = model.play;
        playDirty.store(true, std::memory_order_release);
        markCue();
    }

    void Processor::pushBank()
    {
        auto copy = std::make_unique<SequencerBank>(model.currentBank());
        applyNotes(*copy, model.kit);
        const juce::SpinLock::ScopedLockType lock(handover);
        std::swap(pending.bank, copy);
        bankDirty.store(true, std::memory_order_release);
        markCue();
    }

    void Processor::markCue() noexcept
    {
        if (cueing)
            cueWaiting.store(true, std::memory_order_release);
    }

    void Processor::atNextBar(const std::function<void()>& make)
    {
        // Open while the edits are made, so the audio thread never takes half of them on a bar line.
        cueOpen.store(true, std::memory_order_release);
        cueing = true;
        make();
        cueing = false;
        cueOpen.store(false, std::memory_order_release);
    }

    void Processor::keepAsOneStep(const Project& before)
    {
        ++edits;
        undoStack.push_back(std::make_unique<Project>(before));
        if (undoStack.size() > maxUndo)
            undoStack.erase(undoStack.begin());
        redoStack.clear();
    }

    void Processor::pushUndo()
    {
        ++edits;
        // A gesture's undo step is taken at its first edit: a click that changes nothing leaves none, and keeps redo
        // (the review of D267).
        if (inGesture && gestureStepTaken)
            return;
        if (inGesture)
            gestureStepTaken = true;
        undoStack.push_back(std::make_unique<Project>(model));
        if (undoStack.size() > maxUndo)
            undoStack.erase(undoStack.begin());
        redoStack.clear();
    }

    void Processor::beginGesture()
    {
        inGesture = true;
        gestureStepTaken = false;
    }

    void Processor::endGesture() { inGesture = false; }

    void Processor::forgetHistory()
    {
        undoStack.clear();
        redoStack.clear();
        ++edits;
    }

    void Processor::editKit(const std::function<void(KitValues&)>& edit, bool undoable)
    {
        if (undoable)
            pushUndo();
        ++edits;
        edit(model.kit);
        pushKit();
        pushBank(); // a switch changes the lanes' notes
    }

    void Processor::editPlay(const std::function<void(PlaySettings&)>& edit, bool undoable)
    {
        if (undoable)
            pushUndo();
        ++edits;
        edit(model.play);
        pushPlay();
    }

    void Processor::editBank(const std::function<void(SequencerBank&)>& edit, bool undoable)
    {
        if (undoable)
            pushUndo();
        ++edits;
        edit(model.currentBank());
        pushBank();
    }

    void Processor::replaceProject(const Project& p, bool undoable)
    {
        if (undoable)
            pushUndo();
        ++edits;
        model = p;
        pushKit();
        pushPlay();
        pushBank();
        setChain({ 0 });
    }

    bool Processor::undo()
    {
        if (undoStack.empty())
            return false;
        ++edits;
        redoStack.push_back(std::make_unique<Project>(model));
        model = *undoStack.back();
        undoStack.pop_back();
        pushKit();
        pushPlay();
        pushBank();
        return true;
    }

    bool Processor::redo()
    {
        if (redoStack.empty())
            return false;
        ++edits;
        undoStack.push_back(std::make_unique<Project>(model));
        model = *redoStack.back();
        redoStack.pop_back();
        pushKit();
        pushPlay();
        pushBank();
        return true;
    }

    void Processor::selectBank(int index)
    {
        model.bank = juce::jlimit(0, static_cast<int>(model.banks.size()) - 1, index);
        ++edits;
        pushBank();
    }

    void Processor::addBank()
    {
        if (static_cast<int>(model.banks.size()) >= Project::maxBanks)
            return;
        pushUndo();
        model.banks.push_back(std::make_unique<SequencerBank>());
        model.bank = static_cast<int>(model.banks.size()) - 1;
        pushBank();
    }

    bool Processor::saveTo(const juce::File& f)
    {
        if (! f.replaceWithText(projectToJson(model)))
            return false;
        file = f;
        return true;
    }

    juce::Result Processor::loadFrom(const juce::File& f)
    {
        Project p;
        if (const auto result = projectFromJson(f.loadFileAsString(), p); result.failed())
            return result;
        // A project opened starts its own history: an undo can never bring the one before back under this file's
        // name, for a Save to write over it (the review of D267).
        replaceProject(p, false);
        forgetHistory();
        file = f;
        return juce::Result::ok();
    }

    void Processor::getStateInformation(juce::MemoryBlock& destData)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("project", projectToJson(model));
        o->setProperty("file", file.getFullPathName());
        o->setProperty("followHostPlay", followsHostPlay()); // the DAW's project keeps it (D325)
        o->setProperty("ownOutputs", static_cast<int>(ownOutputs.load())); // and the drums on their own outputs (D343)
        destData.replaceAll(juce::JSON::toString(juce::var(o), true).toRawUTF8(), juce::JSON::toString(juce::var(o), true).getNumBytesAsUTF8());
    }

    void Processor::setStateInformation(const void* data, int sizeInBytes)
    {
        const auto text = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
        const auto state = juce::JSON::parse(text);
        Project p;
        if (projectFromJson(state.getProperty("project", "").toString(), p).wasOk())
        {
            replaceProject(p, false);
            undoStack.clear();
            redoStack.clear();
        }
        if (const auto path = state.getProperty("file", "").toString(); juce::File::isAbsolutePath(path))
            file = juce::File(path);
        setFollowHostPlay(static_cast<bool>(state.getProperty("followHostPlay", true)));
        ownOutputs.store(static_cast<std::uint32_t>(static_cast<int>(state.getProperty("ownOutputs", 0))) & ((1u << laneCount) - 1u));
    }

    // ---- Transport --------------------------------------------------------------------------------------------

    void Processor::setChain(const std::vector<int>& patterns)
    {
        if (patterns.empty())
            return;
        chainOnUi = patterns;
        const juce::SpinLock::ScopedLockType lock(handover);
        pendingChainLength = std::min(static_cast<int>(patterns.size()), coolsynth::synth::StepSequencer::maxChain);
        for (int i = 0; i < pendingChainLength; ++i)
            pendingChain[static_cast<std::size_t>(i)] = patterns[static_cast<std::size_t>(i)];
        chainDirty.store(true, std::memory_order_release);
        if (cueing)
            pendingChainFromBar = true; // stays so while the edits wait, whatever chain joins them
        markCue();
    }

    void Processor::setAccuracy(int level)
    {
        level = juce::jlimit(0, 2, level);
        const bool oversampledBefore = accuracyLevel < 2, oversampled = level < 2;
        accuracyLevel = level;
        watch.setLighter(level >= 1);
        if (oversampled == oversampledBefore)
            return;
        engine.setOversampling(oversampled);
        if (! prepared)
            return;
        // The voices' rate changes: the engine prepared again while the audio thread skips its blocks.
        suspendProcessing(true);
        engine.prepare(rate, preparedBlock, std::max(1, getTotalNumOutputChannels()));
        suspendProcessing(false);
    }

    void Processor::startMcpServer()
    {
        if (mcp != nullptr)
            return;
        // A tool runs on the message thread, as the panel's own edits do, and the server waits for it (10 s at most).
        coolsynth::mcp::Server::Runner runner = [alive = alive](std::function<void()> job)
        {
            if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            {
                job();
                return;
            }
            auto done = std::make_shared<juce::WaitableEvent>();
            juce::MessageManager::callAsync([alive, done, job = std::move(job)]
            {
                if (alive->load())
                    job();
                done->signal();
            });
            done->wait(10000);
        };
        mcp = std::make_unique<coolsynth::mcp::Server>("CoolSynth 808", "0.1", tools808(*this, runner), runner);
        if (mcp->start(mcpPort))
            mcpStatusText = "On: Claude reaches this app at http://127.0.0.1:" + juce::String(mcpPort) + "/mcp.";
        else
        {
            mcp = nullptr;
            mcpStatusText = "Could not start: port " + juce::String(mcpPort) + " is taken (another CoolSynth 808?).";
        }
    }

    void Processor::startCapture(double seconds)
    {
        captureArmed.store(false, std::memory_order_release);
        // The audio thread may still be inside its last copy: wait out a block before the buffer is touched.
        juce::Thread::sleep(static_cast<int>(1000.0 * 2048.0 / std::max(1.0, rate)) + 5);
        captureBuffer.assign(static_cast<std::size_t>(juce::jlimit(0.1, 60.0, seconds) * rate), 0.0f);
        captureCount.store(0, std::memory_order_release);
        captureArmed.store(true, std::memory_order_release);
    }

    void Processor::setDrumNotes(const DrumNotes& notes) noexcept
    {
        for (std::size_t s = 0; s < notes.size(); ++s)
            drumNoteOf[s].store(notes[s], std::memory_order_relaxed);
        rebuildDrumNotes();
        drumNotesChanged.fetch_add(1, std::memory_order_acq_rel);
    }

    DrumNotes Processor::drumNotes() const noexcept
    {
        DrumNotes out {};
        for (std::size_t s = 0; s < out.size(); ++s)
            out[s] = drumNoteOf[s].load(std::memory_order_relaxed);
        return out;
    }

    void Processor::rebuildDrumNotes() noexcept
    {
        // Each note from outside to the drum's own note; with General MIDI's set, its other keys for the same drums
        // too (laneOfNote's: 35, 40, 41, 44, 45, 48, 51, 57), as before.
        const auto notes = drumNotes();
        std::array<int, 128> table {};
        table.fill(-1);
        if (drumNoteSetOf(notes) == 0)
            for (const auto [from, to] : { std::pair { 35, 36 }, { 40, 38 }, { 41, 43 }, { 44, 42 }, { 45, 47 }, { 48, 50 }, { 51, 49 }, { 57, 49 } })
                table[static_cast<std::size_t>(from)] = to;
        for (int s = 0; s < drumSlots; ++s)
            if (const int n = notes[static_cast<std::size_t>(s)]; n >= 0 && n < 128)
                table[static_cast<std::size_t>(n)] = drumSlotEngineNote(s);
        for (std::size_t i = 0; i < table.size(); ++i)
            engineNoteFrom[i].store(table[i], std::memory_order_relaxed);
    }

    void Processor::strike(int lane, bool accent)
    {
        const int note = laneNote(lane, model.kit);
        if (note < 0)
            return;
        // Through a FIFO the audio thread never waits on (the review of D267: a collector's lock).
        if (const auto scope = strikeFifo.write(1); scope.blockSize1 > 0)
            strikes[static_cast<std::size_t>(scope.startIndex1)] = { note, accent };
    }

    int Processor::padIndex(const coolsynth::midi::ControlId& control) noexcept
    {
        if (! control.isValid())
            return -1;
        return ((control.kind == coolsynth::midi::ControlId::Kind::note ? 16 : 0) + control.channel - 1) * 128 + control.number;
    }

    void Processor::setPadNotes(const PadNotes& plain, const PadNotes& shifted) noexcept
    {
        for (std::size_t i = 0; i < plain.size(); ++i)
        {
            padNotes[i].store(plain[i], std::memory_order_relaxed);
            padNotesShifted[i].store(shifted[i], std::memory_order_relaxed);
        }
    }

    void Processor::takeControllerEvents(const std::function<void(const coolsynth::midi::ControllerEvent&)>& use)
    {
        // In turns, so a controller sending on and on never holds the message thread for long.
        for (int round = 0; round < 8; ++round)
        {
            const auto scope = controllerFifo.read(controllerFifo.getNumReady());
            if (scope.blockSize1 + scope.blockSize2 == 0)
                return;
            scope.forEach([&](int index) { use(controllerEvents[static_cast<std::size_t>(index)]); });
        }
    }

    bool Processor::applyRecordedHits()
    {
        const int ready = recordedFifo.getNumReady();
        if (ready <= 0)
            return false;
        // A recording pass is one undo step, however many hits it brings (the review of D267).
        if (! recordingStepTaken)
            pushUndo();
        recordingStepTaken = true;
        ++edits;
        const auto scope = recordedFifo.read(ready);
        auto& bank = model.currentBank();
        const auto apply = [&](int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& hit = recordedHits[static_cast<std::size_t>(start + i)];
                auto& pattern = hit.pattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(hit.pattern)];
                auto& lane = pattern.lanes[static_cast<std::size_t>(hit.lane)];
                auto& stepsOf = hit.variationB ? lane.b : lane.a;
                auto& step = stepsOf[static_cast<std::size_t>(hit.step)];
                step.on = true;
                step.chance = 1.0f;
            }
        };
        apply(scope.startIndex1, scope.blockSize1);
        apply(scope.startIndex2, scope.blockSize2);
        pushBank();
        return true;
    }

    // ---- Audio thread -----------------------------------------------------------------------------------------

    void Processor::pullFromUi(bool barDue, std::int64_t bar) noexcept
    {
        const auto held = [&]
        {
            return cueWaiting.load(std::memory_order_acquire) && (! barDue || cueOpen.load(std::memory_order_acquire));
        };
        if (held())
            return; // waiting for its bar line
        if (! (kitDirty.load(std::memory_order_acquire) || playDirty.load(std::memory_order_acquire)
               || bankDirty.load(std::memory_order_acquire) || chainDirty.load(std::memory_order_acquire)))
            return;
        const juce::SpinLock::ScopedTryLockType lock(handover);
        if (! lock.isLocked())
            return; // the message thread is handing something over: next block
        if (held())
            return; // ...and what it handed over waits for a bar line
        if (kitDirty.exchange(false))
        {
            kitOnAudio = pending.kit;
            fillEngineSettings(kitOnAudio, engineSettings);
        }
        if (playDirty.exchange(false))
        {
            playOnAudio = pending.play;
            coolsynth::synth::SequencerSettings s;
            s.swing = playOnAudio.swing;
            s.variation = playOnAudio.variation;
            s.loop = playOnAudio.loop;
            sequencer.setSettings(s);
        }
        if (bankDirty.exchange(false))
            sequencer.setBank(*pending.bank);
        if (chainDirty.exchange(false) && pendingChainLength > 0)
        {
            if (pendingChainFromBar && barDue)
                sequencer.setChainFrom(pendingChain.data(), pendingChainLength, bar);
            else
                sequencer.setChain(pendingChain.data(), pendingChainLength);
        }
        pendingChainFromBar = false;
        cueWaiting.store(false, std::memory_order_release);
    }

    void Processor::sendClock(juce::MidiBuffer& out, double beat0, double beat1, int numSamples, bool started, bool stopped,
                              int offsetInBlock)
    {
        if (stopped)
            out.addEvent(juce::MidiMessage::midiStop(), 0);
        if (started)
            out.addEvent(juce::MidiMessage::midiStart(), 0);
        if (! playing)
            return;
        // A pulse goes out at the first sample at or after its time, and which block it falls in is decided by that
        // whole sample, as the sequencer decides its notes': a pulse on two blocks' edge went out in both.
        const double beatsPerSample = (beat1 - beat0) / numSamples;
        const auto first = static_cast<std::int64_t>(std::floor(beat0 * 24.0)) - 1;
        const auto last = static_cast<std::int64_t>(std::ceil(beat1 * 24.0)) + 1;
        for (auto m = first; m <= last; ++m)
        {
            const auto offset = static_cast<std::int64_t>(std::ceil((static_cast<double>(m) / 24.0 - beat0) / beatsPerSample - 1.0e-4));
            if (offset >= 0 && offset < numSamples)
                out.addEvent(juce::MidiMessage::midiClock(), offsetInBlock + static_cast<int>(offset));
        }
    }

    bool Processor::takeControl(const juce::MidiMessage& m, int samplePosition, int n) noexcept
    {
        const auto event = coolsynth::midi::toControllerEvent(m);
        if (! event)
            return false;
        if (const auto scope = controllerFifo.write(1); scope.blockSize1 > 0)
            controllerEvents[static_cast<std::size_t>(scope.startIndex1)] = *event;
        const int index = padIndex(event->control);
        const bool down = event->note ? event->value > 0 : event->value >= 64;
        const int plain = index >= 0 ? padNotes[static_cast<std::size_t>(index)].load(std::memory_order_relaxed) : -1;
        if (plain == shiftMark)
        {
            shiftDown = down;
            return false;
        }
        const int note = index < 0 ? -1
                       : shiftDown ? padNotesShifted[static_cast<std::size_t>(index)].load(std::memory_order_relaxed)
                                   : plain;
        if (note < 0 || ! down)
            return false;
        // Hit hard, a pad is accented: an 808's drum is accented or not (D268).
        const auto velocity = static_cast<juce::uint8>(! event->note || event->value >= 100 ? 127 : 1);
        keptBlock.addEvent(juce::MidiMessage::noteOn(midiChannel, note, velocity), samplePosition);
        keptBlock.addEvent(juce::MidiMessage::noteOff(midiChannel, note), std::min(n - 1, samplePosition + 1));
        return true;
    }

    void Processor::takeControllerMessage(const juce::MidiMessage& message) noexcept
    {
        if (message.getRawDataSize() != 3 || ! (message.isNoteOnOrOff() || message.isController()))
            return;
        const juce::SpinLock::ScopedLockType lock(routedWriters);
        if (const auto scope = routedFifo.write(1); scope.blockSize1 > 0)
        {
            auto& r = routedMessages[static_cast<std::size_t>(scope.startIndex1)];
            const auto* data = message.getRawData();
            r.bytes = { data[0], data[1], data[2] };
            r.milliseconds = juce::Time::getMillisecondCounterHiRes();
        }
    }

    void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        const juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        buffer.clear();
        if (n <= 0)
            return;

        pullFromUi();

        // The host's parameters (D343): what they say plays (the panel's edits set them too). A kit waiting for a bar
        // line (an audition, D274) keeps its turn.
        if (! cueWaiting.load(std::memory_order_acquire))
        {
            const auto fromParams = kitFromParams(kitOnAudio);
            if (! sameKit(fromParams, kitOnAudio))
            {
                const bool switched = fromParams.alt != kitOnAudio.alt;
                kitOnAudio = fromParams;
                fillEngineSettings(kitOnAudio, engineSettings);
                if (switched)
                {
                    // a switch thrown by the host: its lanes play the other drum's note from here
                    *notesBank = sequencer.bank();
                    applyNotes(*notesBank, kitOnAudio);
                    sequencer.setBank(*notesBank);
                }
            }
        }

        // A controller's linked controls go to the panel, not the sound (D211, D268); its pads strike their drums here,
        // without waiting for the message thread. The standalone app sorts its inputs first (D271): the block's MIDI
        // is then the sound's, and the controller's own messages come from its input's queue.
        keptBlock.clear();
        bool changed = false;
        const bool routedNow = inputsRouted.load(std::memory_order_relaxed);
        bool translated = false;
        for (const auto metadata : midi)
        {
            auto m = metadata.getMessage();
            if (! routedNow)
            {
                if (coolsynth::midi::ControllerSetup::wanted(m))
                    changed = takeControl(m, metadata.samplePosition, n) || changed;
                if (coolsynth::midi::ControllerSetup::takesFromSound(m))
                {
                    changed = true;
                    continue;
                }
            }
            // A drum's note from outside (D329), past the controller's own: learned when asked, then the drum's own
            // note for everything after.
            if (m.isNoteOnOrOff())
            {
                const int learning = learningSlot.load(std::memory_order_acquire);
                if (learning >= 0 && learning < drumSlots && m.isNoteOn())
                {
                    for (int s = 0; s < drumSlots; ++s)
                        if (drumNoteOf[static_cast<std::size_t>(s)].load(std::memory_order_relaxed) == m.getNoteNumber())
                            drumNoteOf[static_cast<std::size_t>(s)].store(-1, std::memory_order_relaxed);
                    drumNoteOf[static_cast<std::size_t>(learning)].store(m.getNoteNumber(), std::memory_order_relaxed);
                    rebuildDrumNotes();
                    learningSlot.store(-1, std::memory_order_release);
                    drumNotesChanged.fetch_add(1, std::memory_order_acq_rel);
                }
                const int own = engineNoteFrom[static_cast<std::size_t>(m.getNoteNumber() & 127)].load(std::memory_order_relaxed);
                translated = true;
                if (own < 0)
                    continue; // a note no drum has
                m.setNoteNumber(own);
            }
            keptBlock.addEvent(m, metadata.samplePosition);
        }
        {
            // Placed in the block by when they came, as JUCE's MIDI collector places the sound's notes: the time since
            // the last block, squeezed into this one when it was longer.
            const double now = juce::Time::getMillisecondCounterHiRes();
            const double since = lastBlockMilliseconds > 0.0 ? now - lastBlockMilliseconds : 0.0;
            const double elapsed = since * 0.001 * rate;
            const auto scope = routedFifo.read(routedFifo.getNumReady());
            scope.forEach([&](int index)
            {
                const auto& r = routedMessages[static_cast<std::size_t>(index)];
                double at = since > 0.0 ? (r.milliseconds - lastBlockMilliseconds) * 0.001 * rate : 0.0;
                if (elapsed > static_cast<double>(n))
                    at *= static_cast<double>(n) / elapsed;
                const int offset = std::clamp(static_cast<int>(at), 0, n - 1);
                const juce::MidiMessage m(r.bytes[0], r.bytes[1], r.bytes[2]);
                changed = takeControl(m, offset, n) || changed;
            });
            lastBlockMilliseconds = now;
        }
        if (changed || translated)
        {
            // Copied back rather than swapped: the reserved buffer stays the audio thread's (the review of D267).
            midi.clear();
            midi.addEvents(keptBlock, 0, n, 0);
        }

        // The drums struck on the panel, at the block's start.
        {
            const auto scope = strikeFifo.read(strikeFifo.getNumReady());
            scope.forEach([&](int index)
            {
                const auto& s = strikes[static_cast<std::size_t>(index)];
                midi.addEvent(juce::MidiMessage::noteOn(midiChannel, s.note, static_cast<juce::uint8>(s.accent ? 127 : 1)), 0);
                midi.addEvent(juce::MidiMessage::noteOff(midiChannel, s.note), n - 1);
            });
        }

        // The clock's messages first: they say whether the transport is ours or the clock's.
        for (const auto metadata : midi)
        {
            const auto& m = metadata.getMessage();
            const auto at = samplesSoFar + metadata.samplePosition;
            if (m.isMidiClock())
                clock.pulse(at);
            else if (m.isMidiStart())
                clock.start(at);
            else if (m.isMidiContinue())
                clock.resume();
            else if (m.isMidiStop())
                clock.stop();
            else if (m.isSongPositionPointer())
                clock.songPosition(m.getSongPositionPointerMidiBeat());
        }
        // In a DAW (D311, D325): its transport leads while it plays, from where it is, at its tempo, unless the switch
        // keeps the pattern to its own Play key; its tempo leads always. A clock leads only the standalone app.
        bool hostPlays = false;
        double hostBeat = 0.0, hostBpm = 0.0;
        if (wrapperType != wrapperType_Standalone && ! offline)
            if (auto* head = getPlayHead())
                if (const auto position = head->getPosition())
                {
                    if (const auto b = position->getBpm())
                        hostBpm = *b;
                    if (const auto ppq = position->getPpqPosition(); ppq && position->getIsPlaying())
                    {
                        hostPlays = true;
                        hostBeat = std::max(0.0, *ppq);
                    }
                }
        const bool byHost = hostPlays && followHostPlayOn.load(std::memory_order_relaxed);
        hostFollowed.store(byHost, std::memory_order_release);
        const bool byClock = ! byHost && followClockOn.load(std::memory_order_relaxed) && clock.present(samplesSoFar);
        clockFollowed.store(byClock, std::memory_order_release);

        bool started = false, stopped = false;
        const int request = playRequest.exchange(-1, std::memory_order_acq_rel);
        // Following a clock, it plays while the clock plays; a clock gone while it played stops it, rather than going
        // on from a stale place on its own (the review of D267).
        bool wantPlay = byHost ? true : byClock ? clock.isPlaying() : (request < 0 ? playing : request == 1);
        if (! byClock && playing && playingByClock)
            wantPlay = false;
        if (! byHost && playing && playingByHost)
            wantPlay = false; // the host stopped
        const bool wasOwnClock = playing && ! playingByClock && ! playingByHost;
        if (wantPlay && ! playing)
        {
            playing = true;
            started = true;
            playingByClock = byClock;
            playingByHost = byHost;
            if (! byClock && ! byHost)
                beat = 0.0;
            engine.reseed(); // a defined start (D224)
        }
        else if (! wantPlay && playing)
        {
            playing = false;
            stopped = true;
            playingByHost = false;
        }

        // A jump asked for (the WAV export's loop, D331): from there on, what rings ringing on.
        if (const double to = jumpRequest.exchange(-1.0, std::memory_order_acq_rel); to >= 0.0 && playing && ! byClock && ! byHost)
        {
            beat = to;
            engine.reseed();
        }

        double beat0 = beat, bpm = playOnAudio.tempo;
        if (byClock)
            clock.block(samplesSoFar, n, beat0, bpm);
        if (hostBpm > 0.0)
            bpm = juce::jlimit(20.0, 400.0, hostBpm); // the host's tempo, whoever leads
        if (byHost)
            beat0 = hostBeat;

        // Edits waiting for a bar line (an audition in time, D274): stopped or just started, they come in at once;
        // playing, on the first bar line at or after this block's start. In the block, it is split there: the part
        // before plays what was, the part after what came, its tempo and kit too.
        int split = n;
        std::int64_t cueBar = 0;
        if (cueWaiting.load(std::memory_order_acquire))
        {
            if (! playing || started)
                pullFromUi(true, 0);
            else
            {
                cueBar = static_cast<std::int64_t>(std::ceil((beat0 - 1.0e-6) / 4.0));
                const double at = std::ceil((static_cast<double>(cueBar) * 4.0 - beat0) / (bpm / 60.0 / rate) - 1.0e-4);
                if (at <= 0.0)
                    pullFromUi(true, cueBar);
                else if (at < static_cast<double>(n))
                    split = static_cast<int>(at);
            }
            if (! byClock && hostBpm <= 0.0)
                bpm = playOnAudio.tempo;
        }
        const double beatsPerSample = bpm / 60.0 / rate;

        if (fillRequest.exchange(false, std::memory_order_acq_rel))
            sequencer.requestFill();

        events.clear();
        const int room = static_cast<int>(sequencerEvents.size());
        int count = sequencer.process(beat0, bpm, playing, split, rate, sequencerEvents.data(), room);
        double bpmAfter = bpm, beatsPerSampleAfter = beatsPerSample;
        const double beatAtSplit = beat0 + split * beatsPerSample;
        if (split < n)
        {
            settingsBeforeSplit = engineSettings;
            pullFromUi(true, cueBar);
            if (! byClock && hostBpm <= 0.0)
            {
                bpmAfter = playOnAudio.tempo;
                beatsPerSampleAfter = bpmAfter / 60.0 / rate;
            }
            const int more = sequencer.process(beatAtSplit, bpmAfter, playing, n - split, rate, sequencerEvents.data() + count, room - count);
            for (int i = count; i < count + more; ++i)
                sequencerEvents[static_cast<std::size_t>(i)].sampleOffset += split;
            count += more;
        }
        const auto beatAt = [&](int sample)
        {
            return sample < split ? beat0 + sample * beatsPerSample : beatAtSplit + (sample - split) * beatsPerSampleAfter;
        };
        std::uint32_t hitLanes = 0;
        for (int i = 0; i < count; ++i)
        {
            const auto& e = sequencerEvents[static_cast<std::size_t>(i)];
            events.push_back(e);
            if (e.type == coolsynth::synth::EngineMidiEventType::noteOn)
            {
                bool alt = false;
                if (const int lane = laneOfNote(e.noteNumber, alt); lane >= 0)
                    hitLanes |= 1u << lane;
                if (e.value > 0.5f)
                    hitLanes |= accentBit;
            }
        }

        // Notes played on MIDI or struck on the panel: heard at once, and written down when recording.
        const bool rec = recording.load(std::memory_order_acquire) && playing;
        for (const auto metadata : midi)
        {
            const auto& m = metadata.getMessage();
            if (! (m.isNoteOn() || m.isNoteOff()) || static_cast<int>(events.size()) >= maxEvents)
                continue;
            coolsynth::synth::EngineMidiEvent e;
            e.type = m.isNoteOn() ? coolsynth::synth::EngineMidiEventType::noteOn : coolsynth::synth::EngineMidiEventType::noteOff;
            e.sampleOffset = juce::jlimit(0, n - 1, metadata.samplePosition);
            e.noteNumber = static_cast<std::uint8_t>(m.getNoteNumber());
            e.value = m.isNoteOn() ? m.getFloatVelocity() : 0.0f;
            events.push_back(e);
            if (! m.isNoteOn())
                continue;
            bool alt = false;
            const int lane = laneOfNote(m.getNoteNumber(), alt);
            if (lane < 0)
                continue;
            hitLanes |= 1u << lane;
            // Past the end of a chain that does not loop there is no pattern to write into (D297).
            if (const auto place = sequencer.placeAt(beatAt(metadata.samplePosition)); rec && place.pattern >= -1)
            {
                const auto& bank = sequencer.bank();
                const auto& pattern = place.pattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(place.pattern)];
                const int length = juce::jlimit(1, SequencerLane::steps, pattern.lanes[static_cast<std::size_t>(lane)].length);
                const auto scope = recordedFifo.write(1);
                if (scope.blockSize1 > 0)
                    recordedHits[static_cast<std::size_t>(scope.startIndex1)] = { place.pattern, place.variationB, lane,
                                                                                    static_cast<int>(((place.sixteenth % length) + length) % length), alt };
            }
        }
        // In time order, without allocating: an insertion sort of the few events.
        for (std::size_t i = 1; i < events.size(); ++i)
        {
            const auto e = events[i];
            std::size_t j = i;
            while (j > 0 && events[j - 1].sampleOffset > e.sampleOffset)
            {
                events[j] = events[j - 1];
                --j;
            }
            events[j] = e;
        }

        // The drums on their own outputs (D343): a lane switched to its own output, while the host has that output on,
        // leaves the main mix and comes out there alone, from the engine's tap.
        using Watch = coolsynth::synth::v3::CircuitWatch;
        static constexpr std::array<int, laneCount> lightOfLane { Watch::bassDrum, Watch::snare, Watch::lowTom, Watch::midTom, Watch::highTom,
                                                                  Watch::rimShot, Watch::clap, Watch::cowbell, Watch::cymbal, Watch::openHat,
                                                                  Watch::closedHat };
        std::uint32_t separate = 0;
        const auto own = ownOutputs.load(std::memory_order_acquire);
        for (int lane = 0; lane < laneCount; ++lane)
            if (((own >> lane) & 1u) != 0u && lane + 1 < getBusCount(false) && getBus(false, lane + 1)->isEnabled()
                && n <= static_cast<int>(taps[static_cast<std::size_t>(lightOfLane[static_cast<std::size_t>(lane)])].size()))
                separate |= 1u << lightOfLane[static_cast<std::size_t>(lane)];
        watch.setSeparate(separate);
        auto mainBus = getBusBuffer(buffer, false, 0);
        float* const* tapOut = separate != 0 ? tapPointers.data() : nullptr;

        coolsynth::synth::EngineTransportInfo transport;
        transport.hostHasTempo = true;
        transport.hostBpm = bpm;
        transport.hostHasPpq = true;
        transport.hostPpqAtBlockStart = beat0;
        transport.hostIsPlaying = playing;
        if (split == n)
            engine.render(mainBus, std::span<const coolsynth::synth::EngineMidiEvent>(events.data(), events.size()), engineSettings, transport, tapOut);
        else
        {
            // The two parts of a split block, each with its own events, kit and tempo.
            std::size_t before = 0;
            while (before < events.size() && events[before].sampleOffset < split)
                ++before;
            juce::AudioBuffer<float> first(mainBus.getArrayOfWritePointers(), mainBus.getNumChannels(), 0, split);
            engine.render(first, std::span<const coolsynth::synth::EngineMidiEvent>(events.data(), before), settingsBeforeSplit, transport, tapOut);
            for (std::size_t i = before; i < events.size(); ++i)
                events[i].sampleOffset -= split;
            transport.hostBpm = bpmAfter;
            transport.hostPpqAtBlockStart = beatAtSplit;
            juce::AudioBuffer<float> second(mainBus.getArrayOfWritePointers(), mainBus.getNumChannels(), split, n - split);
            std::array<float*, Watch::lightCount> secondTaps {};
            for (std::size_t l = 0; l < secondTaps.size(); ++l)
                secondTaps[l] = tapPointers[l] + split;
            engine.render(second, std::span<const coolsynth::synth::EngineMidiEvent>(events.data() + before, events.size() - before),
                          engineSettings, transport, tapOut != nullptr ? secondTaps.data() : nullptr);
        }
        // each drum's own output: its tap, or silence while it plays in the main
        for (int lane = 0; lane < laneCount && lane + 1 < getBusCount(false); ++lane)
            if (getBus(false, lane + 1)->isEnabled())
            {
                auto out = getBusBuffer(buffer, false, lane + 1);
                const auto light = static_cast<std::size_t>(lightOfLane[static_cast<std::size_t>(lane)]);
                if (((separate >> light) & 1u) != 0u)
                    out.copyFrom(0, 0, taps[light].data(), n);
                else
                    out.clear();
            }
        const double beatEnd = beatAt(n);

        midi.clear();
        if (! byClock && ! byHost && ! isPlugin() && ! offline && sendClockOn.load(std::memory_order_relaxed))
        {
            sendClock(midi, beat0, beatAtSplit, split, started, stopped);
            if (split < n)
                sendClock(midi, beatAtSplit, beatEnd, n - split, false, false, split);
        }
        else if (stopped && wasOwnClock)
            midi.addEvent(juce::MidiMessage::midiStop(), 0); // a clock came in while it led: what it led stops too

        if (playing && ! byClock)
            beat = beatEnd; // led by the host, the next block's start comes from it again
        samplesSoFar += n;
        // A chain that does not loop has played through: its own transport stops (D297). A clock leads on its own.
        if (playing && ! byClock && ! byHost && ! playOnAudio.loop && sequencer.endedAt(beat))
            playRequest.store(0, std::memory_order_release);

        if (playing)
        {
            // The step that plays: the nearest to a moment just under half a step ago, so the light moves on the step.
            const auto place = sequencer.placeAt(std::max(0.0, beat0 - 0.124));
            patternNow.store(place.pattern, std::memory_order_release);
            variationBNow.store(place.variationB, std::memory_order_release);
            sixteenthNow.store(place.sixteenth, std::memory_order_release);
        }
        playingNow.store(playing, std::memory_order_release);
        bpmNow.store(bpm, std::memory_order_release);
        if (hitLanes != 0)
            struck.fetch_or(hitLanes, std::memory_order_acq_rel);

        // For Claude's tools (D333): the loudest sample and the block's level; the left channel captured when asked.
        if (buffer.getNumChannels() > 0)
        {
            const float* left = buffer.getReadPointer(0);
            float peakNow = 0.0f;
            for (int i = 0; i < n; ++i)
                peakNow = std::max(peakNow, std::abs(left[i]));
            lastLevel.store(peakNow, std::memory_order_relaxed);
            float seen = loudest.load(std::memory_order_relaxed);
            while (peakNow > seen && ! loudest.compare_exchange_weak(seen, peakNow, std::memory_order_relaxed))
            {
            }
            if (captureArmed.load(std::memory_order_acquire))
            {
                const int at = captureCount.load(std::memory_order_relaxed);
                const int room = static_cast<int>(captureBuffer.size()) - at;
                const int take = std::min(room, n);
                if (take > 0)
                {
                    std::copy(left, left + take, captureBuffer.begin() + at);
                    captureCount.store(at + take, std::memory_order_release);
                }
                else
                    captureArmed.store(false, std::memory_order_release);
            }
        }
    }

    juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new cs808::Processor(); }
