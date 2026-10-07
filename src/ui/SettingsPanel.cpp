#include "SettingsPanel.h"

using Theme::txt;

//==============================================================================
const std::vector<ModelDownloader::Model>& ModelDownloader::models()
{
    static const std::vector<Model> list = {
        { "ggml-large-v3-turbo-q5_0.bin", "large-v3-turbo q5  (574 MB) – genaueste Erkennung, empfohlen" },
        { "ggml-medium-q5_0.bin",         "medium q5          (539 MB) – genau, etwas schneller auf älteren Rechnern" },
        { "ggml-small-q5_1.bin",          "small q5           (190 MB) – schnell, für ältere Rechner" },
        { "ggml-large-v3-turbo.bin",      "large-v3-turbo     (1,6 GB) – höchste Qualität, nur mit GPU" },
        { "ggml-base-q5_1.bin",           "base q5            (60 MB) – sehr schnell, ungenauer" },
    };
    return list;
}

ModelDownloader::~ModelDownloader()
{
    alive->store (false);
    task.reset();
}

bool ModelDownloader::start (const juce::String& fileName)
{
    if (task != nullptr)
        return false;

    target = AppState::getModelsDir().getChildFile (fileName);
    partFile = target.withFileExtension (".part");
    partFile.deleteFile();
    atomicProgress.store (0.0);
    progressValue = 0.0;

    juce::URL url ("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" + fileName);
    task = url.downloadToFile (partFile, juce::URL::DownloadTaskOptions().withListener (this));
    return task != nullptr;
}

void ModelDownloader::cancel()
{
    task.reset();
    partFile.deleteFile();
    atomicProgress.store (0.0);
}

void ModelDownloader::progress (juce::URL::DownloadTask*, juce::int64 downloaded, juce::int64 total)
{
    if (total > 0)
        atomicProgress.store ((double) downloaded / (double) total);
    else
        atomicProgress.store (-1.0);   // unbekannte Größe: ProgressBar zeigt Animation
}

void ModelDownloader::finished (juce::URL::DownloadTask* t, bool success)
{
    const bool httpOk = t->statusCode() == 0 || (t->statusCode() >= 200 && t->statusCode() < 300);
    const auto code = t->statusCode();

    juce::MessageManager::callAsync ([this, flag = alive, success, httpOk, code]
    {
        if (! flag->load())
            return;

        task.reset();

        juce::String error;
        // Kleine Dateien sind HTML-Fehlerseiten, keine Modelle
        const bool plausible = partFile.getSize() > 10 * 1024 * 1024;

        if (success && httpOk && plausible && partFile.moveFileTo (target))
        {
            atomicProgress.store (1.0);
            if (onFinished) onFinished (true, target, {});
            return;
        }

        error = success ? ("Server-Antwort " + juce::String (code)) : juce::String ("Verbindung fehlgeschlagen");
        partFile.deleteFile();
        atomicProgress.store (0.0);
        if (onFinished) onFinished (false, {}, error);
    });
}

//==============================================================================
WirelessRow::WirelessRow (const WirelessDeviceConfig& c) : config (c)
{
    typeBox.addItem ("Shure", 1);
    typeBox.addItem ("Sennheiser EW-DX", 2);
    typeBox.setTooltip (Theme::txt ("Shure: ULX-D, QLX-D, SLX-D, Axient Digital  ·  Sennheiser: EW-DX EM 2 / EM 4"));
    typeBox.setSelectedId (c.type == WirelessDeviceConfig::Type::sennheiserEwdx ? 2 : 1, juce::dontSendNotification);
    typeBox.onChange = [this] { commit(); };
    addAndMakeVisible (typeBox);

    for (auto* ed : { &hostEditor, &labelEditor })
    {
        ed->setFont (Theme::font (15.0f));
        ed->setIndents (8, 7);
        ed->onReturnKey = [this] { commit(); };
        ed->onFocusLost = [this] { commit(); };
        addAndMakeVisible (*ed);
    }

    hostEditor.setText (c.host, false);
    hostEditor.setTextToShowWhenEmpty ("IP-Adresse, z. B. 192.168.1.50", Theme::textFaint);
    hostEditor.setInputRestrictions (64, "0123456789.:abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-");
    labelEditor.setText (c.label, false);
    labelEditor.setTextToShowWhenEmpty ("Bezeichnung, z. B. Rack links", Theme::textFaint);

    removeButton.onClick = [this] { if (onRemove) onRemove(); };
    addAndMakeVisible (removeButton);

    statusLabel.setFont (Theme::font (12.0f));
    statusLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (statusLabel);
}

