#include "Transport.h"

#include "Editor.h"
#include "Text.h"

namespace cs808
{
    namespace
    {
        constexpr int keyHeight = 26;

        float wordWidth(const juce::String& text, float height = 10.0f)
        {
            return shapedWidth(labelFont(height), text.toUpperCase());
        }
    }

    // =============================================================================================================
    // The tempo: its number in lit orange, dragged up and down as the old tempo box was

    class TransportStrip::TempoReadout final : public juce::Component
    {
    public:
        explicit TempoReadout(Processor& p) : processor(p) {}

        // HOST in a DAW (D325), SYNC while the beat sync leads with other peers (D361)
        static int width() { return juce::roundToInt(shapedWidth(monoFont(13.0f), "120.0") + 5.0f + std::max({ wordWidth("BPM"), wordWidth("HOST"), wordWidth("SYNC") }) + 12.0f); }

        void paint(juce::Graphics& g) override
        {
            // following a clock, the sync or in a DAW, theirs; HOST under it while the host's Play leads (D325), SYNC
            // while the sync leads with others (D361)
            const bool led = processor.followingClock() || processor.isPlugin() || processor.followingSync();
            const double tempo = led ? processor.tempoNow() : processor.project().play.tempo;
            const auto number = juce::String(tempo, 1);
            const auto font = monoFont(13.0f);
            const float numberWidth = shapedWidth(font, number);
            const auto area = getLocalBounds().toFloat().reduced(6.0f, 0.0f);
            const auto numberArea = area.withWidth(numberWidth + 2.0f);
            g.setFont(font);
            // its glow, as a lit display's
            g.setColour(juce::Colour(0xffffa04a).withAlpha(0.16f));
            for (const auto d : { juce::Point<float>(-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
                shapedText(g, number, numberArea.translated(d.x, d.y), juce::Justification::centredLeft, false);
            g.setColour(theme().lit);
            shapedText(g, number, numberArea, juce::Justification::centredLeft, false);
            // BPM on the same baseline: the label's cap height sits lower than the digits' centre
            g.setColour(juce::Colour(0xffddd5c3));
            g.setFont(labelFont(10.0f));
            shapedText(g, processor.followingHost() ? "HOST" : processor.followingSync() ? "SYNC" : "BPM",
                       area.withTrimmedLeft(numberWidth + 5.0f).translated(0.0f, 1.0f), juce::Justification::centredLeft, false);
        }

        void mouseDown(const juce::MouseEvent&) override
        {
            // from the tempo shown: with the sync's peers, the session's (D361)
            startValue = processor.followingSync() ? processor.tempoNow() : processor.project().play.tempo;
            processor.beginGesture();
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            // Following a MIDI clock, or in a DAW, the clock or the host sets the tempo: the readout takes no edits.
            // With the beat sync it does: the tempo goes to the whole session (D361).
            if (processor.followingClock() || processor.isPlugin())
                return;
            const double steps = std::round(-e.getDistanceFromDragStartY() / 2.0);
            const double v = juce::jlimit(30.0, 300.0, startValue + steps * 0.5);
            processor.editPlay([v](PlaySettings& p) { p.tempo = v; });
            repaint();
        }

        void mouseUp(const juce::MouseEvent&) override { processor.endGesture(); }

        void mouseDoubleClick(const juce::MouseEvent&) override
        {
            if (! processor.followingClock() && ! processor.isPlugin())
                processor.editPlay([](PlaySettings& p) { p.tempo = 120.0; });
            repaint();
        }

    private:
        Processor& processor;
        double startValue = 120.0;
    };

    // =============================================================================================================

    TransportStrip::TransportStrip(Editor& e) : editor(e)
    {
        auto& p = editor.processor;
        const auto key = [this](const juce::String& text, std::function<void()> act, int width = 0)
        {
            auto* c = new Chip();
            c->text = text;
            c->fontHeight = 10.0f;
            c->onClick = [act = std::move(act)](const juce::MouseEvent&) { act(); };
            items.add(c);
            addAndMakeVisible(c);
            widths.push_back(width > 0 ? width : juce::roundToInt(wordWidth(text) + 20.0f));
            return c;
        };

        // Lit too while a start waits for the beat sync's next bar line (D361); Stop then calls it off.
        auto* play = key("Play", [&p] { p.setPlaying(! (p.isPlaying() || p.countingIn())); }, juce::roundToInt(std::max(wordWidth("Play"), wordWidth("Stop")) + 22.0f));
        play->isOn = [&p] { return p.isPlaying() || p.countingIn(); };
        play->textOf = [&p] { return juce::String(p.isPlaying() || p.countingIn() ? "Stop" : "Play"); };

        auto* rec = key("Rec", [&p] { p.setRecording(! p.isRecording()); }, juce::roundToInt(wordWidth("Rec") + 32.0f));
        rec->filledWhenOn = false;
        rec->onLens = Lens::red;
        rec->isOn = [&p] { return p.isRecording(); };

        auto* tempo = new TempoReadout(p);
        items.add(tempo);
        addAndMakeVisible(tempo);
        widths.push_back(TempoReadout::width());

        // The beat sync, the standalone's (D361): its lens lit while on, the other peers counted on it; a click
        // switches it.
        if (p.hasBeatSync())
        {
            syncKey = key("Sync", [&p] { p.setBeatSyncOn(! p.beatSyncOn()); }, juce::roundToInt(wordWidth("Sync 88") + 32.0f));
            syncKey->filledWhenOn = false;
            syncKey->isOn = [&p] { return p.beatSyncOn(); };
            syncKey->textOf = [&p] { return syncKeyText(p.syncPeers()); };
            syncKey->setTooltip("The beat sync: the tempo, the beat, Play and Stop shared with the other apps on this network that have it. Click to switch it on or off.");
        }

        key("Tap", [this] { editor.tapTempo(); });

        auto* swing = new Knob();
        swing->diameter = 18.0f;
        swing->defaultValue = 0.0f;
        swing->get = [&p] { return (p.project().play.swing - 0.5f) / 0.25f; };
        swing->set = [&p](float v) { p.editPlay([v](PlaySettings& s) { s.swing = 0.5f + 0.25f * v; }); };
        swing->gestureStart = [&p] { p.beginGesture(); };
        swing->gestureEnd = [&p] { p.endGesture(); };
        swingLabelAfter = items.size();
        items.add(swing);
        addAndMakeVisible(swing);
        widths.push_back(8 + 18 + 10 + swingWordWidth + 6);

        separatorAfter = items.size() - 1;
        // Lift the Lid (D315): its lens lit while the lid is up.
        auto* lidKey = key("Lid", [this] { editor.liftLid(! editor.isLidLifted()); }, juce::roundToInt(wordWidth("Lid") + 32.0f));
        lidKey->filledWhenOn = false;
        lidKey->isOn = [this] { return editor.isLidLifted(); };
        key("Library", [this] { editor.openLibrary(); });
        key("File", [this] { editor.showFileMenu(); });
        // The options window (D311): its lens lit while it is open.
        auto* options = key("Options", [this] { editor.toggleOptions(); }, juce::roundToInt(wordWidth("Options") + 32.0f));
        options->filledWhenOn = false;
        options->isOn = [this] { return editor.optionsOpen(); };
    }

    TransportStrip::~TransportStrip() = default;

    int TransportStrip::preferredWidth() const
    {
        int w = 2 * padX + gap * (items.size() - 1) + 9; // the separator: a line with 4 px each side
        for (const int x : widths)
            w += x;
        return w;
    }

    void TransportStrip::resized()
    {
        int x = padX;
        for (int i = 0; i < items.size(); ++i)
        {
            const int w = widths[static_cast<std::size_t>(i)];
            if (i == swingLabelAfter)
                items[i]->setBounds(x + 4, 1, 26, height - 1); // the knob's centre 17 px in, as the mockup's padding
            else if (dynamic_cast<Chip*>(items[i]) != nullptr)
                items[i]->setBounds(x, (height - keyHeight) / 2, w, keyHeight);
            else
                items[i]->setBounds(x, 0, w, height);
            x += w + gap;
            if (i == separatorAfter)
            {
                separatorX = x + 4;
                x += 9 + gap;
            }
        }
    }

    void TransportStrip::paint(juce::Graphics& g)
    {
        drawSmokedGlass(g, getLocalBounds().toFloat(), 5.0f, true);
        // the separator between the transport and the files
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.fillRect(static_cast<float>(separatorX), (height - 20) * 0.5f, 1.0f, 20.0f);
        // Swing's word right of its knob
        const auto* swing = items[swingLabelAfter];
        g.setColour(juce::Colour(0xfff4efe4));
        g.setFont(labelFont(10.0f));
        shapedText(g, "SWING", juce::Rectangle<float>(static_cast<float>(swing->getX() - 4 + 8 + 18 + 10), 0.0f, static_cast<float>(swingWordWidth) + 10.0f, static_cast<float>(height)),
                   juce::Justification::centredLeft, false);
    }
}
