#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../AudioExport.h"

/*
    The WAV export's dialog (D331), from the File menu's Export audio: a small card over the panel, as the library's,
    drawn as the options are. What plays (the pattern shown or the chain), how many times, the end (cut, ringing on,
    or a seamless loop), the mix alone or each drum too; then Export asks where, renders and writes. Its choices are
    kept for the next time.
*/
namespace cs808
{
    class ExportPanel final : public juce::Component, private juce::Timer
    {
    public:
        struct Hooks
        {
            juce::String patternName; // "pattern 01", "the fill"
            juce::String chainName;   // "the chain 01 02"
            std::function<ExportJob(bool chain)> jobFor; // the project as it is, with what plays
            juce::PropertiesFile* settings = nullptr;    // where the choices are kept, or none
        };

        explicit ExportPanel(Hooks hooks);
        ~ExportPanel() override;

        std::function<void()> onClose;
        std::unique_ptr<juce::Button> closeKey; // the cross at the top right

        void paint(juce::Graphics& g) override;
        void resized() override;
        bool keyPressed(const juce::KeyPress& k) override;

        /** For the tests: the job as the choices make it, and an export to a file without asking where. */
        [[nodiscard]] ExportJob jobForTesting() const { return job(); }
        void exportToForTesting(const juce::File& mixFile) { start(mixFile); }
        [[nodiscard]] bool busy() const noexcept { return worker != nullptr && ! workerDone.load(); }
        [[nodiscard]] juce::String statusForTesting() const { return status; }

    private:
        [[nodiscard]] juce::Rectangle<int> card() const;
        [[nodiscard]] ExportJob job() const;
        void chooseFile();
        void start(const juce::File& mixFile);
        void finish(); // the worker's end, on the message thread
        void stopWorker();
        void keep();
        void timerCallback() override;

        Hooks hooks;
        int what = 0;  // 0 the pattern, 1 the chain
        int times = 0; // index into timesChoices
        int end = 0;   // ExportEnd
        int files = 0; // 0 the mix, 1 each drum too
        static constexpr std::array<int, 5> timesChoices { 1, 2, 4, 8, 16 };
        std::array<std::unique_ptr<juce::Component>, 4> rows;
        std::unique_ptr<juce::Component> exportKey, cancelKey;
        std::unique_ptr<juce::FileChooser> chooser;

        std::unique_ptr<std::thread> worker;
        std::atomic<bool> cancel { false }, workerDone { false };
        std::atomic<float> progress { 0.0f };
        bool ok = false;
        juce::File written;
        int stemsWritten = 0;
        juce::String status;
    };
}