void WirelessRow::commit()
{
    auto c = config;
    c.type = typeBox.getSelectedId() == 2 ? WirelessDeviceConfig::Type::sennheiserEwdx : WirelessDeviceConfig::Type::shure;
    c.host = hostEditor.getText().trim();
    c.label = labelEditor.getText().trim();

    if (c.type != config.type || c.host != config.host || c.label != config.label)
    {
        config = c;
        if (onChange)
            onChange (c);
    }
}

void WirelessRow::setStatus (const WirelessManager::DeviceStatus& st)
{
    online = st.online;
    auto text = st.text;
    if (st.online && st.model.isNotEmpty())
        text = st.model.trim() + Theme::txt ("  ·  ") + text;

    if (statusLabel.getText() != text)
        statusLabel.setText (text, juce::dontSendNotification);

    statusLabel.setColour (juce::Label::textColourId, st.online ? Theme::ok : Theme::alert);
    repaint();
}

void WirelessRow::paint (juce::Graphics& g)
{
    g.setColour (Theme::background.withAlpha (0.5f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    g.setColour (online ? Theme::ok : Theme::alert);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ 16.0f, 22.0f }));
}

void WirelessRow::resized()
{
    auto b = getLocalBounds().reduced (10, 6).withTrimmedLeft (18);
    auto top = b.removeFromTop (32);
    removeButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    typeBox.setBounds (top.removeFromLeft (juce::jmin (190, top.getWidth() / 3)));
    top.removeFromLeft (8);
    hostEditor.setBounds (top.removeFromLeft (top.getWidth() / 2));
    top.removeFromLeft (8);
    labelEditor.setBounds (top);
    statusLabel.setBounds (b.withTrimmedTop (2));
}

