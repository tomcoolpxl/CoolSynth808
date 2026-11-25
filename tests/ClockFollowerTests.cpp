// The clock follower handed its pulses a block late, as JUCE's standalone player does (CoolSynth's D359): aimed a block
// on, it plays with its leader; without, a block behind.

#include <cmath>
#include <cstdint>

#include <juce_core/juce_core.h>

#include "ClockFollower.h"

namespace
{
    class ClockFollowerTests final : public juce::UnitTest
    {
    public:
        ClockFollowerTests() : juce::UnitTest("Clock follower", "CoolSynth808") {}

        /** How far behind its leader the follower plays, in ms, at a steady clock handed over a block late. */
        static double msBehind(bool blockLate)
        {
            constexpr double rate = 44100.0, bpm = 125.0;
            constexpr int block = 441;
            const double pulse = rate * 60.0 / bpm / 24.0; // 882 samples
            cs808::ClockFollower clock;
            clock.prepare(rate);
            clock.setBlockLate(blockLate);
            // the leader starts at 0 and pulses at once; each message reaches the follower a block later
            bool started = false;
            std::int64_t next = 0;
            double behind = 0.0;
            for (std::int64_t now = 0; now < static_cast<std::int64_t>(rate * 8.0); now += block)
            {
                if (! started)
                {
                    clock.start(now + block);
                    started = true;
                }
                while (static_cast<double>(next) * pulse + block < static_cast<double>(now + block))
                {
                    clock.pulse(static_cast<std::int64_t>(std::llround(static_cast<double>(next) * pulse)) + block);
                    ++next;
                }
                double beat = 0.0, blockBpm = 0.0;
                clock.block(now, block, beat, blockBpm);
                const double leader = static_cast<double>(now) / (pulse * 24.0);
                behind = (leader - beat) * 60.0 / bpm * 1000.0;
            }
            return behind;
        }

        void runTest() override
        {
            beginTest("handed_a_block_late_it_plays_with_its_leader");
            {
                const double late = msBehind(false), aimed = msBehind(true);
                logMessage("behind the leader: " + juce::String(late, 2) + " ms as before, " + juce::String(aimed, 2) + " ms aimed a block on");
                expect(late > 8.0 && late < 11.0, "without, a block (10 ms) behind");
                expect(std::abs(aimed) < 1.0, "aimed a block on, within a millisecond");
            }
        }
    };

    static ClockFollowerTests clockFollowerTests;
}
