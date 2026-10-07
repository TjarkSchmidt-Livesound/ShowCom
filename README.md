# ShowCom

**Talkback-Log für den Livesound-Betrieb** – hört das Talkback mit und transkribiert es live, für Windows (ASIO) und macOS (CoreAudio, Apple-Chip und Intel).

![Board mit Personen-Kacheln, Akku- und HF-Anzeige](docs/board.png)

- **Board im RF-Board-Stil**: Pro Person eine hohe Kachel mit Foto, Lichtstrahlen in ihrer Farbe, Akku oben rechts, letzter Ansage, Verlauf von HF-Signal und Talkback-Pegel sowie Frequenz, Restlaufzeit und dBm. Warnungen wie *Akku kritisch*, *Akku niedrig*, *Schwaches Signal* oder *Sender aus* erscheinen direkt auf der Kachel.
- **Talkback mitloggen – pro Person wählbar**: Talkback-Personen bekommen eine Kachel mit Foto im oberen Drittel und darunter ihren letzten Ansagen; fällt dein Name, leuchtet dieses Feld, bis du den Alarm quittierst. Personen ohne Talkback (Redner, Bandmitglieder) werden nur funküberwacht und erscheinen als große Foto-Kachel mit Akku und HF-Verlauf.
- **Freigestellte Profilbilder**: Wie auf dem RF-Board steht die Person ohne Hintergrund vor den Lichtstrahlen ihrer Farbe – bei Nur-Funk-Kacheln groß, mit leichtem Schein. Der Hintergrund wird beim Import automatisch entfernt (auf dem Mac ab macOS 12 mit Apples Personen-Erkennung, sonst mit einem eigenen Verfahren, das bei ruhigem Hintergrund gut funktioniert). PNGs mit Transparenz werden direkt übernommen. Pro Person abschaltbar.
- **Dashboard anpassen – für jeden**: Am Rechner und auf jedem Browser-Gerät lässt sich einzeln festlegen, wer auf dem Board erscheint und in welcher Reihenfolge (Kacheln ziehen oder mit ◀ ▶ verschieben, Auge = ein/aus).
- **Browser-Zugriff**: Eingebauter Webserver (Port 8080). Jedes Handy/Tablet/Laptop bekommt automatisch eine eigene Geräte-ID mit eigener Oberfläche, eigenem Dashboard-Layout und eigenem Alarm-Namen (inkl. Ton und Vibration). Personen anlegen, löschen und Geräte entfernen geht nur am Host.
- **Funkempfänger im Netzwerk**: Shure ULX-D, QLX-D, SLX-D und Axient Digital (TCP 2202) sowie Sennheiser EW-DX EM 2/EM 4 (SSCv1, UDP 45). Jeder Person wird ihr Funkkanal zugewiesen.
- **Eine Person pro Eingang**: Jedes Talkback-Mikro liegt auf einem eigenen Kanal des Audio-Interfaces. Dadurch ist immer eindeutig, wer gesprochen hat – ohne fehleranfällige Stimmerkennung.
- **Benutzer mit Avataren**: Name, Funktion (FOH, Monitor, Licht …), Farbe, Profilbild (Klick oder Drag & Drop), Eingang und Sprach-Schwelle mit Live-Pegel.
- **Tabs**: *Konversation* (alle chronologisch), *Erwähnungen*, ein Tab pro Person, *Personen*, *Einstellungen*.
- **Namens-Alarm**: Dein Name (plus Varianten) wird in jedem Transkript gesucht – ohne Groß/klein, mit Umlaut-Faltung und optional unscharf („Tjarck“ → „Tjark“). Bei einem Treffer blinkt oben ein großes Banner, der Beitrag wird markiert, ein Klick springt hin.
- **Lokal & offline**: Spracherkennung mit [whisper.cpp](https://github.com/ggml-org/whisper.cpp), auf dem Mac per GPU (Metal). Kein Internet am Venue nötig, keine Daten in der Cloud.
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
Transcriber – eigener Thread, whisper.cpp
   │  48 kHz → 16 kHz, Pegel anheben, Halluzinationen filtern, Namen als Prompt,
   │  Live-Zwischenstände schnell (verkürztes Fenster), Endergebnis genau (volles Fenster,
   │  Beam-Search), Talkback-Fachwörter und eigene Begriffe als Kontext
   ▼
TranscriptModel → Namenssuche (KeywordMatcher) → GUI (JUCE)
```

| Datei | Aufgabe |
|---|---|
| `src/AudioEngine.*` | Gerät, Echtzeit-Callback, FIFOs, Pegel, Segmentierung |
| `src/core/SpeechSegmenter.*` | Sprach-Gate (reines C++, getestet) |
| `src/core/KeywordMatcher.*` | Namenssuche (reines C++, getestet) |
| `src/Transcriber.*` | whisper.cpp-Anbindung, Resampling, Filter, Modell-Laden |
| `src/TranscriptModel.*` | Einträge, Erwähnungen, Protokolldateien |
| `src/AppState.*` | Benutzer, Avatare, Dashboard-Layout, Einstellungen (JSON) |
| `src/core/Cutout.*` | Profilbilder freistellen (reines C++, getestet); `src/CutoutMac.mm` nutzt auf dem Mac Apple Vision |
| `src/AppController.*` | Verdrahtung, Zuhören an/aus |
| `src/ui/*` | Oberfläche |

| Konversation | Personen |
|---|---|
| ![Konversation](docs/konversation.png) | ![Personen](docs/personen.png) |

## Bauen

Voraussetzungen: CMake ≥ 3.22, Git. JUCE 8.0.9 und whisper.cpp 1.9.5 lädt CMake beim ersten Konfigurieren selbst herunter.

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
- `core` prüft außerdem den Shure-Parser mit Antworten von ULX-D, SLX-D und Axient Digital.
- `engine`: Die komplette Audio-Engine mit einem simulierten 8-Kanal-Interface – automatische Kanal-Aktivierung, Schnitt, Zeitstempel, Signalinhalt, Pause – sowie die Netzwerk-Clients gegen einen simulierten Shure-ULX-D- und Sennheiser-EW-DX-Empfänger.

## Erste Schritte

1. **Einstellungen → Spracherkennung**: Modell `large-v3-turbo q5` (574 MB, genaueste Erkennung) *Herunterladen*, Sprache Deutsch, Erkennung **Genau**. Unter *Eigene Begriffe* Bandname, Songtitel und Fachwörter eintragen – Whisper schreibt sie dann richtig.
2. **Einstellungen → Audio-Interface**: unter Windows Typ **ASIO** und dein Interface wählen, unter macOS das CoreAudio-Gerät.
3. **Personen**: Für jede Person *+ Person*, Name, Funktion, Profilbild (*Hintergrund ausblenden* ist voreingestellt; am besten ein Foto vor ruhigem Hintergrund oder ein freigestelltes PNG). **Talkback dieser Person mitloggen** an → **Eingang** wählen; aus → reine Funküberwachung (Redner, Band). Die **Sprach-Schwelle** (gelber Strich) knapp über das Grundrauschen bzw. Bühnen-Übersprechen stellen – der Pegelbalken zeigt live, ob Sprache erkannt wird.
4. **Einstellungen → Mein Name**: deinen Namen und Varianten eintragen, z. B. `Tjark, Tjarki, Monitor-Tjark`. Mit *Alarm testen* prüfen.
5. **Board → Dashboard anpassen**: Reihenfolge und Auswahl der Kacheln festlegen.
6. Fertig – ShowCom hört ab dem Start zu (oben **HÖRT ZU**). In der Umbaupause lässt es sich per Klick pausieren.

Zum Ausprobieren ohne Interface und ohne Funk: Programm mit `--demo` starten. Das füllt die Anzeige mit Beispiel-Talkback und simuliert sechs Funkstrecken (wenn Personen angelegt sind).

## Browser-Zugriff

Unter **Einstellungen → Webzugriff** stehen die Adressen, z. B. `http://192.168.1.20:8080`. Diese Adresse auf dem Handy, Tablet oder einem zweiten Rechner im selben Netz öffnen:

1. Beim ersten Aufruf bekommt das Gerät eine eigene ID (z. B. `/d/7F5W7T`) und behält sie (im Browser gespeichert).
2. Oben **Mein Name** eintragen – fällt dieser Name, alarmiert genau dieses Gerät (Banner, leuchtendes Feld, mit **Ton an** auch akustisch und per Vibration).
3. Über **Dashboard anpassen** festlegen, wen dieses Gerät sieht und in welcher Reihenfolge: Kacheln ziehen (am Handy am runden Griff), mit ◀ ▶ verschieben, mit dem Auge ein- und ausblenden. Das Layout wird am Host pro Gerät gespeichert.

Am Host sind alle Geräte unter *Webzugriff* aufgelistet (Name, Alarm-Name, zuletzt gesehen) und lassen sich dort entfernen. macOS fragt beim ersten Start, ob ShowCom eingehende Verbindungen annehmen darf – erlauben.

Hinweis: Der Webzugriff hat kein Passwort. Nur in vertrauenswürdigen Netzen (Produktionsnetz) betreiben.

| Browser (Desktop) | Browser (Handy) |
|---|---|
| ![Web-Oberfläche](docs/web.png) | ![Web-Oberfläche auf dem Handy](docs/web_mobil.png) |

![Dashboard anpassen im Browser](docs/web_anpassen.png)

## Funkempfänger anbinden

Unter **Einstellungen → Funkempfänger** jeden Empfänger mit Typ und IP-Adresse eintragen (optional mit Port, z. B. `192.168.1.50:2202`). Der Status-Punkt wird grün, sobald Daten kommen. Danach unter **Personen → Funkstrecke** den passenden Kanal wählen – die Liste zeigt die Kanalnamen, die am Empfänger eingestellt sind.

| Hersteller | Geräte | Protokoll | Am Gerät einstellen |
|---|---|---|---|
| Shure | ULX-D, QLX-D, SLX-D, Axient Digital | TCP 2202, ASCII-Befehle | nichts (Standard). Rechner und Empfänger im selben Netz. |
| Sennheiser | EW-DX EM 2, EM 2 Dante, EM 4 Dante | Sound Control Protocol v1, JSON über UDP 45 | Drittanbieter-Zugriff **„Legacy (SSCv1)“** aktivieren (Control Cockpit → Gerät → *Access*) |

Angezeigt werden – je nachdem, was das Gerät liefert – Akku in % bzw. Balken, Restlaufzeit, Frequenz, HF-Pegel (Shure, dBm) bzw. Signalqualität (Sennheiser, %), aktive Antenne, Sendertyp, Stummschaltung und ob der Sender an ist. ShowCom fragt nur ab; es ändert keine Einstellungen am Empfänger (einzige Ausnahme: bei Shure wird die Meter-Rate auf 1 s gesetzt, damit der Pegelverlauf läuft).

Hinweis: Das neuere, verschlüsselte SSCv2 der EW-DX (HTTPS mit Passwort) ist noch nicht eingebaut; dafür bitte vorerst SSCv1 aktivieren.

![Funkempfänger in den Einstellungen](docs/funkempfaenger.png)

## Praxis-Tipps

- **Signal abgreifen**: Talkback-Mikros vor dem Talkback-Bus abgreifen (Direct Out, Split, Dante/AVB-Stream) und einzeln auf das Interface legen. Ein Summen-Talkback funktioniert technisch auch, dann ist aber nur eine „Person“ erkennbar.
- **ASIO ist meist Single-Client**: Läuft auf demselben Interface schon eine DAW für den Show-Mitschnitt, kann ShowCom es oft nicht gleichzeitig öffnen. Lösungen: ein separates kleines Interface, ein Multi-Client-Treiber oder ein eigener Dante-/AVB-Kanal für diesen Rechner.
- **Zugewiesene Eingänge** werden automatisch am Interface aktiviert.
- **Whisper-Eigenheiten**: In Stille oder Rauschen erfindet Whisper gern Floskeln („Untertitel im Auftrag des ZDF“). Die bekannten werden herausgefiltert, und das Sprach-Gate schickt nur echte Sprache zur Erkennung. Wenn trotzdem Unsinn erscheint: Schwelle der betreffenden Person anheben.
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
- **whisper.cpp / ggml**: MIT. Whisper-Modelle: MIT (OpenAI).
- **Steinberg ASIO SDK**: GPLv3 oder proprietäre Steinberg-Lizenz. Das SDK liegt nicht im Repository, sondern wird beim Bauen eingebunden.
