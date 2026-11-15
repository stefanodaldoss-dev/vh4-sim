#include "MainComponent.h"
using namespace juce;

static const char* chNames[4] = {"1  CLEAN", "2  CRUNCH", "3  LEAD", "4  LEAD"};

void MainComponent::makeKnob(Knob& k, const String& name, double def) {
    k.s.setSliderStyle(Slider::RotaryVerticalDrag);
    k.s.setTextBoxStyle(Slider::TextBoxBelow, false, 56, 18);
    k.s.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    k.s.setRange(0.0, 10.0, 0.1); k.s.setValue(def, dontSendNotification); k.s.setDoubleClickReturnValue(true, def);
    k.s.setLookAndFeel(&look); k.s.setColour(Slider::textBoxTextColourId, Colour(0xffe8e2cf)); k.s.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    k.s.onValueChange = [this] { if (!loading) pushParams(); };
    k.l.setText(name, dontSendNotification); k.l.setJustificationType(Justification::centred);
    k.l.setFont(Font(FontOptions(14.0f, Font::bold))); k.l.setColour(Label::textColourId, Colour(0xffd9b44a));
    addAndMakeVisible(k.s); addAndMakeVisible(k.l);
}

MainComponent::MainComponent() {
    for (int c = 1; c <= 4; c++) { double d[5] = {6, 5.5, 5, 6, 5}; for (int i = 0; i < 5; i++) chan[c][i] = d[i]; }
    chan[1][0] = 3; chan[2][0] = 5;
    makeKnob(kGain, "GAIN", 6); makeKnob(kBass, "BASS", 5.5); makeKnob(kMid, "MID", 5); makeKnob(kTreble, "TREBLE", 6); makeKnob(kVol, "VOLUME", 5);
    makeKnob(kMaster, "MASTER", 5); makeKnob(kPres, "PRESENCE", 5); makeKnob(kDeep, "DEEP", 8.5);
    makeKnob(kOut, "OUTPUT", 5); makeKnob(kTrim, "INPUT", 5);
    // OUTPUT e INPUT in dB: 5 = 0 dB, un punto = 3 dB
    for (auto* k : {&kOut, &kTrim}) { k->s.textFromValueFunction = [](double v) { return String((v - 5.0) * 3.0, 0) + " dB"; }; k->s.updateText(); }
    for (int i = 0; i < 4; i++) {
        chBtn[i].setButtonText(chNames[i]); chBtn[i].setClickingTogglesState(true); chBtn[i].setRadioGroupId(7);
        chBtn[i].setColour(TextButton::buttonOnColourId, Colour(0xffd9b44a)); chBtn[i].setColour(TextButton::textColourOnId, Colours::black);
        chBtn[i].onClick = [this, i] { if (chBtn[i].getToggleState()) selectChannel(i + 1); };
        addAndMakeVisible(chBtn[i]);
    }
    brightBtn.onClick = [this] { bright = brightBtn.getToggleState(); pushParams(); }; addAndMakeVisible(brightBtn);
    quality.addItem("Alta qualita' (4x)", 1); quality.addItem("Leggera (2x)", 2); quality.setSelectedId(1, dontSendNotification);
    quality.onChange = [this] { amp.prepare(sampleRateNow, quality.getSelectedId() == 1 ? 4 : 2); };
    addAndMakeVisible(quality);
    inputSel.addItem("Ingresso 1", 1); inputSel.addItem("Ingresso 2", 2); inputSel.setSelectedId(1, dontSendNotification); addAndMakeVisible(inputSel);
    audioBtn.onClick = [this] { showAudioSettings(); }; addAndMakeVisible(audioBtn);
    info.setFont(Font(FontOptions(13.0f))); info.setColour(Label::textColourId, Colour(0xffb8bcc2)); info.setJustificationType(Justification::centredLeft); addAndMakeVisible(info);
    hint.setText("Collega la chitarra all'ingresso 1 della scheda audio. Usa cuffie o monitor: tieni il volume basso all'inizio.", dontSendNotification);
    hint.setFont(Font(FontOptions(12.5f))); hint.setColour(Label::textColourId, Colour(0xff8a8e94)); addAndMakeVisible(hint);

    loadState();
    selectChannel(channel);
    setSize(960, 560);
    // 2 ingressi, 2 uscite; lo stato salvato del dispositivo viene ripristinato da loadState()
    setAudioChannels(2, 2, savedDeviceState.get());
    startTimerHz(10);
}

MainComponent::~MainComponent() {
    saveState(); stopTimer(); shutdownAudio();
    for (auto* k : {&kGain, &kBass, &kMid, &kTreble, &kVol, &kMaster, &kPres, &kDeep, &kOut, &kTrim}) k->s.setLookAndFeel(nullptr);
}

