#include "ExportPanel.h"

#include "Text.h"
#include "Theme.h"
#include "WindowLook.h"
#include "Widgets.h"

namespace cs808
{
    namespace
    {
        // As the options window's (D311): its padding, label column and rows.
        constexpr int cardWidth = 600, cardHeight = 336, pad = 22, labelWidth = 130, rowHeight = 30, rowGap = 8, titleHeight = 30;
        constexpr int keyWidth = 120, keyHeight = 32;
        const char* const rowNames[] = { "Play", "Times", "End", "Files" };

        juce::String seconds(double s) { return juce::String(s, s < 10.0 ? 1 : 0) + " s"; }
    }

    ExportPanel::ExportPanel(Hooks h) : hooks(std::move(h))
    {
        setWantsKeyboardFocus(true);
        if (auto* s = hooks.settings)
        {
            what = juce::jlimit(0, 1, s->getIntValue("exportWhat", 0));
            times = juce::jlimit(0, static_cast<int>(timesChoices.size()) - 1, s->getIntValue("exportTimes", 0));
            end = juce::jlimit(0, 2, s->getIntValue("exportEnd", 0));
            files = juce::jlimit(0, 1, s->getIntValue("exportFiles", 0));
        }
        const auto choices = [this](std::vector<juce::String> options, int& value)
        {
            auto c = std::make_unique<Choices>();
            c->options = std::move(options);
            c->fontHeight = 10.5f;
            c->get = [&value] { return value; };
            c->set = [this, &value](int v)
            {
                if (busy())
                    return;
                value = v;
                keep();
                repaint();
            };
            return c;
        };
        std::vector<juce::String> timesWords;
        for (const int t : timesChoices)
            timesWords.push_back(juce::String(t) + (t == 1 ? " time" : " times"));
        rows[0] = choices({ hooks.patternName, hooks.chainName }, what);
        rows[1] = choices(timesWords, times);
        rows[2] = choices({ "Cut", "Ring out", "Seamless loop" }, end);
        rows[3] = choices({ "Mix", "Mix and each drum" }, files);
        for (auto& r : rows)
            addAndMakeVisible(*r);

        auto exportChip = std::make_unique<Chip>();
        exportChip->text = "Export...";
        exportChip->fontHeight = 11.0f;
        exportChip->onClick = [this](const juce::MouseEvent&) { chooseFile(); };
        exportKey = std::move(exportChip);
        addAndMakeVisible(*exportKey);
        auto cancelChip = std::make_unique<Chip>();
        cancelChip->text = "Cancel";
        cancelChip->fontHeight = 11.0f;
        cancelChip->onClick = [this](const juce::MouseEvent&) { cancel.store(true); };
        cancelKey = std::move(cancelChip);
        addChildComponent(*cancelKey);
    }

    ExportPanel::~ExportPanel()
    {
        stopWorker();
    }

    juce::Rectangle<int> ExportPanel::card() const
    {
        return getLocalBounds().withSizeKeepingCentre(cardWidth, cardHeight);
    }

    ExportJob ExportPanel::job() const
    {
        auto j = hooks.jobFor ? hooks.jobFor(what == 1) : ExportJob {};
        j.times = timesChoices[static_cast<std::size_t>(times)];
        j.end = static_cast<ExportEnd>(end);
        j.stems = files == 1;
        return j;
    }

    void ExportPanel::keep()
    {
        if (auto* s = hooks.settings)
        {
            s->setValue("exportWhat", what);
            s->setValue("exportTimes", times);
            s->setValue("exportEnd", end);
            s->setValue("exportFiles", files);
        }
    }

    void ExportPanel::paint(juce::Graphics& g)
    {
        const auto& th = theme();
        g.setColour(th.desk.withAlpha(0.6f));
        g.fillAll();
        const auto box = card();
        g.setColour(th.panel.darker(0.25f));
        g.fillRoundedRectangle(box.toFloat(), 10.0f);
        g.setColour(th.ink.withAlpha(0.5f));
        g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 10.0f, 1.0f);

        auto inner = box.reduced(pad, 0);
        g.setColour(th.ink);
        g.setFont(headFont(15.0f));
        shapedText(g, "Export audio", inner.withY(box.getY() + 14).withHeight(titleHeight), juce::Justification::centredLeft, false);
        g.setColour(th.line);
        g.fillRect(inner.getX(), box.getY() + 14 + titleHeight, inner.getWidth(), 1);

        int y = box.getY() + 14 + titleHeight + 14;
        for (const char* name : rowNames)
        {
            g.setColour(th.dim);
            g.setFont(labelFont(11.0f));
            shapedText(g, juce::String(name).toUpperCase(), juce::Rectangle<int>(inner.getX(), y, labelWidth, rowHeight), juce::Justification::centredLeft, false);
            y += rowHeight + rowGap;
        }

