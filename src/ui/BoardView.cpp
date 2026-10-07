#include "BoardView.h"

using Theme::txt;

namespace
{
    constexpr float cardRadius = 16.0f;
    constexpr int gap = 14;

    juce::String formatMinutes (int minutes)
    {
        if (minutes < 0)
            return {};
        return juce::String (minutes / 60) + "h " + juce::String (minutes % 60).paddedLeft ('0', 2) + "m";
    }

    juce::String formatFrequency (int kHz)
    {
        if (kHz <= 0)
            return juce::String::fromUTF8 ("– MHz");
        return juce::String ((double) kHz / 1000.0, 3) + " MHz";
    }

    /** Vorberechnetes Bild an seinen Platz kopieren – bei 1:1 ohne Umrechnung (deutlich schneller). */
    void blit (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> r)
    {
        g.setOpacity (1.0f);
        const auto ri = r.toNearestInt();
        if (img.getWidth() == ri.getWidth() && img.getHeight() == ri.getHeight() && ri.toFloat() == r
            && g.getInternalContext().getPhysicalPixelScaleFactor() == 1.0f)
            g.drawImageAt (img, ri.getX(), ri.getY());
        else
            g.drawImage (img, r, juce::RectanglePlacement::stretchToFit);
    }

    juce::Path historyPath (const std::deque<float>& h, juce::Rectangle<float> area, int capacity, bool closed)
    {
        juce::Path p;
        if (h.size() < 2)
            return p;

        const float step = area.getWidth() / (float) juce::jmax (1, capacity - 1);
        const float startX = area.getRight() - step * (float) (h.size() - 1);
        auto yFor = [&] (float v) { return area.getBottom() - juce::jlimit (0.0f, 1.0f, v) * area.getHeight(); };

        if (closed)
            p.startNewSubPath (startX, area.getBottom());
        else
            p.startNewSubPath (startX, yFor (h.front()));

        float x = startX;
        for (float v : h)
        {
            p.lineTo (x, yFor (v));
            x += step;
        }

        if (closed)
        {
            p.lineTo (area.getRight(), area.getBottom());
            p.closeSubPath();
        }

        return p;
    }
}

BoardView::BoardView (AppController& a) : app (a)
{
    setOpaque (true);
    content.setOpaque (true);   // nichts dahinter muss mitgezeichnet werden
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    editButton.setButtonText (txt ("Dashboard anpassen"));
    editButton.setTooltip (txt ("Auswählen, wer auf dem Board erscheint und in welcher Reihenfolge"));
    editButton.onClick = [this] { setEditing (! editing); };
    addAndMakeVisible (editButton);

    app.transcript.addListener (this);
    app.state.addChangeListener (this);
    app.state.boardLayoutChanged.addChangeListener (this);
    transcriptReset();
    startTimerHz (20);
}

BoardView::~BoardView()
{
    app.transcript.removeListener (this);
    app.state.removeChangeListener (this);
    app.state.boardLayoutChanged.removeChangeListener (this);
}

//==============================================================================
void BoardView::transcriptEntryAdded (const TranscriptEntry& e)
{
    auto& d = recent[e.userId];
    d.push_back ({ e.text, e.time, e.isMention() });
    while (d.size() > recentLength)
        d.pop_front();
}

void BoardView::transcriptReset()
{
    recent.clear();
    for (auto& e : app.transcript.getEntries())
        transcriptEntryAdded (e);
}

void BoardView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! drag.active)
        layoutCards();
    repaint();
}

void BoardView::timerCallback()
{
    // Talkback-Pegel mit 10 Hz mitschreiben, auch wenn das Board gerade nicht sichtbar ist
    if (++tick % 2 == 0)
    {
        for (auto& u : app.state.getUsers())
        {
            auto& h = talkbackHistory[u.id];
            const float db = u.channel >= 0 ? app.engine.getLevelDb (u.channel) : -120.0f;
            h.push_back (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f));
            while ((int) h.size() > talkbackHistoryLength)
                h.pop_front();
        }
    }

    // Neu zeichnen mit 10 Hz (Graphen laufen ohnehin mit 10 Hz); nur das pulsierende Alarm-Feld braucht 20 Hz
    const bool pulsing = getAlertUserId != nullptr && getAlertUserId().isNotEmpty();
    if (isShowing() && (pulsing || tick % 2 == 0))
        content.repaint();
}

//==============================================================================
void BoardView::resized()
{
    auto b = getLocalBounds();
    auto bar = b.removeFromTop (toolbarH);
    editButton.setBounds (bar.removeFromRight (editing ? 110 : 200).reduced (8, 5).translated (-6, 0));
    viewport.setBounds (b);
    layoutCards();
}

