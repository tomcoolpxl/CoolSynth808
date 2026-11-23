#include "Processor.h"

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

    Processor::Processor()
        : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        events.reserve(maxEvents);
        sequencerEvents.resize(512);
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
        for (auto& note : padNotes)
            note.store(-1, std::memory_order_relaxed);
        // The computer's controller setup (D211, D268): read once for the program. Tests (no wrapper) set their own.
        if (wrapperType == wrapperType_Standalone)
        {
            midiOutKeeper = makeStandaloneMidiOutKeeper(); // MIDI clock out at its own samples (the review of D267)
            midiInRouter = makeStandaloneMidiInRouter(*this); // the controller's input apart from the sound's (D271)
        }
        if (wrapperType != wrapperType_Undefined)
        {
            startedSetup = true;
            if (liveProcessors.fetch_add(1, std::memory_order_relaxed) == 0)
                coolsynth::midi::ControllerSetup::instance().start();
        }
    }

    Processor::~Processor()
    {
        midiInRouter.reset(); // gives the inputs back to the standalone's player before anything here goes
        // The last one: the setup's timers go while JUCE still runs (the review of D211).
        if (startedSetup && liveProcessors.fetch_sub(1, std::memory_order_relaxed) == 1)
            coolsynth::midi::ControllerSetup::instance().shutDown();
    }

    bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
               && layouts.getMainInputChannelSet().isDisabled();
    }

    void Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        engine.prepare(rate, std::max(1, samplesPerBlock), std::max(1, getTotalNumOutputChannels()));
        clock.prepare(rate);
        keptBlock.ensureSize(8192);
        sequencer.reset();
        playing = false;
        beat = 0.0;
        kitDirty = playDirty = bankDirty = true;
        chainDirty = true;
    }

    // ---- The project ------------------------------------------------------------------------------------------

    void Processor::pushKit()
    {
        const juce::SpinLock::ScopedLockType lock(handover);
        pending.kit = model.kit;
        kitDirty.store(true, std::memory_order_release);
    }

    void Processor::pushPlay()
    {
        const juce::SpinLock::ScopedLockType lock(handover);
        pending.play = model.play;
        playDirty.store(true, std::memory_order_release);
    }

    void Processor::pushBank()
    {
        auto copy = std::make_unique<SequencerBank>(model.currentBank());
        applyNotes(*copy, model.kit);
        const juce::SpinLock::ScopedLockType lock(handover);
        std::swap(pending.bank, copy);
        bankDirty.store(true, std::memory_order_release);
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

    void Processor::setPadNotes(const PadNotes& notes) noexcept
    {
        for (std::size_t i = 0; i < notes.size(); ++i)
            padNotes[i].store(notes[i], std::memory_order_relaxed);
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

    void Processor::pullFromUi() noexcept
    {
        if (! (kitDirty.load(std::memory_order_acquire) || playDirty.load(std::memory_order_acquire)
               || bankDirty.load(std::memory_order_acquire) || chainDirty.load(std::memory_order_acquire)))
            return;
        const juce::SpinLock::ScopedTryLockType lock(handover);
        if (! lock.isLocked())
            return; // the message thread is handing something over: next block
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
            sequencer.setSettings(s);
        }
        if (bankDirty.exchange(false))
            sequencer.setBank(*pending.bank);
        if (chainDirty.exchange(false) && pendingChainLength > 0)
            sequencer.setChain(pendingChain.data(), pendingChainLength);
    }

    void Processor::sendClock(juce::MidiBuffer& out, double beat0, double beat1, int numSamples, bool started, bool stopped)
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
                out.addEvent(juce::MidiMessage::midiClock(), static_cast<int>(offset));
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
        const int note = index >= 0 ? padNotes[static_cast<std::size_t>(index)].load(std::memory_order_relaxed) : -1;
        if (note < 0 || ! (event->note ? event->value > 0 : event->value >= 64))
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

        // A controller's linked controls go to the panel, not the sound (D211, D268); its pads strike their drums here,
        // without waiting for the message thread. The standalone app sorts its inputs first (D271): the block's MIDI
        // is then the sound's, and the controller's own messages come from its input's queue.
        keptBlock.clear();
        bool changed = false;
        const bool routedNow = inputsRouted.load(std::memory_order_relaxed);
        for (const auto metadata : midi)
        {
            const auto& m = metadata.getMessage();
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
        if (changed)
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
        const bool byClock = clock.present(samplesSoFar);
        clockFollowed.store(byClock, std::memory_order_release);

        bool started = false, stopped = false;
        const int request = playRequest.exchange(-1, std::memory_order_acq_rel);
        // Following a clock, it plays while the clock plays; a clock gone while it played stops it, rather than going
        // on from a stale place on its own (the review of D267).
        bool wantPlay = byClock ? clock.isPlaying() : (request < 0 ? playing : request == 1);
        if (! byClock && playing && playingByClock)
            wantPlay = false;
        const bool wasOwnClock = playing && ! playingByClock;
        if (wantPlay && ! playing)
        {
            playing = true;
            started = true;
            playingByClock = byClock;
            if (! byClock)
                beat = 0.0;
            engine.reseed(); // a defined start (D224)
        }
        else if (! wantPlay && playing)
        {
            playing = false;
            stopped = true;
        }

        double beat0 = beat, bpm = playOnAudio.tempo;
        if (byClock)
            clock.block(samplesSoFar, n, beat0, bpm);
        const double beatsPerSample = bpm / 60.0 / rate;

        if (fillRequest.exchange(false, std::memory_order_acq_rel))
            sequencer.requestFill();

        events.clear();
        const int count = sequencer.process(beat0, bpm, playing, n, rate, sequencerEvents.data(), static_cast<int>(sequencerEvents.size()));
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
            if (rec)
            {
                const auto place = sequencer.placeAt(beat0 + metadata.samplePosition * beatsPerSample);
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

        coolsynth::synth::EngineTransportInfo transport;
        transport.hostHasTempo = true;
        transport.hostBpm = bpm;
        transport.hostHasPpq = true;
        transport.hostPpqAtBlockStart = beat0;
        transport.hostIsPlaying = playing;
        engine.render(buffer, std::span<const coolsynth::synth::EngineMidiEvent>(events.data(), events.size()), engineSettings, transport);

        midi.clear();
        if (! byClock)
            sendClock(midi, beat0, beat0 + n * beatsPerSample, n, started, stopped);
        else if (stopped && wasOwnClock)
            midi.addEvent(juce::MidiMessage::midiStop(), 0); // a clock came in while it led: what it led stops too

        if (playing && ! byClock)
            beat = beat0 + n * beatsPerSample;
        samplesSoFar += n;

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
    }

    juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new cs808::Processor(); }
