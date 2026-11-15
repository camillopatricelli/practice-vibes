// The synthesised ("electronic") drum kit: each hit is computed once into a buffer when the kit loads.
// Classic drum-machine recipes: pitch-swept sines for kick and toms, tone plus filtered noise for the snare,
// and six detuned square waves (the "808 metal") through high filters for hi-hats and cymbals.

#include "DrumKit.h"

#include <cmath>

namespace pv
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        // RBJ cookbook biquad, transposed direct form II.
        struct Biquad
        {
            enum class Type { lowPass, highPass, bandPass };

            Biquad (Type type, double frequency, double q, double sampleRate)
            {
                frequency = juce::jlimit (10.0, sampleRate * 0.45, frequency);
                const auto w = juce::MathConstants<double>::twoPi * frequency / sampleRate;
                const auto alpha = std::sin (w) / (2.0 * q);
                const auto cosW = std::cos (w);
                double b0 = 0, b1 = 0, b2 = 0;

                switch (type)
                {
                    case Type::lowPass:  b0 = (1.0 - cosW) / 2.0; b1 = 1.0 - cosW;    b2 = b0; break;
                    case Type::highPass: b0 = (1.0 + cosW) / 2.0; b1 = -(1.0 + cosW); b2 = b0; break;
                    case Type::bandPass: b0 = alpha;              b1 = 0.0;           b2 = -alpha; break;
                }

                const auto a0 = 1.0 + alpha;
                c0 = (float) (b0 / a0); c1 = (float) (b1 / a0); c2 = (float) (b2 / a0);
                d1 = (float) (-2.0 * cosW / a0); d2 = (float) ((1.0 - alpha) / a0);
            }

            float process (float x)
            {
                const auto y = c0 * x + z1;
                z1 = c1 * x - d1 * y + z2;
                z2 = c2 * x - d2 * y;
                return y;
            }

            float c0, c1, c2, d1, d2, z1 = 0.0f, z2 = 0.0f;
        };

        // Six square waves at inharmonic ratios: the metallic source of hi-hats and cymbals.
        struct Metal
        {
            Metal (float scale, double sampleRate, juce::Random& random)
            {
                static constexpr float base[] { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };

                for (size_t i = 0; i < 6; ++i)
                {
                    increments[i] = base[i] * scale / (float) sampleRate;
                    phases[i] = random.nextFloat();
                }
            }

            float next()
            {
                float sum = 0.0f;

                for (size_t i = 0; i < 6; ++i)
                {
                    phases[i] += increments[i];
                    phases[i] -= std::floor (phases[i]);
                    sum += phases[i] < 0.5f ? 1.0f : -1.0f;
                }

                return sum / 6.0f;
            }

            std::array<float, 6> increments {}, phases {};
        };

        struct Hit
        {
            double sampleRate;
            float velocity;     // 0..1
            float variation;    // about 1.0, slightly different per round robin
            juce::Random random;

            float noise() { return random.nextFloat() * 2.0f - 1.0f; }
            float bright() const { return 0.55f + 0.45f * velocity; }
            int samples (double seconds) const { return (int) (seconds * sampleRate); }
        };

        float attack (float t, float seconds) { return t < seconds ? t / seconds : 1.0f; }

        void pan (juce::AudioBuffer<float>& buffer, int i, float value, float position)
        {
            const auto angle = (position + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            buffer.setSample (0, i, value * std::cos (angle) * juce::MathConstants<float>::sqrt2);
            buffer.setSample (1, i, value * std::sin (angle) * juce::MathConstants<float>::sqrt2);
        }

        juce::AudioBuffer<float> kick (Hit& h)
        {
            juce::AudioBuffer<float> out (2, h.samples (0.6));
            Biquad clickFilter (Biquad::Type::lowPass, 3500.0 * h.bright(), 0.7, h.sampleRate);
            float phase = 0.0f;

            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const auto t = (float) (i / h.sampleRate);
                const auto frequency = (47.0f + (90.0f + 40.0f * h.velocity) * std::exp (-t / 0.032f)) * h.variation;
                phase += frequency / (float) h.sampleRate;
                const auto body = std::sin (twoPi * phase) * std::exp (-t / (0.26f + 0.06f * h.velocity)) * attack (t, 0.0008f);
                const auto click = clickFilter.process (h.noise()) * std::exp (-t / 0.004f) * 0.6f;
                pan (out, i, std::tanh (1.8f * (body + click)), 0.0f);
            }

            return out;
        }

        juce::AudioBuffer<float> snare (Hit& h)
        {
            juce::AudioBuffer<float> out (2, h.samples (0.5));
            Biquad high (Biquad::Type::highPass, 1400.0, 0.7, h.sampleRate);
            Biquad low (Biquad::Type::lowPass, 8500.0 * h.bright(), 0.7, h.sampleRate);
            float phase1 = 0.0f, phase2 = 0.0f;
            const auto noiseDecay = (0.12f + 0.06f * h.velocity) * h.variation;

            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const auto t = (float) (i / h.sampleRate);
                const auto sweep = 1.0f + 0.2f * std::exp (-t / 0.01f);
                phase1 += 185.0f * sweep * h.variation / (float) h.sampleRate;
                phase2 += 330.0f * sweep * h.variation / (float) h.sampleRate;
                const auto tone = 0.6f * std::sin (twoPi * phase1) * std::exp (-t / 0.08f)
                                + 0.35f * std::sin (twoPi * phase2) * std::exp (-t / 0.05f);
                const auto rattle = low.process (high.process (h.noise())) * std::exp (-t / noiseDecay);
                pan (out, i, (0.55f * tone + 0.9f * rattle) * attack (t, 0.0005f), 0.0f);
            }

            return out;
        }

        juce::AudioBuffer<float> hiHat (Hit& h, bool open)
        {
            juce::AudioBuffer<float> out (2, h.samples (open ? 1.2 : 0.25));
            Metal metal (1.7f * h.variation, h.sampleRate, h.random);
            Biquad high (Biquad::Type::highPass, 7000.0, 0.7, h.sampleRate);
            Biquad band (Biquad::Type::bandPass, 10000.0 * h.bright(), 0.9, h.sampleRate);
            const auto decay = open ? 0.33f : 0.03f + 0.015f * h.velocity;

            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const auto t = (float) (i / h.sampleRate);
                const auto source = 0.75f * metal.next() + 0.45f * h.noise();
                const auto value = high.process (band.process (source)) * std::exp (-t / decay) * attack (t, 0.0004f);
                pan (out, i, 3.0f * value, 0.25f);
            }

            return out;
        }

        juce::AudioBuffer<float> cymbal (Hit& h, bool crash)
        {
            juce::AudioBuffer<float> out (2, h.samples (crash ? 2.8 : 2.2));
            Metal metal (crash ? 2.6f : 3.1f, h.sampleRate, h.random);
            Biquad highLeft (Biquad::Type::highPass, crash ? 3500.0 : 5000.0, 0.7, h.sampleRate);
            Biquad highRight (Biquad::Type::highPass, crash ? 3500.0 : 5000.0, 0.7, h.sampleRate);
            Biquad lowLeft (Biquad::Type::lowPass, 14000.0 * h.bright(), 0.7, h.sampleRate);
            Biquad lowRight (Biquad::Type::lowPass, 14000.0 * h.bright(), 0.7, h.sampleRate);
            const auto decay = (crash ? 0.85f : 0.9f) * h.variation;
            float pingPhase1 = 0.0f, pingPhase2 = 0.0f;

            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const auto t = (float) (i / h.sampleRate);
                const auto shared = metal.next() * (crash ? 0.4f : 0.5f);
                const auto noiseLevel = crash ? 0.7f : 0.22f;
                auto left = lowLeft.process (highLeft.process (shared + noiseLevel * h.noise()));
                auto right = lowRight.process (highRight.process (shared + noiseLevel * h.noise()));
                const auto envelope = std::exp (-t / decay) * attack (t, crash ? 0.002f : 0.0005f);
                left *= envelope;
                right *= envelope;

                if (! crash)
                {
                    // The ride's "ping": a few clear partials over the wash, plus the stick.
                    pingPhase1 += 3150.0f * h.variation / (float) h.sampleRate;
                    pingPhase2 += 4730.0f * h.variation / (float) h.sampleRate;
                    const auto ping = (0.22f * std::sin (twoPi * pingPhase1) + 0.12f * std::sin (twoPi * pingPhase2))
                                    * std::exp (-t / 0.35f) * h.bright();
                    const auto stick = h.noise() * std::exp (-t / 0.002f) * 0.3f;
                    left += ping + stick;
                    right += ping + stick;
                }

                out.setSample (0, i, 1.6f * left);
                out.setSample (1, i, 1.6f * right);
            }

            return out;
        }

        juce::AudioBuffer<float> tom (Hit& h, float frequency, double seconds, float position)
        {
            juce::AudioBuffer<float> out (2, h.samples (seconds));
            Biquad stickFilter (Biquad::Type::lowPass, 3000.0 * h.bright(), 0.7, h.sampleRate);
            float phase1 = 0.0f, phase2 = 0.0f;
            const auto decay = (float) seconds * 0.33f * h.variation;

            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const auto t = (float) (i / h.sampleRate);
                const auto f = frequency * h.variation * (1.0f + 0.35f * std::exp (-t / 0.05f));
                phase1 += f / (float) h.sampleRate;
                phase2 += f * 1.5f / (float) h.sampleRate;
                const auto body = std::sin (twoPi * phase1) * std::exp (-t / decay)
                                + 0.25f * std::sin (twoPi * phase2) * std::exp (-t / (decay * 0.4f));
                const auto stick = stickFilter.process (h.noise()) * std::exp (-t / 0.006f) * 0.35f;
                pan (out, i, std::tanh (1.4f * (body + stick)) * attack (t, 0.0006f), position);
            }

            return out;
        }

        void fadeEnd (juce::AudioBuffer<float>& buffer, double sampleRate)
        {
            const auto length = juce::jmin (buffer.getNumSamples(), (int) (0.02 * sampleRate));

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.applyGainRamp (ch, buffer.getNumSamples() - length, length, 1.0f, 0.0f);
        }
    }

    DrumSamples synthesiseDrumKit (double sampleRate)
    {
        DrumSamples kit;
        kit.sampleRate = sampleRate;

        static constexpr float layerVelocities[] { 0.3f, 0.5f, 0.75f, 1.0f };
        static constexpr float roundRobinVariation[] { 1.0f, 1.012f };

        for (int d = 0; d < numDrums; ++d)
        {
            auto& layers = kit.hits[(size_t) d];
            float peak = 0.0f;

            for (size_t layer = 0; layer < 4; ++layer)
            {
                layers.emplace_back();

                for (size_t rr = 0; rr < 2; ++rr)
                {
                    Hit h { sampleRate, layerVelocities[layer], roundRobinVariation[rr],
                            juce::Random ((juce::int64) (d * 100 + (int) layer * 10 + (int) rr + 1)) };

                    juce::AudioBuffer<float> hit;

                    switch ((Drum) d)
                    {
                        case Drum::kick:      hit = kick (h); break;
                        case Drum::snare:     hit = snare (h); break;
                        case Drum::closedHat: hit = hiHat (h, false); break;
                        case Drum::openHat:   hit = hiHat (h, true); break;
                        case Drum::ride:      hit = cymbal (h, false); break;
                        case Drum::crash:     hit = cymbal (h, true); break;
                        case Drum::tomHigh:   hit = tom (h, 190.0f, 0.7, 0.2f); break;
                        case Drum::tomLow:    hit = tom (h, 112.0f, 0.9, -0.25f); break;
                    }

                    // Soft layers are quieter as well as darker.
                    hit.applyGain (std::pow (layerVelocities[layer], 0.9f));
                    fadeEnd (hit, sampleRate);
                    peak = juce::jmax (peak, hit.getMagnitude (0, hit.getNumSamples()));
                    layers.back().push_back (std::move (hit));
                }
            }

            // Same loudness convention as the sampled kit: the loudest hit of each drum peaks at -1 dB.
            for (auto& roundRobins : layers)
                for (auto& hit : roundRobins)
                    hit.applyGain (0.89f / juce::jmax (0.0001f, peak));
        }

        return kit;
    }

    const char* drumName (Drum drum)
    {
        switch (drum)
        {
            case Drum::crash:     return "Crash";
            case Drum::ride:      return "Ride";
            case Drum::openHat:   return "Open hi-hat";
            case Drum::closedHat: return "Closed hi-hat";
            case Drum::tomHigh:   return "High tom";
            case Drum::tomLow:    return "Low tom";
            case Drum::snare:     return "Snare";
            case Drum::kick:      return "Kick";
        }

        return "";
    }

    bool DrumSamples::isEmpty() const
    {
        for (const auto& layers : hits)
            if (layers.empty())
                return true;

        return false;
    }
}
