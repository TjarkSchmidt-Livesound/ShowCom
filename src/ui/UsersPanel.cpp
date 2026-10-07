#include "UsersPanel.h"

using Theme::txt;

//==============================================================================
AvatarPicker::AvatarPicker (AppController& a) : app (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (txt ("Klicken oder Bild hierher ziehen, um einen Avatar zu wählen"));
}

void AvatarPicker::setUser (const juce::String& id)
{
    userId = id;
    repaint();
}

void AvatarPicker::paint (juce::Graphics& g)
{
    auto* u = app.state.findUser (userId);
    if (u == nullptr)
        return;

    auto b = getLocalBounds().toFloat();
    Theme::drawAvatar (g, b, app.state.getAvatar (userId), u->name, u->colour, dragOver);

    if (isMouseOver() || dragOver)
    {
        const float side = juce::jmin (b.getWidth(), b.getHeight());
        auto c = b.withSizeKeepingCentre (side, side).reduced (side * 0.08f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (c);
        g.setColour (Theme::text);
        g.setFont (Theme::font (14.0f, true));
        g.drawText (txt ("Bild wählen …"), c, juce::Justification::centred);
    }
}

void AvatarPicker::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked())
        choose();
}

void AvatarPicker::choose()
{
    chooser = std::make_unique<juce::FileChooser> (txt ("Avatar wählen"),
                                                   juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
                                                   "*.png;*.jpg;*.jpeg;*.gif");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<AvatarPicker> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->apply (fc.getResult());
                          });
}

void AvatarPicker::apply (const juce::File& f)
{
    if (! app.state.setAvatarFromFile (userId, f))
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Avatar")
                                          .withMessage (txt ("Das Bild konnte nicht geladen werden (PNG, JPG oder GIF).") )
                                          .withButton ("OK"),
                                      nullptr);
    repaint();
}

bool AvatarPicker::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("png;jpg;jpeg;gif"))
            return true;

    return false;
}

void AvatarPicker::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
    {
        juce::File file (f);
        if (file.hasFileExtension ("png;jpg;jpeg;gif"))
        {
            apply (file);
            return;
        }
    }
}

//==============================================================================
UserEditor::UserEditor (AppController& a) : app (a)
{
    for (auto* l : { &nameLabel, &roleLabel, &channelLabel, &thresholdLabel, &colourLabel })
    {
        l->setFont (Theme::font (13.0f, true));
        l->setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (*l);
    }

    addAndMakeVisible (avatar);

    removeAvatarButton.onClick = [this] { app.state.clearAvatar (userId); avatar.repaint(); };
    addAndMakeVisible (removeAvatarButton);

    for (auto* ed : { &nameEditor, &roleEditor })
    {
        ed->setFont (Theme::font (17.0f));
        ed->setIndents (8, 8);
        ed->onTextChange = [this] { commit(); };
        addAndMakeVisible (*ed);
    }
    roleEditor.setTextToShowWhenEmpty ("z. B. FOH, Monitor, Licht, Stage", Theme::textFaint);

    channelBox.onChange = [this] { commit(); };
    addAndMakeVisible (channelBox);

    thresholdSlider.setRange (-72.0, -6.0, 1.0);
    thresholdSlider.setTextValueSuffix (" dB");
    thresholdSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
    thresholdSlider.onValueChange = [this] { commit(); };
    addAndMakeVisible (thresholdSlider);

    deleteButton.setColour (juce::TextButton::buttonColourId, Theme::record.withAlpha (0.25f));
    deleteButton.onClick = [this]
    {
        auto* u = app.state.findUser (userId);
        if (u == nullptr)
            return;

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Person entfernen")
                                          .withMessage (u->name + txt (" wirklich entfernen? Bisherige Transkripte bleiben erhalten."))
                                          .withButton ("Entfernen")
                                          .withButton ("Abbrechen"),
                                      [safe = juce::Component::SafePointer<UserEditor> (this), id = userId] (int result)
                                      {
                                          if (safe == nullptr || result != 1)
                                              return;

                                          safe->app.state.removeUser (id);
                                          if (safe->onDeleted)
                                              safe->onDeleted();
                                      });
    };
    addAndMakeVisible (deleteButton);

    startTimerHz (30);
}

