#include "SettingsPanel.h"

using Theme::txt;

//==============================================================================
const std::vector<ModelDownloader::Model>& ModelDownloader::models()
{
    static const char* whisperUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";
    static const std::vector<Model> list = {
        { "Parakeet v3  (670 MB) – schnell, sparsam und genau, empfohlen", "parakeet-tdt-0.6b-v3-int8",
          "https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/resolve/main/",
          { { "tokens.txt", 93939 }, { "decoder.int8.onnx", 11845275 }, { "joiner.int8.onnx", 6355277 },
            { "encoder.int8.onnx", 652184281 } } },
        { "Whisper large-v3-turbo q5  (574 MB) – braucht deutlich mehr Rechenleistung", "", whisperUrl,
          { { "ggml-large-v3-turbo-q5_0.bin", 574041195 } } },
        { "Whisper small q5  (190 MB) – für ältere Rechner, ungenauer", "", whisperUrl,
          { { "ggml-small-q5_1.bin", 190085487 } } },
    };
    return list;
}

juce::File ModelDownloader::Model::target() const
{
    const auto dir = AppState::getModelsDir();
    return juce::String (folder).isNotEmpty() ? dir.getChildFile (folder) : dir.getChildFile (files.front().name);
}

bool ModelDownloader::Model::isInstalled() const
{
    if (juce::String (folder).isEmpty())
        return target().getSize() > files.front().bytes / 2;

    for (auto& f : files)
        if (target().getChildFile (f.name).getSize() < f.bytes / 2)
            return false;
    return true;
}

ModelDownloader::~ModelDownloader()
{
    alive->store (false);
    task.reset();
}

bool ModelDownloader::start (const Model& m)
{
    if (task != nullptr)
        return false;

    current = &m;
    fileIndex = 0;
    doneBytes = 0;
    totalBytes = 0;
    for (auto& f : m.files)
        totalBytes += f.bytes;

    atomicProgress.store (0.0);
    progressValue = 0.0;
    startNext();
    return task != nullptr;
}

void ModelDownloader::startNext()
{
    const auto& f = current->files[fileIndex];
    const bool inFolder = juce::String (current->folder).isNotEmpty();

    target = inFolder ? current->target().getChildFile (f.name) : current->target();
    target.getParentDirectory().createDirectory();
    partFile = target.withFileExtension (target.getFileExtension() + ".part");
    partFile.deleteFile();

    if (target.getSize() >= f.bytes / 2 && inFolder)
    {
        // schon vorhanden (z. B. abgebrochener Download): überspringen
        doneBytes += f.bytes;
        if (++fileIndex < current->files.size())
            startNext();
        return;
    }

    juce::URL url (juce::String (current->baseUrl) + f.name);
    task = url.downloadToFile (partFile, juce::URL::DownloadTaskOptions().withListener (this));
}

void ModelDownloader::cancel()
{
    task.reset();
    partFile.deleteFile();
    atomicProgress.store (0.0);
}

void ModelDownloader::progress (juce::URL::DownloadTask*, juce::int64 downloaded, juce::int64)
{
    if (totalBytes > 0)
        atomicProgress.store (juce::jlimit (0.0, 1.0, (double) (doneBytes + downloaded) / (double) totalBytes));
    else
        atomicProgress.store (-1.0);
}

void ModelDownloader::finished (juce::URL::DownloadTask* t, bool success)
{
    const bool httpOk = t->statusCode() == 0 || (t->statusCode() >= 200 && t->statusCode() < 300);
    const auto code = t->statusCode();

    juce::MessageManager::callAsync ([this, flag = alive, success, httpOk, code]
    {
        if (! flag->load() || current == nullptr)
            return;

        task.reset();

        const auto& f = current->files[fileIndex];
        // Viel zu kleine Dateien sind HTML-Fehlerseiten, keine Modelle
        const bool plausible = partFile.getSize() >= f.bytes / 2;

        if (success && httpOk && plausible && (target.deleteFile(), partFile.moveFileTo (target)))
        {
            doneBytes += f.bytes;
            if (++fileIndex < current->files.size())
            {
                startNext();
                if (task != nullptr || fileIndex >= current->files.size())
                    return;
            }

            atomicProgress.store (1.0);
            if (onFinished) onFinished (true, current->target(), {});
            return;
        }

        const auto error = success ? ("Server-Antwort " + juce::String (code)) : juce::String ("Verbindung fehlgeschlagen");
        partFile.deleteFile();
        atomicProgress.store (0.0);
        if (onFinished) onFinished (false, {}, error);
    });
}

