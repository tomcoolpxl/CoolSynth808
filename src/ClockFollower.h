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
            justStarted = false;
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
                pulseBeat += 1.0 / pulsesPerBeat;
        }

        void start(std::int64_t at) noexcept
        {
            running = true;
            pulseBeat = -1.0 / pulsesPerBeat; // the first pulse after a start is beat 0
            beat = 0.0;
            justStarted = true;
            (void) at;
        }

        void resume() noexcept { running = true; }
        void stop() noexcept { running = false; }

        /** Song position pointer: sixteenths from the start. */
        void songPosition(int sixteenths) noexcept
        {
            pulseBeat = static_cast<double>(sixteenths) * 0.25 - 1.0 / pulsesPerBeat;
            beat = static_cast<double>(sixteenths) * 0.25;
        }

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
            // Where the pulses put the block's start: the last pulse's beat plus the time since, no further than
            // the next pulse.
            double target = beat;
            if (lastPulse >= 0 && pulseBeat >= 0.0)
                target = pulseBeat + std::min(static_cast<double>(now - lastPulse) * beatsPerSample, 1.0 / pulsesPerBeat);
            const double error = target - beat;
            if (std::abs(error) > 0.5 && ! justStarted)
                beat = target; // far off (a song position, a missed start): go there at once
            justStarted = false;
            const double bend = std::clamp(error * 2.0, -0.05, 0.05);
            blockBpm = baseBpm * (1.0 + bend);
            beatAtStart = beat;
            beat += static_cast<double>(samples) * blockBpm / 60.0 / sampleRate;
        }

    private:
        static constexpr double pulsesPerBeat = 24.0;
        static constexpr double smoothing = 0.1;

        double sampleRate = 48000.0;
        std::int64_t lastPulse = -1;
        double intervalEstimate = 0.0;
        double pulseBeat = -1.0;
        double beat = 0.0;
        bool running = false;
        bool justStarted = false;
    };
}
