#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// Moteur de traitement "AutoMaster" : analyse (volume, spectre), EQ adaptatif,
// compression douce, largeur stereo, gain automatique et limiteur a anticipation.
// Aucune allocation dans process() (seulement dans prepare / startLearn).

namespace am
{
//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0; }

    // |H(f)|^2 : reponse en amplitude (au carre) a la frequence f
    double magSq (double fs, double f) const noexcept
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double c1 = std::cos (w), c2 = std::cos (2.0 * w);
        const double num = b0 * b0 + b1 * b1 + b2 * b2 + 2.0 * (b0 * b1 + b1 * b2) * c1 + 2.0 * b0 * b2 * c2;
        const double den = 1.0 + a1 * a1 + a2 * a2 + 2.0 * (a1 + a1 * a2) * c1 + 2.0 * a2 * c2;
        return num / std::max (den, 1e-18);
    }

    void setRaw (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    void peaking (double fs, double f, double q, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        setRaw (1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
    }

    void lowShelf (double fs, double f, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double c = std::cos (w0), s = std::sin (w0);
        const double alpha = s / std::sqrt (2.0);
        const double t = 2.0 * std::sqrt (A) * alpha;
        setRaw (A * ((A + 1) - (A - 1) * c + t),
                2 * A * ((A - 1) - (A + 1) * c),
                A * ((A + 1) - (A - 1) * c - t),
                (A + 1) + (A - 1) * c + t,
                -2 * ((A - 1) + (A + 1) * c),
                (A + 1) + (A - 1) * c - t);
    }

    void highShelf (double fs, double f, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double c = std::cos (w0), s = std::sin (w0);
        const double alpha = s / std::sqrt (2.0);
        const double t = 2.0 * std::sqrt (A) * alpha;
        setRaw (A * ((A + 1) + (A - 1) * c + t),
                -2 * A * ((A - 1) + (A + 1) * c),
                A * ((A + 1) + (A - 1) * c - t),
                (A + 1) - (A - 1) * c + t,
                2 * ((A - 1) - (A + 1) * c),
                (A + 1) - (A - 1) * c - t);
    }
};

// Ponderation K (ITU-R BS.1770) pour la mesure en LUFS.
struct KWeight
{
    Biquad shelf, hp;

    void prepare (double fs)
    {
        const double pi = juce::MathConstants<double>::pi;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
            hp.a1 = 2.0 * (K * K - 1.0) / a0;
            hp.a2 = (1.0 - K / Q + K * K) / a0;
        }
        shelf.reset();
        hp.reset();
    }

    inline double process (double x) noexcept { return hp.process (shelf.process (x)); }
};

//==============================================================================
class Engine
{
public:
    static constexpr int NB = 9;
    static constexpr int NS = 24; // points du spectre pondere K (echelle log) servant a estimer le gain
    static constexpr int FFT_ORDER = 11;
    static constexpr int FFT_N = 1 << FFT_ORDER;
    static constexpr int HOP = FFT_N / 2;

    static constexpr std::array<double, NB> kCenter { 60, 125, 250, 500, 1000, 2000, 4000, 8000, 14000 };
    static constexpr std::array<double, NB> kFc     { 80, 125, 250, 500, 1000, 2000, 4000, 8000, 10000 };
    static constexpr std::array<double, NB> kMaxBoost { 2.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 2.5 };
    static constexpr double kMaxCut = -4.0;

    struct Params
    {
        float eqAmount = 0.7f;   // 0..1
        float tilt = 0.f;        // dB
        float comp = 0.4f;       // 0..1
        float width = 1.f;       // 0.5..1.5
        float targetLufs = -14.f;
        float ceilingDb = -1.f;
        float trimDb = 0.f;
    };

    struct Learned
    {
        bool valid = false;
        float lufs = -70.f, peakDb = -60.f, thrDb = -18.f;
        std::array<float, NB> corr {};
        std::array<float, NS> kshare {};
    };

    enum Status { None = 0, Learning = 1, Ok = 2, TooLittle = 3 };

    // Valeurs lisibles depuis l'interface
    std::atomic<float> outLufs { -100.f }, grDb { 0.f }, learnedLufs { -100.f }, learnedSeconds { 0.f }, autoTrimShown { 0.f };
    std::atomic<int> status { None };

