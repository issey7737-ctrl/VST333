// Test hors-ligne du moteur : signal synthetique "sombre" -> apprentissage -> traitement.
#include "AutoMasterEngine.h"
#include <cstdio>
#include <random>

using namespace am;

static double lufsOf (const std::vector<float>& L, const std::vector<float>& R, double sr, size_t from)
{
    KWeight a, b;
    a.prepare (sr); b.prepare (sr);
    double s = 0; size_t n = 0;
    for (size_t i = from; i < L.size(); ++i)
    {
        const double x = a.process (L[i]), y = b.process (R[i]);
        s += x * x + y * y; ++n;
    }
    return -0.691 + 10.0 * std::log10 (s / (double) n + 1e-12);
}

static double bandRms (const std::vector<float>& x, double sr, double f, size_t from, bool high)
{
    // energie au-dessus (high) ou en dessous de f via biquad simple
    Biquad q;
    if (high) q.highShelf (sr, f, -60.0); else q.lowShelf (sr, f, -60.0); // attenue le haut/bas
    double e = 0, e0 = 0; size_t n = 0;
    for (size_t i = from; i < x.size(); ++i) { const double y = q.process (x[i]); e += y * y; e0 += (double) x[i] * x[i]; ++n; }
    return 10.0 * std::log10 (e0 / (double) n + 1e-12) - 10.0 * std::log10 (e / (double) n + 1e-12);
}

int main()
{
    int failures = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        Engine e;
        e.prepare (sr, 512);
        Engine::Params p; p.targetLufs = -14.f; p.ceilingDb = -1.f; p.eqAmount = 1.0f; p.comp = 0.5f;
        e.setParams (p);

        const size_t N = (size_t) (sr * 40);
        std::vector<float> L (N), R (N);
        std::mt19937 rng (1);
        std::normal_distribution<float> nd (0.f, 1.f);
        double l1 = 0, l2 = 0, r1 = 0, r2 = 0;
        for (size_t i = 0; i < N; ++i)
        {
            const double t = (double) i / sr;
            const double w1 = nd (rng), w2 = nd (rng);
            l1 += 0.04 * (w1 - l1); l2 += 0.04 * (l1 - l2); // passe-bas -> son "sombre"
            r1 += 0.04 * (w2 - r1); r2 += 0.04 * (r1 - r2);
            const double beat = std::exp (-std::fmod (t, 0.5) * 18.0) * std::sin (2 * 3.14159265 * 55.0 * t);
            L[i] = (float) (0.5 * (0.25 * l2 * 8 + 0.35 * beat));
            R[i] = (float) (0.5 * (0.25 * r2 * 8 + 0.35 * beat));
        }

        const double inL = lufsOf (L, R, sr, (size_t) (sr * 20));
        const auto inL0 = L; const auto inR0 = R;

        // Apprentissage pendant 15 s
        e.startLearn();
        const int B = 512;
        std::vector<float> tl (L), tr (R);
        size_t i = 0;
        for (; i < (size_t) (sr * 15); i += B) e.process (tl.data() + i, tr.data() + i, (int) std::min<size_t> (B, N - i));
        e.stopLearn();

        // Traitement du reste (nouvelle copie, entree identique)
        Engine::Params p2 = p; e.setParams (p2);
        std::vector<float> ol (inL0), orr (inR0);
        for (size_t k = 0; k < N; k += B) e.process (ol.data() + k, orr.data() + k, (int) std::min<size_t> (B, N - k));

        const auto& ln = e.getLearned();
        double peak = 0; bool bad = false;
        for (size_t k = 0; k < N; ++k) { peak = std::max (peak, (double) std::max (std::abs (ol[k]), std::abs (orr[k]))); if (! std::isfinite (ol[k]) || ! std::isfinite (orr[k])) bad = true; }
        const double outLufs = lufsOf (ol, orr, sr, (size_t) (sr * 20));
        const double tiltIn = bandRms (inL0, sr, 3000.0, (size_t) (sr * 20), true);
        const double tiltOut = bandRms (ol, sr, 3000.0, (size_t) (sr * 20), true);
        const double ceilLin = std::pow (10.0, -1.0 / 20.0);

        std::printf ("sr=%.0f status=%d learned=%.1f LUFS peak=%.1f dBFS | in=%.1f LUFS out=%.1f LUFS | peakOut=%.3f (ceil %.3f) | latency=%d | nan=%d\n",
                     sr, e.status.load(), ln.lufs, ln.peakDb, inL, outLufs, peak, ceilLin, e.getLatency(), (int) bad);
        std::printf ("   corr dB:"); for (auto c : ln.corr) std::printf (" %+.1f", c);
        std::printf ("\n   autoTrim=%+.1f dB, GR=%.1f dB, meter=%.1f LUFS\n", e.autoTrimShown.load(), e.grDb.load(), e.outLufs.load());

        if (e.status.load() != Engine::Ok) { std::printf ("FAIL: apprentissage non valide\n"); ++failures; }
        if (bad) { std::printf ("FAIL: NaN/Inf\n"); ++failures; }
        if (peak > ceilLin + 1e-4) { std::printf ("FAIL: depassement du plafond\n"); ++failures; }
        if (std::abs (outLufs - (-14.0)) > 2.5) { std::printf ("WARN: volume final loin de la cible (%.1f)\n", outLufs); }
        (void) tiltIn; (void) tiltOut;
    }

    // Silence : pas de plantage, "pas assez de son"
    {
        Engine e; e.prepare (48000, 512);
        std::vector<float> a (48000 * 5, 0.f), b (a);
        e.startLearn();
        for (size_t k = 0; k < a.size(); k += 512) e.process (a.data() + k, b.data() + k, (int) std::min<size_t> (512, a.size() - k));
        e.stopLearn();
        std::printf ("silence: status=%d (attendu 3)\n", e.status.load());
        if (e.status.load() != Engine::TooLittle) ++failures;
    }

    // Latence : une impulsion apparait L-1 echantillons plus tard
    {
        Engine e; e.prepare (48000, 512);
        Engine::Params p; p.comp = 0; p.eqAmount = 0; e.setParams (p);
        std::vector<float> a (2048, 0.f), b (2048, 0.f);
        a[100] = 0.1f; b[100] = 0.1f;
        e.process (a.data(), b.data(), 2048);
        size_t at = 0; for (size_t k = 0; k < a.size(); ++k) if (std::abs (a[k]) > 0.05f) { at = k; break; }
        std::printf ("impulsion: attendu index %d, trouve %zu (valeur %.3f)\n", 100 + e.getLatency(), at, a[at]);
        if ((int) at != 100 + e.getLatency()) ++failures;
    }

    std::printf (failures ? "ECHEC (%d)\n" : "TOUS LES TESTS OK\n", failures);
    return failures;
}