//==============================================================================
SettingsPanel::SettingsPanel (AppController& a)
    : app (a),
      deviceSelector (a.engine.getDeviceManager(), 0, AudioEngine::maxChannels, 0, 0, false, false, false, false)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    auto styleHint = [] (juce::Label& l)
    {
        l.setFont (Theme::font (13.0f));
        l.setColour (juce::Label::textColourId, Theme::textDim);
        l.setJustificationType (juce::Justification::topLeft);
    };

    // --- Audio --------------------------------------------------------------
    content.addAndMakeVisible (deviceSelector);
    audioHint.setText (txt (
                           #if JUCE_WINDOWS
                            "Windows: Als Gerätetyp „ASIO“ wählen und unter „Active input channels“ die "
                            "Talkback-Kanäle aktivieren (zugewiesene Eingänge werden automatisch eingeschaltet). "
                            "Viele ASIO-Treiber erlauben nur ein Programm gleichzeitig – läuft parallel ein "
                            "Mitschnitt-DAW auf demselben Interface, einen Multi-Client-Treiber oder ein zweites "
                            "Interface (z. B. Dante Virtual Soundcard / AVB) verwenden."
                           #else
                            "macOS: Das Interface erscheint unter „CoreAudio“. Mehrere Programme können es "
                            "gleichzeitig nutzen. Beim ersten Start fragt macOS nach Mikrofon-Zugriff – bitte erlauben."
                           #endif
                           ), juce::dontSendNotification);
    styleHint (audioHint);
    content.addAndMakeVisible (audioHint);

    // --- Funkempfänger ------------------------------------------------------
    wirelessHint.setText (txt ("Empfänger im selben Netzwerk wie dieser Rechner. Shure: Steuerung über TCP-Port 2202 "
                               "(Standard, nichts einzustellen). Sennheiser EW-DX: im Empfänger bzw. in Control Cockpit unter "
                               "„Access“ den Drittanbieter-Zugriff „Legacy (SSCv1)“ aktivieren (UDP-Port 45). "
                               "Die Zuordnung Person ↔ Funkkanal erfolgt unter „Personen“."),
                          juce::dontSendNotification);
    styleHint (wirelessHint);
    content.addAndMakeVisible (wirelessHint);

    addWirelessButton.onClick = [this]
    {
        store ([] (AppSettings& s)
        {
            WirelessDeviceConfig d;
            d.id = juce::Uuid().toString();
            s.wirelessDevices.push_back (d);
        });
    };
    content.addAndMakeVisible (addWirelessButton);

    // --- Transkription -----------------------------------------------------
    modelLabel.setFont (Theme::font (15.0f, true));
    content.addAndMakeVisible (modelLabel);
    styleHint (modelStatus);
    content.addAndMakeVisible (modelStatus);

    chooseModelButton.onClick = [this] { chooseModelFile(); };
    content.addAndMakeVisible (chooseModelButton);

    int id = 1;
    for (auto& m : ModelDownloader::models())
        downloadBox.addItem (txt (m.label), id++);
    // large-v3-turbo q5 ist bei Deutsch am genauesten; die Schnelligkeit kommt über den Live-Text.
    downloadBox.setSelectedId (1, juce::dontSendNotification);
    content.addAndMakeVisible (downloadBox);

    downloadButton.onClick = [this] { startDownload(); };
    content.addAndMakeVisible (downloadButton);
    content.addChildComponent (progressBar);

    downloader.onFinished = [this] (bool ok, juce::File file, juce::String error)
    {
        progressBar.setVisible (false);
        downloadButton.setButtonText ("Herunterladen");

        if (ok)
            store ([file] (AppSettings& s) { s.modelPath = file.getFullPathName(); });
        else
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Download fehlgeschlagen")
                                              .withMessage (error + txt ("\n\nAlternativ das Modell im Browser laden und über „Modelldatei wählen …“ einbinden:\n"
                                                                         "huggingface.co/ggerganov/whisper.cpp"))
                                              .withButton ("OK"),
                                          nullptr);
    };

    languageBox.addItem ("Deutsch", 1);
    languageBox.addItem ("Englisch", 2);
    languageBox.addItem ("Automatisch erkennen (gemischt)", 3);
    languageBox.onChange = [this]
    {
        const int sel = languageBox.getSelectedId();
        store ([sel] (AppSettings& s) { s.language = sel == 1 ? "de" : sel == 2 ? "en" : "auto"; });
    };
    content.addAndMakeVisible (languageBox);

    gpuToggle.onClick = [this] { store ([v = gpuToggle.getToggleState()] (AppSettings& s) { s.useGpu = v; }); };
    modeLabel.setText ("Erkennung", juce::dontSendNotification);
    modeLabel.setFont (Theme::font (13.0f, true));
    modeLabel.setColour (juce::Label::textColourId, Theme::textDim);
    content.addAndMakeVisible (modeLabel);

    modeBox.addItem (txt ("Genau – volle Whisper-Qualität (empfohlen)"), 3);
    modeBox.addItem (txt ("Ausgewogen – etwas schneller"), 2);
    modeBox.addItem (txt ("Schnell – verkürztes Hörfenster, ungenauer"), 1);
    modeBox.onChange = [this] { store ([v = modeBox.getSelectedId() - 1] (AppSettings& s) { s.recognitionMode = v; }); };
    content.addAndMakeVisible (modeBox);

    vocabLabel.setText (txt ("Eigene Begriffe (Bandname, Songtitel, Künstler, Fachwörter – kommagetrennt)"), juce::dontSendNotification);
    vocabLabel.setFont (Theme::font (13.0f, true));
    vocabLabel.setColour (juce::Label::textColourId, Theme::textDim);
    content.addAndMakeVisible (vocabLabel);

    vocabEditor.setFont (Theme::font (15.0f));
    vocabEditor.setIndents (8, 7);
    vocabEditor.setTextToShowWhenEmpty (txt ("z. B. Die Ärzte, Westerland, Shure Axient, Talkback-Mic"), Theme::textFaint);
    vocabEditor.onTextChange = [this] { if (! loading) vocabDirtyAt = juce::Time::currentTimeMillis(); };
    vocabEditor.onReturnKey  = [this] { vocabDirtyAt = 1; };
    vocabEditor.onFocusLost  = [this] { if (vocabDirtyAt != 0) vocabDirtyAt = 1; };
    content.addAndMakeVisible (vocabEditor);
    content.addAndMakeVisible (gpuToggle);
    liveToggle.onClick = [this] { store ([v = liveToggle.getToggleState()] (AppSettings& s) { s.liveText = v; }); };
    content.addAndMakeVisible (liveToggle);

    // --- Mein Name ---------------------------------------------------------
    keywordsHint.setText (txt ("Dein Name und Varianten, kommagetrennt – z. B. „Tjark, Tjarki, Monitor-Tjark“. "
                               "Wird einer davon erwähnt, erscheint oben ein Alarm und der Beitrag wird markiert. "
                               "Die Namen helfen außerdem der Spracherkennung, sie richtig zu schreiben."),
                          juce::dontSendNotification);
    styleHint (keywordsHint);
    content.addAndMakeVisible (keywordsHint);

    keywordsEditor.setFont (Theme::font (18.0f, true));
    keywordsEditor.setIndents (10, 10);
    keywordsEditor.setTextToShowWhenEmpty ("Dein Name, Spitzname, Funktion ...", Theme::textFaint);
    keywordsEditor.onTextChange = [this] { if (! loading) keywordsDirtyAt = juce::Time::currentTimeMillis(); };
    keywordsEditor.onReturnKey  = [this] { keywordsDirtyAt = 1; };
    keywordsEditor.onFocusLost  = [this] { if (keywordsDirtyAt != 0) keywordsDirtyAt = 1; };
    content.addAndMakeVisible (keywordsEditor);

    fuzzyToggle.onClick = [this] { store ([v = fuzzyToggle.getToggleState()] (AppSettings& s) { s.fuzzyKeywords = v; }); };
    flashToggle.onClick = [this] { store ([v = flashToggle.getToggleState()] (AppSettings& s) { s.flashAlert = v; }); };
    content.addAndMakeVisible (fuzzyToggle);
    content.addAndMakeVisible (flashToggle);

    testAlertButton.setColour (juce::TextButton::buttonColourId, Theme::alert.withAlpha (0.35f));
    testAlertButton.onClick = [this]
    {
        if (keywordsDirtyAt != 0)
            timerCallback();   // ungespeicherte Eingabe zuerst übernehmen

        app.injectTestMention();
    };
    content.addAndMakeVisible (testAlertButton);

    loadFromState();
    startTimerHz (5);
}

