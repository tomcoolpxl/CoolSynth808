#include "Processor.h"

#include "Editor.h"

namespace cs808
{
    namespace
    {
        constexpr std::size_t maxUndo = 64;
        constexpr int maxEvents = 1024;
        constexpr int midiChannel = 10;
        constexpr std::uint32_t accentBit = 1u << laneCount;
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
    }

    Processor::~Processor() = default;

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
        panelNotes.reset(rate);
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
        if (inGesture)
            return;
        undoStack.push_back(std::make_unique<Project>(model));
        if (undoStack.size() > maxUndo)
            undoStack.erase(undoStack.begin());
        redoStack.clear();
    }

    void Processor::beginGesture()
    {
        pushUndo();
        inGesture = true;
    }

    void Processor::endGesture() { inGesture = false; }

    void Processor::editKit(const std::function<void(KitValues&)>& edit)
    {
        pushUndo();
        edit(model.kit);
        pushKit();
        pushBank(); // a switch changes the lanes' notes
    }

    void Processor::editPlay(const std::function<void(PlaySettings&)>& edit)
    {
        pushUndo();
        edit(model.play);
        pushPlay();
    }

    void Processor::editBank(const std::function<void(SequencerBank&)>& edit, bool undoable)
    {
        if (undoable)
            pushUndo();
        edit(model.currentBank());
        pushBank();
    }

    void Processor::replaceProject(const Project& p, bool undoable)
    {
        if (undoable)
            pushUndo();
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
        pushBank();
    }

    void Processor::addBank()
    {
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
        replaceProject(p, true);
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
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        panelNotes.addMessageToQueue(juce::MidiMessage::noteOn(midiChannel, note, static_cast<juce::uint8>(accent ? 127 : 1)).withTimeStamp(now));
        panelNotes.addMessageToQueue(juce::MidiMessage::noteOff(midiChannel, note).withTimeStamp(now + 0.05));
    }

    bool Processor::applyRecordedHits()
    {
        const int ready = recordedFifo.getNumReady();
        if (ready <= 0)
            return false;
        pushUndo();
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

    void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        const juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        buffer.clear();
        if (n <= 0)
            return;

        pullFromUi();

        juce::MidiBuffer panel;
        panelNotes.removeNextBlockOfMessages(panel, n);
        midi.addEvents(panel, 0, n, 0);

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
        const bool wantPlay = byClock ? clock.isRunning() : (request < 0 ? playing : request == 1);
        if (wantPlay && ! playing)
        {
            playing = true;
            started = true;
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
