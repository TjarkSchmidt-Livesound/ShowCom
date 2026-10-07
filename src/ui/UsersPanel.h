#pragma once

#include "AppController.h"
#include "Theme.h"

/** Avatar, der per Klick ein Bild auswählen lässt (auch Drag & Drop einer Bilddatei). */
class AvatarPicker : public juce::Component,
                     public juce::SettableTooltipClient,
                     public juce::FileDragAndDropTarget
{
public:
    explicit AvatarPicker (AppController&);

    void setUser (const juce::String& id);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override  { dragOver = true;  repaint(); }
    void fileDragExit (const juce::StringArray&) override             { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void choose();
    void apply (const juce::File&);

    AppController& app;
    juce::String userId;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;
};

//==============================================================================
/** Bearbeiten einer Person: Name, Funktion, Eingang, Schwelle, Farbe, Avatar. */
class UserEditor : public juce::Component,
                   private juce::Timer
{
public:
    explicit UserEditor (AppController&);

    void setUser (const juce::String& id);
    void refreshChannels();
    void refreshRfList();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onDeleted;

private:
    void timerCallback() override;
    void commit();
    UserProfile current() const;

    AppController& app;
    juce::String userId;
    bool loading = false;

    AvatarPicker avatar { app };
    juce::TextButton removeAvatarButton { "Bild entfernen" };
    juce::Label nameLabel { {}, "Name" }, roleLabel { {}, "Funktion" }, channelLabel { {}, "Eingang" },
                thresholdLabel { {}, "Sprach-Schwelle" }, colourLabel { {}, "Farbe" };
    juce::TextEditor nameEditor, roleEditor;
    juce::ComboBox channelBox;
    juce::ToggleButton talkbackToggle { "Talkback dieser Person mitloggen" };
    juce::Label rfLabel { {}, "Funkstrecke" };
    juce::ComboBox rfBox;
    std::vector<WirelessManager::ChannelRef> rfRefs;
    juce::Slider thresholdSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Rectangle<int> meterArea, swatchArea, hintArea, rfStatusArea;
    juce::TextButton deleteButton { "Person entfernen" };
    juce::String channelWarning;
    int rfRefreshTick = 0;
};

//==============================================================================
class UsersPanel : public juce::Component,
                   private juce::ListBoxModel,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    explicit UsersPanel (AppController&);
    ~UsersPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refreshChannels()     { editor.refreshChannels(); list.repaint(); }

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void selectUser (const juce::String& id);

    AppController& app;
    juce::ListBox list { "users", this };
    juce::TextButton addButton { "+ Person" }, upButton, downButton;
    UserEditor editor { app };
    juce::String selectedId;
};