//==============================================================================
WirelessRow::WirelessRow (const WirelessDeviceConfig& c) : config (c)
{
    typeBox.addItem ("Shure (AD, ADX, SLX-D, SLX-D+, QLX-D)", 1);
    typeBox.addItem (Theme::txt ("Shure Axient Digital PSM (In-Ear)"), 3);
    typeBox.addItem ("Sennheiser EW-DX", 2);
    typeBox.setTooltip (Theme::txt ("Shure-Empfänger: Axient Digital (AD4D/AD4Q, ADX), SLX-D/SLX-D+, QLX-D  ·  "
                                    "Shure AD PSM: In-Ear-Sender ADTD/ADTQ  ·  Sennheiser: EW-DX EM 2 / EM 4"));
    typeBox.setSelectedId (c.type == WirelessDeviceConfig::Type::sennheiserEwdx ? 2
                           : c.type == WirelessDeviceConfig::Type::shurePsm ? 3 : 1, juce::dontSendNotification);
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

    enabledToggle.setButtonText ("aktiv");
    enabledToggle.setTooltip (Theme::txt ("Aus = dieses Gerät nicht verbinden und nicht überwachen (z. B. abgebaut oder Ersatz)"));
    enabledToggle.setToggleState (c.enabled, juce::dontSendNotification);
    enabledToggle.onClick = [this] { commit(); };
    addAndMakeVisible (enabledToggle);

    // Einzelne Kanäle (Empfänger bzw. In-Ear-Sender) an/aus
    for (int ch = 1; ch <= WirelessManager::defaultChannelCount (c); ++ch)
    {
        auto* b = channelButtons.add (new juce::TextButton (juce::String (ch)));
        b->setClickingTogglesState (true);
        b->setToggleState (c.disabledChannels.count (ch) == 0, juce::dontSendNotification);
        b->setTooltip (Theme::txt ("Kanal ") + juce::String (ch) + Theme::txt (" überwachen (aus = keine Anzeige, keine Warnungen)"));
        b->setColour (juce::TextButton::buttonOnColourId, Theme::accent.withAlpha (0.35f));
        b->setColour (juce::TextButton::textColourOnId, Theme::text);
        b->setColour (juce::TextButton::textColourOffId, Theme::textFaint);
        b->onClick = [this] { commit(); };
        addAndMakeVisible (b);
    }

    logButton.setButtonText (Theme::txt ("Protokoll …"));
    logButton.setTooltip (Theme::txt ("Speichert die zuletzt empfangenen Daten des Geräts als Textdatei – hilfreich, "
                                      "wenn ein Gerät nicht richtig angezeigt wird (z. B. AD PSM)"));
    logButton.onClick = [this] { saveLog(); };
    addAndMakeVisible (logButton);

    statusLabel.setFont (Theme::font (12.0f));
    statusLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (statusLabel);
}

void WirelessRow::commit()
{
    auto c = config;
    c.type = typeBox.getSelectedId() == 2 ? WirelessDeviceConfig::Type::sennheiserEwdx
           : typeBox.getSelectedId() == 3 ? WirelessDeviceConfig::Type::shurePsm
                                          : WirelessDeviceConfig::Type::shure;
    c.host = hostEditor.getText().trim();
    c.label = labelEditor.getText().trim();
    c.enabled = enabledToggle.getToggleState();
    c.disabledChannels.clear();
    for (int i = 0; i < channelButtons.size(); ++i)
        if (! channelButtons[i]->getToggleState())
            c.disabledChannels.insert (i + 1);

    if (c.type != config.type || c.host != config.host || c.label != config.label
        || c.enabled != config.enabled || c.disabledChannels != config.disabledChannels)
    {
        config = c;
        if (onChange)
            onChange (c);
    }
}

