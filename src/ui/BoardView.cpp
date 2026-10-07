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
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    app.transcript.addListener (this);
    app.state.addChangeListener (this);
    transcriptReset();
    startTimerHz (20);
}

BoardView::~BoardView()
{
    app.transcript.removeListener (this);
    app.state.removeChangeListener (this);
}

//==============================================================================
void BoardView::transcriptEntryAdded (const TranscriptEntry& e)
{
    lastUtterance[e.userId] = { e.text, e.time, e.isMention() };
}

void BoardView::transcriptReset()
{
    lastUtterance.clear();
    for (auto& e : app.transcript.getEntries())
        lastUtterance[e.userId] = { e.text, e.time, e.isMention() };
}

void BoardView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    layoutCards();
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

    if (isShowing())
        content.repaint();
}

//==============================================================================
void BoardView::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutCards();
}

void BoardView::layoutCards()
{
    const auto& users = app.state.getUsers();
    const int n = (int) users.size();
    const int viewW = viewport.getWidth();
    const int viewH = viewport.getHeight() - (n > 0 ? 10 : 0);

    cardRects.clear();

    if (n == 0)
    {
        content.setSize (viewW, viewport.getHeight());
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
}

void BoardView::Content::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    const auto& users = owner.app.state.getUsers();

    if (users.empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (17.0f));
        g.drawFittedText (txt ("Noch keine Personen. Lege sie unter „Personen“ an, weise Talkback-Eingang und Funkstrecke zu – "
                               "hier erscheint dann für jede Person eine Kachel."),
                          getLocalBounds().reduced (60), juce::Justification::centred, 3);
        return;
    }

    const auto clip = g.getClipBounds().toFloat();

    for (size_t i = 0; i < users.size() && i < owner.cardRects.size(); ++i)
        if (owner.cardRects[i].intersects (clip))
            owner.paintCard (g, owner.cardRects[i], users[i], (int) i);
}

void BoardView::Content::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    const auto& users = owner.app.state.getUsers();
    for (size_t i = 0; i < owner.cardRects.size() && i < users.size(); ++i)
        if (owner.cardRects[i].contains (e.position) && owner.onOpenUser)
            owner.onOpenUser (users[i].id);
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
    auto lastArea = inner.removeFromBottom (juce::jmin (78.0f, inner.getHeight() * 0.3f));
    auto photo = juce::Rectangle<float> (r.getX(), header.getBottom() - 10.0f, r.getWidth(), lastArea.getY() - header.getBottom() + 10.0f);

    juce::Path cardPath;
    cardPath.addRoundedRectangle (r, cardRadius);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cardPath);

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
        auto photoBox = photo.reduced (photo.getWidth() * 0.06f, 0.0f);

        if (image.isValid())
        {
            const float side = juce::jmin (photoBox.getWidth(), photoBox.getHeight());
            auto imgArea = juce::Rectangle<float> (side, side).withCentre ({ photoBox.getCentreX(), photoBox.getBottom() - side * 0.5f });
            g.drawImage (image, imgArea, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        }
        else
        {
            g.setColour (u.colour.brighter (0.6f).withAlpha (0.9f));
            g.setFont (Theme::font (juce::jmin (photoBox.getWidth(), photoBox.getHeight()) * 0.42f, true));
            g.drawText (Theme::initials (u.name), photoBox, juce::Justification::centred, false);
        }

        // Unterer Bereich abdunkeln, Foto weich auslaufen lassen
        const float fadeTop = lastArea.getY() - 70.0f;
        juce::ColourGradient fade (Theme::panel.withAlpha (0.0f), r.getX(), fadeTop,
                                   Theme::panel.withAlpha (0.96f), r.getX(), lastArea.getY() + 6.0f, false);
        g.setGradientFill (fade);
        g.fillRect (r.withTop (fadeTop).withBottom (lastArea.getY() + 6.0f));
        g.setColour (Theme::panel.withAlpha (0.96f));
        g.fillRect (r.withTop (lastArea.getY() + 6.0f));

        // --- Graph: HF-Signal (Fläche) + Talkback-Pegel (weiße Linie) -----
        auto plot = graph.reduced (0.0f, 6.0f);
        g.setColour (Theme::background.withAlpha (0.6f));
        g.fillRect (graph);

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

        g.setColour (Theme::text);
        g.setFont (Theme::font (26.0f, true));
        g.drawText (u.name, h.removeFromTop (30.0f), juce::Justification::centredLeft, true);

        juce::String sub;
        if (! st.name.empty())
            sub << juce::String (st.name);
        if (u.role.isNotEmpty())
            sub << (sub.isNotEmpty() ? Theme::txt ("  ·  ") : "") << u.role;

        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (Theme::font (12.0f, true));
        g.drawText (sub.toUpperCase(), h, juce::Justification::topLeft, true);
    }

    // --- Live-Text (spricht gerade) oder letzte Ansage -------------------------
    const auto& liveMap = app.transcript.getLive();
    if (auto lv = liveMap.find (u.id); lv != liveMap.end())
    {
        auto la = lastArea.reduced (0.0f, 4.0f);
        g.setColour (lv->second.isMention() ? Theme::alert : u.colour.brighter (0.4f));
        g.setFont (Theme::font (11.0f, true));
        g.drawText (lv->second.isMention() ? txt ("ERWÄHNT  ·  SPRICHT …") : txt ("SPRICHT …"),
                    la.removeFromTop (16.0f), juce::Justification::centredLeft);

        g.setColour (Theme::text.withAlpha (0.75f));
        g.setFont (Theme::font (14.0f));
        g.drawFittedText (lv->second.text + txt (" …"), la.toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
    }
    else if (auto it = lastUtterance.find (u.id); it != lastUtterance.end())
    {
        auto la = lastArea.reduced (0.0f, 4.0f);
        g.setColour (it->second.mention ? Theme::alert : Theme::textFaint);
        g.setFont (Theme::font (11.0f, true));
        g.drawText ((it->second.mention ? txt ("ERWÄHNT  ·  ") : juce::String()) + it->second.time.formatted ("%H:%M:%S"),
                    la.removeFromTop (16.0f), juce::Justification::centredLeft);

        g.setColour (Theme::text.withAlpha (0.92f));
        g.setFont (Theme::font (14.0f));
        g.drawFittedText (txt ("„") + it->second.text + txt ("“"), la.toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
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
