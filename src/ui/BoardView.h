#pragma once

#include "AppController.h"
#include "Theme.h"

#include <deque>

/**
    Board: eine hohe Kachel pro Person, wie ein RF-Board.
    Foto mit Farbverlauf, Akku oben rechts, letzte Ansage, Verlauf von HF-Signal
    und Talkback-Pegel, darunter Frequenz, Laufzeit und Pegel.

    "Anpassen": Kacheln per Ziehen umsortieren und einzeln ein-/ausblenden.
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

    /** Liefert die Person, deren Alarm gerade ansteht (leer = keiner). Ihr Kommunikationsfeld leuchtet. */
    std::function<juce::String()> getAlertUserId;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (BoardView& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
    private:
        BoardView& owner;
    };

    // Werkzeuge auf einer Kachel im Anpassen-Modus
    enum class Tool { none, left, eye, right };
    struct ToolRects { juce::Rectangle<float> left, eye, right; };
    static ToolRects toolRectsFor (juce::Rectangle<float> card);
    Tool toolAt (int cardIndex, juce::Point<float>) const;
    int cardAt (juce::Point<float>) const;

    void setEditing (bool);
    void moveCard (int from, int to);
    void toggleHidden (const juce::String& userId);
    void commitLayout();
    void paintEditOverlay (juce::Graphics&, juce::Rectangle<float>, const UserProfile&, int index, int count);

    juce::Image getPhotoLayer (const UserProfile&, const juce::Image&, bool cutout, float side, float pixelScale);

    void timerCallback() override;
    void transcriptEntryAdded (const TranscriptEntry&) override;
    void transcriptReset() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void layoutCards();
    void paintCard (juce::Graphics&, juce::Rectangle<float>, const UserProfile&, int index);
    static void drawBattery (juce::Graphics&, juce::Rectangle<float>, int percent, bool known);
    void paintCommunication (juce::Graphics&, juce::Rectangle<float>, const UserProfile&, bool alerting);
    static void drawPill (juce::Graphics&, juce::Rectangle<float> centreArea, const juce::String& text, juce::Colour bg, juce::Colour fg);

    AppController& app;
    juce::Viewport viewport;
    Content content { *this };
    juce::TextButton editButton;
    std::vector<juce::Rectangle<float>> cardRects;
    juce::StringArray cardIds;          // Personen in Anzeigereihenfolge (im Anpassen-Modus inkl. ausgeblendeter)

    bool editing = false;
    struct Drag { int index = -1; bool active = false; float grabX = 0, mouseX = 0; int drop = -1; } drag;
    Tool hoverTool = Tool::none;
    int hoverCard = -1;

    struct PhotoLayer { juce::String key; juce::Image image; };
    std::map<juce::String, PhotoLayer> photoCache;

    static constexpr int toolbarH = 38;

    struct Last { juce::String text; juce::Time time; bool mention = false; };
    std::map<juce::String, std::deque<Last>> recent;   // letzte Ansagen pro Person
    static constexpr size_t recentLength = 8;
    std::map<juce::String, std::deque<float>> talkbackHistory;
    int tick = 0;

    static constexpr int talkbackHistoryLength = 600;   // 60 s bei 10 Hz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoardView)
};
