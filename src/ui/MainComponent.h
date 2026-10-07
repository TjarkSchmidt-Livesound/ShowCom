#pragma once

#include "AppController.h"
#include "Theme.h"
#include "TranscriptView.h"
#include "UsersPanel.h"
#include "SettingsPanel.h"
#include "BoardView.h"

#include <set>

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
/** Hinweisleiste: sagt klar, warum (noch) nichts transkribiert wird, mit Sprung zur Lösung. */
class SetupBar : public juce::Component
{
public:
    SetupBar() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    /** Gibt true zurück, wenn sich etwas geändert hat (Layout neu berechnen). */
    bool set (const juce::String& message, const juce::String& action, bool critical);

    std::function<void()> onAction;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override   { if (e.mouseWasClicked() && onAction) onAction(); }

private:
    juce::String text, actionText;
    bool isCritical = true;
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
    void transcriptLiveChanged (const juce::String& userId) override;
    std::set<juce::String> alertedUtterances;   // "userId:utterance" – Alarm nur einmal pro Ansage
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void toggleListening();
    void syncUserTabs();
    void updateMentionTabName();
    void jumpToEntry (juce::int64 id);
    int indexOfTab (juce::Component*) const;

    AppController& app;
    Theme::LookAndFeel lnf;

    juce::TextButton listenButton;
    juce::Label statusLabel;
    juce::Rectangle<int> headerArea, presenceArea;
    AlertBanner banner { app };
    SetupBar setupBar;
    std::function<void()> setupAction;
    juce::int64 silentSinceMs = 0;
    void updateSetupBar();
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    BoardView board { app };
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