    Engine()
    {
        hann.resize (FFT_N);
        for (int i = 0; i < FFT_N; ++i)
            hann[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) FFT_N);
        fftData.assign ((size_t) FFT_N * 2, 0.f);
        fifo.assign ((size_t) FFT_N, 0.f);
        binPow.assign ((size_t) FFT_N / 2 + 1, 0.0);
        blocks.reserve (40000);
    }

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        L = std::max (8, (int) std::round (0.003 * sr));
        dlL.assign ((size_t) L, 0.0);
        dlR.assign ((size_t) L, 0.0);
        tbuf.assign ((size_t) L, 1.0);
        mbuf.assign ((size_t) L, 1.0);
        mSum = (double) L;
        wp = 0;
        limEnv = 1.0;
        limRelCoef = 1.0 - std::exp (-1.0 / (0.06 * sr));
        detCoef = 1.0 - std::exp (-1.0 / (0.010 * sr));
        atkCoef = 1.0 - std::exp (-1.0 / (0.025 * sr));
        relCoef = 1.0 - std::exp (-1.0 / (0.180 * sr));
        meterCoef = 1.0 - std::exp (-1.0 / (0.400 * sr));
        grAvgCoef = 1.0 - std::exp (-1.0 / (5.0 * sr));
        avgGrDb = 0.0;
        subLen = (int) std::round (0.1 * sr);
        kIn[0].prepare (sr); kIn[1].prepare (sr);
        kOut[0].prepare (sr); kOut[1].prepare (sr);
        for (int b = 0; b < NB; ++b) { eqL[(size_t) b].reset(); eqR[(size_t) b].reset(); }
        bandGain.fill (0.0);
        applied.fill (1e9);
        detSq = 0; compGainDb = 0; meterPow = 0; trimCur = 1.0;
        learning = false;
        trimKeyValid = false;
        updateAutoTrim();
        trimCur = std::pow (10.0, (autoTrimDb + p.trimDb) / 20.0);
        updateEq (true);
    }

    int getLatency() const noexcept { return L - 1; }

    void setParams (const Params& np)
    {
        p = np;
        updateAutoTrim();
    }

    bool isLearning() const noexcept { return learning; }
    const Learned& getLearned() const noexcept { return learned; }

    //--------------------------------------------------------------------------
    void startLearn()
    {
        blocks.clear();
        std::fill (binPow.begin(), binPow.end(), 0.0);
        frames = 0; fifoPos = 0; peakLin = 0.0;
        acc = 0; accN = 0; subIdx = 0; subCount = 0; subPow.fill (0.0);
        kIn[0].prepare (sr); kIn[1].prepare (sr);
        learning = true;
        status.store (Learning);
    }

    void stopLearn()
    {
        if (! learning) return;
        learning = false;
        finalizeLearn();
    }

    //--------------------------------------------------------------------------
    void process (float* l, float* r, int n) noexcept
    {
        updateEq (false);

        const double makeupDb = juce::jlimit (0.0, 8.0, -avgGrDb); // compensation lente de la compression
        const double trimTarget = std::pow (10.0, (autoTrimDb + (double) p.trimDb + makeupDb) / 20.0);
        const double trimStart = trimCur;
        const double ceil = std::pow (10.0, (double) p.ceilingDb / 20.0);
        const double ratio = 1.0 + (double) p.comp * 2.5;
        const double thr = learned.valid ? (double) learned.thrDb : -18.0;
        const double widthS = (double) p.width;
        const double kKnee = 6.0;
        double minEnv = 1.0;

        for (int i = 0; i < n; ++i)
        {
            const double inL = l[i], inR = r[i];
            if (learning) analyse (inL, inR);

            double xl = inL, xr = inR;
            for (int b = 0; b < NB; ++b)
            {
                xl = eqL[(size_t) b].process (xl);
                xr = eqR[(size_t) b].process (xr);
            }

            const double m = 0.5 * (xl + xr), s = 0.5 * (xl - xr) * widthS;
            xl = m + s;
            xr = m - s;

            // Compression douce (liee stereo, genou doux)
            if (ratio > 1.001)
            {
                detSq += (0.5 * (xl * xl + xr * xr) - detSq) * detCoef;
                const double lvl = 10.0 * std::log10 (detSq + 1e-12);
                const double over = lvl - thr;
                double outLvl;
                if (2.0 * over < -kKnee) outLvl = lvl;
                else if (2.0 * std::abs (over) <= kKnee)
                {
                    const double t = over + kKnee * 0.5;
                    outLvl = lvl + (1.0 / ratio - 1.0) * t * t / (2.0 * kKnee);
                }
                else outLvl = thr + over / ratio;
                const double tgt = outLvl - lvl;
                compGainDb += (tgt - compGainDb) * (tgt < compGainDb ? atkCoef : relCoef);
            }
            else
                compGainDb += (0.0 - compGainDb) * relCoef;

            if (detSq > 1e-5) avgGrDb += (compGainDb - avgGrDb) * grAvgCoef; // moyenne lente (~5 s), hors silences
            const double cg = std::exp (compGainDb * 0.11512925464970229);
            const double tg = trimStart + (trimTarget - trimStart) * (double) (i + 1) / (double) n;
            xl *= cg * tg;
            xr *= cg * tg;

            // Limiteur a anticipation (la sortie est retardee de L-1 echantillons)
            const double pk = std::max (std::abs (xl), std::abs (xr));
            const double tgtG = pk > ceil ? ceil / pk : 1.0;
            const size_t w = (size_t) wp;
            tbuf[w] = tgtG;
            double mn = 1.0;
            for (int k = 0; k < L; ++k) mn = std::min (mn, tbuf[(size_t) k]);
            mSum += mn - mbuf[w];
            mbuf[w] = mn;
            const double g = std::min (1.0, mSum / (double) L);
            if (g < limEnv) limEnv = g;
            else limEnv += (g - limEnv) * limRelCoef;
            minEnv = std::min (minEnv, limEnv);

            dlL[w] = xl;
            dlR[w] = xr;
            wp = (wp + 1) % L;
            const size_t rp = (size_t) wp;
            const double ol = juce::jlimit (-ceil, ceil, dlL[rp] * limEnv);
            const double orr = juce::jlimit (-ceil, ceil, dlR[rp] * limEnv);
            l[i] = (float) ol;
            r[i] = (float) orr;

            const double kl = kOut[0].process (ol), kr = kOut[1].process (orr);
            meterPow += (kl * kl + kr * kr - meterPow) * meterCoef;
        }

        trimCur = trimTarget;
        outLufs.store ((float) (-0.691 + 10.0 * std::log10 (meterPow + 1e-12)));
        grDb.store ((float) (20.0 * std::log10 (minEnv + 1e-9)));
    }

    //--------------------------------------------------------------------------
    // Sauvegarde / restauration des reglages appris (stockes dans le projet du DAW)
    juce::String getLearnedString() const
    {
        if (! learned.valid) return {};
        juce::String s ("2 ");
        s << learned.lufs << " " << learned.peakDb << " " << learned.thrDb;
        for (auto v : learned.corr)  s << " " << v;
        for (auto v : learned.kshare) s << " " << v;
        return s;
    }

    void setLearnedString (const juce::String& s)
    {
        juce::StringArray t;
        t.addTokens (s, " ", "");
        t.removeEmptyStrings();
        if (t.size() != 4 + NB + NS || t[0] != "2") return;
        Learned ln;
        ln.valid = true;
        ln.lufs = t[1].getFloatValue();
        ln.peakDb = t[2].getFloatValue();
        ln.thrDb = t[3].getFloatValue();
        for (int i = 0; i < NB; ++i) ln.corr[(size_t) i] = t[4 + i].getFloatValue();
        for (int i = 0; i < NS; ++i) ln.kshare[(size_t) i] = t[4 + NB + i].getFloatValue();
        learned = ln;
        ++learnedStamp;
        learnedLufs.store (ln.lufs);
        status.store (Ok);
        updateAutoTrim();
    }

