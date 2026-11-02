#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Modo",
                 StringArray { "Robot", "Clasico", "Tune-Pro" }, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Tonalidad",
                 StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Escala",
                 StringArray { "Cromatica", "Mayor", "Menor" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Velocidad",
                 NormalisableRange<float> (0.f, 400.f, 1.f, 0.5f), 40.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Cantidad",
                 NormalisableRange<float> (0.f, 100.f, 1.f), 100.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mezcla",
                 NormalisableRange<float> (0.f, 100.f, 1.f), 100.f));
    return { p.begin(), p.end() };
}

AutoTunasProcessor::AutoTunasProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool AutoTunasProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void AutoTunasProcessor::prepareToPlay (double sr, int)
{
    fs = sr;
    yinW = 1024;
    yinTauMax = juce::jmin (yinW, (int) (fs / 70.0));
    hop = 512; hopCount = 0;

    ring.assign (8192, 0.f); ringMask = 8191; ringWp = 0;
    frame.assign ((size_t) (yinW + yinTauMax), 0.f);
    yinD.assign ((size_t) yinTauMax + 2, 0.f);

    Wd = ((int) (0.035 * fs) / 2) * 2;                 // ventana ~35 ms (par)
    const int size = juce::nextPowerOfTwo (Wd + 4);
    for (auto& b : delayBuf) b.assign ((size_t) size, 0.f);
    dMask = size - 1; dWp = 0; phase = 0.f;

    shiftTarget = shiftSm = 0.f; haveCenter = false; currentNote = -1000.f;
    setLatencySamples (Wd / 2);
}

bool AutoTunasProcessor::inScale (int note, int key, int scale)
{
    static const bool masks[3][12] = {
        { 1,1,1,1,1,1,1,1,1,1,1,1 },   // cromatica
        { 1,0,1,0,1,1,0,1,0,1,0,1 },   // mayor
        { 1,0,1,1,0,1,0,1,1,0,1,0 } }; // menor natural
    return masks[scale][((note - key) % 12 + 12) % 12];
}

float AutoTunasProcessor::quantise (float midi) const
{
    float best = midi, bd = 1e9f;
    const int r = (int) std::round (midi);
    for (int n = r - 2; n <= r + 2; ++n)
        if (inScale (n, curKey, curScale))
        {
            const float d = std::fabs ((float) n - midi);
            if (d < bd) { bd = d; best = (float) n; }
        }
    return best;
}

// YIN: devuelve frecuencia en Hz, o 0 si no hay tono claro
float AutoTunasProcessor::detectFrequency()
{
    const int W = yinW, T = yinTauMax;
    const float* x = frame.data();

    double e = 0.0;
    for (int j = 0; j < W; ++j) e += (double) x[j] * x[j];
    if (e / W < 1e-6) return 0.f;                       // puerta de ruido (~ -60 dB)

    yinD[0] = 1.f;
    double running = 0.0;
    for (int tau = 1; tau <= T; ++tau)
    {
        double s = 0.0;
        for (int j = 0; j < W; ++j) { const float d = x[j] - x[j + tau]; s += (double) d * d; }
        running += s;
        yinD[(size_t) tau] = running > 0.0 ? (float) (s * tau / running) : 1.f;
    }

    const int minTau = juce::jmax (2, (int) (fs / 1000.0));
    int est = -1;
    for (int tau = minTau; tau < T; ++tau)
        if (yinD[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < T && yinD[(size_t) tau + 1] < yinD[(size_t) tau]) ++tau;
            est = tau; break;
        }
    if (est < 0) return 0.f;

    float better = (float) est;
    if (est > 1 && est < T)
    {
        const float s0 = yinD[(size_t) est - 1], s1 = yinD[(size_t) est], s2 = yinD[(size_t) est + 1];
        const float den = 2.f * (2.f * s1 - s2 - s0);
        if (std::fabs (den) > 1e-9f) better += (s2 - s0) / den;
    }
    const float f = (float) fs / better;
    return (f >= 70.f && f <= 1000.f) ? f : 0.f;
}