void BoardView::setEditing (bool on)
{
    editing = on;
    drag = {};
    editButton.setButtonText (on ? txt ("Fertig") : txt ("Dashboard anpassen"));
    editButton.setColour (juce::TextButton::buttonColourId, on ? Theme::accent : Theme::panelRaised);
    editButton.setColour (juce::TextButton::textColourOffId, on ? Theme::background : Theme::text);
    content.setMouseCursor (on ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
    resized();
    repaint();
}

void BoardView::layoutCards()
{
    cardIds.clear();
    for (auto* u : app.state.getBoardUsers (editing))
        cardIds.add (u->id);

    const int n = cardIds.size();
    const int viewW = viewport.getWidth();
    const int viewH = viewport.getHeight() - (n > 0 ? 10 : 0);

    cardRects.clear();

    if (n == 0)
    {
        content.setSize (viewW, viewport.getHeight());
        content.repaint();
        return;
    }

    const int cardW = juce::jlimit (180, 300, (viewW - gap * (n + 1)) / n);
    const int cardH = juce::jmax (420, viewH - gap * 2);

    for (int i = 0; i < n; ++i)
        cardRects.push_back (juce::Rectangle<int> (gap + i * (cardW + gap), gap, cardW, cardH).toFloat());

    content.setSize (juce::jmax (viewW, gap + n * (cardW + gap)), cardH + gap * 2);
    content.repaint();
}

void BoardView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Leiste über den Kacheln
    auto bar = getLocalBounds().removeFromTop (toolbarH).toFloat();

    if (editing)
    {
        g.setColour (Theme::accent.withAlpha (0.14f));
        g.fillRect (bar);
        g.setColour (Theme::accent);
        g.setFont (Theme::font (13.0f, true));
        auto t = bar.reduced (16.0f, 0.0f);
        g.drawText (txt ("DASHBOARD ANPASSEN"), t.removeFromLeft (170.0f), juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (12.5f));
        g.drawText (txt ("Kacheln mit der Maus ziehen oder mit ◀ ▶ verschieben · Auge blendet ein/aus · "
                         "gilt für diesen Rechner, jedes Browser-Gerät hat sein eigenes Layout."),
                    t.withTrimmedRight (120.0f), juce::Justification::centredLeft, true);
    }
    else
    {
        const auto& all = app.state.getUsers();
        const int shown = (int) app.state.getBoardUsers (false).size();
        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (12.0f, true));
        juce::String info = "BOARD";
        if (! all.empty())
            info << txt ("  ·  ") << shown << " von " << (int) all.size() << " Personen";
        g.drawText (info, bar.reduced (16.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void BoardView::Content::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    auto& ids = owner.cardIds;
    auto& rects = owner.cardRects;

    if (ids.isEmpty())
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (17.0f));
        g.drawFittedText (owner.app.state.getUsers().empty()
                              ? txt ("Noch keine Personen. Lege sie unter „Personen“ an, weise Talkback-Eingang und Funkstrecke zu – "
                                     "hier erscheint dann für jede Person eine Kachel.")
                              : txt ("Alle Personen sind ausgeblendet. Oben rechts auf „Dashboard anpassen“ klicken und wieder einblenden."),
                          getLocalBounds().reduced (60), juce::Justification::centred, 3);
        return;
    }

    const auto clip = g.getClipBounds().toFloat();
    const int n = juce::jmin (ids.size(), (int) rects.size());

    // Beim Ziehen: übrige Kacheln rücken auf, an der Zielstelle ist ein Platzhalter
    std::vector<int> slots;   // Slot -> Kachel-Index (-1 = Platzhalter)
    for (int i = 0; i < n; ++i)
        if (! (owner.drag.active && i == owner.drag.index))
            slots.push_back (i);
    if (owner.drag.active)
        slots.insert (slots.begin() + juce::jlimit (0, (int) slots.size(), owner.drag.drop), -1);

    for (int s = 0; s < (int) slots.size() && s < n; ++s)
    {
        const auto r = rects[(size_t) s];
        if (! r.intersects (clip))
            continue;

        if (slots[(size_t) s] < 0)
        {
            g.setColour (Theme::accent.withAlpha (0.08f));
            g.fillRoundedRectangle (r, cardRadius);
            juce::Path outline, dashed;
            outline.addRoundedRectangle (r.reduced (1.0f), cardRadius);
            const float dashes[] = { 8.0f, 6.0f };
            juce::PathStrokeType (2.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Theme::accent);
            g.fillPath (dashed);
            continue;
        }

        if (auto* u = owner.app.state.findUser (ids[slots[(size_t) s]]))
        {
            owner.paintCard (g, r, *u, s);
            if (owner.editing)
                owner.paintEditOverlay (g, r, *u, s, n);
        }
    }

    // Gezogene Kachel folgt der Maus
    if (owner.drag.active && owner.drag.index >= 0 && owner.drag.index < n)
    {
        if (auto* u = owner.app.state.findUser (ids[owner.drag.index]))
        {
            auto r = rects[(size_t) owner.drag.index].withX (owner.drag.mouseX - owner.drag.grabX).translated (0.0f, -6.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 28, { 0, 12 }).drawForRectangle (g, r.toNearestInt());
            owner.paintCard (g, r, *u, owner.drag.drop);
            owner.paintEditOverlay (g, r, *u, owner.drag.drop, n);
        }
    }
}

int BoardView::cardAt (juce::Point<float> p) const
{
    for (int i = 0; i < (int) cardRects.size() && i < cardIds.size(); ++i)
        if (cardRects[(size_t) i].contains (p))
            return i;
    return -1;
}

BoardView::ToolRects BoardView::toolRectsFor (juce::Rectangle<float> card)
{
    auto row = card.reduced (10.0f).removeFromBottom (44.0f);
    ToolRects t;
    t.left = row.removeFromLeft (40.0f);
    t.right = row.removeFromRight (40.0f);
    t.eye = row.reduced (5.0f, 0.0f);
    return t;
}

BoardView::Tool BoardView::toolAt (int cardIndex, juce::Point<float> p) const
{
    if (! editing || cardIndex < 0 || cardIndex >= (int) cardRects.size())
        return Tool::none;

    const auto t = toolRectsFor (cardRects[(size_t) cardIndex]);
    if (t.left.contains (p))  return Tool::left;
    if (t.right.contains (p)) return Tool::right;
    if (t.eye.contains (p))   return Tool::eye;
    return Tool::none;
}

void BoardView::Content::mouseMove (const juce::MouseEvent& e)
{
    const int card = owner.cardAt (e.position);
    const auto tool = owner.toolAt (card, e.position);
    if (tool != owner.hoverTool || card != owner.hoverCard)
    {
        owner.hoverTool = tool;
        owner.hoverCard = card;
        setMouseCursor (! owner.editing ? juce::MouseCursor::PointingHandCursor
                        : tool != Tool::none ? juce::MouseCursor::PointingHandCursor
                                             : juce::MouseCursor::DraggingHandCursor);
        repaint();
    }
}

