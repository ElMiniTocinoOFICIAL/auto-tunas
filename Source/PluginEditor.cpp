#include "PluginEditor.h"

static const juce::Colour kAccent (0xff00e5ff), kBg1 (0xff0b0f1a), kBg2 (0xff1b1030);

AutoTunasEditor::AutoTunasEditor (AutoTunasProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setSize (520, 340);

    const char* names[3] = { "ROBOT", "CLASICO", "TUNE-PRO" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeBtn[i];
        b.setButtonText (names[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter ("mode"))
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
        };
        addAndMakeVisible (b);
    }

    auto fillBox = [this] (juce::ComboBox& box, const char* id)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
            box.addItemList (c->choices, 1);
        addAndMakeVisible (box);
    };
    fillBox (keyBox, "key");
    fillBox (scaleBox, "scale");
    keyA   = std::make_unique<CA> (proc.apvts, "key", keyBox);
    scaleA = std::make_unique<CA> (proc.apvts, "scale", scaleBox);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const char* text, const char* suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        s.setTextValueSuffix (suffix);
        s.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (s); addAndMakeVisible (l);
    };
    setupKnob (speed, speedL, "VELOCIDAD", " ms");
    setupKnob (amount, amountL, "CANTIDAD", " %");
    setupKnob (mix, mixL, "MEZCLA", " %");
    speedA  = std::make_unique<SA> (proc.apvts, "speed", speed);
    amountA = std::make_unique<SA> (proc.apvts, "amount", amount);
    mixA    = std::make_unique<SA> (proc.apvts, "mix", mix);

    noteLabel.setJustificationType (juce::Justification::centred);
    noteLabel.setFont (juce::FontOptions (18.f, juce::Font::bold));
    noteLabel.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (noteLabel);

    startTimerHz (20);
    timerCallback();
}

juce::String AutoTunasEditor::noteName (float midi)
{
    if (midi <= 0.f) return "--";
    static const char* n[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int m = (int) std::round (midi);
    return juce::String (n[m % 12]) + juce::String (m / 12 - 1);
}

void AutoTunasEditor::timerCallback()
{
    const int m = (int) *proc.apvts.getRawParameterValue ("mode");
    modeBtn[juce::jlimit (0, 2, m)].setToggleState (true, juce::dontSendNotification);
    speed.setEnabled (m != 0);   // Robot ignora la velocidad (siempre instantaneo)
    noteLabel.setText ("Detectada: " + noteName (proc.detectedMidi.load())
                       + "   ->   Objetivo: " + noteName (proc.targetMidi.load()),
                       juce::dontSendNotification);
}

void AutoTunasEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (kBg1, 0, 0, kBg2, 0, (float) getHeight(), false));
    g.fillAll();
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (30.f, juce::Font::bold));
    g.drawText ("AUTO-TUNAS", getLocalBounds().removeFromTop (60), juce::Justification::centred);
}

void AutoTunasEditor::resized()
{
    auto r = getLocalBounds().reduced (20);
    r.removeFromTop (50);

    auto row = r.removeFromTop (36);
    const int w = row.getWidth() / 3;
    for (auto& b : modeBtn) b.setBounds (row.removeFromLeft (w).reduced (4));

    r.removeFromTop (10);
    auto row2 = r.removeFromTop (28);
    keyBox.setBounds (row2.removeFromLeft (row2.getWidth() / 2).reduced (4, 0));
    scaleBox.setBounds (row2.reduced (4, 0));

    r.removeFromTop (8);
    noteLabel.setBounds (r.removeFromTop (28));

    auto knobs = r;
    const int kw = knobs.getWidth() / 3;
    auto place = [&] (juce::Slider& s, juce::Label& l)
    {
        auto c = knobs.removeFromLeft (kw);
        l.setBounds (c.removeFromTop (20));
        s.setBounds (c);
    };
    place (speed, speedL); place (amount, amountL); place (mix, mixL);
}
