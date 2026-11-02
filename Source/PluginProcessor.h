#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <vector>

class AutoTunasProcessor : public juce::AudioProcessor
{
public:
    AutoTunasProcessor();
    ~AutoTunasProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AUTO-TUNAS"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> detectedMidi { 0.f }, targetMidi { 0.f };   // para la interfaz

private:
    // --- detección de tono (YIN) ---
    float detectFrequency();
    void  analyse();
    float quantise (float midi) const;
    static bool inScale (int note, int key, int scale);

    double fs = 44100.0;
    int yinW = 1024, yinTauMax = 630, hop = 512, hopCount = 0;
    std::vector<float> ring, frame, yinD;
    int ringMask = 0, ringWp = 0;

    // --- cambio de tono (dos taps con delay modulado) ---
    int Wd = 1536, dMask = 0, dWp = 0;
    std::array<std::vector<float>, 2> delayBuf;
    float phase = 0.f;

    // --- estado de corrección ---
    float shiftTarget = 0.f, shiftSm = 0.f;
    float center = 0.f, currentNote = -1000.f;
    bool haveCenter = false;

    // parámetros cacheados por bloque
    int curMode = 1, curKey = 0, curScale = 0;
    float curAmount = 1.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoTunasProcessor)
};