void BoardView::Content::mouseDown (const juce::MouseEvent& e)
{
    owner.drag = {};
    if (! owner.editing)
        return;

    const int i = owner.cardAt (e.position);
    if (i >= 0 && owner.toolAt (i, e.position) == Tool::none)
    {
        owner.drag.index = i;
        owner.drag.grabX = e.position.x - owner.cardRects[(size_t) i].getX();
        owner.drag.mouseX = e.position.x;
        owner.drag.drop = i;
    }
}

void BoardView::Content::mouseDrag (const juce::MouseEvent& e)
{
    auto& d = owner.drag;
    if (! owner.editing || d.index < 0)
        return;

    if (! d.active && e.getDistanceFromDragStart() < 6)
        return;

    d.active = true;
    d.mouseX = e.position.x;

    // Ziel-Slot: dort, wo die Mitte der gezogenen Kachel gerade ist
    const auto& rects = owner.cardRects;
    const float centre = d.mouseX - d.grabX + rects[(size_t) d.index].getWidth() * 0.5f;
    int drop = 0;
    for (int s = 0; s < (int) rects.size() - 1; ++s)
        if (centre > rects[(size_t) s].getCentreX() + (rects[(size_t) s].getWidth() + (float) gap) * 0.5f)
            drop = s + 1;
    d.drop = juce::jlimit (0, (int) rects.size() - 1, drop);

    const auto rel = e.getEventRelativeTo (&owner.viewport);
    owner.viewport.autoScroll (rel.x, rel.y, 60, 14);
    repaint();
}

void BoardView::Content::mouseUp (const juce::MouseEvent& e)
{
    auto d = owner.drag;
    owner.drag = {};

    if (d.active)
    {
        owner.moveCard (d.index, d.drop);
        return;
    }

    if (! e.mouseWasClicked())
        return;

    const int i = owner.cardAt (e.position);
    if (i < 0)
        return;

    if (! owner.editing)
    {
        if (owner.onOpenUser)
            owner.onOpenUser (owner.cardIds[i]);
        return;
    }

    switch (owner.toolAt (i, e.position))
    {
        case Tool::left:  owner.moveCard (i, i - 1); break;
        case Tool::right: owner.moveCard (i, i + 1); break;
        case Tool::eye:   owner.toggleHidden (owner.cardIds[i]); break;
        case Tool::none:  break;
    }
}

void BoardView::moveCard (int from, int to)
{
    to = juce::jlimit (0, cardIds.size() - 1, to);
    if (from < 0 || from >= cardIds.size() || from == to)
    {
        content.repaint();
        return;
    }

    cardIds.move (from, to);
    commitLayout();
}

void BoardView::toggleHidden (const juce::String& userId)
{
    auto hidden = app.state.getSettings().boardHidden;
    if (hidden.contains (userId))
        hidden.removeString (userId);
    else
        hidden.add (userId);

    app.state.setBoardLayout (editing ? cardIds : app.state.getSettings().boardOrder, hidden);
}

void BoardView::commitLayout()
{
    // Im Anpassen-Modus enthält cardIds alle Personen; sonst die ausgeblendeten hinten anhängen
    auto order = cardIds;
    for (auto* u : app.state.getBoardUsers (true))
        if (! order.contains (u->id))
            order.add (u->id);

    app.state.setBoardLayout (order, app.state.getSettings().boardHidden);
}

