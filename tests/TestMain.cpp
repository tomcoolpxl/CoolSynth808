// CoolSynth 808's test program: the project, the processor playing the engine, the library.

#include <cstdio>
#include <iostream>

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

namespace
{
    class StdoutLogger : public juce::Logger
    {
    protected:
        void logMessage(const juce::String& message) override { std::cout << message.toRawUTF8() << std::endl; }
    };
}

int main()
{
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);
    juce::ScopedJuceInitialiser_GUI initialiser;
    juce::SystemStats::setApplicationCrashHandler([](void*)
    {
        std::fputs(("\nCRASH\n" + juce::SystemStats::getStackBacktrace() + "\n").toRawUTF8(), stderr);
        std::fflush(stderr);
    });
    juce::UnitTestRunner runner;
    if (const auto only = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_TEST_ONLY", {}); only.isNotEmpty())
    {
        juce::Array<juce::UnitTest*> chosen;
        for (auto* test : juce::UnitTest::getAllTests())
            if (test->getName().containsIgnoreCase(only))
                chosen.add(test);
        runner.runTests(chosen);
    }
    else
        runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (auto* result = runner.getResult(i))
            failures += result->failures;
    juce::Logger::setCurrentLogger(nullptr);
    return failures == 0 ? 0 : 1;
}
