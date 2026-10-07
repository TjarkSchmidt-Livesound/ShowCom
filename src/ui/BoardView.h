#pragma once

#include "AppController.h"
#include "Theme.h"

#include <deque>

/**
    Board: eine hohe Kachel pro Person, wie ein RF-Board.
    Foto mit Farbverlauf, Akku oben rechts, letzte Ansage, Verlauf von HF-Signal
    und Talkback-Pegel, darunter Frequenz, Laufzeit und Pegel.
*/
class BoardView : public juce::Component,
                  private juce::Timer,
                  private TranscriptModel::Listener,
                  private juce::ChangeListener
{
public:
    explicit BoardView (AppController&);
    ~BoardView() override;

    std::function<void (const juce::String& userId)> onOpenUser;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (BoardView& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        BoardView& owner;
    };

    void timerCallback() override;
    void transcriptEntryAdded (const TranscriptEntry&) override;
    void transcriptReset() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void layoutCards();
    void paintCard (juce::Graphics&, juce::Rectangle<float>, const UserProfile&, int index);
    static void drawBattery (juce::Graphics&, juce::Rectangle<float>, int percent, bool known);
    static void drawPill (juce::Graphics&, juce::Rectangle<float> centreArea, const juce::String& text, juce::Colour bg, juce::Colour fg);

    AppController& app;
    juce::Viewport viewport;
    Content content { *this };
    std::vector<juce::Rectangle<float>> cardRects;

    struct Last { juce::String text; juce::Time time; bool mention = false; };
    std::map<juce::String, Last> lastUtterance;
    std::map<juce::String, std::deque<float>> talkbackHistory;
    int tick = 0;

    static constexpr int talkbackHistoryLength = 600;   // 60 s bei 10 Hz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoardView)
};