void BoardView::paintEditOverlay (juce::Graphics& g, juce::Rectangle<float> r, const UserProfile& u, int index, int count)
{
    const bool hidden = ! app.state.isOnBoard (u.id);

    if (hidden)
    {
        g.setColour (Theme::background.withAlpha (0.72f));
        g.fillRoundedRectangle (r, cardRadius);
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r.reduced (1.0f), cardRadius);
        const float dashes[] = { 7.0f, 6.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (Theme::textFaint);
        g.fillPath (dashed);
    }

    // Leiste unten abdecken, damit die Knöpfe nicht über der Fußzeile stehen
    {
        auto strip = r.withTop (r.getBottom() - 64.0f);
        juce::Path p;
        p.addRoundedRectangle (strip.getX(), strip.getY(), strip.getWidth(), strip.getHeight(), cardRadius, cardRadius, false, false, true, true);
        g.setColour (Theme::panel.withAlpha (0.97f));
        g.fillPath (p);
    }

    // Griff im Fotobereich
    {
        auto grip = juce::Rectangle<float> (48.0f, 48.0f).withCentre ({ r.getCentreX(), r.getBottom() - 112.0f });
        g.setColour (Theme::background.withAlpha (0.85f));
        g.fillEllipse (grip);
        g.setColour (Theme::border);
        g.drawEllipse (grip, 1.0f);

        juce::Path arrows;
        const auto c = grip.getCentre();
        const float a = 15.0f, w = 5.0f;
        arrows.addTriangle (c.x - a - 4.0f, c.y, c.x - a + 4.0f, c.y - w, c.x - a + 4.0f, c.y + w);
        arrows.addTriangle (c.x + a + 4.0f, c.y, c.x + a - 4.0f, c.y - w, c.x + a - 4.0f, c.y + w);
        arrows.addRectangle (c.x - a + 3.0f, c.y - 1.25f, (a - 3.0f) * 2.0f, 2.5f);
        g.setColour (Theme::text);
        g.fillPath (arrows);
    }

    const auto t = toolRectsFor (r);
    const bool isHoverCard = hoverCard == index && ! drag.active;

    auto button = [&] (juce::Rectangle<float> b, bool enabled, bool hover)
    {
        g.setColour (Theme::background.withAlpha (0.92f));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (hover && enabled ? Theme::accent : Theme::border);
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, hover && enabled ? 1.5f : 1.0f);
        return enabled ? Theme::text : Theme::textFaint.withAlpha (0.5f);
    };

    auto triangle = [&] (juce::Rectangle<float> b, bool left, juce::Colour col)
    {
        const auto c = b.getCentre();
        juce::Path p;
        if (left) p.addTriangle (c.x - 6.0f, c.y, c.x + 5.0f, c.y - 7.0f, c.x + 5.0f, c.y + 7.0f);
        else      p.addTriangle (c.x + 6.0f, c.y, c.x - 5.0f, c.y - 7.0f, c.x - 5.0f, c.y + 7.0f);
        g.setColour (col);
        g.fillPath (p);
    };

    triangle (t.left,  true,  button (t.left,  index > 0,         isHoverCard && hoverTool == Tool::left));
    triangle (t.right, false, button (t.right, index < count - 1, isHoverCard && hoverTool == Tool::right));

    const auto eyeCol = button (t.eye, true, isHoverCard && hoverTool == Tool::eye);
    {
        auto b = t.eye;
        const auto label = hidden ? txt ("Aus") : txt ("An");
        const bool withLabel = b.getWidth() >= 70.0f;
        auto icon = juce::Rectangle<float> (22.0f, 14.0f).withCentre ({ withLabel ? b.getCentreX() - 12.0f : b.getCentreX(), b.getCentreY() });
        juce::Path eye;
        eye.startNewSubPath (icon.getX(), icon.getCentreY());
        eye.quadraticTo (icon.getCentreX(), icon.getY() - 5.0f, icon.getRight(), icon.getCentreY());
        eye.quadraticTo (icon.getCentreX(), icon.getBottom() + 5.0f, icon.getX(), icon.getCentreY());
        eye.closeSubPath();
        g.setColour (hidden ? Theme::textFaint : Theme::accent);
        g.strokePath (eye, juce::PathStrokeType (1.6f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (icon.getCentre()));
        if (hidden)
            g.drawLine (icon.getX(), icon.getBottom() + 2.0f, icon.getRight(), icon.getY() - 2.0f, 1.8f);

        if (withLabel)
        {
            g.setColour (hidden ? Theme::textFaint : eyeCol);
            g.setFont (Theme::font (12.5f, true));
            g.drawText (label, icon.withX (icon.getRight() + 6.0f).withWidth (40.0f), juce::Justification::centredLeft, false);
        }

        if (hidden)
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (12.0f, true));
            g.drawText (txt ("AUSGEBLENDET"), r.withTop (r.getBottom() - 92.0f).withHeight (20.0f), juce::Justification::centred, false);
        }
    }
}

juce::Image BoardView::getPhotoLayer (const UserProfile& u, const juce::Image& image, bool cutout, float side, float pixelScale)
{
    // Skalieren, Schein und Schatten kosten viel – pro Person und Größe nur einmal rechnen.
    const int px = juce::jmax (8, juce::roundToInt (side * pixelScale));
    const auto key = u.avatarFile + (cutout ? "#cut#" : "#") + juce::String (px) + "#" + u.colour.toString();

    auto& entry = photoCache[u.id];
    if (entry.key == key && entry.image.isValid())
        return entry.image;

    juce::Image layer;

    if (! cutout)
    {
        layer = image.rescaled (px, px, juce::Graphics::highResamplingQuality);
    }
    else
    {
        const int pad = juce::roundToInt ((float) px * 0.15f);
        const int total = px + pad * 2;
        layer = juce::Image (juce::Image::ARGB, total, total, true);

        // Weicher Schein aus der Silhouette (klein weichzeichnen, dann hochskalieren)
        constexpr int small = 96, spad = 15;
        juce::Image sil (juce::Image::ARGB, small + spad * 2, small + spad * 2, true);
        {
            juce::Graphics sg (sil);
            sg.drawImage (image, juce::Rectangle<float> ((float) spad, (float) spad, (float) small, (float) small));
        }
        juce::ImageConvolutionKernel kernel (11);
        kernel.createGaussianBlur (5.0f);
        juce::Image blurred (juce::Image::ARGB, sil.getWidth(), sil.getHeight(), true);
        kernel.applyToImage (blurred, sil, sil.getBounds());

        juce::Graphics lg (layer);
        lg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto all = layer.getBounds().toFloat();
        const auto inner = all.reduced ((float) pad);
        const float k = inner.getWidth() / (float) small;
        const auto glowArea = inner.withSizeKeepingCentre ((float) blurred.getWidth() * k, (float) blurred.getHeight() * k);

        lg.setColour (u.colour.brighter (0.7f).withAlpha (0.55f));
        lg.drawImage (blurred, glowArea, juce::RectanglePlacement::stretchToFit, true);
        lg.setColour (juce::Colours::black.withAlpha (0.45f));
        lg.drawImage (blurred, glowArea.translated (0.0f, inner.getHeight() * 0.025f).reduced (inner.getWidth() * 0.02f),
                      juce::RectanglePlacement::stretchToFit, true);
        lg.setOpacity (1.0f);
        lg.drawImage (image, inner, juce::RectanglePlacement::stretchToFit);
    }

    entry = { key, layer };
    return layer;
}

