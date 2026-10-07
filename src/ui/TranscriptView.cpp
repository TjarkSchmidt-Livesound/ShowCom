#include "TranscriptView.h"

namespace
{
    constexpr int avatarSize = 40;
    constexpr int leftText = 16 + avatarSize + 14;
    constexpr int rightPad = 20;
    constexpr int topLine = 30;
    constexpr float textSize = 17.0f;
}

TranscriptView::TranscriptView (AppController& a, Filter f, const juce::String& uid)
    : app (a), filter (f), userId (uid)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    jumpButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    jumpButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    jumpButton.onClick = [this] { scrollToBottom(); };
    addChildComponent (jumpButton);

    app.transcript.addListener (this);
    app.state.addChangeListener (this);
    startTimerHz (filter == Filter::user ? 30 : 4);
}

TranscriptView::~TranscriptView()
{
    app.transcript.removeListener (this);
    app.state.removeChangeListener (this);
}

bool TranscriptView::accepts (const TranscriptEntry& e) const
{
    switch (filter)
    {
        case Filter::all:      return true;
        case Filter::mentions: return e.isMention();
        case Filter::user:     return e.userId == userId;
    }
    return true;
}

int TranscriptView::textWidth() const
{
    return juce::jmax (100, viewport.getMaximumVisibleWidth() - leftText - rightPad);
}

void TranscriptView::layoutRow (Row& row, const TranscriptEntry& e) const
{
    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.setJustification (juce::Justification::topLeft);

    const auto normal = Theme::font (textSize);
    const auto strong = Theme::font (textSize, true);
    int pos = 0;

    for (auto& m : e.mentions)
    {
        const int start = (int) m.charStart, len = (int) m.charLength;
        if (start > pos)
            s.append (e.text.substring (pos, start), normal, Theme::text);

        s.append (e.text.substring (start, start + len), strong, Theme::alert);
        pos = start + len;
    }

    if (pos < e.text.length())
        s.append (e.text.substring (pos), normal, Theme::text);

    row.layout.createLayout (s, (float) textWidth());
    row.height = juce::jmax (avatarSize + 20, topLine + (int) std::ceil (row.layout.getHeight()) + 16);
}

void TranscriptView::relayout()
{
    int y = 8;
    for (auto& r : rows)
    {
        if (auto* e = app.transcript.findEntry (r.id))
            layoutRow (r, *e);

        r.y = y;
        y += r.height + 4;
    }

    content.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (y + 8, viewport.getHeight()));
    content.repaint();
}

void TranscriptView::rebuild()
{
    rows.clear();
    for (auto& e : app.transcript.getEntries())
        if (accepts (e))
            rows.push_back ({ e.id, 0, 0, {} });

    relayout();
    scrollToBottom();
}

bool TranscriptView::isScrolledToBottom() const
{
    return viewport.getViewPositionY() + viewport.getViewHeight() >= content.getHeight() - 30;
}

void TranscriptView::scrollToBottom()
{
    viewport.setViewPosition (0, juce::jmax (0, content.getHeight() - viewport.getViewHeight()));
    jumpButton.setVisible (false);
}

void TranscriptView::scrollToEntry (juce::int64 id)
{
    for (auto& r : rows)
    {
        if (r.id == id)
        {
            viewport.setViewPosition (0, juce::jmax (0, r.y - viewport.getViewHeight() / 3));
            flashId = id;
            flashUntilMs = juce::Time::getMillisecondCounterHiRes() + 2500.0;
            content.repaint();
            return;
        }
    }
}

//==============================================================================
void TranscriptView::transcriptEntryAdded (const TranscriptEntry& e)
{
    if (! accepts (e))
        return;

    const bool follow = isScrolledToBottom();

    Row r;
    r.id = e.id;
    layoutRow (r, e);
    r.y = rows.empty() ? 8 : rows.back().y + rows.back().height + 4;
    rows.push_back (std::move (r));

    content.setSize (viewport.getMaximumVisibleWidth(),
                     juce::jmax (rows.back().y + rows.back().height + 12, viewport.getHeight()));
    content.repaint();

    if (follow)
        scrollToBottom();
    else
        jumpButton.setVisible (true);
}

