#pragma once
#include "PluginProcessor.h"

class AutoTunasEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AutoTunasEditor (AutoTunasProcessor&);
    ~AutoTunasEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    static juce::String noteName (float midi);

    AutoTunasProcessor& proc;
    juce::TextButton modeBtn[3];
    juce::ComboBox keyBox, scaleBox;
    juce::Slider speed, amount, mix;
    juce::Label speedL, amountL, mixL, noteLabel;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> speedA, amountA, mixA;
    std::unique_ptr<CA> keyA, scaleA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoTunasEditor)
};
