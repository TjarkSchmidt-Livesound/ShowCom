#pragma once

#include "AppController.h"
#include "Theme.h"
#include "TranscriptView.h"
#include "UsersPanel.h"
#include "SettingsPanel.h"

/** Großes, blinkendes Banner, wenn dein Name fällt. Klick = gesehen + hinspringen. */
class AlertBanner : public juce::Component,
                    private juce::Timer
{
public:
    explicit AlertBanner (AppController&);

    void show (const TranscriptEntry&, bool flash);
    void acknowledge();
    bool isActive() const                   { return active; }

    std::function<void (juce::int64 entryId)> onJump;
    std::function<void()> onVisibilityChanged;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    AppController& app;
    bool active = false, flashing = true, phase = false;
    int pendingCount = 0;
    juce::int64 entryId = -1;
    juce::String speaker, role, text, keyword, time;
    juce::Colour colour;
    juce::String userId;
};

//==============================================================================
class MainComponent : public juce::Component,
                      private TranscriptModel::Listener,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit MainComponent (AppController&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void transcriptEntryAdded (const TranscriptEntry&) override;
    void transcriptReset() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void toggleSession();
    void syncUserTabs();
    void updateMentionTabName();
    void jumpToEntry (juce::int64 id);
    int indexOfTab (juce::Component*) const;

    AppController& app;
    Theme::LookAndFeel lnf;

    juce::TextButton sessionButton;
    juce::Label statusLabel;
    juce::Rectangle<int> headerArea, presenceArea;
    AlertBanner banner { app };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    TranscriptView conversation { app, TranscriptView::Filter::all };
    TranscriptView mentions { app, TranscriptView::Filter::mentions };
    UsersPanel usersPanel { app };
    SettingsPanel settingsPanel { app };

    struct UserTab { juce::String userId; std::unique_ptr<TranscriptView> view; };
    std::vector<UserTab> userTabs;

    int mentionCount = 0;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