// Se ejecuta cada "hop" muestras: decide cuantos semitonos hay que mover la voz
void AutoTunasProcessor::analyse()
{
    const int len = (int) frame.size();
    for (int k = 0; k < len; ++k)
        frame[(size_t) k] = ring[(size_t) ((ringWp - len + k) & ringMask)];

    const float f = detectFrequency();
    if (f > 0.f)
    {
        const float midi = 69.f + 12.f * std::log2 (f / 440.f);
        float shift;

        if (curMode == 2)   // ---- TUNE-PRO: corrige el centro del tono y conserva el vibrato ----
        {
            const float coef = 1.f - std::exp (-(float) hop / (float) fs / 0.12f);
            if (! haveCenter || std::fabs (midi - center) > 6.f) { center = midi; haveCenter = true; }
            else center += coef * (midi - center);

            if (currentNote < -100.f || ! inScale ((int) currentNote, curKey, curScale)
                || std::fabs (center - currentNote) > 0.7f)      // histeresis entre notas
                currentNote = quantise (center);

            shift = (currentNote - center) * curAmount;
        }
        else                // ---- ROBOT / CLASICO: ajusta directo a la nota mas cercana ----
        {
            const float t = quantise (midi);
            shift = (t - midi) * curAmount;
            targetMidi.store (t);
        }
        if (curMode == 2) targetMidi.store (currentNote);
        detectedMidi.store (midi);
        shiftTarget = shift;
    }
    else
    {
        shiftTarget = 0.f; haveCenter = false; currentNote = -1000.f;
        detectedMidi.store (0.f); targetMidi.store (0.f);
    }
}

void AutoTunasProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nCh = juce::jmin (buffer.getNumChannels(), 2), n = buffer.getNumSamples();

    curMode   = (int) *apvts.getRawParameterValue ("mode");
    curKey    = (int) *apvts.getRawParameterValue ("key");
    curScale  = (int) *apvts.getRawParameterValue ("scale");
    curAmount = *apvts.getRawParameterValue ("amount") * 0.01f;
    const float mix = *apvts.getRawParameterValue ("mix") * 0.01f;
    const float speedMs = *apvts.getRawParameterValue ("speed");

    // Robot = instantaneo (sin suavizado). Clasico/Tune-Pro = segun "Velocidad"
    const float tau = curMode == 0 ? 0.f : juce::jmax (0.001f, speedMs * 0.001f);
    const float smooth = tau <= 0.f ? 1.f : 1.f - std::exp (-1.f / ((float) fs * tau));

    const float dW = (float) Wd;
    const auto readTap = [this] (const std::vector<float>& b, float d)
    {
        const float pos = (float) dWp - d;
        const int i0 = (int) std::floor (pos);
        const float fr = pos - (float) i0;
        return b[(size_t) (i0 & dMask)] * (1.f - fr) + b[(size_t) ((i0 + 1) & dMask)] * fr;
    };
    const float twoPi = juce::MathConstants<float>::twoPi;

    for (int i = 0; i < n; ++i)
    {
        float in[2] = { 0.f, 0.f }, mono = 0.f;
        for (int c = 0; c < nCh; ++c) { in[c] = buffer.getSample (c, i); mono += in[c]; }
        mono /= (float) juce::jmax (1, nCh);

        ring[(size_t) ringWp] = mono;
        ringWp = (ringWp + 1) & ringMask;
        if (++hopCount >= hop) { hopCount = 0; analyse(); }

        shiftSm += smooth * (shiftTarget - shiftSm);
        const float ratio = std::pow (2.f, shiftSm / 12.f);

        const float ph2 = std::fmod (phase + 0.5f, 1.f);
        const float w1 = 0.5f - 0.5f * std::cos (twoPi * phase);
        const float w2 = 0.5f - 0.5f * std::cos (twoPi * ph2);
        const float d1 = phase * dW, d2 = ph2 * dW;

        for (int c = 0; c < nCh; ++c)
        {
            delayBuf[(size_t) c][(size_t) dWp] = in[c];
            float wet = w1 * readTap (delayBuf[(size_t) c], d1) + w2 * readTap (delayBuf[(size_t) c], d2);
            if (curMode == 0) wet = std::tanh (1.6f * wet) / std::tanh (1.6f);   // toque "robot"
            const float dry = delayBuf[(size_t) c][(size_t) ((dWp - Wd / 2) & dMask)];  // dry alineado a la latencia
            buffer.setSample (c, i, dry * (1.f - mix) + wet * mix);
        }

        dWp = (dWp + 1) & dMask;
        phase += (1.f - ratio) / dW;
        phase -= std::floor (phase);
    }
}

juce::AudioProcessorEditor* AutoTunasProcessor::createEditor() { return new AutoTunasEditor (*this); }

void AutoTunasProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void AutoTunasProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutoTunasProcessor(); }