//==============================================================================
void BoardView::drawBattery (juce::Graphics& g, juce::Rectangle<float> r, int percent, bool known)
{
    auto body = r.withTrimmedRight (4.0f);
    auto nub = juce::Rectangle<float> (body.getRight() + 1.0f, r.getCentreY() - r.getHeight() * 0.2f, 3.0f, r.getHeight() * 0.4f);

    const auto colour = ! known ? Theme::textFaint
                      : percent > 35 ? Theme::ok
                      : percent > 15 ? Theme::alert
                                     : Theme::record;

    g.setColour (colour.withAlpha (known ? 1.0f : 0.6f));
    g.drawRoundedRectangle (body.reduced (0.75f), 4.0f, 1.5f);
    g.fillRoundedRectangle (nub, 1.0f);

    if (! known)
        return;

    const int segments = juce::jlimit (0, 5, (percent + 19) / 20);
    auto inner = body.reduced (3.5f);
    const float segW = (inner.getWidth() - 4.0f * 1.5f) / 5.0f;

    for (int s = 0; s < segments; ++s)
        g.fillRoundedRectangle (inner.getX() + (float) s * (segW + 1.5f), inner.getY(), segW, inner.getHeight(), 1.5f);
}

void BoardView::drawPill (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour bg, juce::Colour fg)
{
    const auto f = Theme::font (11.0f, true);
    const float w = juce::GlyphArrangement::getStringWidth (f, text) + 18.0f;
    auto pill = area.withSizeKeepingCentre (w, 20.0f);

    g.setColour (bg);
    g.fillRoundedRectangle (pill, 5.0f);
    g.setColour (fg);
    g.setFont (f);
    g.drawText (text, pill, juce::Justification::centred, false);
}

void BoardView::paintCommunicationText (juce::Graphics& g, juce::Rectangle<float> area, const UserProfile& u, bool alerting, size_t count,
                                        const std::function<std::tuple<juce::String, juce::String, bool, bool> (size_t)>& get)
{
    auto inner = area.reduced (10.0f, 8.0f);
    auto head = inner.removeFromTop (16.0f);
    g.setFont (Theme::font (10.5f, true));
    g.setColour (alerting ? Theme::alert : Theme::textFaint);
    g.drawText (alerting ? txt ("● ALARM – AN DICH") : txt ("LETZTE ANSAGEN"), head, juce::Justification::centredLeft);
    inner.removeFromTop (4.0f);

    // Einträge von unten nach oben stapeln: Live-Text zuerst, dann die neuesten Ansagen
    struct Item { juce::String text; juce::String meta; bool mention, live; };
    std::vector<Item> items;
    for (size_t i = 0; i < count; ++i)
    {
        auto [text, meta, mention, live] = get (i);
        items.push_back ({ text, meta, mention, live });
    }

    if (items.empty())
    {
        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (12.0f));
        g.drawText (txt ("Noch nichts gesagt."), inner, juce::Justification::topLeft);
        return;
    }

    float bottom = inner.getBottom();
    const float width = inner.getWidth() - 8.0f;

    for (auto& item : items)
    {
        juce::AttributedString as;
        as.setWordWrap (juce::AttributedString::byWord);
        as.append (item.text, Theme::font (13.0f, item.mention),
                   item.live ? Theme::textDim : item.mention ? Theme::alert.brighter (0.3f) : Theme::text.withAlpha (0.9f));
        juce::TextLayout tl;
        tl.createLayout (as, width);

        const float h = 14.0f + tl.getHeight() + 6.0f;
        if (bottom - h < inner.getY())
        {
            // Passt nicht mehr ganz: oberste Zeile abschneiden statt weglassen, wenn es die erste ist
            if (&item != &items.front())
                break;
        }

        auto box = juce::Rectangle<float> (inner.getX(), bottom - h, inner.getWidth(), h);
        bottom -= h;

        if (item.mention)
        {
            g.setColour (Theme::alert);
            g.fillRoundedRectangle (box.getX() - 6.0f, box.getY() + 2.0f, 3.0f, box.getHeight() - 6.0f, 1.5f);
        }

        g.setFont (Theme::font (10.0f, true));
        g.setColour (item.live ? u.colour.brighter (0.4f) : item.mention ? Theme::alert : Theme::textFaint);
        g.drawText (item.meta + (item.mention && ! item.live ? txt ("  ·  ERWÄHNT") : juce::String()),
                    box.withHeight (14.0f), juce::Justification::centredLeft);

        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (inner.toNearestInt());
            tl.draw (g, box.withTrimmedTop (14.0f).withWidth (width));
        }

        if (bottom < inner.getY())
            break;
    }
}

void BoardView::paintCommunication (juce::Graphics& g, juce::Rectangle<float> area, const UserProfile& u, bool alerting)
{
    if (area.getHeight() < 30.0f)
        return;

    // Feld; leuchtet, solange ein Alarm dieser Person ansteht
    const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounterHiRes() * 0.008f);

    if (alerting)
    {
        g.setColour (Theme::alert.withAlpha (0.10f + 0.18f * pulse));
        g.fillRoundedRectangle (area.expanded (3.0f), 12.0f);
        g.setColour (Theme::alert.withAlpha (0.55f + 0.45f * pulse));
        g.drawRoundedRectangle (area.expanded (2.0f), 11.0f, 2.5f);
    }
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (area, 10.0f);
    }

    // Einträge sammeln (billig) – gesetzt und gezeichnet wird nur, wenn sich etwas geändert hat
    struct Item { juce::String text; juce::String meta; bool mention, live; };
    std::vector<Item> items;

    const auto& liveMap = app.transcript.getLive();
    if (auto lv = liveMap.find (u.id); lv != liveMap.end())
        items.push_back ({ lv->second.text + txt (" …"), txt ("spricht …"), lv->second.isMention(), true });

    if (auto it = recent.find (u.id); it != recent.end())
        for (auto e = it->second.rbegin(); e != it->second.rend(); ++e)
            items.push_back ({ e->text, e->time.formatted ("%H:%M:%S"), e->mention, false });

    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    juce::String key;
    key << area.getWidth() << "x" << area.getHeight() << "@" << scale << (alerting ? "A" : "-") << u.colour.toString();
    for (auto& item : items)
        key << "|" << item.meta << (item.mention ? "!" : "") << (item.live ? "~" : "") << item.text;

    auto& cache = commCache[u.id];
    if (cache.key != key || ! cache.image.isValid())
    {
        juce::Image img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (area.getWidth() * scale)),
                         juce::jmax (1, juce::roundToInt (area.getHeight() * scale)), true);
        {
            juce::Graphics ig (img);
            ig.addTransform (juce::AffineTransform::translation (-area.getX(), -area.getY()).scaled (scale));
            paintCommunicationText (ig, area, u, alerting, items.size(),
                                    [&items] (size_t i) { const auto& it = items[i]; return std::make_tuple (it.text, it.meta, it.mention, it.live); });
        }
        cache = { key, img };
    }

    blit (g, cache.image, area);   // (setzt die Deckkraft zurück – sonst übernimmt das Bild die der Feldfarbe)
}

