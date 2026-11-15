#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AmpProcessor.h"

// Manopola semplice in stile pannello scuro
class KnobLook : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override {
        auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(6.0f);
        float d = juce::jmin(r.getWidth(), r.getHeight()), cx = r.getCentreX(), cy = r.getCentreY(), rad = d * 0.5f;
        g.setColour(juce::Colour(0xff0b0c0e)); g.fillEllipse(cx - rad - 3, cy - rad - 3, d + 6, d + 6);
        juce::ColourGradient gr(juce::Colour(0xff4a4d52), cx - rad * 0.4f, cy - rad * 0.6f, juce::Colour(0xff1d1f22), cx + rad * 0.6f, cy + rad * 0.8f, false);
        g.setGradientFill(gr); g.fillEllipse(cx - rad, cy - rad, d, d);
        g.setColour(juce::Colour(0xff6b6f75)); g.drawEllipse(cx - rad, cy - rad, d, d, 1.2f);
        float ang = a0 + pos * (a1 - a0);
        juce::Path p; p.addRoundedRectangle(-1.8f, -rad + 3.0f, 3.6f, rad * 0.45f, 1.5f);
        g.setColour(juce::Colour(0xffe8e2cf)); g.fillPath(p, juce::AffineTransform::rotation(ang).translated(cx, cy));
    }
};

class MainComponent : public juce::AudioAppComponent, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    void releaseResources() override {}
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Knob { juce::Slider s; juce::Label l; };
    void timerCallback() override;
    void makeKnob(Knob& k, const juce::String& name, double def);
    void pushParams();           // legge i controlli e li manda al motore
    void selectChannel(int ch);
    void saveState(); void loadState();
    void showAudioSettings();
    juce::File stateFile() const;

    AmpProcessor amp;
    KnobLook look;
    // per canale: gain, bass, mid, treble, volume
    double chan[5][5];
    int channel = 3; bool bright = false;
    Knob kGain, kBass, kMid, kTreble, kVol, kMaster, kPres, kDeep, kOut, kTrim;
    juce::TextButton chBtn[4]; juce::ToggleButton brightBtn{"Bright"};
    juce::ComboBox quality, inputSel; juce::TextButton audioBtn{"Impostazioni audio..."};
    juce::Label info, hint;
    float inLevel = 0, outLevel = 0; double sampleRateNow = 48000; int bufferNow = 0;
    bool loading = false;
    std::unique_ptr<juce::XmlElement> savedDeviceState; juce::HeapBlock<float> scratch; int scratchSize = 0;
    std::unique_ptr<juce::DialogWindow> dlg;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
