// The CLAP (CoolSynth's D367). JUCE hosts VST3 but not CLAP, so, as CoolSynth checks its own CLAP (its D228), a CLAP
// host from outside does it: free-audio's clap-validator, named by COOLSYNTH_CLAP_VALIDATOR (it is not committed),
// runs its whole suite on the CLAP of this test's own build, out of process. Without the validator or the CLAP built
// (the CI builds only the test program) the test is skipped. The processor itself: made by a CLAP host it counts as a
// plugin, which a CLAP's empty wrapper type alone would not say.

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Processor.h"

namespace
{
    class ClapTests final : public juce::UnitTest
    {
    public:
        ClapTests() : juce::UnitTest("CLAP", "CoolSynth808") {}

        void runTest() override
        {
            beginTest("the_tests_processor_is_no_plugin_and_no_clap");
            {
                auto p = std::make_unique<cs808::Processor>();
                expect(! p->is_clap && ! p->madeByHostOrApp() && ! p->isPlugin(), "a test is a test");
            }

            beginTest("clap_validator_passes_the_clap");
            const juce::File clap = juce::File(COOLSYNTH808_BINARY_DIR).getChildFile("CoolSynth808_artefacts/" COOLSYNTH808_CONFIG "/CLAP/CoolSynth 808.clap");
            const juce::File validator(juce::SystemStats::getEnvironmentVariable("COOLSYNTH_CLAP_VALIDATOR", {}));
            if (! clap.existsAsFile() || validator == juce::File() || ! validator.existsAsFile())
            {
                logMessage(! clap.existsAsFile() ? "no CLAP built at " + clap.getFullPathName() + ": skipped"
                                                 : juce::String("no CLAP host (COOLSYNTH_CLAP_VALIDATOR names clap-validator): skipped"));
                expect(true);
                return;
            }
            juce::ChildProcess run;
            expect(run.start(juce::StringArray { validator.getFullPathName(), "validate", clap.getFullPathName(), "--only-failed", "--hide-output" }),
                   "clap-validator starts");
            const auto output = run.readAllProcessOutput(); // until it ends
            const bool ended = run.waitForProcessToFinish(60000);
            expect(ended, "clap-validator ends");
            const auto code = run.getExitCode();
            logMessage(output.trim().fromLastOccurrenceOf("\n", false, false)); // its summary line
            if (code != 0)
                logMessage(output);
            expectEquals(static_cast<int>(code), 0, "clap-validator finds no fault");
        }
    };

    static ClapTests clapTests;
}