private:
    //--------------------------------------------------------------------------
    double slotFreq (int j) const noexcept
    {
        const double fmax = std::min (20000.0, sr * 0.49);
        return 30.0 * std::pow (fmax / 30.0, (double) j / (double) (NS - 1));
    }

    // Variation de volume (ponderee K) apportee par l'EQ, calculee a partir de la vraie reponse des filtres
    double eqDeltaDb() const
    {
        std::array<Biquad, NB> f;
        for (int b = 0; b < NB; ++b)
        {
            const size_t i = (size_t) b;
            double g = (double) learned.corr[i] * (double) p.eqAmount + (double) p.tilt * std::log2 (kCenter[i] / 1000.0) / 3.0;
            g = juce::jlimit (-8.0, 8.0, g);
            if (b == 0) f[i].lowShelf (sr, kFc[i], g);
            else if (b == NB - 1) f[i].highShelf (sr, kFc[i], g);
            else f[i].peaking (sr, kFc[i], 0.9, g);
        }
        double sum = 0.0, tot = 0.0;
        for (int j = 0; j < NS; ++j)
        {
            double h = 1.0;
            for (int b = 0; b < NB; ++b) h *= f[(size_t) b].magSq (sr, slotFreq (j));
            sum += (double) learned.kshare[(size_t) j] * h;
            tot += (double) learned.kshare[(size_t) j];
        }
        return 10.0 * std::log10 (std::max (sum / std::max (tot, 1e-12), 1e-6));
    }

    void updateAutoTrim()
    {
        double t = 0.0;
        if (learned.valid)
        {
            const double key[2] = { (double) p.eqAmount, (double) p.tilt };
            if (! trimKeyValid || key[0] != trimKey[0] || key[1] != trimKey[1] || learnedStamp != trimStamp)
            {
                cachedEqDelta = eqDeltaDb();
                trimKey[0] = key[0]; trimKey[1] = key[1]; trimStamp = learnedStamp; trimKeyValid = true;
            }
            t = (double) p.targetLufs - (double) learned.lufs - cachedEqDelta;
            const double maxByPeak = ((double) p.ceilingDb - (double) learned.peakDb) + 5.0; // max ~5 dB de limitation
            t = std::min (t, maxByPeak);
            t = juce::jlimit (-12.0, 12.0, t);
        }
        autoTrimDb = t;
        autoTrimShown.store ((float) t);
    }

    void updateEq (bool force)
    {
        for (int b = 0; b < NB; ++b)
        {
            const size_t i = (size_t) b;
            double tgt = learned.valid ? (double) learned.corr[i] * (double) p.eqAmount : 0.0;
            tgt += (double) p.tilt * std::log2 (kCenter[i] / 1000.0) / 3.0;
            tgt = juce::jlimit (-8.0, 8.0, tgt);
            bandGain[i] = force ? tgt : bandGain[i] + (tgt - bandGain[i]) * 0.35;
            if (std::abs (bandGain[i] - applied[i]) > 0.005)
            {
                applied[i] = bandGain[i];
                if (b == 0)
                {
                    eqL[i].lowShelf (sr, kFc[i], bandGain[i]);
                    eqR[i].lowShelf (sr, kFc[i], bandGain[i]);
                }
                else if (b == NB - 1)
                {
                    eqL[i].highShelf (sr, kFc[i], bandGain[i]);
                    eqR[i].highShelf (sr, kFc[i], bandGain[i]);
                }
                else
                {
                    eqL[i].peaking (sr, kFc[i], 0.9, bandGain[i]);
                    eqR[i].peaking (sr, kFc[i], 0.9, bandGain[i]);
                }
            }
        }
    }

    //--------------------------------------------------------------------------
    void analyse (double inL, double inR) noexcept
    {
        peakLin = std::max (peakLin, std::max (std::abs (inL), std::abs (inR)));

        // Volume (LUFS) : blocs de 100 ms, fenetre de 400 ms
        const double kl = kIn[0].process (inL), kr = kIn[1].process (inR);
        acc += kl * kl + kr * kr;
        if (++accN >= subLen)
        {
            subPow[(size_t) (subIdx & 3)] = acc / (double) accN;
            ++subIdx; ++subCount;
            acc = 0; accN = 0;
            if (subCount >= 4 && blocks.size() < blocks.capacity())
            {
                const double pw = 0.25 * (subPow[0] + subPow[1] + subPow[2] + subPow[3]);
                blocks.push_back ((float) (-0.691 + 10.0 * std::log10 (pw + 1e-12)));
            }
        }

        // Spectre moyen (FFT 2048, recouvrement 50 %) sur le signal mono
        fifo[(size_t) fifoPos++] = (float) (0.5 * (inL + inR));
        if (fifoPos >= FFT_N)
        {
            double e = 0.0;
            for (int i = 0; i < FFT_N; ++i) e += (double) fifo[(size_t) i] * (double) fifo[(size_t) i];
            e /= (double) FFT_N;
            if (e > 1e-6) // > -60 dBFS : on ignore les silences
            {
                for (int i = 0; i < FFT_N; ++i) fftData[(size_t) i] = fifo[(size_t) i] * hann[(size_t) i];
                std::fill (fftData.begin() + FFT_N, fftData.end(), 0.f);
                fft.performFrequencyOnlyForwardTransform (fftData.data());
                for (int k = 1; k <= FFT_N / 2; ++k)
                    binPow[(size_t) k] += (double) fftData[(size_t) k] * (double) fftData[(size_t) k];
                ++frames;
            }
            std::copy (fifo.begin() + HOP, fifo.end(), fifo.begin());
            fifoPos = HOP;
        }
    }

    void finalizeLearn()
    {
        learnedSeconds.store ((float) blocks.size() * 0.1f);

        // Volume integre avec portes -70 LUFS puis -10 LU (BS.1770-4)
        double sumP = 0; int cnt = 0;
        for (auto v : blocks) if (v > -70.f) { sumP += std::pow (10.0, (double) (v + 0.691f) / 10.0); ++cnt; }
        if (cnt < 20 || frames < 20) { status.store (TooLittle); return; }
        const double relGate = -0.691 + 10.0 * std::log10 (sumP / cnt) - 10.0;
        double sumP2 = 0; int cnt2 = 0;
        for (auto v : blocks) if ((double) v > relGate) { sumP2 += std::pow (10.0, (double) (v + 0.691f) / 10.0); ++cnt2; }
        if (cnt2 < 10) { status.store (TooLittle); return; }
        const double lufs = -0.691 + 10.0 * std::log10 (sumP2 / cnt2);

        // Energie par bande d'octave
        std::array<double, NB + 1> edges { 20, 88, 177, 354, 707, 1414, 2828, 5657, 10000, std::min (22000.0, sr * 0.49) };
        std::array<double, NB> bandPow {};
        for (int k = 1; k <= FFT_N / 2; ++k)
        {
            const double f = (double) k * sr / (double) FFT_N;
            for (int b = 0; b < NB; ++b)
                if (f >= edges[(size_t) b] && f < edges[(size_t) b + 1]) { bandPow[(size_t) b] += binPow[(size_t) k]; break; }
        }
        double total = 0;
        for (auto v : bandPow) total += v;
        if (total <= 0) { status.store (TooLittle); return; }

        std::array<double, NB> d {};
        for (int b = 0; b < NB; ++b)
        {
            const double oct = std::log2 (edges[(size_t) b + 1] / edges[(size_t) b]);
            const double dB = 10.0 * std::log10 (bandPow[(size_t) b] / oct / (double) frames + 1e-12);
            const double tgt = -3.0 * std::log2 (std::max (kCenter[(size_t) b], 120.0) / 1000.0); // pente cible ~ -3 dB / octave
            d[(size_t) b] = tgt - dB;
        }
        double mean = 0;
        for (int b = 1; b <= 7; ++b) mean += d[(size_t) b];
        mean /= 7.0;

        Learned ln;
        ln.valid = true;
        ln.lufs = (float) lufs;
        ln.peakDb = (float) (20.0 * std::log10 (peakLin + 1e-9));
        ln.thrDb = (float) (lufs + 2.0);
        for (int b = 0; b < NB; ++b)
        {
            ln.corr[(size_t) b] = (float) juce::jlimit (kMaxCut, kMaxBoost[(size_t) b], d[(size_t) b] - mean);
        }
        {
            const double fmax = std::min (20000.0, sr * 0.49);
            std::array<double, NS> ks {};
            double ksum = 0;
            for (int k = 1; k <= FFT_N / 2; ++k)
            {
                const double f = (double) k * sr / (double) FFT_N;
                if (f < 25.0 || f > fmax) continue;
                const double w = binPow[(size_t) k] * kIn[0].shelf.magSq (sr, f) * kIn[0].hp.magSq (sr, f);
                int j = (int) std::lround (std::log (f / 30.0) / std::log (fmax / 30.0) * (double) (NS - 1));
                j = juce::jlimit (0, NS - 1, j);
                ks[(size_t) j] += w; ksum += w;
            }
            for (int j = 0; j < NS; ++j) ln.kshare[(size_t) j] = (float) (ks[(size_t) j] / std::max (ksum, 1e-30));
        }
        learned = ln;
        ++learnedStamp;
        learnedLufs.store (ln.lufs);
        updateAutoTrim();
        status.store (Ok);
    }

    //--------------------------------------------------------------------------
    double sr = 44100.0;
    Params p;
    Learned learned;
    bool learning = false;
    double autoTrimDb = 0.0, trimCur = 1.0, cachedEqDelta = 0.0, trimKey[2] { 0, 0 };
    bool trimKeyValid = false;
    int learnedStamp = 0, trimStamp = -1;

    std::array<Biquad, NB> eqL, eqR;
    std::array<double, NB> bandGain {}, applied {};

    double detSq = 0, compGainDb = 0, detCoef = 0, atkCoef = 0, relCoef = 0, avgGrDb = 0, grAvgCoef = 0;

    int L = 144, wp = 0;
    std::vector<double> dlL, dlR, tbuf, mbuf;
    double mSum = 0, limEnv = 1.0, limRelCoef = 0.0;

    KWeight kOut[2], kIn[2];
    double meterPow = 0, meterCoef = 0;

    // Analyse
    juce::dsp::FFT fft { FFT_ORDER };
    std::vector<float> hann, fftData, fifo;
    std::vector<double> binPow;
    std::vector<float> blocks;
    int fifoPos = 0, subLen = 4410, accN = 0, subIdx = 0, subCount = 0;
    long long frames = 0;
    double acc = 0, peakLin = 0;
    std::array<double, 4> subPow {};
};
} // namespace am