void UserEditor::setUser (const juce::String& id)
{
    userId = id;
    avatar.setUser (id);

    auto* u = app.state.findUser (id);
    setVisible (u != nullptr);
    if (u == nullptr)
        return;

    const juce::ScopedValueSetter<bool> svs (loading, true);
    nameEditor.setText (u->name, false);
    roleEditor.setText (u->role, false);
    thresholdSlider.setValue (u->thresholdDb, juce::dontSendNotification);
    refreshChannels();
    repaint();
}

void UserEditor::refreshChannels()
{
    auto* u = app.state.findUser (userId);
    if (u == nullptr)
        return;

    const juce::ScopedValueSetter<bool> svs (loading, true);

    const auto names = app.engine.getInputChannelNames();
    channelBox.clear (juce::dontSendNotification);
    channelBox.addItem (txt ("– kein Eingang –"), 1);

    for (int ch = 0; ch < names.size() && ch < AudioEngine::maxChannels; ++ch)
    {
        juce::String label;
        label << (ch + 1) << "  " << names[ch];

        if (auto* other = app.state.findUserForChannel (ch, userId))
            label << "   (" << other->name << ")";

        channelBox.addItem (label, ch + 2);
    }

    if (u->channel >= names.size())
        channelBox.addItem ("Eingang " + juce::String (u->channel + 1) + " (am Interface nicht vorhanden)", u->channel + 2);

    channelBox.setSelectedId (u->channel + 2, juce::dontSendNotification);

    channelWarning = {};
    if (auto* other = app.state.findUserForChannel (u->channel, userId))
        channelWarning = txt ("Achtung: Dieser Eingang ist schon ") + other->name + txt (" zugewiesen. Pro Eingang zählt nur eine Person.");
    else if (u->channel >= 0 && u->channel >= names.size())
        channelWarning = txt ("Das aktuelle Audio-Interface hat diesen Eingang nicht.");
    else if (names.isEmpty())
        channelWarning = txt ("Kein Audio-Interface aktiv – unter Einstellungen auswählen.");

    repaint();
}

UserProfile UserEditor::current() const
{
    auto* u = app.state.findUser (userId);
    return u != nullptr ? *u : UserProfile();
}

void UserEditor::commit()
{
    if (loading || app.state.findUser (userId) == nullptr)
        return;

    auto u = current();
    u.name = nameEditor.getText().trim().isEmpty() ? u.name : nameEditor.getText().trim();
    u.role = roleEditor.getText().trim();
    u.channel = channelBox.getSelectedId() - 2;
    u.thresholdDb = (float) thresholdSlider.getValue();
    app.state.updateUser (u);
    refreshChannels();
    avatar.repaint();
}

void UserEditor::timerCallback()
{
    if (isShowing())
        repaint (meterArea.expanded (4, 30));
}

void UserEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! swatchArea.contains (e.getPosition()))
        return;

    const int size = 30, gap = 10;
    const int index = (e.x - swatchArea.getX()) / (size + gap);

    if (index >= 0 && index < 10)
    {
        auto u = current();
        u.colour = AppState::paletteColour (index);
        app.state.updateUser (u);
        repaint();
        avatar.repaint();
    }
}