void TranscriptView::transcriptReset()
{
    rebuild();
}

void TranscriptView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
    content.repaint();
}

void TranscriptView::timerCallback()
{
    if (filter == Filter::user)
        repaint (0, 0, getWidth(), headerHeight());

    if (flashId >= 0 && juce::Time::getMillisecondCounterHiRes() > flashUntilMs)
    {
        flashId = -1;
        content.repaint();
    }

    if (jumpButton.isVisible() && isScrolledToBottom())
        jumpButton.setVisible (false);
}

//==============================================================================
void TranscriptView::resized()
{
    auto b = getLocalBounds();
    b.removeFromTop (headerHeight());
    viewport.setBounds (b);

    jumpButton.setBounds (b.getRight() - 140, b.getBottom() - 48, 120, 34);

    if (lastWidth != getWidth())
    {
        const bool follow = rows.empty() || isScrolledToBottom();
        lastWidth = getWidth();
        relayout();
        if (follow)
            scrollToBottom();
    }
    else
    {
        content.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (content.getHeight(), viewport.getHeight()));
    }
}

void TranscriptView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    if (filter == Filter::user)
        paintUserHeader (g, getLocalBounds().removeFromTop (headerHeight()));
}

void TranscriptView::paintUserHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto* u = app.state.findUser (userId);
    if (u == nullptr)
        return;

    g.setColour (Theme::panel);
    g.fillRect (area);
    g.setColour (Theme::border);
    g.fillRect (area.removeFromBottom (1));

    auto b = area.reduced (16, 10);
    const bool speaking = app.engine.isSpeaking (u->channel);
    Theme::drawAvatar (g, b.removeFromLeft (b.getHeight()).toFloat(), app.state.getAvatar (u->id), u->name, u->colour, speaking);
    b.removeFromLeft (14);

    auto meterArea = b.removeFromRight (juce::jmin (260, b.getWidth() / 3)).toFloat();
    auto textArea = b;

    g.setColour (Theme::text);
    g.setFont (Theme::font (22.0f, true));
    g.drawText (u->name, textArea.removeFromTop (textArea.getHeight() / 2 + 4), juce::Justification::bottomLeft);

    juce::String sub = u->role;
    if (u->channel >= 0)
    {
        const auto names = app.engine.getInputChannelNames();
        const auto chName = names[u->channel];
        sub << (sub.isNotEmpty() ? "  ·  " : "") << "Eingang " << (u->channel + 1)
            << (chName.isNotEmpty() ? " (" + chName + ")" : juce::String());
    }
    else
    {
        sub << (sub.isNotEmpty() ? "  ·  " : "") << "kein Eingang zugewiesen";
    }

    const int count = (int) rows.size();
    sub << "  ·  " << count << (count == 1 ? " Beitrag" : juce::String (juce::CharPointer_UTF8 (" Beitr\xc3\xa4ge")));

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (14.0f));
    g.drawText (sub, textArea, juce::Justification::topLeft);

    if (u->channel >= 0)
    {
        auto m = meterArea.withSizeKeepingCentre (meterArea.getWidth(), 10.0f);
        Theme::drawMeter (g, m, app.engine.getLevelDb (u->channel), u->thresholdDb, speaking, u->colour);

        g.setColour (speaking ? u->colour : Theme::textFaint);
        g.setFont (Theme::font (13.0f, speaking));
        g.drawText (speaking ? juce::String ("spricht ...") : juce::String ("still"),
                    meterArea.withTrimmedTop (meterArea.getHeight() / 2 + 8), juce::Justification::topRight);
    }
}

//==============================================================================
const TranscriptView::Row* TranscriptView::rowAt (int y) const
{
    for (auto& r : rows)
        if (y >= r.y && y < r.y + r.height)
            return &r;

    return nullptr;
}

