#include "Theme.h"

#include <map>

namespace Theme
{

/** Profilbilder (512 px) für die kleinen Kreise einmal passend verkleinern statt bei jedem Zeichnen. */
static juce::Image scaledFor (const juce::Image& image, juce::Rectangle<float> area, juce::Graphics& g)
{
    struct Entry { juce::Image original; std::map<int, juce::Image> sizes; };
    static std::map<const void*, Entry> cache;

    const int px = juce::roundToInt (juce::jmax (area.getWidth(), area.getHeight()) * g.getInternalContext().getPhysicalPixelScaleFactor());
    if (px <= 0 || px >= image.getWidth())
        return image;

    const void* key = image.getPixelData().get();
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 64)
            cache.clear();
        it = cache.emplace (key, Entry { image, {} }).first;   // hält das Original fest: Adresse bleibt eindeutig
    }

    auto& scaled = it->second.sizes[px];
    if (! scaled.isValid())
        scaled = image.rescaled (px, px, juce::Graphics::highResamplingQuality);
    return scaled;
}

void drawAvatar (juce::Graphics& g, juce::Rectangle<float> b, const juce::Image& image,
                 const juce::String& name, juce::Colour colour, bool speaking)
{
    b = b.withSizeKeepingCentre (juce::jmin (b.getWidth(), b.getHeight()), juce::jmin (b.getWidth(), b.getHeight()));
    const float ring = juce::jmax (2.0f, b.getWidth() * 0.06f);
    auto inner = b.reduced (ring + 1.0f);

    if (speaking)
    {
        // Leuchten
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (b.expanded (ring * 1.5f));
    }

    juce::Path circle;
    circle.addEllipse (inner);

    if (image.isValid())
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (circle);

        // Hinter freigestellten Bildern: Farbfläche der Person mit leichtem Licht
        juce::ColourGradient bgFill (colour.brighter (0.25f).withMultipliedSaturation (0.8f), inner.getCentreX(), inner.getY() + inner.getHeight() * 0.35f,
                                     colour.withMultipliedBrightness (0.35f), inner.getCentreX(), inner.getBottom() + inner.getHeight() * 0.3f, true);
        g.setGradientFill (bgFill);
        g.fillPath (circle);
        g.drawImage (scaledFor (image, inner, g), inner, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    }
    else
    {
        g.setColour (colour.withMultipliedSaturation (0.7f).withMultipliedBrightness (0.45f));
        g.fillPath (circle);
        g.setColour (colour.brighter (0.4f));
        g.setFont (font (inner.getHeight() * 0.40f, true));
        g.drawText (initials (name), inner, juce::Justification::centred, false);
    }

    g.setColour (speaking ? colour.brighter (0.3f) : colour.withAlpha (0.8f));
    g.drawEllipse (b.reduced (ring * 0.5f), speaking ? ring * 1.4f : ring);
}