void UserEditor::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);

    auto* u = app.state.findUser (userId);
    if (u == nullptr)
        return;

    // Pegel live, mit Schwelle
    if (u->channel >= 0)
    {
        const bool speaking = app.engine.isSpeaking (u->channel);
        const float level = app.engine.getLevelDb (u->channel);
        Theme::drawMeter (g, meterArea.toFloat(), level, u->thresholdDb, speaking, u->colour);

        g.setFont (Theme::font (13.0f, speaking));
        g.setColour (speaking ? u->colour : Theme::textFaint);
        g.drawText ((speaking ? txt ("● Sprache erkannt") : txt ("○ still"))
                        + "     Pegel " + juce::String (juce::roundToInt (level)) + " dBFS",
                    meterArea.translated (0, 14).withHeight (18), juce::Justification::centredLeft);
    }

    // Farbauswahl
    for (int i = 0; i < 10; ++i)
    {
        auto r = juce::Rectangle<float> ((float) swatchArea.getX() + (float) i * 40.0f, (float) swatchArea.getY(), 30.0f, 30.0f);
        const auto c = AppState::paletteColour (i);
        g.setColour (c);
        g.fillEllipse (r.reduced (3.0f));

        if (c == u->colour)
        {
            g.setColour (Theme::text);
            g.drawEllipse (r, 2.0f);
        }
    }

    // Hinweise
    g.setFont (Theme::font (13.0f));
    auto hint = hintArea;

    if (channelWarning.isNotEmpty())
    {
        g.setColour (Theme::alert);
        g.drawFittedText (channelWarning, hint.removeFromTop (36), juce::Justification::topLeft, 2);
    }

    g.setColour (Theme::textFaint);
    g.drawFittedText (txt ("Tipp: Den gelben Strich knapp über das Grundrauschen bzw. Bühnen-Übersprechen "
                           "stellen. Liegt der Pegel beim Sprechen deutlich darüber, wird sauber geschnitten."),
                      hint, juce::Justification::topLeft, 3);
}

void UserEditor::resized()
{
    auto b = getLocalBounds().reduced (24);

    auto top = b.removeFromTop (128);
    auto avatarCol = top.removeFromLeft (128);
    avatar.setBounds (avatarCol);
    top.removeFromLeft (24);

    nameLabel.setBounds (top.removeFromTop (20));
    nameEditor.setBounds (top.removeFromTop (38));
    top.removeFromTop (10);
    roleLabel.setBounds (top.removeFromTop (20));
    roleEditor.setBounds (top.removeFromTop (38));

    b.removeFromTop (6);
    removeAvatarButton.setBounds (b.removeFromTop (28).removeFromLeft (128));
    b.removeFromTop (14);

    channelLabel.setBounds (b.removeFromTop (20));
    channelBox.setBounds (b.removeFromTop (34));
    b.removeFromTop (14);

    thresholdLabel.setBounds (b.removeFromTop (20));
    thresholdSlider.setBounds (b.removeFromTop (30));
    b.removeFromTop (6);
    meterArea = b.removeFromTop (10).withTrimmedRight (82);
    b.removeFromTop (30);

    colourLabel.setBounds (b.removeFromTop (20));
    swatchArea = b.removeFromTop (34).withWidth (400);
    b.removeFromTop (14);

    deleteButton.setBounds (b.removeFromBottom (34).removeFromRight (170));
    hintArea = b.withTrimmedBottom (10);
}

//==============================================================================
UsersPanel::UsersPanel (AppController& a) : app (a)
{
    list.setRowHeight (72);
    list.setColour (juce::ListBox::backgroundColourId, Theme::panel);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    addButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    addButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    addButton.onClick = [this] { selectUser (app.state.addUser()); };
    addAndMakeVisible (addButton);

    upButton.setButtonText (txt ("▲"));
    downButton.setButtonText (txt ("▼"));
    upButton.setTooltip ("Nach oben");
    downButton.setTooltip ("Nach unten");
    upButton.onClick   = [this] { app.state.moveUser (selectedId, -1); };
    downButton.onClick = [this] { app.state.moveUser (selectedId, +1); };
    addAndMakeVisible (upButton);
    addAndMakeVisible (downButton);

    editor.onDeleted = [this]
    {
        const auto& users = app.state.getUsers();
        selectUser (users.empty() ? juce::String() : users.front().id);
    };
    addAndMakeVisible (editor);

    app.state.addChangeListener (this);
    list.updateContent();

    if (! app.state.getUsers().empty())
        selectUser (app.state.getUsers().front().id);
    else
        editor.setUser ({});

    startTimerHz (20);
}

