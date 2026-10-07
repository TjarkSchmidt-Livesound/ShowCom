#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

/**
    Spracherkennung mit NVIDIA Parakeet TDT 0.6B v3 über sherpa-onnx (ONNX Runtime, nur CPU).

    Gegenüber Whisper:
    - rechnet nur so lange, wie tatsächlich gesprochen wurde (kein festes 30-s-Fenster),
      ca. 5–10× weniger Rechenzeit bei vergleichbarer Genauigkeit auf Deutsch,
    - erfindet in Stille/Rauschen keine Sätze (Transducer statt Sprachmodell-Decoder),
    - Namen und Fachbegriffe werden als "Hotwords" bevorzugt erkannt.

    Dazu eine Sprachprüfung mit Silero VAD: Abschnitte ohne echte Sprache (Musik-Übersprechen,
    Rumpeln, Klatschen) werden gar nicht erst erkannt.

    Nur von einem Thread benutzen (dem Transcriber-Thread).
*/
class ParakeetEngine
{
public:
    ParakeetEngine();
    ~ParakeetEngine();

    /** Ordner mit encoder*.onnx, decoder*.onnx, joiner*.onnx, tokens.txt – oder eine Datei darin. */
    static juce::File findModelDir (const juce::File& fileOrDir);
    static bool isModelDir (const juce::File& dir);
    static bool isAvailable();   // mit sherpa-onnx gebaut?

    bool load (const juce::File& modelDir, int numThreads, juce::String& error);
    bool isLoaded() const;
    void unload();

    /** pcm: 16 kHz mono. hotwords: eine Zeile pro Begriff. */
    juce::String transcribe (const std::vector<float>& pcm, const juce::StringArray& hotwords);

    // --- Sprachprüfung (Silero VAD) ------------------------------------------------
    bool loadVad (const juce::File& sileroModel);
    bool hasVad() const;

    /** Sekunden erkannter Sprache im Abschnitt (16 kHz). -1 = keine Prüfung möglich. */
    double speechSeconds (const std::vector<float>& pcm);

    /** Erzeugt bpe.vocab aus tokens.txt (wird für Hotwords gebraucht). */
    static bool writeBpeVocab (const juce::File& tokens, const juce::File& target);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
