## ShowCom

Holt Namens-Erwähnungen, den Alarm, die Sprecher und die Funkdaten aus **ShowCom** (Talkback-Transkription) nach Companion – alles im lokalen Netz, ohne Internet.

### Einrichten

1. In ShowCom unter **Einstellungen → Webzugriff** den Browser-Zugriff einschalten.
2. Unter **Einstellungen → Bitfocus Companion** den Zugriff erlauben (optional mit Schlüssel). Dort stehen auch Host und Port.
3. Hier in Companion **Host**, **Port** (Standard 8080) und ggf. den **Schlüssel** eintragen.

### Trigger

- **Automatisch:** Für jede Person in ShowCom gibt es einen Trigger mit ihrem Namen (inklusive Spitznamen).
- **Eigene Trigger:** Im Feld „Eigene Trigger“ beliebige Namen oder Begriffe, durch Komma getrennt. Varianten mit `|`: `Licht|LD|Lichtpult, Pyro, Tjark`.
- Erkennung wie in ShowCom: ganze Wörter, Groß/Klein egal, Umlaute (ü = ue), „Tims Mikro“, auf Wunsch 1 Buchstabe Abweichung ab 5 Zeichen.
- **Live-Text:** Der Trigger löst schon während des Sprechens aus. Der fertige Satz zählt danach nicht noch einmal.
- Ein Trigger bleibt aktiv, bis er zurückgesetzt wird (Taste, Aktion „Trigger zurücksetzen“ oder „Alarm quittieren“). Alternativ eine **Haltezeit** in Sekunden einstellen.

**Als Companion-Trigger nutzen:** Unter *Triggers* einen neuen Trigger anlegen.
- Ereignis **„On variable change“** auf `$(showcom:trig_<name>_count)`. Dieser Wert zählt bei jeder Erwähnung hoch.
- Oder Bedingung **Feedback „Name/Begriff erwähnt“**.

Danach beliebige Aktionen auslösen, z. B. Licht, Video, OSC oder eine Nachricht.

### Feedbacks

| Feedback | Wann |
|---|---|
| Name/Begriff erwähnt | Trigger wurde gesagt (optional: nur von einer bestimmten Person) |
| Wort gehört | beliebige Wörter in den letzten N Sekunden, ohne festen Trigger |
| ShowCom-Alarm aktiv | der „Mein Name“-Alarm am ShowCom-Rechner ist offen |
| ShowCom hört zu / Verbindung | Zustand |
| Person spricht gerade | Talkback-Kanal aktiv |
| Funk: stummgeschaltet | AF- oder RF-Mute am Sender |
| Funk: Akku schwach | unter der Grenze (Konfiguration oder pro Feedback) |
| Funk: Sender aus / offline | Sender aus oder Empfänger nicht erreichbar |
| Funk: Signal schwach | unter der Grenze |
| In-Ear (AD PSM) aus | IEM-Sender aus oder offline |
| Personenfarbe | Taste in der Farbe der Person |

Bei Funk-Feedbacks steht „irgendeine Person“ für eine Sammelanzeige.

### Aktionen

- Alarm quittieren (optional auch alle Trigger zurücksetzen)
- Zuhören an / aus / umschalten
- Alarm-Popup am ShowCom-Rechner ein/aus
- Testalarm in ShowCom
- Trigger zurücksetzen (einzeln oder alle, optional mit Zählern)
- Trigger testweise auslösen (nur in Companion, zum Testen der eigenen Abläufe)

Die Steuerung lässt sich in ShowCom abschalten. Dann sind nur Anzeigen möglich.

### Variablen (Auswahl)

- `alarm_active`, `alarm_name`, `alarm_keyword`, `alarm_text`, `alarm_time`, `mention_count`
- `last_speaker`, `last_text`, `last_trigger`, `last_trigger_by`, `speaking`
- `low_battery`, `muted`, `tx_problems`, `weak_signal`, `iem_off` (Namenslisten)
- pro Trigger: `trig_<name>`, `trig_<name>_count`, `_by`, `_text`, `_time`
- pro Person: `p_<name>_battery`, `_runtime`, `_signal`, `_mute`, `_tx`, `_iem`, `_speaking`, `_last_text`

Namen werden in Variablen klein und ohne Umlaute geschrieben, z. B. `Jürgen Müller` → `p_juergen_mueller_battery`.

### Presets

Fertige Tasten für Alarm, Zuhören, Popup, Funk-Sammelanzeigen, je eine Taste pro Trigger und pro Person.