UsersPanel::~UsersPanel()
{
    app.state.removeChangeListener (this);
}

void UsersPanel::selectUser (const juce::String& id)
{
    selectedId = id;
    list.updateContent();

    const auto& users = app.state.getUsers();
    for (int i = 0; i < (int) users.size(); ++i)
        if (users[(size_t) i].id == id)
            list.selectRow (i, false, true);

    editor.setUser (id);
}

int UsersPanel::getNumRows()
{
    return (int) app.state.getUsers().size();
}

void UsersPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto& users = app.state.getUsers();
    if (row < 0 || row >= (int) users.size())
        return;

    const auto& u = users[(size_t) row];
    auto b = juce::Rectangle<int> (0, 0, width, height).reduced (8, 4);

    if (selected)
    {
        g.setColour (Theme::panelRaised);
        g.fillRoundedRectangle (b.toFloat(), 8.0f);
        g.setColour (u.colour);
        g.fillRoundedRectangle (b.toFloat().removeFromLeft (3.0f), 1.5f);
    }

    b.reduce (10, 6);
    const bool speaking = app.engine.isSpeaking (u.channel);
    Theme::drawAvatar (g, b.removeFromLeft (b.getHeight()).toFloat(), app.state.getAvatar (u.id), u.name, u.colour, speaking);
    b.removeFromLeft (12);

    g.setColour (Theme::text);
    g.setFont (Theme::font (16.0f, true));
    g.drawText (u.name, b.removeFromTop (22), juce::Justification::centredLeft, true);

    juce::String sub;
    if (u.role.isNotEmpty())
        sub << u.role << "  ·  ";
    sub << (u.channel >= 0 ? "Eingang " + juce::String (u.channel + 1) : txt ("kein Eingang"));

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText (sub, b.removeFromTop (18), juce::Justification::centredLeft, true);

    if (u.channel >= 0)
        Theme::drawMeter (g, b.removeFromTop (12).withSizeKeepingCentre (b.getWidth(), 6).toFloat(),
                          app.engine.getLevelDb (u.channel), u.thresholdDb, speaking, u.colour);
}

void UsersPanel::selectedRowsChanged (int lastRowSelected)
{
    const auto& users = app.state.getUsers();
    if (lastRowSelected >= 0 && lastRowSelected < (int) users.size() && users[(size_t) lastRowSelected].id != selectedId)
    {
        selectedId = users[(size_t) lastRowSelected].id;
        editor.setUser (selectedId);
    }
}

void UsersPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    list.updateContent();

    const auto& users = app.state.getUsers();
    int index = -1;
    for (int i = 0; i < (int) users.size(); ++i)
        if (users[(size_t) i].id == selectedId)
            index = i;

    if (index < 0)
    {
        selectUser (users.empty() ? juce::String() : users.front().id);
        return;
    }

    if (list.getSelectedRow() != index)
        list.selectRow (index, false, true);

    editor.refreshChannels();
    list.repaint();
}

void UsersPanel::timerCallback()
{
    if (isShowing())
        list.repaint();
}

void UsersPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    if (app.state.getUsers().empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (16.0f));
        g.drawFittedText (txt ("Lege für jede Person am Talkback einen Eintrag an und weise ihr den "
                               "Eingang am Audio-Interface zu, auf dem ihr Talkback-Mikro liegt."),
                          getLocalBounds().withTrimmedLeft (340).reduced (40), juce::Justification::centred, 4);
    }
}

void UsersPanel::resized()
{
    auto b = getLocalBounds().reduced (16);

    auto left = b.removeFromLeft (320);
    auto buttons = left.removeFromBottom (40);
    addButton.setBounds (buttons.removeFromLeft (150));
    downButton.setBounds (buttons.removeFromRight (44));
    buttons.removeFromRight (6);
    upButton.setBounds (buttons.removeFromRight (44));
    left.removeFromBottom (10);
    list.setBounds (left);

    b.removeFromLeft (16);
    editor.setBounds (b.withWidth (juce::jmin (b.getWidth(), 640)));
}