void drawMeter (juce::Graphics& g, juce::Rectangle<float> b, float levelDb, float thresholdDb,
                bool speaking, juce::Colour colour)
{
    constexpr float minDb = -72.0f, maxDb = 0.0f;
    auto toX = [&] (float db) { return b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); };

    g.setColour (Theme::background);
    g.fillRoundedRectangle (b, 3.0f);

    const float x = toX (levelDb);
    auto fill = b.withRight (x);

    juce::Colour c = levelDb > -3.0f ? Theme::record
                   : levelDb >= thresholdDb ? (speaking ? colour : colour.withAlpha (0.7f))
                   : Theme::textFaint;
    g.setColour (c);
    g.fillRoundedRectangle (fill, 3.0f);

    // Schwelle
    const float tx = toX (thresholdDb);
    g.setColour (Theme::alert);
    g.fillRect (juce::Rectangle<float> (tx - 1.0f, b.getY() - 2.0f, 2.0f, b.getHeight() + 4.0f));
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    auto& scheme = getCurrentColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, Theme::background);
    scheme.setUIColour (ColourScheme::widgetBackground, Theme::panelRaised);
    scheme.setUIColour (ColourScheme::menuBackground, Theme::panelRaised);
    scheme.setUIColour (ColourScheme::outline, Theme::border);
    scheme.setUIColour (ColourScheme::defaultText, Theme::text);
    scheme.setUIColour (ColourScheme::defaultFill, Theme::accent);
    scheme.setUIColour (ColourScheme::highlightedText, Theme::background);
    scheme.setUIColour (ColourScheme::highlightedFill, Theme::accent);
    scheme.setUIColour (ColourScheme::menuText, Theme::text);
    setColourScheme (scheme);

    setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
    setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    setColour (juce::TextButton::textColourOffId, Theme::text);
    setColour (juce::TextButton::textColourOnId, Theme::background);
    setColour (juce::ComboBox::backgroundColourId, Theme::panelRaised);
    setColour (juce::ComboBox::outlineColourId, Theme::border);
    setColour (juce::TextEditor::backgroundColourId, Theme::background);
    setColour (juce::TextEditor::outlineColourId, Theme::border);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
    setColour (juce::TextEditor::textColourId, Theme::text);
    setColour (juce::Label::textColourId, Theme::text);
    setColour (juce::ListBox::backgroundColourId, Theme::panel);
    setColour (juce::ScrollBar::thumbColourId, Theme::border.brighter (0.2f));
    setColour (juce::Slider::thumbColourId, Theme::alert);
    setColour (juce::Slider::trackColourId, Theme::border);
    setColour (juce::Slider::backgroundColourId, Theme::background);
    setColour (juce::ToggleButton::tickColourId, Theme::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Theme::textDim);
    setColour (juce::ToggleButton::textColourId, Theme::text);
    setColour (juce::TabbedComponent::backgroundColourId, Theme::background);
    setColour (juce::TabbedComponent::outlineColourId, Theme::border);
    setColour (juce::ProgressBar::backgroundColourId, Theme::background);
    setColour (juce::ProgressBar::foregroundColourId, Theme::accent);
    setColour (juce::PopupMenu::backgroundColourId, Theme::panelRaised);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.3f));
    setColour (juce::AlertWindow::backgroundColourId, Theme::panelRaised);
}

int LookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (font (15.0f, true), button.getButtonText(), 0.0f, 0.0f);
    return juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth()) + 44 + tabDepth / 4;
}

void LookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
{
    auto area = button.getActiveArea().toFloat();
    const bool front = button.isFrontTab();
    const auto colour = button.getTabBackgroundColour();
    const bool hasColour = colour.getAlpha() > 0 && colour != Theme::panel;

    if (front)
    {
        g.setColour (Theme::panelRaised);
        g.fillRoundedRectangle (area.reduced (2.0f, 4.0f), 6.0f);
    }
    else if (isMouseOver)
    {
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (area.reduced (2.0f, 4.0f), 6.0f);
    }

    auto content = area.reduced (14.0f, 0.0f);

    if (hasColour)
    {
        auto dot = content.removeFromLeft (10.0f).withSizeKeepingCentre (8.0f, 8.0f);
        g.setColour (colour);
        g.fillEllipse (dot);
        content.removeFromLeft (6.0f);
    }

    g.setColour (front ? Theme::text : Theme::textDim);
    g.setFont (font (15.0f, front));
    g.drawText (button.getButtonText(), content, juce::Justification::centredLeft, true);

    if (front)
    {
        g.setColour (hasColour ? colour : Theme::accent);
        g.fillRoundedRectangle (area.reduced (14.0f, 0.0f).removeFromBottom (3.0f), 1.5f);
    }
}

void LookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void LookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics& g, int w, int h)
{
    g.setColour (Theme::border);
    g.fillRect (0, h - 1, w, 1);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                        bool isMouseOver, bool isButtonDown)
{
    auto b = button.getLocalBounds().toFloat().reduced (0.5f);
    auto c = backgroundColour;

    if (! button.isEnabled())       c = c.withMultipliedAlpha (0.4f);
    else if (isButtonDown)          c = c.brighter (0.15f);
    else if (isMouseOver)           c = c.brighter (0.08f);

    g.setColour (c);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (Theme::border.withAlpha (0.8f));
    g.drawRoundedRectangle (b, 6.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jmin (16.0f, (float) buttonHeight * 0.5f), true);
}

}
