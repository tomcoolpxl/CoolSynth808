#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

/*
    The standalone app's audio and MIDI devices, for the options window (D311). In a plugin, and in the tests, there
    are none: the host owns its devices, and the options window leaves those parts out.
*/
namespace cs808
{
    /** The standalone app's device manager, or null when this is not the standalone app. */
    juce::AudioDeviceManager* standaloneDeviceManager();
    /** Keeps the devices as now chosen, at once: JUCE keeps them only when the app closes. */
    void saveStandaloneDeviceState();
}