SettingsPanel::~SettingsPanel()
{
    if (keywordsDirtyAt != 0)
        store ([t = keywordsEditor.getText()] (AppSettings& s) { s.keywords = t; });
    if (vocabDirtyAt != 0)
        store ([t = vocabEditor.getText()] (AppSettings& s) { s.vocabulary = t; });
}

void SettingsPanel::loadFromState()
{
    const juce::ScopedValueSetter<bool> svs (loading, true);
    const auto& s = app.state.getSettings();

    modelLabel.setText (s.modelPath.isEmpty() ? txt ("Kein Modell gewählt") : juce::File (s.modelPath).getFileName(),
                        juce::dontSendNotification);
    languageBox.setSelectedId (s.language == "de" ? 1 : s.language == "en" ? 2 : 3, juce::dontSendNotification);
    gpuToggle.setToggleState (s.useGpu, juce::dontSendNotification);
    modeBox.setSelectedId (s.recognitionMode + 1, juce::dontSendNotification);
    if (! vocabEditor.hasKeyboardFocus (true))
        vocabEditor.setText (s.vocabulary, false);
    liveToggle.setToggleState (s.liveText, juce::dontSendNotification);

    if (! keywordsEditor.hasKeyboardFocus (true))
        keywordsEditor.setText (s.keywords, false);

    fuzzyToggle.setToggleState (s.fuzzyKeywords, juce::dontSendNotification);
    flashToggle.setToggleState (s.flashAlert, juce::dontSendNotification);

    if ((int) s.wirelessDevices.size() != wirelessRows.size())
        rebuildWirelessRows();
}