void TranscriptView::paintRow (juce::Graphics& g, const Row& r, const TranscriptEntry& e, int width)
{
    auto bounds = juce::Rectangle<int> (8, r.y, width - 16, r.height);
    auto* u = app.state.findUser (e.userId);
    const auto colour = u != nullptr ? u->colour : Theme::textDim;
    const auto name = u != nullptr ? u->name : e.speakerName;

    if (e.isMention())
    {
        g.setColour (Theme::alert.withAlpha (0.12f));
        g.fillRoundedRectangle (bounds.toFloat(), 8.0f);
        g.setColour (Theme::alert);
        g.fillRoundedRectangle (bounds.toFloat().removeFromLeft (4.0f), 2.0f);
    }

    if (e.id == flashId)
    {
        g.setColour (Theme::accent.withAlpha (0.18f));
        g.fillRoundedRectangle (bounds.toFloat(), 8.0f);
    }

    Theme::drawAvatar (g, juce::Rectangle<float> (16.0f, (float) r.y + 10.0f, (float) avatarSize, (float) avatarSize),
                       app.state.getAvatar (e.userId), name, colour, false);

    // Kopfzeile: Name · Funktion · Uhrzeit
    auto head = juce::Rectangle<int> (leftText, r.y + 8, width - leftText - rightPad, 20);

    g.setFont (Theme::font (15.0f, true));
    g.setColour (colour);
    const int nameW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 10;
    g.drawText (name, head.removeFromLeft (nameW), juce::Justification::centredLeft, true);

    g.setFont (Theme::font (13.0f));
    g.setColour (Theme::textFaint);
    juce::String meta;
    if (u != nullptr && u->role.isNotEmpty())
        meta << u->role << "  ·  ";
    meta << e.time.formatted ("%H:%M:%S");
    g.drawText (meta, head, juce::Justification::centredLeft, true);

    if (e.isMention())
    {
        g.setColour (Theme::alert);
        g.setFont (Theme::font (12.0f, true));
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x97\x8f ERW\xc3\x84HNT"), head, juce::Justification::centredRight, false);
    }

    r.layout.draw (g, juce::Rectangle<float> ((float) leftText, (float) (r.y + topLine),
                                              r.layout.getWidth(), r.layout.getHeight()));
}

void TranscriptView::Content::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    const auto clip = g.getClipBounds();

    if (owner.rows.empty())
    {
        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (16.0f));

        juce::String msg;
        switch (owner.filter)
        {
            case Filter::all:      msg = "Noch keine Transkripte. Starte oben eine Session."; break;
            case Filter::mentions: msg = juce::String (juce::CharPointer_UTF8 ("Noch keine Erw\xc3\xa4hnungen deines Namens.")); break;
            case Filter::user:     msg = Theme::txt ("Noch keine Beiträge von dieser Person."); break;
        }

        g.drawFittedText (msg, getLocalBounds().reduced (40), juce::Justification::centred, 3);
        return;
    }

    // Nur sichtbare Zeilen zeichnen (binäre Suche nach der ersten)
    auto it = std::lower_bound (owner.rows.begin(), owner.rows.end(), clip.getY(),
                                [] (const Row& r, int y) { return r.y + r.height < y; });

    for (; it != owner.rows.end() && it->y < clip.getBottom(); ++it)
        if (auto* e = owner.app.transcript.findEntry (it->id))
            owner.paintRow (g, *it, *e, getWidth());
}

void TranscriptView::Content::mouseUp (const juce::MouseEvent& ev)
{
    if (! ev.mods.isPopupMenu())
        return;

    auto* row = owner.rowAt (ev.y);
    if (row == nullptr)
        return;

    auto* e = owner.app.transcript.findEntry (row->id);
    if (e == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Text kopieren");
    menu.addItem (2, "Zeile mit Zeit und Name kopieren");
    menu.addSeparator();
    menu.addItem (3, "Position in der Aufnahme: " + juce::RelativeTime (e->sessionOffsetSec).getDescription(), false);

    const auto text = e->text;
    const auto line = TranscriptModel::formatLine (*e);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [text, line] (int result)
                        {
                            if (result == 1) juce::SystemClipboard::copyTextToClipboard (text);
                            if (result == 2) juce::SystemClipboard::copyTextToClipboard (line);
                        });
}
