# ShowCom

**Talkback-Log für den Livesound-Betrieb** – hört das Talkback mit und transkribiert es live, für Windows (ASIO) und macOS (CoreAudio, Apple-Chip und Intel).

![Board mit Personen-Kacheln, Akku- und HF-Anzeige](docs/board.png)

- **Board im RF-Board-Stil**: Pro Person eine hohe Kachel mit Foto, Lichtstrahlen in ihrer Farbe, Akku oben rechts, letzter Ansage, Verlauf von HF-Signal und Talkback-Pegel sowie Frequenz, Restlaufzeit und dBm. Warnungen wie *Akku kritisch*, *Akku niedrig*, *Schwaches Signal* oder *Sender aus* erscheinen direkt auf der Kachel.
- **Talkback mitloggen – pro Person wählbar**: Talkback-Personen bekommen eine Kachel mit Foto im oberen Drittel und darunter ihren letzten Ansagen; fällt dein Name, leuchtet dieses Feld, bis du den Alarm quittierst. Personen ohne Talkback (Redner, Bandmitglieder) werden nur funküberwacht und erscheinen als große Foto-Kachel mit Akku und HF-Verlauf.
- **Freigestellte Profilbilder**: Wie auf dem RF-Board steht die Person ohne Hintergrund vor den Lichtstrahlen ihrer Farbe – bei Nur-Funk-Kacheln groß, mit leichtem Schein. Der Hintergrund wird beim Import automatisch entfernt (auf dem Mac ab macOS 12 mit Apples Personen-Erkennung, sonst mit einem eigenen Verfahren, das bei ruhigem Hintergrund gut funktioniert). PNGs mit Transparenz werden direkt übernommen. Pro Person abschaltbar.
- **Dashboard anpassen – für jeden**: Am Rechner und auf jedem Browser-Gerät lässt sich einzeln festlegen, wer auf dem Board erscheint und in welcher Reihenfolge (Kacheln ziehen oder mit ◀ ▶ verschieben, Auge = ein/aus). Mit ↵ beginnt ab einer Kachel eine **zweite Zeile**.
- **Mute auf einen Blick**: Ist ein Sender stumm geschaltet, bekommt die Kachel einen dicken roten Rahmen und den Hinweis **MUTE AF** (Audio stumm), **MUTE RF** (Träger aus) oder **MUTE IEM RF** (In-Ear-Sender ohne HF).
- **Alarm-Popup**: Ein kleines Fenster, immer im Vordergrund, zeigt nur den Alarm (App: Knopf *Alarm-Popup* oben rechts; Browser: *🔔 Alarm-Popup* – am Handy wird die Seite selbst zur Alarm-Ansicht). Antippen = gesehen, auch im jeweils anderen Fenster.
- **Spitznamen**: Pro Person ein oder mehrere Spitznamen (Personen → *Spitzname*). Sie stehen auf der Kachel, werden bei der Erkennung bevorzugt – und steht der Name einer Person unter *Mein Name*, lösen auch ihre Spitznamen den Alarm aus.
- **Favoriten**: Oben in der Kopfleiste und als eigene Tabs erscheinen nur Personen mit Stern (Personen → *Favorit*). Ein Klick auf eine andere Kachel öffnet ihren Tab trotzdem.
- **Browser-Zugriff**: Eingebauter Webserver (Port 8080). Jedes Handy/Tablet/Laptop bekommt automatisch eine eigene Geräte-ID mit eigener Oberfläche, eigenem Dashboard-Layout und eigenem Alarm-Namen (inkl. Ton und Vibration). Personen anlegen, löschen und Geräte entfernen geht nur am Host.
- **Funk im Netzwerk**: Shure Axient Digital (AD4D/AD4Q, ADX), SLX-D, SLX-D+, QLX-D und Axient Digital PSM (In-Ear, ADTD/ADTQ) über TCP 2202 sowie Sennheiser EW-DX EM 2/EM 4 (SSCv1, UDP 45). Jeder Person werden Mikrofon-Funkstrecke und In-Ear-Sender zugewiesen; der Audiopegel kann direkt vom Empfänger kommen. Geräte und einzelne Kanäle lassen sich abschalten.
- **Bitfocus Companion**: Eigenes Companion-Modul. Bestimmte Namen oder Begriffe werden zu Triggern für Companion (Licht, Video, OSC …). Dazu kommen Alarm quittieren, Zuhören an/aus und Feedbacks für Sprecher, Mute, Akku, Sender aus und In-Ear sowie Variablen und fertige Tasten.
- **Eine Person pro Eingang**: Jedes Talkback-Mikro liegt auf einem eigenen Kanal des Audio-Interfaces. Dadurch ist immer eindeutig, wer gesprochen hat – ohne fehleranfällige Stimmerkennung.
- **Benutzer mit Avataren**: Name, Funktion (FOH, Monitor, Licht …), Farbe, Profilbild (Klick oder Drag & Drop), Eingang und Sprach-Schwelle mit Live-Pegel.
- **Tabs**: *Konversation* (alle chronologisch), *Erwähnungen*, ein Tab pro Person, *Personen*, *Einstellungen*.
- **Namens-Alarm**: Dein Name (plus Varianten) wird in jedem Transkript gesucht – ohne Groß/klein, mit Umlaut-Faltung und optional unscharf („Tjarck“ → „Tjark“). Bei einem Treffer blinkt oben ein großes Banner, der Beitrag wird markiert, ein Klick springt hin.
- **Lokal & offline, sparsam**: Spracherkennung mit [NVIDIA Parakeet TDT 0.6B v3](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3) über [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) – reine CPU, standardmäßig nur die Hälfte der Kerne (höchstens 4), kein Internet am Venue nötig, keine Daten in der Cloud. Das Modell wird einmalig heruntergeladen (670 MB). [Whisper](https://github.com/ggml-org/whisper.cpp) bleibt als Alternative wählbar.
- **Sprachprüfung**: Bevor erkannt wird, prüft [Silero VAD](https://github.com/snakers4/silero-vad), ob überhaupt gesprochen wurde. Musik-Übersprechen, Rauschen und Klatschen werden verworfen – das spart Rechenzeit und verhindert erfundenen Text.
- **Live-Text**: Schon während jemand spricht, erscheint der Text (alle ~0,8 s aktualisiert); der Namens-Alarm kommt mitten im Satz. Nach dem letzten Wort wird der Text innerhalb einer halben Sekunde abgeschlossen.
- **Hinweisleiste**: Fehlt etwas (Modell, Eingang, Mikrofon-Zugriff), steht oben, was zu tun ist – mit Sprung zur richtigen Einstellung.
- **Hört immer zu, speichert nichts**: ShowCom hört ab dem Programmstart mit (oben „HÖRT ZU“, per Klick pausierbar). Es werden keine Audiodateien und keine Protokolle geschrieben; das Audio wird nur im Arbeitsspeicher verarbeitet.
- **Sicher am Pult**: Die Software öffnet das Interface nur mit Eingängen und gibt nie Audio aus.

## Aufbau

```
Audio-Interface (ASIO / CoreAudio)
   │  Audio-Callback (Echtzeit): kopiert nur in lock-freie FIFOs, misst Pegel
   ▼
AudioEngine – Verarbeitungs-Thread
   │  pro Person: Sprach-Gate (Pre-Roll 300 ms, Nachlauf 700 ms, max. 25 s)
   ▼
Transcriber – eigener Thread
   │  48 kHz → 16 kHz, Pegel anheben, Silero VAD: nur echte Sprache weiter
   │  Parakeet TDT v3 (sherpa-onnx, CPU): rechnet nur so lange, wie gesprochen wurde,
   │  Namen der Crew, Alarm-Namen und eigene Begriffe als Hotwords
   │  (alternativ Whisper: Namen als Prompt, Halluzinationsfilter)
   ▼
TranscriptModel → Namenssuche (KeywordMatcher) → GUI (JUCE)
```

| Datei | Aufgabe |
|---|---|
| `src/AudioEngine.*` | Gerät, Echtzeit-Callback, FIFOs, Pegel, Segmentierung |
| `src/core/SpeechSegmenter.*` | Sprach-Gate (reines C++, getestet) |
| `src/core/KeywordMatcher.*` | Namenssuche (reines C++, getestet) |
| `src/Transcriber.*` | Warteschlange, Resampling, Sprachprüfung, Whisper-Anbindung, Modell-Laden |
| `src/ParakeetEngine.*` | Parakeet und Silero VAD über sherpa-onnx |
| `src/TranscriptModel.*` | Einträge, Erwähnungen, Protokolldateien |
| `src/AppState.*` | Benutzer, Avatare, Dashboard-Layout, Einstellungen (JSON) |
| `src/core/Cutout.*` | Profilbilder freistellen (reines C++, getestet); `src/CutoutMac.mm` nutzt auf dem Mac Apple Vision |
| `src/AppController.*` | Verdrahtung, Zuhören an/aus |
| `src/ui/*` | Oberfläche |

| Konversation | Personen |
|---|---|
| ![Konversation](docs/konversation.png) | ![Personen](docs/personen.png) |

## Bauen

Voraussetzungen: CMake ≥ 3.22, Git. JUCE 8.0.9, whisper.cpp 1.9.5 und die vorgebauten sherpa-onnx-1.13.8-Bibliotheken (inkl. ONNX Runtime, statisch gelinkt) lädt CMake beim ersten Konfigurieren selbst herunter.

### Windows (mit ASIO)

1. **Visual Studio 2022 oder neuer** (Workload „Desktopentwicklung mit C++“) und **CMake** installieren.
2. **ASIO SDK** von Steinberg laden: <https://www.steinberg.net/developers/> (seit Oktober 2025 auch unter GPLv3 verfügbar) und entpacken, z. B. nach `C:\SDKs\asiosdk`. Der Ordner muss `common\iasiodrv.h` enthalten.
3. In der „Developer PowerShell for VS“:

```powershell
cmake -S . -B build -A x64 -DTBS_ASIO_SDK_DIR="C:\SDKs\asiosdk"
cmake --build build --config Release --parallel
```

Das Programm liegt danach unter `build\ShowCom_artefacts\Release\ShowCom.exe`.

**GPU unter Windows (optional, deutlich schneller):**
- NVIDIA: CUDA Toolkit installieren und zusätzlich `-DGGML_CUDA=ON` angeben.
- AMD/Intel/NVIDIA: Vulkan SDK installieren und `-DGGML_VULKAN=ON` angeben.

Ohne GPU läuft alles auf der CPU. Auf einem aktuellen 8-Kerner reicht das für `large-v3-turbo q5` im Talkback-Betrieb (kurze Sätze) meist aus.

### macOS

Xcode bzw. die Command Line Tools und CMake (`brew install cmake`) installieren, dann:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
open "build/ShowCom_artefacts/Release/ShowCom.app"
```

Auf Macs mit Apple-Chip ist Metal (GPU) automatisch aktiv. Für Intel-Macs: `-DCMAKE_OSX_ARCHITECTURES=x86_64 -DGGML_METAL=OFF` ergänzen. Beim ersten Start fragt macOS nach dem Mikrofon-Zugriff – erlauben, sonst kommen nur Nullen an.

### Automatisch über GitHub

Das Repository enthält `.github/workflows/build.yml`. Nach einem Push auf GitHub baut GitHub Actions automatisch:
- **Windows** mit ASIO (das SDK wird mit Prüfsumme geladen),
- **macOS** je einmal für Apple-Chip (mit Metal-GPU) und für Intel-Macs (CPU mit AVX2), ad-hoc signiert.

Die fertigen Programme liegen unter *Actions → letzter Lauf → Artifacts*: `ShowCom-Windows.exe`, `ShowCom-macOS-AppleSilicon.dmg` und `ShowCom-macOS-Intel.dmg`. Nach jedem erfolgreichen Build auf `main` stehen sie außerdem unter *Releases* zum Download (Version aus `CMakeLists.txt`). Da die Mac-App nicht notarisiert ist: beim ersten Öffnen Rechtsklick → *Öffnen*, oder `xattr -dr com.apple.quarantine "ShowCom.app"`.

### Tests

```bash
ctest --test-dir build -C Release --output-on-failure
```

- `core`: Namenssuche (Umlaute, Mehrwort-Namen, Genitiv, unscharf) und Sprach-Gate.
- `core` prüft außerdem den Shure-Parser mit Antworten von QLX-D/ULX-D, SLX-D/SLX-D+, Axient Digital (inkl. AF-/RF-Mute) und AD PSM.
- `companion-module`: `npm test` prüft Namenserkennung, Trigger-Logik und das Modul gegen einen nachgebauten ShowCom-Server.
- `engine`: Die komplette Audio-Engine mit einem simulierten 8-Kanal-Interface – automatische Kanal-Aktivierung, Schnitt, Zeitstempel, Signalinhalt, Pause – sowie die Netzwerk-Clients gegen einen simulierten Shure-ULX-D- und Sennheiser-EW-DX-Empfänger.

## Erste Schritte

1. **Einstellungen → Spracherkennung**: **Parakeet v3** *Herunterladen* (einmalig, ca. 670 MB – am besten zu Hause, danach läuft alles offline). Unter *Eigene Begriffe* Bandname, Songtitel und Fachwörter eintragen – sie werden dann bevorzugt erkannt. Für einen Rechner ohne Internet: die vier Dateien von [Hugging Face](https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/tree/main) (`encoder.int8.onnx`, `decoder.int8.onnx`, `joiner.int8.onnx`, `tokens.txt`) in einen Ordner legen, per USB-Stick übertragen und über *Modell wählen …* einbinden.
2. **Einstellungen → Audio-Interface**: unter Windows Typ **ASIO** und dein Interface wählen, unter macOS das CoreAudio-Gerät.
3. **Personen**: Für jede Person *+ Person*, Name, Funktion, Profilbild (*Hintergrund ausblenden* ist voreingestellt; am besten ein Foto vor ruhigem Hintergrund oder ein freigestelltes PNG). **Talkback dieser Person mitloggen** an → **Eingang** wählen; aus → reine Funküberwachung (Redner, Band). Die **Sprach-Schwelle** (gelber Strich) knapp über das Grundrauschen bzw. Bühnen-Übersprechen stellen – der Pegelbalken zeigt live, ob Sprache erkannt wird.
4. **Einstellungen → Mein Name**: deinen Namen und Varianten eintragen, z. B. `Tjark, Tjarki, Monitor-Tjark`. Mit *Alarm testen* prüfen.
5. **Board → Dashboard anpassen**: Reihenfolge und Auswahl der Kacheln festlegen.
6. Fertig – ShowCom hört ab dem Start zu (oben **HÖRT ZU**). In der Umbaupause lässt es sich per Klick pausieren.

Zum Ausprobieren ohne Interface und ohne Funk: Programm mit `--demo` starten. Das füllt die Anzeige mit Beispiel-Talkback und simuliert sechs Funkstrecken (wenn Personen angelegt sind).

## Browser-Zugriff

Unter **Einstellungen → Webzugriff** stehen die Adressen, z. B. `http://192.168.1.20:8080`. Diese Adresse auf dem Handy, Tablet oder einem zweiten Rechner im selben Netz öffnen:

1. Beim ersten Aufruf bekommt das Gerät eine eigene ID (z. B. `/d/7F5W7T`) und behält sie (im Browser gespeichert).
2. Oben **Mein Name** eintragen – fällt dieser Name, alarmiert genau dieses Gerät (Banner, leuchtendes Feld, auf dem Handy zusätzlich Vibration; kein Ton).
3. Über **Dashboard anpassen** festlegen, wen dieses Gerät sieht und in welcher Reihenfolge: Kacheln ziehen (am Handy am runden Griff), mit ◀ ▶ verschieben, mit dem Auge ein- und ausblenden, mit ↵ eine zweite Zeile beginnen. Das Layout wird am Host pro Gerät gespeichert.

Jedes Gerät meldet sich mit seinem Namen an: dem Netzwerknamen, wenn der Router ihn kennt (z. B. „Tjarks-iPad“), sonst Gerätetyp und Browser („iPhone · Safari“). Am Host sind alle Geräte unter *Webzugriff* aufgelistet; dort lässt sich der Name ändern (er bleibt für dieses Gerät gespeichert) und ein Gerät entfernen. macOS fragt beim ersten Start, ob ShowCom eingehende Verbindungen annehmen darf – erlauben.

Hinweis: Der Webzugriff hat kein Passwort. Nur in vertrauenswürdigen Netzen (Produktionsnetz) betreiben.

| Browser (Desktop) | Browser (Handy) |
|---|---|
| ![Web-Oberfläche](docs/web.png) | ![Web-Oberfläche auf dem Handy](docs/web_mobil.png) |

![Dashboard anpassen im Browser](docs/web_anpassen.png)

## Bitfocus Companion

Mit dem Modul **ShowCom** für [Bitfocus Companion](https://bitfocus.io/companion) (ab Version 4.0) werden Namen im Talkback zu Triggern für Companion. Beispiel: Fällt „Pyro“ oder „Tjark“, leuchtet eine Taste auf dem Stream Deck oder Companion löst einen Ablauf aus.

**Einrichten**

1. In ShowCom unter **Einstellungen → Webzugriff** den Browser-Zugriff einschalten. Unter **Einstellungen → Bitfocus Companion** ist der Zugriff standardmäßig erlaubt. Dort kannst du auch einen Schlüssel setzen und die Steuerung abschalten (dann nur Anzeigen).
2. Die Datei `showcom-companion-<version>.tgz` aus dem GitHub-Release laden. In Companion unter **Modules → Import module package** importieren.
3. In Companion unter **Connections** die Verbindung **ShowCom** anlegen. Host und Port eintragen (stehen in den ShowCom-Einstellungen); läuft Companion auf demselben Rechner, `127.0.0.1`.

**Was das Modul kann**

- **Trigger**: Für jede Person automatisch einer (inkl. Spitznamen). Dazu eigene Begriffe, z. B. `Licht|LD, Pyro, Konfetti`. Die Erkennung ist dieselbe wie in ShowCom (Umlaute, Genitiv, unscharf). Mit Live-Text löst der Trigger schon mitten im Satz aus. Optional zählt es nicht, wenn jemand seinen eigenen Namen sagt.
- **Als Companion-Trigger**: *Triggers → On variable change* auf `$(showcom:trig_<name>_count)` oder Bedingung über das Feedback *Name/Begriff erwähnt*.
- **Feedbacks**:
  - Name erwähnt (optional nur von einer bestimmten Person)
  - Wort in den letzten N Sekunden gehört
  - ShowCom-Alarm aktiv, hört zu, Verbindung
  - Person spricht
  - Funk: Mute AF/RF, Akku schwach, Sender aus/offline, Signal schwach
  - In-Ear aus
  - Personenfarbe
- **Aktionen**:
  - Alarm quittieren
  - Zuhören an/aus/umschalten
  - Alarm-Popup am Rechner
  - Testalarm
  - Trigger zurücksetzen oder testweise auslösen
- **Variablen**:
  - Alarm (wer, was, wann)
  - letzter Satz und letzter Trigger
  - Listen „spricht“, „Akku schwach“, „Mute“, „Sender aus“
  - pro Trigger: Anzahl, gesagt von, Text, Zeit
  - pro Person: Akku, Restlaufzeit, Signal, Mute, Sender, In-Ear, zuletzt gesagt
- **Presets**: fertige Tasten für Alarm, Zuhören, Funk-Sammelanzeigen, jeden Trigger und jede Person.

Technisch fragt das Modul `GET /api/companion/state` ab (Standard alle 250 ms) und schickt Befehle per `POST /api/companion/command`. Ist ein Schlüssel gesetzt, kommt er als Header `X-ShowCom-Key` (oder `?key=`) mit. Alles bleibt im lokalen Netz. Das Modul liegt im Ordner `companion-module/` und wird von GitHub Actions mitgebaut.

## Funkempfänger anbinden

Unter **Einstellungen → Funkempfänger** jeden Empfänger mit Typ und IP-Adresse eintragen (optional mit Port, z. B. `192.168.1.50:2202`). Der Status-Punkt wird grün, sobald Daten kommen. Danach unter **Personen → Funkstrecke** bzw. **In-Ear-Sender** den passenden Kanal wählen – die Liste zeigt die Kanalnamen, die am Gerät eingestellt sind.

- **aktiv** abwählen: Das Gerät wird nicht mehr verbunden und nicht überwacht (z. B. Ersatz-Rack). Die Kanal-Knöpfe **1–4** schalten einzelne Empfänger- bzw. Senderkanäle ab – sie verschwinden aus der Auswahl unter *Personen*; eine schon zugewiesene Kachel zeigt „Überwachung aus“, ohne Warnungen.
- **macOS**: Beim ersten Verbinden fragt macOS, ob ShowCom auf Geräte im lokalen Netzwerk zugreifen darf – **erlauben**. Falls versehentlich abgelehnt: *Systemeinstellungen → Datenschutz & Sicherheit → Lokales Netzwerk → ShowCom* einschalten. Ohne diese Erlaubnis kommt keine Verbindung zu den Empfängern zustande.
- **Status lesen**: „Verbunden – warte auf Antwort …“ bzw. „Verbunden, aber keine Antwort“ heißt: Die IP ist erreichbar, aber das Gerät antwortet nicht auf Steuerbefehle (falsche Netzwerkbuchse, z. B. Dante statt Steuerung). „Keine Verbindung“ heißt: IP/Netzwerk falsch oder (macOS) Zugriff aufs lokale Netzwerk nicht erlaubt.
- **Pegel direkt von der Funkstrecke**: Unter *Personen* „Pegel direkt von der Funkstrecke anzeigen“ – der Verlauf auf der Kachel kommt dann vom Empfänger statt vom Audio-Interface. Bei Personen ohne Talkback passiert das automatisch.
- **Protokoll …** speichert die zuletzt empfangenen Meldungen und unbekannte Befehle eines Geräts als Textdatei – für die Fehlersuche.

| Hersteller | Geräte | Protokoll | Am Gerät einstellen |
|---|---|---|---|
| Shure | Axient Digital (AD4D, AD4Q, ADX-Sender), SLX-D, SLX-D+, QLX-D | TCP 2202, ASCII-Befehle | nichts (Standard). Rechner und Empfänger im selben Netz. |
| Shure | Axient Digital PSM (ADTD, ADTQ) – In-Ear | TCP 2202 (siehe Hinweis) | nichts |
| Sennheiser | EW-DX EM 2, EM 2 Dante, EM 4 Dante | Sound Control Protocol v1, JSON über UDP 45 | Drittanbieter-Zugriff **„Legacy (SSCv1)“** aktivieren (Control Cockpit → Gerät → *Access*) |

Angezeigt werden – je nachdem, was das Gerät liefert – Akku in % bzw. Balken, Restlaufzeit, Frequenz, HF-Pegel (Shure, dBm) bzw. Signalqualität (Sennheiser, %), aktive Antenne, Sendertyp, Audiopegel, Mute getrennt nach AF (Mute-Taste/Mute-Modus am Sender, Audio-Mute am Empfänger) und RF (Axient Digital: HF-Ausgang des Senders stumm) und ob der Sender an ist. Beim In-Ear-Sender: HF an/aus und Eingangspegel links/rechts. ShowCom fragt nur ab; es ändert keine Einstellungen am Gerät (einzige Ausnahme: bei Shure wird die Meter-Rate auf 250 ms gesetzt, damit der Pegelverlauf flüssig läuft).

Hinweis zu **Axient Digital PSM**: Shure hat für ADTD/ADTQ noch keine Befehlsliste für Fremdsteuerung veröffentlicht. ShowCom spricht die Sender wie Axient Digital an (gleiche Plattform, TCP 2202) und erkennt HF-Status und Eingangspegel nach diesem Muster. Wenn ein Wert nicht erscheint: im Gerät unter *Protokoll …* die Datei speichern – damit lässt sich die Anbindung genau auf euer Gerät abstimmen.

Hinweis: Das neuere, verschlüsselte SSCv2 der EW-DX (HTTPS mit Passwort) ist noch nicht eingebaut; dafür bitte vorerst SSCv1 aktivieren.

![Funkempfänger in den Einstellungen](docs/funkempfaenger.png)

## Praxis-Tipps

- **Signal abgreifen**: Talkback-Mikros vor dem Talkback-Bus abgreifen (Direct Out, Split, Dante/AVB-Stream) und einzeln auf das Interface legen. Ein Summen-Talkback funktioniert technisch auch, dann ist aber nur eine „Person“ erkennbar.
- **ASIO ist meist Single-Client**: Läuft auf demselben Interface schon eine DAW für den Show-Mitschnitt, kann ShowCom es oft nicht gleichzeitig öffnen. Lösungen: ein separates kleines Interface, ein Multi-Client-Treiber oder ein eigener Dante-/AVB-Kanal für diesen Rechner.
- **Zugewiesene Eingänge** werden automatisch am Interface aktiviert.
- **Parakeet oder Whisper?** Parakeet ist der Standard: Auf Deutsch ähnlich genau wie Whisper large-v3-turbo, aber mit etwa einem Fünftel der Rechenzeit, und es erfindet keine Sätze. Whisper nur wählen, wenn ein Rechner mit starker GPU da ist und Parakeet bei euren Stimmen/Akzenten schlechter ist.
- **Whisper-Eigenheiten**: In Stille oder Rauschen erfindet Whisper gern Floskeln („Untertitel im Auftrag des ZDF“). Die bekannten werden herausgefiltert, und die Sprachprüfung schickt nur echte Sprache zur Erkennung.
- **Namen richtig schreiben**: Alle Personennamen und deine Namen werden der Erkennung als Kontext mitgegeben, das verbessert die Schreibweise deutlich.

## Daten

| Was | Wo |
|---|---|
| Einstellungen, Personen | Windows: `%APPDATA%\ShowCom\settings.json` · macOS: `~/Library/Application Support/ShowCom/` |
| Profilbilder (512 × 512 px, dazu die freigestellte Fassung `…_cut.png`) | `…/ShowCom/avatars/` |
| Modelle | `…/ShowCom/models/` |

## Nächste Ausbaustufen

- Suche im Transkript, optional Transkript als Text exportieren
- Optionaler Ton oder Push-Benachrichtigung aufs Handy bei Erwähnung
- Stimmerkennung (Diarisierung) für Summen-Talkback

## Lizenzen

- **JUCE 8**: AGPLv3 oder kommerzielle JUCE-Lizenz. Für eigene/interne Nutzung problemlos; wer das Programm weitergibt, muss AGPLv3 einhalten oder eine JUCE-Lizenz haben.
- **sherpa-onnx**: Apache-2.0. **ONNX Runtime**: MIT. **Silero VAD**: MIT.
- **Parakeet TDT 0.6B v3** (NVIDIA): CC BY 4.0 – Modell wird nicht mitgeliefert, sondern beim Einrichten heruntergeladen.
- **whisper.cpp / ggml**: MIT. Whisper-Modelle: MIT (OpenAI).
- **Steinberg ASIO SDK**: GPLv3 oder proprietäre Steinberg-Lizenz. Das SDK liegt nicht im Repository, sondern wird beim Bauen eingebunden.