void MainComponent::selectChannel(int ch) {
    loading = true;
    channel = ch;
    for (int i = 0; i < 4; i++) chBtn[i].setToggleState(i + 1 == ch, dontSendNotification);
    kGain.s.setValue(chan[ch][0], dontSendNotification); kBass.s.setValue(chan[ch][1], dontSendNotification); kMid.s.setValue(chan[ch][2], dontSendNotification);
    kTreble.s.setValue(chan[ch][3], dontSendNotification); kVol.s.setValue(chan[ch][4], dontSendNotification);
    brightBtn.setVisible(ch == 1); loading = false;
    pushParams(); repaint();
}

void MainComponent::pushParams() {
    chan[channel][0] = kGain.s.getValue(); chan[channel][1] = kBass.s.getValue(); chan[channel][2] = kMid.s.getValue();
    chan[channel][3] = kTreble.s.getValue(); chan[channel][4] = kVol.s.getValue();
    vh4::AmpParams p; p.channel = channel;
    p.gain = chan[channel][0] / 10.0; p.bass = chan[channel][1] / 10.0; p.mid = chan[channel][2] / 10.0; p.treble = chan[channel][3] / 10.0; p.volume = chan[channel][4] / 10.0;
    p.master = kMaster.s.getValue() / 10.0; p.presence = kPres.s.getValue() / 10.0; p.deep = kDeep.s.getValue() / 10.0;
    p.bright = bright; p.inputTrimDb = (kTrim.s.getValue() - 5.0) * 3.0; p.outputDb = (kOut.s.getValue() - 5.0) * 3.0;
    amp.setParams(p);
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    sampleRateNow = sampleRate; bufferNow = samplesPerBlockExpected;
    scratchSize = jmax(samplesPerBlockExpected, 4096); scratch.malloc((size_t)scratchSize);
    amp.prepare(sampleRate, quality.getSelectedId() == 1 ? 4 : 2);
}

void MainComponent::getNextAudioBlock(const AudioSourceChannelInfo& b) {
    ScopedNoDenormals nd;
    auto* buf = b.buffer; int n = b.numSamples;
    if (n > scratchSize) { scratch.malloc((size_t)n); scratchSize = n; }      // di norma non succede: gia' allocato in prepareToPlay
    int inCh = juce::jmin(inputSel.getSelectedId() - 1, buf->getNumChannels() - 1);
    amp.process(buf->getReadPointer(inCh, b.startSample), scratch.get(), n);
    for (int c = 0; c < buf->getNumChannels(); c++) buf->copyFrom(c, b.startSample, scratch.get(), n);
}

void MainComponent::timerCallback() {
    float pi = amp.inPeak.exchange(0), po = amp.outPeak.exchange(0);
    inLevel = jmax(pi, inLevel * 0.85f); outLevel = jmax(po, outLevel * 0.85f);
    String s;
    if (auto* d = deviceManager.getCurrentAudioDevice()) {
        double sr = d->getCurrentSampleRate(); int buf = d->getCurrentBufferSizeSamples();
        double lat = (d->getInputLatencyInSamples() + d->getOutputLatencyInSamples() + buf) / sr * 1000.0 + amp.dspLatencySeconds() * 1000.0;
        s = d->getName() + "  |  " + String((int)sr) + " Hz, buffer " + String(buf) + "  |  latenza stimata ~" + String(lat, 1) + " ms  |  CPU "
            + String((int)(deviceManager.getCpuUsage() * 100)) + "%";
    } else s = "Nessun dispositivo audio: apri 'Impostazioni audio'";
    info.setText(s, dontSendNotification); repaint();
}

void MainComponent::showAudioSettings() {
    auto* sel = new AudioDeviceSelectorComponent(deviceManager, 1, 2, 2, 2, false, false, true, false);
    sel->setSize(560, 440);
    DialogWindow::LaunchOptions o; o.content.setOwned(sel); o.dialogTitle = "Impostazioni audio";
    o.dialogBackgroundColour = Colour(0xff15171a); o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = false;
    o.launchAsync();
}

File MainComponent::stateFile() const {
    return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("VH4Sim").getChildFile("impostazioni.xml");
}

void MainComponent::saveState() {
    XmlElement x("VH4Sim");
    x.setAttribute("channel", channel); x.setAttribute("bright", bright); x.setAttribute("quality", quality.getSelectedId()); x.setAttribute("input", inputSel.getSelectedId());
    x.setAttribute("master", kMaster.s.getValue()); x.setAttribute("pres", kPres.s.getValue()); x.setAttribute("deep", kDeep.s.getValue());
    x.setAttribute("out", kOut.s.getValue()); x.setAttribute("trim", kTrim.s.getValue());
    for (int c = 1; c <= 4; c++) for (int i = 0; i < 5; i++) x.setAttribute("c" + String(c) + "_" + String(i), chan[c][i]);
    if (auto dev = deviceManager.createStateXml()) x.addChildElement(dev.release());
    auto f = stateFile(); f.getParentDirectory().createDirectory(); x.writeTo(f);
}

