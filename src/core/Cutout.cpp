#include "Cutout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace Cutout
{
namespace
{
    struct Lab { float l, a, b; };

    float srgbToLinear (float c)
    {
        c /= 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f);
    }

    float labF (float t)
    {
        return t > 0.008856f ? std::cbrt (t) : 7.787f * t + 16.0f / 116.0f;
    }

    Lab toLab (float r, float g, float b)
    {
        const float R = srgbToLinear (r), G = srgbToLinear (g), B = srgbToLinear (b);
        const float x = (0.4124f * R + 0.3576f * G + 0.1805f * B) / 0.95047f;
        const float y = (0.2126f * R + 0.7152f * G + 0.0722f * B);
        const float z = (0.0193f * R + 0.1192f * G + 0.9505f * B) / 1.08883f;
        const float fx = labF (x), fy = labF (y), fz = labF (z);
        return { 116.0f * fy - 16.0f, 500.0f * (fx - fy), 200.0f * (fy - fz) };
    }

    float dist (const Lab& p, const Lab& q)
    {
        const float dl = p.l - q.l, da = p.a - q.a, db = p.b - q.b;
        return std::sqrt (dl * dl + da * da + db * db);
    }

    /** Separierbarer Box-Blur auf einem Kanal-Feld. */
    void boxBlur (std::vector<float>& v, int w, int h, int r)
    {
        std::vector<float> tmp (v.size());
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                float s = 0; int n = 0;
                for (int k = -r; k <= r; ++k)
                {
                    const int xx = std::clamp (x + k, 0, w - 1);
                    s += v[(size_t) (y * w + xx)]; ++n;
                }
                tmp[(size_t) (y * w + x)] = s / (float) n;
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                float s = 0; int n = 0;
                for (int k = -r; k <= r; ++k)
                {
                    const int yy = std::clamp (y + k, 0, h - 1);
                    s += tmp[(size_t) (yy * w + x)]; ++n;
                }
                v[(size_t) (y * w + x)] = s / (float) n;
            }
    }

    /** Behält nur große Vordergrund-Flächen (Person), kleine Inseln fliegen raus. */
    void keepLargeComponents (std::vector<uint8_t>& fg, int w, int h)
    {
        std::vector<int> label (fg.size(), -1);
        std::vector<int> sizes;
        std::deque<int> q;

        for (int i = 0; i < w * h; ++i)
        {
            if (! fg[(size_t) i] || label[(size_t) i] >= 0)
                continue;

            const int id = (int) sizes.size();
            int count = 0;
            label[(size_t) i] = id;
            q.push_back (i);

            while (! q.empty())
            {
                const int p = q.front(); q.pop_front(); ++count;
                const int x = p % w, y = p / w;
                const int nb[4] = { x > 0 ? p - 1 : -1, x < w - 1 ? p + 1 : -1, y > 0 ? p - w : -1, y < h - 1 ? p + w : -1 };
                for (int n : nb)
                    if (n >= 0 && fg[(size_t) n] && label[(size_t) n] < 0)
                    {
                        label[(size_t) n] = id;
                        q.push_back (n);
                    }
            }
            sizes.push_back (count);
        }

        if (sizes.empty())
            return;

        const int largest = *std::max_element (sizes.begin(), sizes.end());
        const int minSize = std::max ((int) (0.015f * (float) (w * h)), (int) (0.15f * (float) largest));

        for (int i = 0; i < w * h; ++i)
            if (fg[(size_t) i] && sizes[(size_t) label[(size_t) i]] < minSize)
                fg[(size_t) i] = 0;
    }

    /** Lücken im Vordergrund schließen, die nicht mit dem Bildrand verbunden sind (z. B. Augen, Brillengläser). */
    void fillHoles (std::vector<uint8_t>& fg, int w, int h)
    {
        std::vector<uint8_t> outside (fg.size(), 0);
        std::deque<int> q;
        auto push = [&] (int p) { if (! fg[(size_t) p] && ! outside[(size_t) p]) { outside[(size_t) p] = 1; q.push_back (p); } };

        for (int x = 0; x < w; ++x) { push (x); push ((h - 1) * w + x); }
        for (int y = 0; y < h; ++y) { push (y * w); push (y * w + w - 1); }

        while (! q.empty())
        {
            const int p = q.front(); q.pop_front();
            const int x = p % w, y = p / w;
            if (x > 0) push (p - 1);
            if (x < w - 1) push (p + 1);
            if (y > 0) push (p - w);
            if (y < h - 1) push (p + w);
        }

        for (size_t i = 0; i < fg.size(); ++i)
            if (! outside[i])
                fg[i] = 1;
    }
}

