#include "MainComponent.h"
#include <BinaryData.h>

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
AlertPopupWindow::AlertPopupWindow (AppController& a, AlertBanner& b)
    : juce::DocumentWindow (Theme::txt ("ShowCom – Alarm"), Theme::background, juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (a, b), false);
    setResizable (true, false);
    setResizeLimits (300, 140, 1200, 600);
    centreWithSize (460, 190);
    setAlwaysOnTop (true);
}

AlertPopupWindow::Content::Content (AppController& a, AlertBanner& b) : app (a), banner (b)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (8);
}

void AlertPopupWindow::Content::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && banner.isActive())
        banner.acknowledge();
}

void AlertPopupWindow::Content::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().reduced (14);

    if (! banner.isActive())
    {
        // Ruhe: nur ein kleiner Status
        g.fillAll (Theme::panel);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (20.0f, true));
        g.drawText (txt ("Kein Alarm"), b.removeFromTop (b.getHeight() / 2), juce::Justification::centredBottom);
        g.setFont (Theme::font (13.0f, true));
        const bool listening = app.isListening();
        g.setColour (listening ? Theme::ok : Theme::alert);
        g.drawText (listening ? txt ("●  ShowCom hört zu") : txt ("❚❚  pausiert"), b.withTrimmedTop (6), juce::Justification::centredTop);
        return;
    }

    const auto info = banner.getInfo();
    const auto bg = info.phase ? Theme::alert : Theme::alert.darker (0.9f);
    const auto fg = info.phase ? Theme::background : Theme::alert;
    g.fillAll (bg);

    auto bottom = b.removeFromBottom (18);
    g.setColour (fg.withAlpha (0.8f));
    g.setFont (Theme::font (11.0f, true));
    g.drawText (txt ("KLICKEN = GESEHEN"), bottom, juce::Justification::centredRight);

    const float side = juce::jmin (64.0f, (float) b.getHeight() * 0.6f);
    auto left = b.removeFromLeft ((int) side + 12);
    Theme::drawAvatar (g, left.toFloat().withSizeKeepingCentre (side, side), app.state.getAvatar (banner.getUserId()),
                       info.speaker, info.colour, false);

    juce::String head;
    head << (info.keyword.isNotEmpty() ? info.keyword.toUpperCase() : txt ("DEIN NAME")) << txt (" ERWÄHNT");
    juce::String who;
    who << info.speaker << (info.role.isNotEmpty() ? " (" + info.role + ")" : juce::String()) << txt ("  ·  ") << info.time;
    if (info.more > 0)
        who << "   (+" << info.more << " weitere)";

    g.setColour (fg);
    g.setFont (Theme::font (19.0f, true));
    g.drawText (head, b.removeFromTop (26), juce::Justification::centredLeft, true);
    g.setFont (Theme::font (13.0f, true));
    g.drawText (who, b.removeFromTop (20), juce::Justification::centredLeft, true);
    g.setFont (Theme::font (16.0f));
    g.drawFittedText (txt ("„") + info.text + txt ("“"), b.reduced (0, 2), juce::Justification::topLeft, 4);
}

//==============================================================================
bool SetupBar::set (const juce::String& message, const juce::String& action, bool critical)
{
    if (message == text && action == actionText && critical == isCritical)
        return false;

    text = message;
    actionText = action;
    isCritical = critical;
    repaint();
    return true;
}

void SetupBar::paint (juce::Graphics& g)
{
    const auto colour = isCritical ? Theme::alert : Theme::accent;
    g.fillAll (colour.withAlpha (0.16f));
    g.setColour (colour);
    g.fillRect (getLocalBounds().removeFromLeft (4));

    auto b = getLocalBounds().reduced (18, 0);

    if (actionText.isNotEmpty())
    {
        const auto f = Theme::font (13.0f, true);
        const int w = juce::GlyphArrangement::getStringWidthInt (f, actionText) + 28;
        auto pill = b.removeFromRight (w).withSizeKeepingCentre (w, 28).toFloat();
        g.setColour (colour);
        g.fillRoundedRectangle (pill, 6.0f);
        g.setColour (Theme::background);
        g.setFont (f);
        g.drawText (actionText, pill, juce::Justification::centred);
        b.removeFromRight (12);
    }

    g.setColour (Theme::text);
    g.setFont (Theme::font (14.0f));
    g.drawFittedText (text, b, juce::Justification::centredLeft, 2);
}