void SettingsPanel::store (const std::function<void (AppSettings&)>& change)
{
    if (loading)
        return;

    auto s = app.state.getSettings();
    change (s);
    app.state.updateSettings (s);
    loadFromState();
}

void SettingsPanel::timerCallback()
{
    if (vocabDirtyAt != 0 && juce::Time::currentTimeMillis() - vocabDirtyAt > 700)
    {
        vocabDirtyAt = 0;
        store ([t = vocabEditor.getText()] (AppSettings& s) { s.vocabulary = t; });
    }

    // Namen nach kurzer Tipp-Pause übernehmen
    if (keywordsDirtyAt != 0 && juce::Time::currentTimeMillis() - keywordsDirtyAt > 700)
    {
        keywordsDirtyAt = 0;
        store ([t = keywordsEditor.getText()] (AppSettings& s) { s.keywords = t; });
    }

    downloader.syncProgress();

    for (auto* row : wirelessRows)
        row->setStatus (app.wireless.getStatus (row->config.id));

    auto status = app.transcriber.getModelStatusText();
    if (app.transcriber.getModelState() == Transcriber::ModelState::none)
        status = txt ("Unten ein Modell herunterladen oder eine vorhandene ggml-Datei wählen.");
    if (app.transcriber.getModelState() == Transcriber::ModelState::ready && app.transcriber.getLastRealtimeFactor() > 0.0)
        status << txt ("  ·  letzte Rechenzeit: ") << juce::String (app.transcriber.getLastRealtimeFactor() * 100.0, 0)
               << txt (" % der Sprechdauer");
    modelStatus.setText (status, juce::dontSendNotification);
}

void SettingsPanel::rebuildWirelessRows()
{
    wirelessRows.clear();

    for (auto& d : app.state.getSettings().wirelessDevices)
    {
        auto* row = wirelessRows.add (new WirelessRow (d));
        const auto id = d.id;

        row->onChange = [this, id] (const WirelessDeviceConfig& c)
        {
            store ([id, c] (AppSettings& s)
            {
                for (auto& dev : s.wirelessDevices)
                    if (dev.id == id)
                        dev = c;
            });
        };

        row->onRemove = [this, id]
        {
            // Nicht im Klick-Callback der Zeile löschen, die gerade abgearbeitet wird
            juce::Component::SafePointer<SettingsPanel> self (this);
            juce::MessageManager::callAsync ([safe = self, id]
            {
                if (safe != nullptr)
                    safe->store ([id] (AppSettings& s)
                    {
                        auto& v = s.wirelessDevices;
                        v.erase (std::remove_if (v.begin(), v.end(), [&] (const WirelessDeviceConfig& dev) { return dev.id == id; }), v.end());
                    });
            });
        };

        content.addAndMakeVisible (row);
    }

    content.resized();
}

void SettingsPanel::chooseModelFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Whisper-Modell (ggml-*.bin)", AppState::getModelsDir(), "*.bin");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SettingsPanel> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->store ([f = fc.getResult()] (AppSettings& s) { s.modelPath = f.getFullPathName(); });
                          });
}

