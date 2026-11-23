#include "StandaloneMidiIn.h"

#include "Processor.h"
#include "midi/ControllerSetup.h"

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
        namespace midi = coolsynth::midi;

        class Router final : public juce::Timer, private juce::MidiInputCallback
        {
        public:
            explicit Router(Processor& p) : processor(p) { startTimer(500); }

            ~Router() override
            {
                stopTimer();
                processor.setInputsRouted(false);
                if (installedOn != nullptr)
                {
                    // Back as the standalone had it: every input to its player.
                    installedOn->deviceManager.removeMidiInputDeviceCallback({}, this);
                    installedOn->deviceManager.addMidiInputDeviceCallback({}, &installedOn->player);
                }
            }

            void timerCallback() override
            {
                auto* holder = juce::StandalonePluginHolder::getInstance();
                if (holder == nullptr)
                    return;
                if (installedOn != holder)
                {
                    // The player gets the inputs through here from now on.
                    holder->deviceManager.removeMidiInputDeviceCallback({}, &holder->player);
                    player.store(&holder->player, std::memory_order_release);
                    holder->deviceManager.addMidiInputDeviceCallback({}, this);
                    installedOn = holder;
                }

                auto& setup = midi::ControllerSetup::instance();
                const auto input = setup.controllerInput();
                {
                    const juce::SpinLock::ScopedLockType lock(controllerLock);
                    controllerId = input.has_value() ? input->identifier : juce::String();
                }
                hearAllWhileLearning.store(setup.inputChoice() != midi::ControllerSetup::InputChoice::device, std::memory_order_relaxed);
                processor.setInputsRouted(input.has_value());

                // Windows keeps a MIDI input closed until it is switched on in the settings: the controller's is opened.
                if (input.has_value() && ! holder->deviceManager.isMidiInputDeviceEnabled(input->identifier)
                    && juce::MidiInput::getAvailableDevices().contains(*input))
                    holder->deviceManager.setMidiInputDeviceEnabled(input->identifier, true);
            }

        private:
            void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& m) override
            {
                auto* sound = player.load(std::memory_order_acquire);
                if (sound == nullptr)
                    return;
                const bool learning = midi::ControllerSetup::instance().isLearning();
                if (learning && source != nullptr && midi::ControllerSetup::wanted(m))
                    midi::ControllerSetup::instance().noteLearnSource(source->getDeviceInfo());

                juce::String id;
                {
                    const juce::SpinLock::ScopedLockType lock(controllerLock);
                    id = controllerId;
                }
                if (id.isNotEmpty())
                {
                    const bool fromController = source != nullptr && source->getIdentifier() == id;
                    if (fromController || (learning && hearAllWhileLearning.load(std::memory_order_relaxed)))
                    {
                        if (midi::ControllerSetup::wanted(m))
                            processor.takeControllerMessage(m);
                        if (midi::ControllerSetup::takesFromSound(m))
                            return;
                    }
                }
                sound->handleIncomingMidiMessage(source, m);
            }

            Processor& processor;
            juce::StandalonePluginHolder* installedOn = nullptr; // message thread
            std::atomic<juce::AudioProcessorPlayer*> player { nullptr };
            juce::SpinLock controllerLock;
            juce::String controllerId; // empty: no controller input, every message to the player as before
            std::atomic<bool> hearAllWhileLearning { true };
        };
    }

    std::unique_ptr<juce::Timer> makeStandaloneMidiInRouter(Processor& processor) { return std::make_unique<Router>(processor); }
#else
    std::unique_ptr<juce::Timer> makeStandaloneMidiInRouter(Processor&) { return {}; }
#endif
}