//==============================================================================
MainComponent::MainComponent (AppController& a) : app (a)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    listenButton.onClick = [this] { toggleListening(); };
    addAndMakeVisible (listenButton);

    popupButton.setButtonText (txt ("Alarm-Popup"));
    popupButton.setTooltip (txt ("Kleines Fenster, immer im Vordergrund, das nur den Alarm zeigt"));
    popupButton.setClickingTogglesState (false);
    popupButton.setColour (juce::TextButton::buttonOnColourId, Theme::alert);
    popupButton.setColour (juce::TextButton::textColourOnId, Theme::background);
    popupButton.onClick = [this] { toggleAlertPopup(); };
    addAndMakeVisible (popupButton);

    statusLabel.setFont (Theme::font (13.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (statusLabel);

    banner.onJump = [this] (juce::int64 id) { jumpToEntry (id); };
    banner.onVisibilityChanged = [this] { resized(); };
    addChildComponent (banner);

    setupBar.onAction = [this] { if (setupAction) setupAction(); };
    addChildComponent (setupBar);

    tabs.setTabBarDepth (46);
    tabs.setOutline (0);
    tabs.setIndent (0);
    tabs.addTab ("Board", Theme::panel, &board, false);
    tabs.addTab ("Konversation", Theme::panel, &conversation, false);
    tabs.addTab (txt ("Erwähnungen"), Theme::alert, &mentions, false);
    tabs.addTab ("Personen", Theme::panel, &usersPanel, false);
    tabs.addTab ("Einstellungen", Theme::panel, &settingsPanel, false);
    addAndMakeVisible (tabs);

    board.getAlertUserId = [this] { return banner.getUserId(); };

    board.onOpenUser = [this] (const juce::String& id)
    {
        // Keine Favoritin/kein Favorit: Tab vorübergehend dazunehmen
        if (std::none_of (userTabs.begin(), userTabs.end(), [&] (const UserTab& t) { return t.userId == id; }))
        {
            extraTabUser = id;
            syncUserTabs();
        }

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

    // Bitfocus Companion: Alarm abfragen, quittieren, Popup schalten
    app.getAlarmInfo = [this]
    {
        WebServer::Snapshot::Alarm a;
        a.active = banner.isActive();
        if (a.active)
        {
            const auto info = banner.getInfo();
            a.keyword = info.keyword;
            a.userId = banner.getUserId();
            a.text = info.text;
            a.time = info.time;
        }
        return a;
    };
    app.acknowledgeAlarm = [this] { banner.acknowledge(); };
    app.toggleAlarmPopup = [this] { toggleAlertPopup(); };

    timerCallback();
    startTimerHz (15);
    setSize (1280, 820);
}

MainComponent::~MainComponent()
{
    alertPopup.reset();   // greift auf das Banner zu – zuerst schließen
    app.onListeningChanged = nullptr;
    app.onAudioDeviceChanged = nullptr;
    app.getAlarmInfo = nullptr;
    app.acknowledgeAlarm = nullptr;
    app.toggleAlarmPopup = nullptr;
    app.transcript.removeListener (this);
    app.state.removeChangeListener (this);

    tabs.clearTabs();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::toggleAlertPopup()
{
    if (alertPopup == nullptr)
    {
        alertPopup = std::make_unique<AlertPopupWindow> (app, banner);
        alertPopup->onClosed = [this] { popupButton.setToggleState (false, juce::dontSendNotification); };
    }

    const bool show = ! alertPopup->isVisible();
    alertPopup->setVisible (show);
    if (show)
        alertPopup->toFront (false);
    popupButton.setToggleState (show, juce::dontSendNotification);
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
    // Eigene Tabs für Favoriten, deren Talkback mitgeloggt wird (plus eine zuletzt vom Board geöffnete Person)
    std::vector<UserProfile> users;
    for (auto& u : app.state.getUsers())
        if (u.talkbackLog && (u.favorite || u.id == extraTabUser))
            users.push_back (u);

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

    // Wurde schon beim Live-Text alarmiert? Dann nicht noch einmal blinken.
    const auto key = e.userId + ":" + juce::String (e.utteranceId);
    if (e.utteranceId < 0 || alertedUtterances.count (key) == 0)
        banner.show (e, app.state.getSettings().flashAlert);
    alertedUtterances.insert (key);
}

void MainComponent::transcriptLiveChanged (const juce::String& userId)
{
    const auto& live = app.transcript.getLive();
    auto it = live.find (userId);
    if (it == live.end() || ! it->second.isMention())
        return;

    // Name fällt gerade – sofort alarmieren, nicht erst nach Ende der Ansage
    const auto key = userId + ":" + juce::String (it->second.utteranceId);
    if (alertedUtterances.insert (key).second)
        banner.show (it->second, app.state.getSettings().flashAlert);
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
void MainComponent::updateSetupBar()
{
    juce::String msg, action;
    bool critical = true;
    std::function<void()> act;

    auto goSettings = [this] { tabs.setCurrentTabIndex (indexOfTab (&settingsPanel)); };
    auto goPeople   = [this] { tabs.setCurrentTabIndex (indexOfTab (&usersPanel)); };

    const auto inputs = app.engine.getInputChannelNames();
    const auto modelState = app.transcriber.getModelState();

    bool anyAssigned = false, anySignal = false, anyTalkbackUser = false;
    for (auto& u : app.state.getUsers())
    {
        anyTalkbackUser = anyTalkbackUser || u.talkbackLog;
        if (AppState::logsTalkback (u) && u.channel < inputs.size())
        {
            anyAssigned = true;
            if (app.engine.getLevelDb (u.channel) > -100.0f)
                anySignal = true;
        }
    }

    const auto now = juce::Time::currentTimeMillis();
    if (! anyAssigned || anySignal)
        silentSinceMs = now;

    if (inputs.isEmpty())
    {
        msg = txt ("Kein Audio-Eingang aktiv. Wähle unter Einstellungen dein Audio-Interface bzw. das Mikrofon.");
        action = "Zu den Einstellungen";
        act = goSettings;
    }
    else if (modelState == Transcriber::ModelState::none || modelState == Transcriber::ModelState::failed)
    {
        msg = txt ("Noch kein Spracherkennungs-Modell geladen – ohne Modell wird nichts transkribiert. "
                   "Unter Einstellungen → Spracherkennung „Parakeet v3“ herunterladen (einmalig, danach offline).");
        action = "Modell laden";
        act = goSettings;
    }
    else if (modelState == Transcriber::ModelState::loading)
    {
        msg = txt ("Spracherkennungs-Modell wird geladen …");
        critical = false;
    }
    else if (! anyAssigned && anyTalkbackUser)
    {
        msg = txt ("Noch keiner Person ist ein Eingang zugewiesen. Transkribiert werden nur Eingänge, die einer Person gehören "
                   "(z. B. Eingang 1 = eingebautes Mikrofon).");
        action = "Personen einrichten";
        act = goPeople;
    }
    else if (app.isListening() && now - silentSinceMs > 8000)
    {
       #if JUCE_MAC
        msg = txt ("Von den zugewiesenen Eingängen kommt kein Signal. Ist der Mikrofon-Zugriff erlaubt? "
                   "Systemeinstellungen → Datenschutz & Sicherheit → Mikrofon → ShowCom einschalten, dann ShowCom neu starten.");
       #else
        msg = txt ("Von den zugewiesenen Eingängen kommt kein Signal. Stimmen Interface und Eingangs-Zuordnung? "
                   "Unter Windows auch: Einstellungen → Datenschutz → Mikrofon.");
       #endif
        action = "Pegel prüfen";
        act = goPeople;
    }

    setupAction = act;
    const bool show = msg.isNotEmpty();
    const bool changed = setupBar.set (msg, action, critical);

    if (show != setupBar.isVisible())
    {
        setupBar.setVisible (show);
        resized();
    }
    else if (changed)
    {
        setupBar.repaint();
    }
}

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
    updateSetupBar();
    statusLabel.setColour (juce::Label::textColourId,
                           app.transcriber.getModelState() == Transcriber::ModelState::ready ? Theme::textDim : Theme::alert);

    // Sprech-Anzeige oben nur neu zeichnen, wenn sich etwas geändert hat
    juce::String presence;
    for (auto& u : app.state.getUsers())
        presence << (AppState::logsTalkback (u) && app.engine.isSpeaking (u.channel) ? "1" : "0");
    if (presence != lastPresence)
    {
        lastPresence = presence;
        repaint (presenceArea);
    }
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
        auto iconArea = h.removeFromLeft (40).withSizeKeepingCentre (38, 38).toFloat();
        const auto icon = juce::ImageCache::getFromMemory (BinaryData::icon_small_png, BinaryData::icon_small_pngSize);
        g.drawImage (icon, iconArea.expanded (5.0f), juce::RectanglePlacement::centred);
        h.removeFromLeft (8);

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

    // Favoriten: kleine Avatare, die leuchten, wenn jemand spricht
    auto p = presenceArea;
    for (auto& u : app.state.getUsers())
    {
        if (! u.favorite)
            continue;
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
    h.removeFromLeft (168);   // Icon + Logo
    listenButton.setBounds (h.removeFromLeft (240));
    h.removeFromLeft (20);
    popupButton.setBounds (h.removeFromRight (124));
    h.removeFromRight (12);

    const auto& allUsers = app.state.getUsers();
    const int favourites = (int) std::count_if (allUsers.begin(), allUsers.end(), [] (const UserProfile& u) { return u.favorite; });
    const int presenceWidth = juce::jmin (h.getWidth() / 2, favourites * 44);
    presenceArea = h.removeFromLeft (presenceWidth);
    h.removeFromLeft (12);
    statusLabel.setBounds (h);

    if (setupBar.isVisible())
        setupBar.setBounds (b.removeFromTop (44));

    banner.setVisible (banner.isActive());
    if (banner.isActive())
        banner.setBounds (b.removeFromTop (76));

    tabs.setBounds (b);
}
