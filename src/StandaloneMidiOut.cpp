#include "StandaloneMidiOut.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_processors/juce_audio_processors.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_gui_extra/juce_gui_extra.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace cs808
{
#if JucePlugin_Build_Standalone
    namespace
    {
        class Keeper final : public juce::Timer
        {
        public:
            Keeper() { startTimer(500); }

            void timerCallback() override
            {
                if (auto* holder = juce::StandalonePluginHolder::getInstance())
                    if (auto* out = holder->deviceManager.getDefaultMidiOutput())
                        if (! out->isBackgroundThreadRunning())
                            out->startBackgroundThread();
            }
        };
    }

    std::unique_ptr<juce::Timer> makeStandaloneMidiOutKeeper() { return std::make_unique<Keeper>(); }
#else
    std::unique_ptr<juce::Timer> makeStandaloneMidiOutKeeper() { return {}; }
#endif
}