void SettingsPanel::startDownload()
{
    if (downloader.isRunning())
    {
        downloader.cancel();
        progressBar.setVisible (false);
        downloadButton.setButtonText ("Herunterladen");
        return;
    }

    const auto index = (size_t) juce::jmax (0, downloadBox.getSelectedId() - 1);
    const auto fileName = juce::String (ModelDownloader::models()[index].file);
    const auto existing = AppState::getModelsDir().getChildFile (fileName);

    if (existing.existsAsFile())
    {
        store ([existing] (AppSettings& s) { s.modelPath = existing.getFullPathName(); });
        return;
    }

    if (downloader.start (fileName))
    {
        progressBar.setVisible (true);
        downloadButton.setButtonText ("Abbrechen");
    }
}

//==============================================================================
void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void SettingsPanel::resized()
{
    viewport.setBounds (getLocalBounds());
    content.setSize (viewport.getMaximumVisibleWidth(), 10);
    content.resized();
}

void SettingsPanel::Content::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    for (auto& s : owner.sections)
    {
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (s.area.toFloat(), 10.0f);
        g.setColour (Theme::text);
        g.setFont (Theme::font (18.0f, true));
        g.drawText (s.title, s.area.reduced (20, 0).withHeight (48), juce::Justification::centredLeft);
    }
}

void SettingsPanel::Content::resized()
{
    auto& o = owner;
    o.sections.clear();

    const int w = juce::jmin (getWidth() - 32, 860);
    const int x = 16;
    int y = 16;

    auto beginSection = [&] (const juce::String& title) { o.sections.push_back ({ title, { x, y, w, 0 } }); y += 52; };
    auto endSection   = [&] { auto& s = o.sections.back(); s.area.setHeight (y - s.area.getY() + 16); y += 32; };
    auto row = [&] (int h) { auto r = juce::Rectangle<int> (x + 20, y, w - 40, h); y += h + 10; return r; };

    beginSection ("Audio-Interface");
    o.deviceSelector.setBounds (row (330).withTrimmedLeft (-10));
    y = o.deviceSelector.getBottom() + 10;
    o.audioHint.setBounds (row (64));
    endSection();

    beginSection (txt ("Funkempfänger"));
    o.wirelessHint.setBounds (row (60));
    for (auto* r : o.wirelessRows)
        r->setBounds (row (66));
    o.addWirelessButton.setBounds (row (32).withWidth (230));
    endSection();

    beginSection ("Spracherkennung");
    {
        auto r = row (30);
        o.chooseModelButton.setBounds (r.removeFromRight (190));
        o.modelLabel.setBounds (r);
    }
    o.modelStatus.setBounds (row (20));
    {
        auto r = row (32);
        o.downloadButton.setBounds (r.removeFromRight (140));
        r.removeFromRight (10);
        o.downloadBox.setBounds (r);
    }
    o.progressBar.setBounds (row (16));
    o.languageBox.setBounds (row (32).withWidth (320));
    o.gpuToggle.setBounds (row (26));
    {
        auto r = row (32);
        o.modeLabel.setBounds (r.removeFromLeft (110));
        o.modeBox.setBounds (r.withWidth (juce::jmin (r.getWidth(), 420)));
    }
    o.vocabLabel.setBounds (row (20));
    o.vocabEditor.setBounds (row (34));
    o.liveToggle.setBounds (row (26));
    endSection();

    beginSection (txt ("Mein Name – Erwähnungs-Alarm"));
    o.keywordsHint.setBounds (row (60));
    o.keywordsEditor.setBounds (row (44));
    o.fuzzyToggle.setBounds (row (26));
    {
        auto r = row (32);
        o.testAlertButton.setBounds (r.removeFromRight (160));
        o.flashToggle.setBounds (r);
    }
    endSection();

    setSize (getWidth(), y);
    repaint();
}
