#pragma once

#include <cstdint>
#include <vector>

/**
    Freistellen von Profilbildern ohne KI-Modell: schätzt den Hintergrund aus den Bildrändern,
    lässt ihn von außen her "zulaufen" (Region Growing, folgt auch Verläufen) und liefert
    eine weiche Alpha-Maske. Funktioniert gut bei Porträts vor Wand, Himmel, Bühnen-Backdrop
    oder Studio-Hintergrund; bei sehr unruhigem Hintergrund gibt es false zurück
    (dann bleibt das normale Foto).
*/
namespace Cutout
{
    struct Result
    {
        bool ok = false;
        bool alreadyTransparent = false;   // Bild hatte schon eine Freistellung
        float foregroundFraction = 0.0f;
        std::vector<uint8_t> alpha;        // w*h, 0 = Hintergrund, 255 = Person
    };

    /** rgba: w*h*4 Bytes, nicht vormultipliziert. */
    Result removeBackground (const uint8_t* rgba, int w, int h);
}
