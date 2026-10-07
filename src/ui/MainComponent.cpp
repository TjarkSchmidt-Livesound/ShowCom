#include "MainComponent.h"

using Theme::txt;

//==============================================================================
AlertBanner::AlertBanner (AppController& a) : app (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void AlertBanner::show (const TranscriptEntry& e, bool shouldFlash)
{
    pendingCount = active ? pendingCount + 1 : 0;
    active = true;
    flashing = shouldFlash;
    phase = true;
    entryId = e.id;
    userId = e.userId;
    text = e.text;
    time = e.time.formatted ("%H:%M:%S");
    keyword = e.mentions.empty() ? juce::String() : juce::String::fromUTF8 (e.mentions.front().keyword.c_str());

    if (auto* u = app.state.findUser (e.userId))
    {
        speaker = u->name;
        role = u->role;
        colour = u->colour;
    }
    else
    {
        speaker = e.speakerName;
        role = {};
        colour = Theme::textDim;
    }

    startTimer (400);
    repaint();

    if (onVisibilityChanged)
        onVisibilityChanged();
}

void AlertBanner::acknowledge()
{
    if (! active)
        return;

    active = false;
    pendingCount = 0;
    stopTimer();

    if (onVisibilityChanged)
        onVisibilityChanged();
}

void AlertBanner::timerCallback()
{
    phase = flashing ? ! phase : true;
    repaint();
}

void AlertBanner::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    const auto id = entryId;
    acknowledge();

    if (onJump)
        onJump (id);
}

void AlertBanner::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto bg = phase ? Theme::alert : Theme::alert.darker (0.9f);
    const auto fg = phase ? Theme::background : Theme::alert;

    g.setColour (bg);
    g.fillRect (b);

    auto area = getLocalBounds().reduced (18, 8);

    // rechts: Hinweis
    g.setColour (fg.withAlpha (0.85f));
    g.setFont (Theme::font (13.0f, true));
    g.drawText (txt ("KLICKEN = GESEHEN"), area.removeFromRight (150), juce::Justification::centredRight);

    // links: Avatar
    auto av = area.removeFromLeft (area.getHeight()).toFloat();
    Theme::drawAvatar (g, av, app.state.getAvatar (userId), speaker, colour, false);
    area.removeFromLeft (14);

    auto top = area.removeFromTop (area.getHeight() / 2);

    juce::String head;
    head << (keyword.isNotEmpty() ? keyword.toUpperCase() : juce::String ("DEIN NAME")) << txt (" ERWÄHNT")
         << Theme::txt ("   ·   ") << speaker << (role.isNotEmpty() ? " (" + role + ")" : juce::String()) << Theme::txt ("   ·   ") << time;
    if (pendingCount > 0)
        head << "   (+" << pendingCount << " weitere)";

    g.setColour (fg);
    g.setFont (Theme::font (19.0f, true));
    g.drawText (head, top, juce::Justification::bottomLeft, true);

    g.setFont (Theme::font (16.0f));
    g.drawText (txt ("„") + text + txt ("“"), area, juce::Justification::centredLeft, true);
}

