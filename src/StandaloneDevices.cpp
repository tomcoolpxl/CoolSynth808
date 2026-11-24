#include "StandaloneDevices.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_processors/juce_audio_processors.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_gui_extra/juce_gui_extra.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace cs808
{
#if JucePlugin_Build_Standalone
    juce::AudioDeviceManager* standaloneDeviceManager()
    {
        auto* holder = juce::StandalonePluginHolder::getInstance();
        return holder != nullptr ? &holder->deviceManager : nullptr;
    }

    void saveStandaloneDeviceState()
    {
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            holder->saveAudioDeviceState();
            if (auto* file = dynamic_cast<juce::PropertiesFile*>(holder->settings.get()))
                file->saveIfNeeded();
        }
    }
#else
    juce::AudioDeviceManager* standaloneDeviceManager() { return nullptr; }
    void saveStandaloneDeviceState() {}
#endif
}