void BoardView::paintCardStatic (juce::Graphics& g, juce::Rectangle<float> r, const UserProfile& u,
                                 juce::Rectangle<float> header, juce::Rectangle<float> photo,
                                 juce::Rectangle<float> graph, bool talkbackCard, float pixelScale)
{
        // Grundverlauf in der Farbe der Person
        const auto top = u.colour.withMultipliedSaturation (0.85f).withMultipliedBrightness (0.62f);
        juce::ColourGradient base (top, r.getX(), r.getY(), Theme::panel, r.getX(), r.getBottom(), false);
        base.addColour (0.45, u.colour.withMultipliedBrightness (0.38f).interpolatedWith (Theme::panel, 0.35f));
        g.setGradientFill (base);
        g.fillRect (r);

        // Lichtstrahlen hinter dem Foto
        const juce::Point<float> centre (photo.getCentreX(), photo.getY() + photo.getHeight() * 0.42f);
        const float radius = r.getHeight();
        constexpr int rays = 22;

        for (int i = 0; i < rays; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / (float) rays;
            const float w = 0.055f;
            juce::Path ray;
            ray.startNewSubPath (centre);
            ray.lineTo (centre + juce::Point<float> (std::sin (a - w), -std::cos (a - w)) * radius);
            ray.lineTo (centre + juce::Point<float> (std::sin (a + w), -std::cos (a + w)) * radius);
            ray.closeSubPath();
            g.setColour (u.colour.brighter (0.5f).withAlpha (i % 2 == 0 ? 0.10f : 0.05f));
            g.fillPath (ray);
        }

        juce::ColourGradient glow (u.colour.brighter (0.4f).withAlpha (0.55f), centre.x, centre.y,
                                   u.colour.withAlpha (0.0f), centre.x + photo.getWidth() * 0.65f, centre.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (photo.getWidth() * 1.3f, photo.getWidth() * 1.3f).withCentre (centre));

        // Foto oder Initialen
        const auto image = app.state.getAvatar (u.id);
        const bool cutout = image.isValid() && app.state.isAvatarCutout (u.id);
        auto photoBox = photo.reduced (photo.getWidth() * 0.06f, 0.0f);

        if (cutout)
        {
            // Freigestellt wie auf dem RF-Board: Person steht groß vor den Strahlen,
            // der Kopf ragt leicht in den Namensbereich.
            // Nur-Funk-Karte: Person groß wie auf dem RF-Board, Schultern laufen seitlich aus der Kachel
            const float side = talkbackCard ? juce::jmin (r.getWidth() * 1.12f, photo.getBottom() - (header.getY() + 14.0f))
                                            : juce::jmin (r.getWidth() * 1.5f, (photo.getBottom() - header.getY()) * 0.95f);
            auto imgArea = juce::Rectangle<float> (side, side).withCentre ({ photo.getCentreX(), photo.getBottom() + 4.0f - side * 0.5f });

            // Person + Schein einmal in Zielgröße vorberechnen, danach nur noch 1:1 kopieren
            const auto layer = getPhotoLayer (u, image, true, side, pixelScale);
            const float pad = side * 0.15f;
            g.drawImage (layer, imgArea.expanded (pad), juce::RectanglePlacement::stretchToFit);
        }
        else if (image.isValid())
        {
            const float side = juce::jmin (photoBox.getWidth(), photoBox.getHeight());
            auto imgArea = juce::Rectangle<float> (side, side).withCentre ({ photoBox.getCentreX(), photoBox.getBottom() - side * 0.5f });
            const auto layer = getPhotoLayer (u, image, false, side, pixelScale);
            g.drawImage (layer, imgArea, juce::RectanglePlacement::stretchToFit);
        }
        else
        {
            g.setColour (u.colour.brighter (0.6f).withAlpha (0.9f));
            g.setFont (Theme::font (juce::jmin (photoBox.getWidth(), photoBox.getHeight()) * 0.42f, true));
            g.drawText (Theme::initials (u.name), photoBox, juce::Justification::centred, false);
        }

        // Kopfbereich leicht abdunkeln, damit der Name auf dem Foto lesbar bleibt
        if (cutout)
        {
            juce::ColourGradient shade (juce::Colours::black.withAlpha (0.42f), r.getX(), r.getY(),
                                        juce::Colours::black.withAlpha (0.0f), r.getX(), header.getBottom() + 10.0f, false);
            g.setGradientFill (shade);
            g.fillRect (r.withBottom (header.getBottom() + 10.0f));
        }

        // Unterer Bereich abdunkeln, Foto weich auslaufen lassen
        const float fadeTop = photo.getBottom() - 70.0f;
        juce::ColourGradient fade (Theme::panel.withAlpha (0.0f), r.getX(), fadeTop,
                                   Theme::panel.withAlpha (0.96f), r.getX(), photo.getBottom() + 6.0f, false);
        g.setGradientFill (fade);
        g.fillRect (r.withTop (fadeTop).withBottom (photo.getBottom() + 6.0f));
        g.setColour (Theme::panel.withAlpha (0.96f));
        g.fillRect (r.withTop (photo.getBottom() + 6.0f));

        // Graph-Hintergrund
        g.setColour (Theme::background.withAlpha (0.6f));
        g.fillRect (graph);

}