//==============================================================================
MainComponent::MainComponent (AppController& a) : app (a)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    listenButton.onClick = [this] { toggleListening(); };
    addAndMakeVisible (listenButton);

    statusLabel.setFont (Theme::font (13.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (statusLabel);

    banner.onJump = [this] (juce::int64 id) { jumpToEntry (id); };
    banner.onVisibilityChanged = [this] { resized(); };
    addChildComponent (banner);

    tabs.setTabBarDepth (46);
    tabs.setOutline (0);
    tabs.setIndent (0);
    tabs.addTab ("Board", Theme::panel, &board, false);
    tabs.addTab ("Konversation", Theme::panel, &conversation, false);
    tabs.addTab (txt ("Erwähnungen"), Theme::alert, &mentions, false);
    tabs.addTab ("Personen", Theme::panel, &usersPanel, false);
    tabs.addTab ("Einstellungen", Theme::panel, &settingsPanel, false);
    addAndMakeVisible (tabs);

    board.onOpenUser = [this] (const juce::String& id)
    {
        for (auto& t : userTabs)
            if (t.userId == id)
                tabs.setCurrentTabIndex (indexOfTab (t.view.get()));
    };

    syncUserTabs();
    transcriptReset();

    // Beim allerersten Start direkt zur Einrichtung (nicht im Demo-Modus)
    if (juce::JUCEApplicationBase::getCommandLineParameters().contains ("--demo"))
        tabs.setCurrentTabIndex (indexOfTab (&board));
    else if (app.state.getUsers().empty())
        tabs.setCurrentTabIndex (indexOfTab (&usersPanel));
    else if (app.state.getSettings().modelPath.isEmpty())
        tabs.setCurrentTabIndex (indexOfTab (&settingsPanel));

    app.transcript.addListener (this);
    app.state.addChangeListener (this);
    app.onListeningChanged = [this] { timerCallback(); };
    app.onAudioDeviceChanged  = [this] { usersPanel.refreshChannels(); };

    timerCallback();
    startTimerHz (15);
    setSize (1280, 820);
}

MainComponent::~MainComponent()
{
    app.onListeningChanged = nullptr;
    app.onAudioDeviceChanged = nullptr;
    app.transcript.removeListener (this);
    app.state.removeChangeListener (this);

    tabs.clearTabs();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

int MainComponent::indexOfTab (juce::Component* c) const
{
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (tabs.getTabContentComponent (i) == c)
            return i;

    return -1;
}

//==============================================================================
void MainComponent::syncUserTabs()
{
    const auto& users = app.state.getUsers();
    const int firstUserTab = 3;   // Board, Konversation, Erwähnungen

    bool sameOrder = users.size() == userTabs.size();
    for (size_t i = 0; sameOrder && i < users.size(); ++i)
        sameOrder = users[i].id == userTabs[i].userId;

    if (sameOrder)
    {
        for (size_t i = 0; i < users.size(); ++i)
        {
            const int index = firstUserTab + (int) i;
            const auto name = users[i].name.isNotEmpty() ? users[i].name : juce::String ("?");
            if (tabs.getTabNames()[index] != name)
                tabs.setTabName (index, name);
            tabs.getTabbedButtonBar().setTabBackgroundColour (index, users[i].colour);
        }
        return;
    }

    auto* currentComp = tabs.getCurrentContentComponent();

    for (int i = (int) userTabs.size(); --i >= 0;)
        tabs.removeTab (firstUserTab + i);

    std::vector<UserTab> next;
    for (auto& u : users)
    {
        UserTab t { u.id, nullptr };

        for (auto& old : userTabs)
            if (old.userId == u.id)
                t.view = std::move (old.view);

        if (t.view == nullptr)
            t.view = std::make_unique<TranscriptView> (app, TranscriptView::Filter::user, u.id);

        next.push_back (std::move (t));
    }

    userTabs = std::move (next);

    for (size_t i = 0; i < userTabs.size(); ++i)
        tabs.addTab (users[i].name.isNotEmpty() ? users[i].name : juce::String ("?"), users[i].colour,
                     userTabs[i].view.get(), false, firstUserTab + (int) i);

    const int idx = indexOfTab (currentComp);
    tabs.setCurrentTabIndex (idx >= 0 ? idx : 0);
}

void MainComponent::updateMentionTabName()
{
    const int index = indexOfTab (&mentions);
    if (index >= 0)
        tabs.setTabName (index, mentionCount > 0 ? txt ("Erwähnungen (") + juce::String (mentionCount) + ")"
                                                 : txt ("Erwähnungen"));
}

void MainComponent::jumpToEntry (juce::int64 id)
{
    tabs.setCurrentTabIndex (indexOfTab (&conversation));
    conversation.scrollToEntry (id);
}

//==============================================================================
void MainComponent::transcriptEntryAdded (const TranscriptEntry& e)
{
    if (! e.isMention())
        return;

    ++mentionCount;
    updateMentionTabName();
    banner.show (e, app.state.getSettings().flashAlert);
}

void MainComponent::transcriptReset()
{
    mentionCount = 0;
    for (auto& e : app.transcript.getEntries())
        if (e.isMention())
            ++mentionCount;

    updateMentionTabName();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncUserTabs();
    resized();   // Platz für die Avatar-Leiste
    repaint (headerArea);
}

//==============================================================================
void MainComponent::toggleListening()
{
    if (! app.isListening())
    {
        app.setListening (true);
        timerCallback();
        return;
    }

    // Pausieren mitten in der Show soll kein Versehen sein
    juce::Component::SafePointer<MainComponent> self (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Zuhören pausieren?")
                                      .withMessage (txt ("Solange pausiert ist, wird nichts transkribiert und es gibt keinen Namens-Alarm."))
                                      .withButton ("Pausieren")
                                      .withButton ("Weiter zuhören"),
                                  [safe = self] (int result)
                                  {
                                      if (safe != nullptr && result == 1)
                                      {
                                          safe->app.setListening (false);
                                          safe->timerCallback();
                                      }
                                  });
}

void MainComponent::timerCallback()
{
    if (app.isListening())
    {
        const auto secs = (int) app.engine.getListeningSeconds();
        listenButton.setButtonText (txt ("●  HÖRT ZU   ") + juce::String::formatted ("%02d:%02d:%02d", secs / 3600, (secs / 60) % 60, secs % 60));
        listenButton.setColour (juce::TextButton::buttonColourId, Theme::ok.withAlpha (0.22f));
        listenButton.setColour (juce::TextButton::textColourOffId, Theme::ok);
        listenButton.setTooltip ("Klicken zum Pausieren");
    }
    else
    {
        listenButton.setButtonText (txt ("❚❚  PAUSIERT – fortsetzen"));
        listenButton.setColour (juce::TextButton::buttonColourId, Theme::alert);
        listenButton.setColour (juce::TextButton::textColourOffId, Theme::background);
        listenButton.setTooltip ("Klicken, um wieder zuzuhören");
    }

    juce::String status = app.engine.getDeviceDescription();
    status << "     " << app.transcriber.getModelStatusText();

    const int queue = app.transcriber.getQueueSize() + (app.transcriber.isBusy() ? 1 : 0);
    if (queue > 0)
        status << "     " << txt ("transkribiere … ") << queue;

    if (app.engine.getOverflowCount() > 0)
        status << txt ("     ⚠ Audio-Aussetzer: ") << app.engine.getOverflowCount();

    statusLabel.setText (status, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           app.transcriber.getModelState() == Transcriber::ModelState::ready ? Theme::textDim : Theme::alert);

    repaint (presenceArea);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    g.setColour (Theme::panel);
    g.fillRect (headerArea);
    g.setColour (Theme::border);
    g.fillRect (headerArea.withTop (headerArea.getBottom() - 1));

    auto h = headerArea.reduced (18, 0);
    // Logo: "SHOW" weiß, "COM" in Akzentfarbe, darunter die Funktion
    {
        auto logo = h.removeFromLeft (120);
        auto top = logo.withTrimmedBottom (headerArea.getHeight() / 2 - 4);
        const auto big = Theme::font (19.0f, true);
        const int showW = juce::GlyphArrangement::getStringWidthInt (big, "SHOW");

        g.setFont (big);
        g.setColour (Theme::text);
        g.drawText ("SHOW", top.removeFromLeft (showW + 1), juce::Justification::bottomLeft);
        g.setColour (Theme::accent);
        g.drawText ("COM", top, juce::Justification::bottomLeft);

        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (11.0f, true));
        g.drawText ("TALKBACK LOG", logo.withTrimmedTop (headerArea.getHeight() / 2 + 2), juce::Justification::topLeft);
    }

    // Wer spricht gerade? Kleine Avatare, die leuchten.
    auto p = presenceArea;
    for (auto& u : app.state.getUsers())
    {
        if (p.getWidth() < 40)
            break;

        auto r = p.removeFromLeft (40).toFloat().withSizeKeepingCentre (32.0f, 32.0f);
        const bool speaking = app.engine.isSpeaking (u.channel);

        if (! speaking)
            g.setOpacity (0.45f);

        Theme::drawAvatar (g, r, app.state.getAvatar (u.id), u.name, u.colour, speaking);
        g.setOpacity (1.0f);
        p.removeFromLeft (4);
    }
}

void MainComponent::resized()
{
    auto b = getLocalBounds();

    headerArea = b.removeFromTop (64);
    auto h = headerArea.reduced (16, 12);
    h.removeFromLeft (120);
    listenButton.setBounds (h.removeFromLeft (240));
    h.removeFromLeft (20);

    const int presenceWidth = juce::jmin (h.getWidth() / 2, (int) app.state.getUsers().size() * 44);
    presenceArea = h.removeFromLeft (presenceWidth);
    h.removeFromLeft (12);
    statusLabel.setBounds (h);

    banner.setVisible (banner.isActive());
    if (banner.isActive())
        banner.setBounds (b.removeFromTop (76));

    tabs.setBounds (b);
}
