#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "AppController.h"
#include "Theme.h"

/** Lädt Whisper-Modelle von Hugging Face in den Modell-Ordner. */
class ModelDownloader : private juce::URL::DownloadTaskListener
{
public:
    struct Model { const char* file; const char* label; };
    static const std::vector<Model>& models();

    bool start (const juce::String& fileName);
    void cancel();
    bool isRunning() const                   { return task != nullptr; }
    double& getProgress()                    { return progressValue; }
    void syncProgress()                      { progressValue = atomicProgress.load(); }
    ~ModelDownloader() override;

    std::function<void (bool ok, juce::File file, juce::String error)> onFinished;   // Message-Thread

private:
    void finished (juce::URL::DownloadTask*, bool success) override;
    void progress (juce::URL::DownloadTask*, juce::int64 downloaded, juce::int64 total) override;

    std::unique_ptr<juce::URL::DownloadTask> task;
    juce::File target, partFile;
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

    void setStatus (const WirelessManager::DeviceStatus&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void commit();

    juce::ComboBox typeBox;
    juce::TextEditor hostEditor, labelEditor;
    juce::TextButton removeButton { "Entfernen" };
    juce::Label statusLabel;
    bool online = false;
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
    void chooseRecordingsFolder();
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

    // Transkription
    juce::Label modelLabel, modelStatus;
    juce::TextButton chooseModelButton { txtInit ("Modelldatei wählen …") };
    juce::ComboBox downloadBox;
    juce::TextButton downloadButton { "Herunterladen" };
    ModelDownloader downloader;
    juce::ProgressBar progressBar { downloader.getProgress() };
    juce::ComboBox languageBox;
    juce::ToggleButton gpuToggle { "GPU benutzen (Metal / CUDA / Vulkan, falls einkompiliert)" };
    juce::ToggleButton accurateToggle { "Genauer dekodieren (Beam-Search, etwas langsamer)" };

    // Mein Name
    juce::Label keywordsHint;
    juce::TextEditor keywordsEditor;
    juce::ToggleButton fuzzyToggle { txtInit ("Ähnliche Schreibweisen erkennen (1 Buchstabe Abweichung ab 5 Zeichen)") };
    juce::ToggleButton flashToggle { "Alarm-Banner blinkt" };
    juce::TextButton testAlertButton { "Alarm testen" };

    // Aufnahme
    juce::ToggleButton recordToggle { "Talkback als WAV mitschneiden (eine Datei pro Person, 24 Bit)" };
    juce::Label folderLabel;
    juce::TextButton chooseFolderButton { txtInit ("Ordner wählen …") }, openFolderButton { txtInit ("Ordner öffnen") };

    std::unique_ptr<juce::FileChooser> chooser;
    bool loading = false;
    juce::int64 keywordsDirtyAt = 0;

    static juce::String txtInit (const char* s)     { return juce::String::fromUTF8 (s); }
};
