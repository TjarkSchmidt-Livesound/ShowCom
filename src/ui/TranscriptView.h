#pragma once

#include "AppController.h"
#include "Theme.h"

/**
    Scrollbare Chat-Ansicht der Transkripte.
    Drei Varianten: alle Personen (Konversation), nur Erwähnungen, eine einzelne Person.
*/
class TranscriptView : public juce::Component,
                       private TranscriptModel::Listener,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    enum class Filter { all, mentions, user };

    TranscriptView (AppController&, Filter, const juce::String& userId = {});
    ~TranscriptView() override;

    void scrollToEntry (juce::int64 entryId);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        juce::int64 id = 0;
        int y = 0, height = 0;
        juce::TextLayout layout;
    };

    class Content : public juce::Component
    {
    public:
        explicit Content (TranscriptView& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        TranscriptView& owner;
    };

    // TranscriptModel::Listener
    void transcriptEntryAdded (const TranscriptEntry&) override;
    void transcriptReset() override;
    void transcriptLiveChanged (const juce::String& userId) override;
    // ChangeListener (AppState: Namen, Farben, Avatare)
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    bool accepts (const TranscriptEntry&) const;
    void rebuild();
    void layoutRow (Row&, const TranscriptEntry&, bool live = false) const;
    void layoutLive();
    int rowsBottom() const;
    void relayout();
    bool isScrolledToBottom() const;
    void scrollToBottom();
    int textWidth() const;
    int headerHeight() const        { return filter == Filter::user ? 84 : 0; }
    void paintUserHeader (juce::Graphics&, juce::Rectangle<int>);
    void paintRow (juce::Graphics&, const Row&, const TranscriptEntry&, int width, bool live = false);
    const Row* rowAt (int y) const;

    AppController& app;
    const Filter filter;
    const juce::String userId;

    juce::Viewport viewport;
    Content content { *this };
    std::vector<Row> rows;

    struct LiveRow { juce::String userId; Row row; };
    std::vector<LiveRow> liveRows;   // was gerade gesprochen wird

    juce::TextButton jumpButton { juce::CharPointer_UTF8 ("Neueste \xe2\x86\x93") };
    juce::int64 flashId = -1;
    double flashUntilMs = 0;
    int lastWidth = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriptView)
};