        // What comes out: the format, the length, the drums' files.
        const auto j = job();
        const double length = static_cast<double>(loopSamples(j)) / j.sampleRate;
        const int bars = static_cast<int>(j.patterns.size()) * (j.project.play.variation == SequencerVariation::ab ? 2 : 1) * j.times;
        juce::String what1 = "24-bit WAV, " + juce::String(juce::roundToInt(j.sampleRate)) + " Hz, " + juce::String(bars)
                             + (bars == 1 ? " bar, " : " bars, ") + seconds(length);
        if (j.end == ExportEnd::ring)
            what1 << " and the ring-out";
        if (j.end == ExportEnd::loop)
            what1 << ", the ring-out in its start";
        juce::String what2;
        if (j.stems)
        {
            juce::StringArray codes;
            for (const int lane : lanesHeard(j))
            {
                const auto& in = instruments[static_cast<std::size_t>(lane)];
                codes.add(in.altCode != nullptr && j.project.kit.alt[static_cast<std::size_t>(lane)] ? in.altCode : in.code);
            }
            what2 = codes.isEmpty() ? juce::String("No drum plays here.") : "A file for each drum: " + codes.joinIntoString(" ") + ", as long as the mix";
        }
        g.setColour(th.dim);
        g.setFont(uiFont(12.0f));
        const int x = inner.getX() + labelWidth;
        shapedText(g, what1, juce::Rectangle<int>(x, y, inner.getRight() - x, 18), juce::Justification::centredLeft, true);
        if (what2.isNotEmpty())
            shapedText(g, what2, juce::Rectangle<int>(x, y + 18, inner.getRight() - x, 18), juce::Justification::centredLeft, true);

        // The bottom line: the progress while it renders, then what was written.
        const auto bottom = juce::Rectangle<int>(x, box.getBottom() - pad - keyHeight, inner.getRight() - x - keyWidth - 12, keyHeight);
        if (busy())
        {
            const auto bar = bottom.withSizeKeepingCentre(bottom.getWidth(), 8).toFloat();
            g.setColour(th.ink.withAlpha(0.15f));
            g.fillRoundedRectangle(bar, 4.0f);
            g.setColour(th.lit);
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, progress.load())), 4.0f);
        }
        else if (status.isNotEmpty())
        {
            g.setColour(ok ? th.lit : th.ink);
            g.setFont(uiFont(12.0f));
            shapedText(g, status, bottom, juce::Justification::centredLeft, true);
        }
    }

    void ExportPanel::resized()
    {
        const auto box = card();
        int y = box.getY() + 14 + titleHeight + 14;
        const int x = box.getX() + pad + labelWidth, w = box.getRight() - pad - x;
        for (auto& r : rows)
        {
            r->setBounds(x, y, w, rowHeight);
            y += rowHeight + rowGap;
        }
        const auto key = juce::Rectangle<int>(box.getRight() - pad - keyWidth, box.getBottom() - pad - keyHeight, keyWidth, keyHeight);
        exportKey->setBounds(key);
        cancelKey->setBounds(key);
        if (closeKey == nullptr)
        {
            closeKey = WindowLook::makeCloseKey();
            closeKey->setTooltip("Close (Esc)");
            closeKey->onClick = [this] { if (onClose) onClose(); };
            addAndMakeVisible(*closeKey);
        }
        closeKey->setBounds(box.getRight() - 40, box.getY() + 8, 32, 28);
    }

    bool ExportPanel::keyPressed(const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey && onClose)
        {
            onClose();
            return true;
        }
        return false;
    }

    void ExportPanel::chooseFile()
    {
        if (busy())
            return;
        const auto j = job();
        auto folder = juce::File(hooks.settings != nullptr ? hooks.settings->getValue("exportFolder") : juce::String());
        if (! folder.isDirectory())
            folder = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
        const auto name = "CoolSynth 808 - " + (what == 1 ? hooks.chainName : hooks.patternName).trim() + ".wav";
        chooser = std::make_unique<juce::FileChooser>("Export audio", folder.getChildFile(juce::File::createLegalFileName(name)), "*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe = juce::Component::SafePointer<ExportPanel>(this)](const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;
            auto f = fc.getResult();
            if (f == juce::File())
                return;
            if (! f.hasFileExtension("wav"))
                f = f.withFileExtension("wav");
            if (auto* s = safe->hooks.settings)
                s->setValue("exportFolder", f.getParentDirectory().getFullPathName());
            safe->start(f);
        });
    }

    void ExportPanel::start(const juce::File& mixFile)
    {
        stopWorker();
        cancel.store(false);
        workerDone.store(false);
        progress.store(0.0f);
        ok = false;
        stemsWritten = 0;
        status.clear();
        written = mixFile;
        // The job taken here, on the message thread, from the project as it is; rendered and written on the worker.
        worker = std::make_unique<std::thread>([this, j = job(), mixFile]
        {
            ExportResult result;
            bool fine = renderExport(j, result, progress, cancel);
            int stems = 0;
            if (fine)
                fine = writeWav24(mixFile, result.mix, j.sampleRate);
            for (const auto& [lane, audio] : result.stems)
                if (fine && ! cancel.load())
                {
                    fine = writeWav24(stemFile(mixFile, lane, j.project.kit), audio, j.sampleRate);
                    stems += fine ? 1 : 0;
                }
            ok = fine && ! cancel.load();
            stemsWritten = stems;
            workerDone.store(true, std::memory_order_release);
        });
        exportKey->setVisible(false);
        cancelKey->setVisible(true);
        startTimerHz(30);
        repaint();
    }

    void ExportPanel::timerCallback()
    {
        if (workerDone.load(std::memory_order_acquire))
            finish();
        repaint();
    }

    void ExportPanel::finish()
    {
        stopTimer();
        const bool cancelled = cancel.load();
        stopWorker();
        if (ok)
            status = "Written: " + written.getFileName() + (stemsWritten > 0 ? " and " + juce::String(stemsWritten) + " drum files beside it" : juce::String());
        else
            status = cancelled ? juce::String("Cancelled.") : "Could not write " + written.getFullPathName();
        exportKey->setVisible(true);
        cancelKey->setVisible(false);
        repaint();
    }

    void ExportPanel::stopWorker()
    {
        if (worker == nullptr)
            return;
        if (! workerDone.load())
            cancel.store(true);
        worker->join();
        worker.reset();
    }
}
