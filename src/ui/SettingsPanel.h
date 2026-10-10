#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "AppController.h"
#include "Theme.h"

/** Lädt Modelle (Parakeet: Ordner mit mehreren Dateien, Whisper: eine Datei) von Hugging Face. */
class ModelDownloader : private juce::URL::DownloadTaskListener
{
public:
    struct File { const char* name; juce::int64 bytes; };
    struct Model
    {
        const char* label;
        const char* folder;          // Parakeet: Unterordner im Modell-Ordner; Whisper: leer
        const char* baseUrl;
        std::vector<File> files;
        juce::File target() const;   // Ordner bzw. Datei
        bool isInstalled() const;
    };
    static const std::vector<Model>& models();

    bool start (const Model&);
    void cancel();
    bool isRunning() const                   { return task != nullptr; }
    double& getProgress()                    { return progressValue; }
    void syncProgress()                      { progressValue = atomicProgress.load(); }
    ~ModelDownloader() override;

    std::function<void (bool ok, juce::File file, juce::String error)> onFinished;   // Message-Thread

private:
    void finished (juce::URL::DownloadTask*, bool success) override;
    void progress (juce::URL::DownloadTask*, juce::int64 downloaded, juce::int64 total) override;

    void startNext();

    std::unique_ptr<juce::URL::DownloadTask> task;
    juce::File target, partFile;
    const Model* current = nullptr;
    size_t fileIndex = 0;
    juce::int64 doneBytes = 0, totalBytes = 0;
    double progressValue = 0.0;   // wird von ProgressBar gelesen (Message-Thread)
    std::atomic<double> atomicProgress { 0.0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

//==============================================================================
/** Eine Zeile "Funkempfänger": Typ, IP-Adresse, Bezeichnung, Status. */
class WirelessRow : public juce::Component
{
public:
    explicit WirelessRow (const WirelessDeviceConfig&);

    WirelessDeviceConfig config;
    std::function<void (const WirelessDeviceConfig&)> onChange;
    std::function<void()> onRemove;
    std::function<juce::String()> getLog;   // Rohdaten zur Diagnose

    void setStatus (const WirelessManager::DeviceStatus&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void commit();

    void saveLog();

    juce::ComboBox typeBox;
    juce::TextEditor hostEditor, labelEditor;
    juce::TextButton removeButton { "Entfernen" }, logButton;
    juce::ToggleButton enabledToggle;
    juce::OwnedArray<juce::TextButton> channelButtons;   // Kanäle einzeln an/aus
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    bool online = false;
};

//==============================================================================
/** Ein Browser-Gerät in der Liste am Host: Name, Alarm-Name, zuletzt gesehen, entfernen. */
class WebDeviceRow : public juce::Component
{
public:
    explicit WebDeviceRow (const WebServer::Device& d) : device (d)
    {
        removeButton.onClick = [this] { if (onRemove) onRemove(); };
        addAndMakeVisible (removeButton);

        // Name des Geräts: kommt automatisch (Netzwerkname/Gerätetyp), hier umbenennbar
        nameEditor.setText (d.label, false);
        nameEditor.setFont (Theme::font (14.0f, true));
        nameEditor.setIndents (6, 5);
        nameEditor.setTooltip (Theme::txt ("Name des Geräts – hier ändern, er wird für dieses Gerät gespeichert"));
        auto commit = [this]
        {
            const auto t = nameEditor.getText().trim();
            if (t.isNotEmpty() && t != device.label && onRename)
                onRename (t);
        };
        nameEditor.onReturnKey = [this] { nameEditor.unfocusAllComponents(); };
        nameEditor.onFocusLost = commit;
        addAndMakeVisible (nameEditor);
    }

    WebServer::Device device;
    std::function<void()> onRemove;
    std::function<void (const juce::String&)> onRename;

    void paint (juce::Graphics&) override;
    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 6);
        removeButton.setBounds (b.removeFromRight (100));
        nameEditor.setBounds (b.withTrimmedLeft (22).removeFromTop (b.getHeight() / 2 + 2).withWidth (juce::jmin (260, b.getWidth() - 140)));
    }

private:
    juce::TextButton removeButton { "Entfernen" };
    juce::TextEditor nameEditor;
};

//==============================================================================
class SettingsPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit SettingsPanel (AppController&);
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (SettingsPanel& o) : owner (o) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
        SettingsPanel& owner;
    };

    void timerCallback() override;
    void loadFromState();
    void store (const std::function<void (AppSettings&)>& change);
    void chooseModelFile();
    void startDownload();

    AppController& app;
    juce::Viewport viewport;
    Content content { *this };

    struct Section { juce::String title; juce::Rectangle<int> area; };
    std::vector<Section> sections;

    // Audio
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label audioHint;

    // Funkempfänger
    void rebuildWirelessRows();
    juce::Label wirelessHint;
    juce::OwnedArray<WirelessRow> wirelessRows;
    juce::TextButton addWirelessButton { txtInit ("+ Empfänger hinzufügen") };

    // Webzugriff
    void rebuildWebRows();
    juce::ToggleButton webToggle { "Browser-Zugriff im lokalen Netz (Handy, Tablet, Laptop)" };
    juce::Label webPortLabel { {}, "Port" }, webInfo;
    juce::TextEditor webPortEditor;
    juce::OwnedArray<WebDeviceRow> webRows;
    juce::String webRowsKey;

    // Bitfocus Companion
    juce::ToggleButton companionToggle { txtInit ("Bitfocus Companion darf ShowCom abfragen (Alarm, Namen, Funk, Sprecher)") };
    juce::ToggleButton companionControlToggle { txtInit ("Companion darf steuern (Alarm quittieren, Zuhören an/aus, Popup, Testalarm)") };
    juce::Label companionKeyLabel { {}, txtInit ("Schlüssel (optional)") }, companionInfo;
    juce::TextEditor companionKeyEditor;

    // Transkription
    juce::Label modelLabel, modelStatus;
    juce::TextButton chooseModelButton { txtInit ("Modell wählen …") };
    juce::ComboBox downloadBox;
    juce::TextButton downloadButton { "Herunterladen" };
    ModelDownloader downloader;
    juce::ProgressBar progressBar { downloader.getProgress() };
    juce::ComboBox languageBox;
    juce::ToggleButton gpuToggle { "Whisper: GPU benutzen (Metal / CUDA / Vulkan, falls einkompiliert)" };
    juce::Label modeLabel, vocabLabel;
    juce::ComboBox modeBox;
    juce::TextEditor vocabEditor;
    juce::int64 vocabDirtyAt = 0;
    juce::ToggleButton liveToggle { txtInit ("Live-Text schon während des Sprechens (braucht mehr Rechenleistung)") };

    // Mein Name
    juce::Label keywordsHint;
    juce::TextEditor keywordsEditor;
    juce::ToggleButton fuzzyToggle { txtInit ("Ähnliche Schreibweisen erkennen (1 Buchstabe Abweichung ab 5 Zeichen)") };
    juce::ToggleButton flashToggle { "Alarm-Banner blinkt" };
    juce::TextButton testAlertButton { "Alarm testen" };

    std::unique_ptr<juce::FileChooser> chooser;
    bool loading = false;
    juce::int64 keywordsDirtyAt = 0;

    static juce::String txtInit (const char* s)     { return juce::String::fromUTF8 (s); }
};
