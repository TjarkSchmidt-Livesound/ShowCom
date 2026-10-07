#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Dunkles, blendarmes Design für den FOH-Platz (Stil: RF-Board). */
namespace Theme
{
    // Farbwelt angelehnt an RF-Boards: tiefes Navy, Teal für "alles gut", Amber/Rot für Warnungen
    const juce::Colour background   { 0xff12162b };
    const juce::Colour panel        { 0xff1a2040 };
    const juce::Colour panelRaised  { 0xff242b52 };
    const juce::Colour border       { 0xff323a68 };
    const juce::Colour text         { 0xfff1f3fa };
    const juce::Colour textDim      { 0xff9aa3c7 };
    const juce::Colour textFaint    { 0xff626b94 };
    const juce::Colour accent       { 0xff35d6b4 };
    const juce::Colour record       { 0xffff4d6d };
    const juce::Colour alert        { 0xffffb020 };
    const juce::Colour ok           { 0xff35d6b4 };

    /** UTF-8-Literal -> juce::String (Quelltexte sind UTF-8, MSVC mit /utf-8). */
    inline juce::String txt (const char* utf8)     { return juce::String::fromUTF8 (utf8); }

    inline juce::Font font (float size, bool bold = false)
    {
        juce::Font f { juce::FontOptions (size) };
        return bold ? f.boldened() : f;
    }

    inline juce::String initials (const juce::String& name)
    {
        juce::StringArray parts;
        parts.addTokens (name.trim(), " -", "");
        parts.removeEmptyStrings();

        if (parts.isEmpty())
            return "?";

        juce::String s = parts[0].substring (0, 1);
        if (parts.size() > 1)
            s += parts[parts.size() - 1].substring (0, 1);

        return s.toUpperCase();
    }

    /** Runder Avatar mit Farbring; "speaking" lässt den Ring leuchten. */
    void drawAvatar (juce::Graphics&, juce::Rectangle<float> bounds, const juce::Image& image,
                     const juce::String& name, juce::Colour colour, bool speaking);

    /** Horizontale Pegelanzeige mit Schwellen-Markierung. */
    void drawMeter (juce::Graphics&, juce::Rectangle<float> bounds, float levelDb, float thresholdDb,
                    bool speaking, juce::Colour colour);

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
        void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
        void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override;
        void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOver, bool isButtonDown) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    };
}