void MainComponent::loadState() {
    loading = true;
    auto f = stateFile();
    if (auto x = parseXML(f)) {
        channel = jlimit(1, 4, x->getIntAttribute("channel", 3)); bright = x->getBoolAttribute("bright", false); brightBtn.setToggleState(bright, dontSendNotification);
        quality.setSelectedId(x->getIntAttribute("quality", 1), dontSendNotification); inputSel.setSelectedId(x->getIntAttribute("input", 1), dontSendNotification);
        kMaster.s.setValue(x->getDoubleAttribute("master", 5), dontSendNotification); kPres.s.setValue(x->getDoubleAttribute("pres", 5), dontSendNotification);
        kDeep.s.setValue(x->getDoubleAttribute("deep", 8.5), dontSendNotification);
        kOut.s.setValue(x->getDoubleAttribute("out", 5), dontSendNotification); kTrim.s.setValue(x->getDoubleAttribute("trim", 5), dontSendNotification);
        for (int c = 1; c <= 4; c++) for (int i = 0; i < 5; i++) chan[c][i] = x->getDoubleAttribute("c" + String(c) + "_" + String(i), chan[c][i]);
        if (auto* dev = x->getChildByName("DEVICESETUP")) savedDeviceState.reset(new XmlElement(*dev));
    }
    loading = false;
}

void MainComponent::paint(Graphics& g) {
    g.fillAll(Colour(0xff15171a));
    auto r = getLocalBounds().toFloat();
    g.setGradientFill(ColourGradient(Colour(0xff25282c), 0, 0, Colour(0xff101214), 0, r.getHeight(), false)); g.fillRect(r);
    g.setColour(Colour(0xffe8e2cf)); g.setFont(Font(FontOptions(30.0f, Font::bold))); g.drawText("VH4 SIM", 24, 14, 300, 40, Justification::centredLeft);
    g.setColour(Colour(0xffd9b44a)); g.fillRect(24, 56, 120, 3);
    // pannelli
    g.setColour(Colour(0xff1c1f22)); g.fillRoundedRectangle(20.0f, 150.0f, 920.0f, 200.0f, 8.0f); g.fillRoundedRectangle(20.0f, 365.0f, 920.0f, 150.0f, 8.0f);
    g.setColour(Colour(0xff2d3035)); g.drawRoundedRectangle(20.0f, 150.0f, 920.0f, 200.0f, 8.0f, 1.0f); g.drawRoundedRectangle(20.0f, 365.0f, 920.0f, 150.0f, 8.0f, 1.0f);
    // indicatori di livello
    auto bar = [&](float lev, int y, const String& t) {
        float db = Decibels::gainToDecibels(lev, -60.0f); float f = jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour(Colour(0xff0b0c0e)); g.fillRect(700, y, 150, 10);
        g.setColour(db > -1 ? Colours::red : (db > -12 ? Colour(0xffd9b44a) : Colour(0xff58c66b))); g.fillRect(700.0f, (float)y, 150.0f * f, 10.0f);
        g.setColour(Colour(0xffb8bcc2)); g.setFont(12.0f); g.drawText(t, 700, y + 12, 150, 16, Justification::centredLeft);
    };
    bar(inLevel, 78, "livello ingresso"); bar(outLevel, 112, "livello uscita");
}

void MainComponent::resized() {
    chBtn[0].setBounds(24, 100, 120, 34); chBtn[1].setBounds(150, 100, 120, 34); chBtn[2].setBounds(276, 100, 120, 34); chBtn[3].setBounds(402, 100, 120, 34);
    brightBtn.setBounds(540, 104, 90, 26);
    quality.setBounds(700, 20, 170, 28); inputSel.setBounds(700, 50, 110, 22); audioBtn.setBounds(815, 50, 125, 22);
    auto place = [&](Knob& k, int cx, int top) { k.l.setBounds(cx - 55, top, 110, 20); k.s.setBounds(cx - 55, top + 20, 110, 130); };
    int xs[5] = {110, 290, 470, 650, 830};
    place(kGain, xs[0], 160); place(kBass, xs[1], 160); place(kMid, xs[2], 160); place(kTreble, xs[3], 160); place(kVol, xs[4], 160);
    int ys[5] = {110, 290, 470, 650, 830};
    place(kMaster, ys[0], 372); place(kPres, ys[1], 372); place(kDeep, ys[2], 372); place(kTrim, ys[3], 372); place(kOut, ys[4], 372);
    info.setBounds(24, 520, 912, 18); hint.setBounds(24, 538, 912, 18);
}