void BoardView::paintCard (juce::Graphics& g, juce::Rectangle<float> r, const UserProfile& u, int index)
{
    const auto rf = app.getRfInfo (u);
    const auto& st = rf.state;
    const bool speaking = app.engine.isSpeaking (u.channel);
    const bool hasRf = rf.assigned && rf.online && rf.known;
    const int batt = hasRf && st.txOn ? st.batteryPercentEstimate() : -1;

    // --- Zustand -----------------------------------------------------------
    const bool txOff = hasRf && ! st.txOn;
    const bool critical = hasRf && st.txOn && batt >= 0
                          && (batt <= 15 || (st.batteryMinutes >= 0 && st.batteryMinutes < 30));
    const bool weakRf = hasRf && st.txOn && st.signalNormalised() < 0.2f;
    const bool warning = ! critical && hasRf && st.txOn && ((batt >= 0 && batt <= 35) || weakRf);
    const bool offline = rf.assigned && ! rf.online;

    const auto status = critical ? Theme::record
                      : warning  ? Theme::alert
                      : (txOff || offline) ? Theme::textFaint
                      : Theme::ok;

    // --- Aufteilung --------------------------------------------------------
    auto inner = r.reduced (14.0f, 12.0f);
    auto header = inner.removeFromTop (64.0f);
    const float footerH = 42.0f;
    const float graphH = juce::jlimit (70.0f, 130.0f, r.getHeight() * 0.17f);
    auto footer = inner.removeFromBottom (footerH);
    auto graph = inner.removeFromBottom (graphH).expanded (14.0f, 0.0f);
    auto badges = inner.removeFromBottom (28.0f);

    // Talkback-Person: Foto im oberen Drittel, darunter die Kommunikation.
    // Nur-Funk-Person (Redner, Band): großes Foto über die ganze Höhe.
    const bool talkbackCard = u.talkbackLog;
    const float photoBottom = talkbackCard ? juce::jmax (header.getBottom() + 80.0f, r.getY() + r.getHeight() / 3.0f + 10.0f)
                                           : inner.getBottom();
    auto lastArea = talkbackCard ? inner.withTop (photoBottom + 6.0f).withTrimmedBottom (4.0f)
                                 : inner.withTop (inner.getBottom()).withHeight (0.0f);
    auto photo = juce::Rectangle<float> (r.getX(), header.getBottom() - 10.0f, r.getWidth(), photoBottom - header.getBottom() + 10.0f);

    juce::Path cardPath;
    cardPath.addRoundedRectangle (r, cardRadius);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cardPath);

        // Unveränderliche Teile (Farbverlauf, Strahlen, Foto, Abdunklung) kommen fertig aus dem Zwischenspeicher
        {
            const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            const auto key = juce::String (r.getWidth()) + "x" + juce::String (r.getHeight()) + "@" + juce::String (scale)
                           + "#" + u.colour.toString() + "#" + u.avatarFile + "#" + (u.talkbackLog ? "tb" : "rf")
                           + "#" + (app.state.isAvatarCutout (u.id) ? "cut" : "") + "#" + u.name;
            auto& cache = staticCache[u.id];

            if (cache.key != key || ! cache.image.isValid())
            {
                juce::Image img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (r.getWidth() * scale)),
                                 juce::jmax (1, juce::roundToInt (r.getHeight() * scale)), true);
                {
                    juce::Graphics ig (img);
                    ig.addTransform (juce::AffineTransform::translation (-r.getX(), -r.getY()).scaled (scale));
                    ig.reduceClipRegion (cardPath);
                    paintCardStatic (ig, r, u, header, photo, graph, talkbackCard, scale);
                }
                cache = { key, img };
            }

            blit (g, cache.image, r);
        }

        // --- Graph: HF-Signal (Fläche) + Talkback-Pegel (weiße Linie) -----
        auto plot = graph.reduced (0.0f, 6.0f);

        const auto tb = talkbackHistory.find (u.id);
        const bool haveTb = tb != talkbackHistory.end() && u.channel >= 0
                            && std::any_of (tb->second.begin(), tb->second.end(), [] (float v) { return v > 0.02f; });

        if (rf.assigned && rf.history != nullptr && ! rf.history->empty())
        {
            auto area = historyPath (*rf.history, plot, WirelessManager::historyLength, true);
            juce::ColourGradient fill (status.withAlpha (0.75f), plot.getX(), plot.getY(),
                                       status.withAlpha (0.12f), plot.getX(), plot.getBottom(), false);
            g.setGradientFill (fill);
            g.fillPath (area);
            g.setColour (status);
            g.strokePath (historyPath (*rf.history, plot, WirelessManager::historyLength, false), juce::PathStrokeType (1.4f));

            if (haveTb)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.strokePath (historyPath (tb->second, plot, talkbackHistoryLength, false), juce::PathStrokeType (1.1f));
            }
        }
        else if (haveTb)
        {
            auto area = historyPath (tb->second, plot, talkbackHistoryLength, true);
            juce::ColourGradient fill (u.colour.withAlpha (0.7f), plot.getX(), plot.getY(),
                                       u.colour.withAlpha (0.1f), plot.getX(), plot.getBottom(), false);
            g.setGradientFill (fill);
            g.fillPath (area);
            g.setColour (u.colour.brighter (0.3f));
            g.strokePath (historyPath (tb->second, plot, talkbackHistoryLength, false), juce::PathStrokeType (1.2f));
        }

        if (speaking)
        {
            juce::ColourGradient sg (u.colour.withAlpha (0.0f), r.getX(), graph.getY() - 60.0f,
                                     u.colour.withAlpha (0.35f), r.getX(), graph.getBottom(), false);
            g.setGradientFill (sg);
            g.fillRect (r.withTop (graph.getY() - 60.0f).withBottom (graph.getBottom()));
        }
    }

    // --- Rahmen + Statusleiste --------------------------------------------
    g.setColour (speaking ? u.colour.brighter (0.4f) : status.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.75f), cardRadius, speaking ? 3.0f : 1.5f);
    g.setColour (status);
    g.fillRoundedRectangle (r.getX() + cardRadius, r.getY(), r.getWidth() - cardRadius * 2.0f, 3.0f, 1.5f);

    // --- Kopf ----------------------------------------------------------------
    {
        auto h = header;
        auto topRow = h.removeFromTop (20.0f);

        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.setFont (Theme::font (11.0f, true));
        g.drawText (juce::String (index + 1).paddedLeft ('0', 2), topRow, juce::Justification::centredLeft);

        if (rf.assigned)
            drawBattery (g, topRow.removeFromRight (46.0f).reduced (0.0f, 1.0f), batt, batt >= 0);

        auto nameRow = h.removeFromTop (30.0f);
        g.setFont (Theme::font (26.0f, true));
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawText (u.name, nameRow.translated (0.0f, 1.5f), juce::Justification::centredLeft, true);
        g.setColour (Theme::text);
        g.drawText (u.name, nameRow, juce::Justification::centredLeft, true);

        juce::String sub;
        if (! st.name.empty())
            sub << juce::String (st.name);
        if (u.role.isNotEmpty())
            sub << (sub.isNotEmpty() ? Theme::txt ("  ·  ") : "") << u.role;

        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (Theme::font (12.0f, true));
        g.drawText (sub.toUpperCase(), h, juce::Justification::topLeft, true);
    }

    // --- Kommunikation (nur Talkback-Personen) -------------------------------
    if (talkbackCard)
    {
        const bool alerting = getAlertUserId != nullptr && getAlertUserId() == u.id;
        paintCommunication (g, lastArea, u, alerting);
    }

    // --- Hinweise -----------------------------------------------------------
    if (critical)       drawPill (g, badges, txt ("AKKU KRITISCH"), Theme::record, juce::Colours::white);
    else if (txOff)     drawPill (g, badges, txt ("SENDER AUS"), Theme::textFaint.withAlpha (0.6f), Theme::text);
    else if (offline)   drawPill (g, badges, txt ("EMPFÄNGER OFFLINE"), Theme::textFaint.withAlpha (0.6f), Theme::text);
    else if (weakRf)    drawPill (g, badges, txt ("SCHWACHES SIGNAL"), Theme::alert, Theme::background);
    else if (warning)   drawPill (g, badges, txt ("AKKU NIEDRIG"), Theme::alert, Theme::background);
    else if (speaking)  drawPill (g, badges, txt ("SPRICHT"), u.colour, Theme::background);
    else if (st.muted && hasRf) drawPill (g, badges, txt ("STUMM"), Theme::textFaint.withAlpha (0.6f), Theme::text);

    // --- Fußzeile -------------------------------------------------------------
    {
        auto f = footer.withTrimmedTop (6.0f);
        auto line1 = f.removeFromTop (18.0f);
        auto line2 = f;

        g.setFont (Theme::font (13.0f, true));

        if (rf.assigned)
        {
            g.setColour (Theme::text);
            g.drawText (hasRf ? formatFrequency (st.frequencyKHz) : txt ("–"), line1, juce::Justification::centredLeft);

            juce::String level;
            if (hasRf && st.txOn && ! std::isnan (st.rfDbm))       level = juce::String (juce::roundToInt (st.rfDbm)) + " dBm";
            else if (hasRf && st.txOn && st.rfQualityPercent >= 0) level = "RF " + juce::String (st.rfQualityPercent) + " %";
            g.setColour (Theme::textDim);
            g.drawText (level, line1, juce::Justification::centredRight);

            juce::StringArray parts;
            if (! st.txModel.empty()) parts.add (juce::String (st.txModel));
            if (batt >= 0)             parts.add (juce::String (batt) + "%");
            if (st.batteryMinutes >= 0 && st.txOn) parts.add (formatMinutes (st.batteryMinutes));
            if (offline)               parts.add ("keine Verbindung");

            g.setFont (Theme::font (11.0f, true));
            g.setColour (Theme::textDim);
            g.drawText (parts.joinIntoString ("  "), line2, juce::Justification::centredLeft, true);
            g.drawText (juce::String (st.antenna), line2, juce::Justification::centredRight);
        }
        else
        {
            g.setColour (Theme::text);
            g.drawText (u.channel >= 0 ? "Talkback " + juce::String (u.channel + 1) : txt ("kein Eingang"),
                        line1, juce::Justification::centredLeft);

            if (u.channel >= 0)
            {
                g.setColour (Theme::textDim);
                g.drawText (juce::String (juce::roundToInt (juce::jmax (-99.0f, app.engine.getLevelDb (u.channel)))) + " dBFS",
                            line1, juce::Justification::centredRight);
            }

            g.setFont (Theme::font (11.0f, true));
            g.setColour (Theme::textFaint);
            g.drawText ("keine Funkstrecke zugewiesen", line2, juce::Justification::centredLeft, true);
        }
    }
}
