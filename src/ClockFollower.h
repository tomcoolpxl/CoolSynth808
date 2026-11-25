#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

/*
    Follows an incoming MIDI clock (24 pulses a quarter note, start, continue, stop, song position) as a beat
    position that never jumps: its own beat runs on at the tempo the pulses give, and its speed bends a little
    toward where the pulses say it should be. A computer's MIDI arrives with jitter of a millisecond or two; a
    position set from each pulse would step back and forth, and the sequencer would take each step as a jump.

    Times are in samples since the app started. The audio thread only.
*/
namespace cs808
{
    class ClockFollower final
    {
    public:
        void prepare(double newSampleRate) noexcept
        {
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
            lastPulse = -1;
            intervalEstimate = 0.0;
            pulseBeat = -1.0;
            beat = 0.0;
            running = false;
            pulsed = false;
            snap = false;
        }

        void pulse(std::int64_t at) noexcept
        {
            if (lastPulse >= 0)
            {
                const double interval = static_cast<double>(at - lastPulse);
                // A gap of over a second is a pause, not a tempo.
                if (interval > 0.0 && interval < sampleRate)
                    intervalEstimate = intervalEstimate <= 0.0 ? interval : intervalEstimate + (interval - intervalEstimate) * smoothing;
            }
            lastPulse = at;
            if (running)
            {
                pulseBeat += 1.0 / pulsesPerBeat;
                pulsed = true;
            }
        }

        void start(std::int64_t at) noexcept
        {
            running = true;
            pulseBeat = -1.0 / pulsesPerBeat; // the first pulse after a start is beat 0
            beat = 0.0;
            pulsed = false; // it plays from that pulse, not from the block the start came in (the review of D267)
            snap = true;
            (void) at;
        }

        /** Continue: from where the pulses say, at once (the review of D267: it had run on while stopped). */
        void resume() noexcept
        {
            running = true;
            pulsed = true;
            snap = true;
        }
        void stop() noexcept { running = false; }

        /** The MIDI comes a block late: JUCE's standalone player hands a message arriving during one audio block to
            the next, at the same place in it, so a follower aiming at the pulses plays a block (some 10 ms) behind
            its leader. On, it aims a block further on and plays with it (CoolSynth's D359). Off in a plugin and the
            tests, where the MIDI comes in the block it belongs to. */
        void setBlockLate(bool on) noexcept { blockLate = on; }

        /** Song position pointer: sixteenths from the start. */
        void songPosition(int sixteenths) noexcept
        {
            pulseBeat = static_cast<double>(sixteenths) * 0.25 - 1.0 / pulsesPerBeat;
            beat = static_cast<double>(sixteenths) * 0.25;
            snap = true;
        }

        /** Running, and the first pulse since the start has come: the transport plays. */
        [[nodiscard]] bool isPlaying() const noexcept { return running && pulsed; }

        /** A clock is there: a pulse within the last half second. */
        [[nodiscard]] bool present(std::int64_t now) const noexcept
        {
            return lastPulse >= 0 && static_cast<double>(now - lastPulse) < sampleRate * 0.5;
        }

        [[nodiscard]] bool isRunning() const noexcept { return running; }

        [[nodiscard]] double bpm() const noexcept
        {
            return intervalEstimate > 0.0 ? 60.0 * sampleRate / (intervalEstimate * pulsesPerBeat) : 120.0;
        }

        /** The beat at the start of a block of `samples` starting at `now`, and its tempo for the block (bent toward
            the pulses); advances the follower's own beat past the block. */
        void block(std::int64_t now, int samples, double& beatAtStart, double& blockBpm) noexcept
        {
            const double baseBpm = bpm();
            const double beatsPerSample = baseBpm / 60.0 / sampleRate;
            // Stopped, its beat stays where the pulses left it (the review of D267: it ran on at 95 %).
            if (! isPlaying())
            {
                beatAtStart = beat;
                blockBpm = baseBpm;
                return;
            }
            // Where the pulses put the block's start: the last pulse's beat plus the time since, no further than
            // the next pulse. A pulse later in this block puts the start before it.
            double target = beat;
            // Handed a block late (the standalone's MIDI), the pulses say where the last block began: aimed a block on.
            const std::int64_t aimed = now + (blockLate ? samples : 0);
            if (lastPulse >= 0 && pulseBeat >= 0.0)
                target = pulseBeat + std::min(static_cast<double>(aimed - lastPulse) * beatsPerSample, 1.0 / pulsesPerBeat);
            if (snap || std::abs(target - beat) > 0.5)
                beat = target; // a start, a continue, a song position, or far off: there at once
            snap = false;
            const double error = target - beat;
            const double bend = std::clamp(error * 2.0, -0.05, 0.05);
            blockBpm = baseBpm * (1.0 + bend);
            beatAtStart = beat;
            beat += static_cast<double>(samples) * blockBpm / 60.0 / sampleRate;
        }

    private:
        static constexpr double pulsesPerBeat = 24.0;
        static constexpr double smoothing = 0.25; // a tempo change followed within a few pulses (the review of D267)

        double sampleRate = 48000.0;
        std::int64_t lastPulse = -1;
        double intervalEstimate = 0.0;
        double pulseBeat = -1.0;
        double beat = 0.0;
        bool running = false;
        bool pulsed = false; // a pulse since the start
        bool snap = false;   // go where the pulses say at the next block
        bool blockLate = false;
    };
}