void WirelessRow::saveLog()
{
    const auto text = getLog != nullptr ? getLog() : juce::String();
    const auto name = "ShowCom-Funk-" + (config.host.isNotEmpty() ? config.host.replaceCharacter (':', '_') : juce::String ("Geraet"))
                    + "-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".txt";

    chooser = std::make_unique<juce::FileChooser> (Theme::txt ("Protokoll speichern"),
                                                   juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name),
                                                   "*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [text] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult() != juce::File())
                                  fc.getResult().replaceWithText (text.isNotEmpty() ? text : juce::String ("(noch keine Daten empfangen)"));
                          });
}

void WirelessRow::setStatus (const WirelessManager::DeviceStatus& st)
{
    online = st.online;
    auto text = st.text;
    if (st.online && st.model.isNotEmpty())
        text = st.model.trim() + Theme::txt ("  ·  ") + text;

    if (statusLabel.getText() != text)
        statusLabel.setText (text, juce::dontSendNotification);

    statusLabel.setColour (juce::Label::textColourId, ! config.enabled ? Theme::textFaint : st.online ? Theme::ok : Theme::alert);
    repaint();
}

void WirelessRow::paint (juce::Graphics& g)
{
    g.setColour (Theme::background.withAlpha (0.5f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    g.setColour (! config.enabled ? Theme::textFaint : online ? Theme::ok : Theme::alert);
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

    b.removeFromTop (6);
    auto second = b.removeFromTop (28);
    enabledToggle.setBounds (second.removeFromLeft (74));
    second.removeFromLeft (6);
    for (auto* btn : channelButtons)
    {
        btn->setBounds (second.removeFromLeft (32).reduced (0, 1));
        second.removeFromLeft (4);
    }
    logButton.setBounds (second.removeFromRight (100));
    second.removeFromLeft (8);
    statusLabel.setBounds (second.withTrimmedRight (8));
}

//==============================================================================
void WebDeviceRow::paint (juce::Graphics& g)
{
    g.setColour (Theme::background.withAlpha (0.5f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    const bool recent = juce::Time::currentTimeMillis() - device.lastSeenMs < 10000;
    g.setColour (recent ? Theme::ok : Theme::textFaint);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ 16.0f, (float) getHeight() / 2.0f }));

    auto b = getLocalBounds().reduced (30, 6).withTrimmedRight (110);
    auto top = b.removeFromTop (b.getHeight() / 2);
    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (12.0f, true));
    g.drawText ("ID " + device.id, top.withTrimmedLeft (juce::jmin (260, getWidth() - 148) + 10), juce::Justification::centredLeft, true);

    juce::String info;
    info << Theme::txt ("Alarm-Name: ") << (device.keywords.isNotEmpty() ? device.keywords : Theme::txt ("– nicht gesetzt –"))
         << Theme::txt ("   ·   ")
         << (recent ? Theme::txt ("verbunden") : Theme::txt ("zuletzt ") + juce::Time (device.lastSeenMs).formatted ("%d.%m. %H:%M"));
    if (! device.hidden.empty())
        info << Theme::txt ("   ·   ") << (int) device.hidden.size() << " ausgeblendet";

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.0f));
    g.drawText (info, b, juce::Justification::topLeft, true);
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

    // --- Webzugriff -----------------------------------------------------------
    webToggle.onClick = [this] { store ([v = webToggle.getToggleState()] (AppSettings& s) { s.webEnabled = v; }); };
    content.addAndMakeVisible (webToggle);

    webPortLabel.setFont (Theme::font (13.0f, true));
    webPortLabel.setColour (juce::Label::textColourId, Theme::textDim);
    content.addAndMakeVisible (webPortLabel);

    webPortEditor.setInputRestrictions (5, "0123456789");
    webPortEditor.setIndents (8, 7);
    webPortEditor.onReturnKey = [this] { webPortEditor.unfocusAllComponents(); };
    webPortEditor.onFocusLost = [this]
    {
        const int port = juce::jlimit (1024, 65535, webPortEditor.getText().getIntValue());
        store ([port] (AppSettings& s) { s.webPort = port; });
    };
    content.addAndMakeVisible (webPortEditor);

    webInfo.setFont (Theme::font (14.0f));
    webInfo.setJustificationType (juce::Justification::topLeft);
    content.addAndMakeVisible (webInfo);

    // --- Funkempfänger ------------------------------------------------------
    wirelessHint.setText (txt ("Empfänger im selben Netzwerk wie dieser Rechner. Shure (Axient Digital/ADX, SLX-D, SLX-D+, QLX-D, "
                               "Axient Digital PSM): Steuerung über TCP-Port 2202 (Standard, nichts einzustellen). "
                               "„aktiv“ und die Kanal-Knöpfe schalten die Überwachung einzelner Geräte und Kanäle ab. Sennheiser EW-DX: im Empfänger bzw. in Control Cockpit unter "
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
    // Parakeet: ähnlich genau wie Whisper large-v3-turbo, aber ein Bruchteil der Rechenzeit
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
                                              .withMessage (error + txt ("\n\nAlternativ die Modelldateien im Browser laden und über „Modell wählen …“ einbinden:\n"
                                                                         "Parakeet: huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8 (Ordner mit allen Dateien)\n"
                                                                         "Whisper: huggingface.co/ggerganov/whisper.cpp"))
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

    modeBox.addItem (txt ("Genau – volle Whisper-Qualität"), 3);
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

    {
        const juce::File m (s.modelPath);
        const auto kind = Transcriber::engineForModel (m);
        modelLabel.setText (s.modelPath.isEmpty() ? txt ("Kein Modell gewählt")
                            : kind == Transcriber::Engine::parakeet ? txt ("Parakeet v3  (") + m.getFileName() + ")"
                            : kind == Transcriber::Engine::whisper  ? "Whisper  (" + m.getFileName() + ")"
                                                                     : m.getFileName(),
                            juce::dontSendNotification);

        // Whisper-Einstellungen nur zeigen, wenn Whisper benutzt wird
        const bool whisper = kind == Transcriber::Engine::whisper;
        if (gpuToggle.isVisible() != whisper)
        {
            gpuToggle.setVisible (whisper);
            modeLabel.setVisible (whisper);
            modeBox.setVisible (whisper);
            content.resized();
        }
    }
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

    webToggle.setToggleState (s.webEnabled, juce::dontSendNotification);
    if (! webPortEditor.hasKeyboardFocus (true))
        webPortEditor.setText (juce::String (s.webPort), false);
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

    // Webzugriff: Adressen und Geräte
    {
        juce::String info;
        if (app.web.isRunning())
        {
            const auto urls = app.web.getUrls();
            info << Theme::txt ("Im Browser öffnen:  ") << (urls.isEmpty() ? "http://localhost:" + juce::String (app.web.getPort())
                                                                            : urls.joinIntoString ("   ·   "));
            info << Theme::txt ("\nJedes Gerät bekommt beim ersten Aufruf eine eigene ID, eigene Oberfläche und einen eigenen Alarm-Namen. "
                                "Im Browser passt jedes Gerät sein Dashboard an (wer erscheint, Reihenfolge) – Personen anlegen und löschen nur hier.");
        }
        else
        {
            info = app.state.getSettings().webEnabled ? app.web.getStatusText() : Theme::txt ("Browser-Zugriff ist ausgeschaltet.");
        }

        if (webInfo.getText() != info)
            webInfo.setText (info, juce::dontSendNotification);
        webInfo.setColour (juce::Label::textColourId, app.web.isRunning() ? Theme::text : Theme::alert);

        juce::String key;
        const auto devices = app.web.getDevices();
        for (auto& d : devices)
            key << d.id << d.label << d.keywords << (int) d.hidden.size() << ";";

        if (key != webRowsKey)
        {
            webRowsKey = key;
            rebuildWebRows();
        }
        else
        {
            for (size_t i = 0; i < devices.size() && i < (size_t) webRows.size(); ++i)
            {
                webRows[(int) i]->device = devices[i];
                webRows[(int) i]->repaint();
            }
        }
    }

    for (auto* row : wirelessRows)
        row->setStatus (app.wireless.getStatus (row->config.id));

    auto status = app.transcriber.getModelStatusText();
    if (app.transcriber.getModelState() == Transcriber::ModelState::none)
        status = txt ("Unten „Parakeet v3“ herunterladen (einmalig, danach offline) oder ein vorhandenes Modell wählen.");
    else if (app.transcriber.getEngine() == Transcriber::Engine::whisper)
        status << txt ("  ·  Tipp: Parakeet v3 braucht nur einen Bruchteil der Rechenleistung");
    if (app.transcriber.getRejectedCount() > 0)
        status << txt ("  ·  ") << app.transcriber.getRejectedCount() << txt (" Geräusche ohne Sprache übersprungen");
    if (app.transcriber.getModelState() == Transcriber::ModelState::ready && app.transcriber.getLastRealtimeFactor() > 0.0)
        status << txt ("  ·  letzte Rechenzeit: ") << juce::String (app.transcriber.getLastRealtimeFactor() * 100.0, 0)
               << txt (" % der Sprechdauer");
    modelStatus.setText (status, juce::dontSendNotification);
}

void SettingsPanel::rebuildWebRows()
{
    webRows.clear();

    for (auto& d : app.web.getDevices())
    {
        auto* row = webRows.add (new WebDeviceRow (d));
        const auto id = d.id;
        row->onRename = [this, id] (const juce::String& name) { app.web.renameDevice (id, name); };
        row->onRemove = [this, id]
        {
            juce::Component::SafePointer<SettingsPanel> self (this);
            juce::MessageManager::callAsync ([self, id]
            {
                if (self != nullptr)
                {
                    self->app.web.removeDevice (id);
                    self->timerCallback();
                }
            });
        };
        content.addAndMakeVisible (row);
    }

    content.resized();
}

void SettingsPanel::rebuildWirelessRows()
{
    wirelessRows.clear();

    for (auto& d : app.state.getSettings().wirelessDevices)
    {
        auto* row = wirelessRows.add (new WirelessRow (d));
        const auto id = d.id;
        row->getLog = [this, id] { return app.wireless.getRawLog (id); };

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
    // Parakeet: Ordner (oder eine Datei darin) · Whisper: ggml-*.bin
    chooser = std::make_unique<juce::FileChooser> (txt ("Parakeet-Ordner oder Whisper-Datei (ggml-*.bin) wählen"),
                                                   AppState::getModelsDir(), "*.bin;*.onnx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<SettingsPanel> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;

                              auto f = fc.getResult();
                              if (auto dir = ParakeetEngine::findModelDir (f); dir != juce::File())
                                  f = dir;
                              else if (! (f.existsAsFile() && f.hasFileExtension (".bin")))
                                  return;

                              safe->store ([f] (AppSettings& s) { s.modelPath = f.getFullPathName(); });
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

    const auto index = (size_t) juce::jlimit (0, (int) ModelDownloader::models().size() - 1, downloadBox.getSelectedId() - 1);
    const auto& model = ModelDownloader::models()[index];

    if (model.isInstalled())
    {
        store ([t = model.target()] (AppSettings& s) { s.modelPath = t.getFullPathName(); });
        return;
    }

    if (downloader.start (model))
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

    beginSection (txt ("Webzugriff"));
    {
        auto r = row (28);
        o.webPortEditor.setBounds (r.removeFromRight (90));
        o.webPortLabel.setBounds (r.removeFromRight (44));
        o.webToggle.setBounds (r);
    }
    o.webInfo.setBounds (row (58));
    for (auto* r : o.webRows)
        r->setBounds (row (50));
    endSection();

    beginSection (txt ("Funkempfänger"));
    o.wirelessHint.setBounds (row (76));
    for (auto* r : o.wirelessRows)
        r->setBounds (row (88));
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
    if (o.gpuToggle.isVisible())
    {
        o.gpuToggle.setBounds (row (26));
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
