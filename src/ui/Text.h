#pragma once

#include <unordered_map>

#include <juce_graphics/juce_graphics.h>

/*
    Text shaped once and kept (D306). JUCE shapes a string each time it draws or measures it (40 to 140 us a time
    here), and the panel draws some three hundred words a paint: a pattern key's press took 150 ms to show. Drawing a
    kept shape costs about 5 us. The panel draws and measures its words through these; they place the words exactly
    as Graphics::drawText does (justifyGlyphs on the line's box), and hand a word too wide for its box to drawText,
    which cuts it as before. Message thread only.
*/
namespace cs808
{
    namespace detail
    {
        struct Shaped
        {
            juce::GlyphArrangement glyphs;
            juce::Rectangle<float> box; // with the font's ascent and descent, as justifyGlyphs measures it
        };

        /** The kept shapes, deleted with JUCE's shutdown: as a plain static they outlived JUCE's fonts, and the
            program hung on quitting. */
        struct ShapedCache final : public juce::DeletedAtShutdown
        {
            std::unordered_map<juce::String, Shaped> map;
            ~ShapedCache() override { clearSingletonInstance(); }
            JUCE_DECLARE_SINGLETON_SINGLETHREADED_INLINE(ShapedCache, false)
        };

        inline const Shaped& shapedOf(const juce::Font& font, const juce::String& text)
        {
            auto& cache = ShapedCache::getInstance()->map;
            const auto key = text + juce::String::charToString(0x1f)
                           + juce::String::toHexString(static_cast<juce::pointer_sized_int>(reinterpret_cast<std::uintptr_t>(font.getTypefacePtr().get())))
                           + "|" + juce::String(font.getHeight(), 4) + "|" + juce::String(font.getExtraKerningFactor(), 4)
                           + "|" + juce::String(font.getHorizontalScale(), 4);
            if (const auto found = cache.find(key); found != cache.end())
                return found->second;
            if (cache.size() > 4000) // a bound, should text ever change without end
                cache.clear();
            Shaped s;
            s.glyphs.addLineOfText(font, text, 0.0f, 0.0f);
            s.box = s.glyphs.getBoundingBox(0, -1, true);
            return cache.emplace(key, std::move(s)).first->second;
        }
    }

    /** As g.drawText(text, area, justification, ellipses) in the current font and colour, from the kept shape. */
    inline void shapedText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification justification, bool ellipses = true)
    {
        if (text.isEmpty() || ! area.intersects(g.getClipBounds().toFloat()))
            return;
        const auto& s = detail::shapedOf(g.getCurrentFont(), text);
        if (s.box.getWidth() > area.getWidth() + 0.01f)
        {
            g.drawText(text, area, justification, ellipses);
            return;
        }
        const auto delta = justification.appliedToRectangle(s.box, area).getPosition() - s.box.getPosition();
        s.glyphs.draw(g, juce::AffineTransform::translation(delta));
    }

    inline void shapedText(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification justification, bool ellipses = true)
    {
        shapedText(g, text, area.toFloat(), justification, ellipses);
    }

    /** As GlyphArrangement::getStringWidth, from the kept shape. */
    inline float shapedWidth(const juce::Font& font, const juce::String& text)
    {
        return text.isEmpty() ? 0.0f : detail::shapedOf(font, text).box.getWidth();
    }
}