Result removeBackground (const uint8_t* rgba, int w, int h)
{
    Result res;
    if (rgba == nullptr || w < 16 || h < 16)
        return res;

    const size_t n = (size_t) w * (size_t) h;

    // --- Schon freigestellt (PNG mit Transparenz)? -------------------------------
    {
        size_t transparent = 0;
        for (size_t i = 0; i < n; ++i)
            if (rgba[i * 4 + 3] < 250)
                ++transparent;

        if (transparent > n / 50)
        {
            res.alpha.resize (n);
            size_t opaque = 0;
            for (size_t i = 0; i < n; ++i)
            {
                res.alpha[i] = rgba[i * 4 + 3];
                opaque += res.alpha[i] > 128 ? 1 : 0;
            }
            res.ok = true;
            res.alreadyTransparent = true;
            res.foregroundFraction = (float) opaque / (float) n;
            return res;
        }
    }

    // --- Leicht geglättetes Lab-Bild (Rauschen/JPEG-Artefakte weg) ----------------
    std::vector<float> R (n), G (n), B (n);
    for (size_t i = 0; i < n; ++i)
    {
        R[i] = rgba[i * 4 + 0];
        G[i] = rgba[i * 4 + 1];
        B[i] = rgba[i * 4 + 2];
    }
    const int blur = std::max (1, std::min (w, h) / 256);
    boxBlur (R, w, h, blur);
    boxBlur (G, w, h, blur);
    boxBlur (B, w, h, blur);

    std::vector<Lab> lab (n);
    for (size_t i = 0; i < n; ++i)
        lab[i] = toLab (R[i], G[i], B[i]);

    // Kantenstärke über ±3 px: weiche Verläufe (Vignette, Licht an der Wand) bleiben klein,
    // auch eine flaue, leicht unscharfe Kante zwischen Haut und Wand ergibt einen klaren Ausschlag.
    std::vector<float> grad (n, 0.0f);
    {
        const int s = std::max (3, std::min (w, h) / 170);
        const int d = std::max (2, (s * 2) / 3);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                auto at = [&] (int xx, int yy) -> const Lab& { return lab[(size_t) (std::clamp (yy, 0, h - 1) * w + std::clamp (xx, 0, w - 1))]; };
                float g = dist (at (x + s, y), at (x - s, y));
                g = std::max (g, dist (at (x, y + s), at (x, y - s)));
                g = std::max (g, dist (at (x + d, y + d), at (x - d, y - d)));
                g = std::max (g, dist (at (x + d, y - d), at (x - d, y + d)));
                grad[(size_t) (y * w + x)] = g;
            }
    }

    // --- Hintergrund-Farben aus den Rändern schätzen --------------------------------
    // Oben ganz, seitlich nur die oberen 60 % (unten stehen oft die Schultern am Rand).
    std::vector<Lab> seeds;
    const int depth = std::max (2, std::min (w, h) / 128);
    for (int d = 0; d < depth; ++d)
    {
        for (int x = 0; x < w; x += 2)
            seeds.push_back (lab[(size_t) (d * w + x)]);
        for (int y = 0; y < (h * 6) / 10; y += 2)
        {
            seeds.push_back (lab[(size_t) (y * w + d)]);
            seeds.push_back (lab[(size_t) (y * w + w - 1 - d)]);
        }
    }

    constexpr int k = 6;
    std::array<Lab, k> centres {};
    for (int c = 0; c < k; ++c)
        centres[(size_t) c] = seeds[(seeds.size() * (size_t) c) / k];

    std::vector<int> assign (seeds.size(), 0);
    std::array<int, k> counts {};
    for (int iter = 0; iter < 12; ++iter)
    {
        std::array<Lab, k> sum {};
        counts.fill (0);
        for (size_t i = 0; i < seeds.size(); ++i)
        {
            int best = 0; float bd = 1e9f;
            for (int c = 0; c < k; ++c)
            {
                const float d = dist (seeds[i], centres[(size_t) c]);
                if (d < bd) { bd = d; best = c; }
            }
            assign[i] = best;
            auto& s = sum[(size_t) best];
            s.l += seeds[i].l; s.a += seeds[i].a; s.b += seeds[i].b;
            ++counts[(size_t) best];
        }
        for (int c = 0; c < k; ++c)
            if (counts[(size_t) c] > 0)
                centres[(size_t) c] = { sum[(size_t) c].l / (float) counts[(size_t) c],
                                        sum[(size_t) c].a / (float) counts[(size_t) c],
                                        sum[(size_t) c].b / (float) counts[(size_t) c] };
    }

    // Seltene Randfarben (Haarspitze, die oben ins Bild ragt) gehören nicht zum Hintergrund
    std::vector<Lab> bg;
    int covered = 0;
    for (int c = 0; c < k; ++c)
        if (counts[(size_t) c] >= (int) (0.035 * (double) seeds.size()))
        {
            bg.push_back (centres[(size_t) c]);
            covered += counts[(size_t) c];
        }

    if (bg.empty() || covered < (int) (0.6 * (double) seeds.size()))
        return res;   // Rand zu unruhig – kein klarer Hintergrund

    auto modelDist = [&bg] (const Lab& p)
    {
        float d = 1e9f;
        for (auto& c : bg)
            d = std::min (d, dist (p, c));
        return d;
    };

    // --- Hintergrund von außen her zulaufen lassen ------------------------------------
    constexpr float globalT = 13.0f;   // ähnlich einer Randfarbe
    constexpr float localT  = 3.2f;    // kleiner Schritt zum Nachbarn (Verlauf, Schatten an der Wand)
    constexpr float looseT  = 30.0f;   // ... aber nicht beliebig weit weg von den Randfarben
    constexpr float edgeT   = 6.0f;    // an echten Kanten stoppen (Unterschied über ±3 px)

    std::vector<float> md (n);
    for (size_t i = 0; i < n; ++i)
        md[i] = modelDist (lab[i]);

    std::vector<uint8_t> isBg (n, 0);
    std::deque<int> queue;

    auto seed = [&] (int p) { if (! isBg[(size_t) p] && md[(size_t) p] < globalT) { isBg[(size_t) p] = 1; queue.push_back (p); } };
    for (int x = 0; x < w; ++x) { seed (x); seed ((h - 1) * w + x); }
    for (int y = 0; y < h; ++y) { seed (y * w); seed (y * w + w - 1); }

    while (! queue.empty())
    {
        const int p = queue.front(); queue.pop_front();
        const int x = p % w, y = p / w;
        const int nb[4] = { x > 0 ? p - 1 : -1, x < w - 1 ? p + 1 : -1, y > 0 ? p - w : -1, y < h - 1 ? p + w : -1 };

        for (int q : nb)
        {
            if (q < 0 || isBg[(size_t) q])
                continue;

            const auto qi = (size_t) q;
            const bool nearModel = md[qi] < globalT && grad[qi] < edgeT * 1.8f;
            const bool smoothStep = md[qi] < looseT && grad[qi] < edgeT && dist (lab[qi], lab[(size_t) p]) < localT;

            if (nearModel || smoothStep)
            {
                isBg[qi] = 1;
                queue.push_back (q);
            }
        }
    }

    // Saum nachziehen: die Kante selbst ist eine Mischfarbe. Wo das Zulaufen an der breiten
    // Kantenmessung gestoppt hat, noch ein paar Pixel weitergehen, solange es farblich passt.
    for (int iter = 0; iter < 4; ++iter)
    {
        std::vector<int> add;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const int q = y * w + x;
                if (isBg[(size_t) q])
                    continue;
                const int nb[4] = { x > 0 ? q - 1 : -1, x < w - 1 ? q + 1 : -1, y > 0 ? q - w : -1, y < h - 1 ? q + w : -1 };
                for (int p : nb)
                    if (p >= 0 && isBg[(size_t) p] && md[(size_t) q] < looseT && dist (lab[(size_t) q], lab[(size_t) p]) < 7.0f)
                    {
                        add.push_back (q);
                        break;
                    }
            }
        for (int q : add)
            isBg[(size_t) q] = 1;
        if (add.empty())
            break;
    }

    std::vector<uint8_t> fg (n);
    for (size_t i = 0; i < n; ++i)
        fg[i] = isBg[i] ? 0 : 1;

    keepLargeComponents (fg, w, h);
    fillHoles (fg, w, h);

    // Kontur glätten: Mehrheitsfilter 5x5
    {
        std::vector<float> f (n);
        for (size_t i = 0; i < n; ++i) f[i] = fg[i];
        boxBlur (f, w, h, 2);
        for (size_t i = 0; i < n; ++i) fg[i] = f[i] > 0.5f ? 1 : 0;
    }

    size_t fgCount = 0;
    for (auto v : fg) fgCount += v;
    res.foregroundFraction = (float) fgCount / (float) n;

    if (res.foregroundFraction < 0.06f || res.foregroundFraction > 0.92f)
        return res;   // nichts Sinnvolles gefunden

    // --- Weiche Kante: 1 px nach innen (kein heller Saum), dann auslaufen lassen ----
    std::vector<float> a (n);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const size_t i = (size_t) (y * w + x);
            bool keep = fg[i] != 0;
            if (keep && x > 0 && x < w - 1 && y > 0 && y < h - 1)
                keep = fg[i - 1] && fg[i + 1] && fg[i - (size_t) w] && fg[i + (size_t) w];
            a[i] = keep ? 1.0f : 0.0f;
        }

    boxBlur (a, w, h, 1);
    boxBlur (a, w, h, 1);

    res.alpha.resize (n);
    for (size_t i = 0; i < n; ++i)
        res.alpha[i] = (uint8_t) std::lround (std::clamp (a[i], 0.0f, 1.0f) * 255.0f);

    res.ok = true;
    return res;
}
}
