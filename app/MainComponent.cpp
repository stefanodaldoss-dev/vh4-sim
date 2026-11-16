#include "MainComponent.h"
using namespace juce;

static const char* chNames[4] = {"1  CLEAN", "2  CRUNCH", "3  LEAD", "4  LEAD"};

struct Preset { const char* name; int ch; double gain, bass, mid, treble, vol, master, pres, deep; };
// Valori sulla scala 0-10 delle manopole. I due "Jones" derivano da impostazioni riportate in forum (non verificate).
static const Preset kPresets[] = {
    {"Pulito (CH1)",                       1, 8.0, 5.0, 5.0, 6.0, 8.0, 5.0, 5.0, 5.0},
    {"Crunch (CH2)",                       2, 4.0, 5.5, 5.0, 6.0, 5.0, 5.0, 5.0, 6.0},
    {"Lead (CH3) - predefinito",           3, 6.0, 5.5, 5.0, 6.0, 5.0, 5.0, 5.0, 8.5},
    {"Lead 2 (CH4)",                       4, 4.0, 5.5, 5.0, 6.0, 5.0, 5.0, 5.0, 7.0},
    {"Stile Jones 2011 (da forum)",        3, 5.0, 8.0, 10.0, 9.0, 6.0, 4.0, 8.0, 7.0},
    {"Stile Jones classico (da forum)",    3, 6.0, 9.5, 10.0, 7.0, 5.0, 5.0, 7.5, 7.5},
};

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
    for (int c = 1; c <= 4; c++) for (int i = 0; i < 5; i++) { chan[c][i] = (c == 1) ? kPresets[0].gain : 0; }
    for (int c = 1; c <= 4; c++) { const Preset& pr = kPresets[c - 1]; chan[c][0] = pr.gain; chan[c][1] = pr.bass; chan[c][2] = pr.mid; chan[c][3] = pr.treble; chan[c][4] = pr.vol; }
    chan[3][0] = 6;
    makeKnob(kGain, "GAIN", 6); makeKnob(kBass, "BASS", 5.5); makeKnob(kMid, "MID", 5); makeKnob(kTreble, "TREBLE", 6); makeKnob(kVol, "VOLUME", 5);
    makeKnob(kMaster, "MASTER", 5); makeKnob(kPres, "PRESENCE", 5); makeKnob(kDeep, "DEEP", 8.5); makeKnob(kGate, "GATE", 4);
    makeKnob(kOut, "OUTPUT", 5); makeKnob(kTrim, "INPUT", 5);
    // OUTPUT e INPUT in dB: 5 = 0 dB, un punto = 3 dB
    for (auto* k : {&kOut, &kTrim}) { k->s.textFromValueFunction = [](double v) { return String((v - 5.0) * 3.0, 0) + " dB"; }; k->s.updateText(); }
    kGate.s.textFromValueFunction = [](double v) { return v < 0.05 ? String("off") : String(-90.0 + 5.0 * v, 0) + " dB"; }; kGate.s.updateText();
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
    presetSel.addItem("Preset...", 1000);
    for (int i = 0; i < (int)(sizeof(kPresets) / sizeof(kPresets[0])); i++) presetSel.addItem(kPresets[i].name, i + 1);
    presetSel.setSelectedId(1000, dontSendNotification);
    presetSel.onChange = [this] { int id = presetSel.getSelectedId(); if (id >= 1 && id < 1000) applyPreset(id - 1); };
    addAndMakeVisible(presetSel);
    cabSel.addItem("Cabina integrata", 1); cabSel.addItem("Cabina da file IR", 2); cabSel.setSelectedId(1, dontSendNotification);
    cabSel.onChange = [this] { irActive = (cabSel.getSelectedId() == 2 && irLoaded); pushParams(); }; addAndMakeVisible(cabSel);
    irBtn.onClick = [this] {
        chooser = std::make_unique<FileChooser>("Scegli un file IR della cassa (wav)", File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this](const FileChooser& fc) {
            auto f = fc.getResult(); if (f.existsAsFile()) { loadIR(f); cabSel.setSelectedId(2, dontSendNotification); irActive = irLoaded; pushParams(); }
        });
    };
    addAndMakeVisible(irBtn);
    irName.setFont(Font(FontOptions(12.5f))); irName.setColour(Label::textColourId, Colour(0xffb8bcc2)); addAndMakeVisible(irName);
    inputSel.addItem("Ingresso 1", 1); inputSel.addItem("Ingresso 2", 2); inputSel.setSelectedId(1, dontSendNotification); addAndMakeVisible(inputSel);
    audioBtn.onClick = [this] { showAudioSettings(); }; addAndMakeVisible(audioBtn);
    info.setFont(Font(FontOptions(13.0f))); info.setColour(Label::textColourId, Colour(0xffb8bcc2)); info.setJustificationType(Justification::centredLeft); addAndMakeVisible(info);
    hint.setText("Regola il guadagno della scheda (o INPUT) in modo che i colpi forti stiano a circa -12 dB sull'indicatore d'ingresso. Tieni il volume basso all'inizio.", dontSendNotification);
    hint.setFont(Font(FontOptions(12.5f))); hint.setColour(Label::textColourId, Colour(0xff8a8e94)); addAndMakeVisible(hint);

    loadState();
    selectChannel(channel);
    setSize(960, 610);
    // 2 ingressi, 2 uscite; lo stato salvato del dispositivo viene ripristinato da loadState()
    setAudioChannels(2, 2, savedDeviceState.get());
    // l'IR si carica dopo l'avvio dell'audio (la convoluzione deve essere gia' preparata)
    if (savedIr.existsAsFile()) { loadIR(savedIr); cabSel.setSelectedId(savedCab == 2 ? 2 : 1, dontSendNotification); irActive = (cabSel.getSelectedId() == 2); pushParams(); }
    startTimerHz(10);
}

MainComponent::~MainComponent() {
    saveState(); stopTimer(); shutdownAudio();
    for (auto* k : {&kGain, &kBass, &kMid, &kTreble, &kVol, &kMaster, &kPres, &kDeep, &kGate, &kOut, &kTrim}) k->s.setLookAndFeel(nullptr);
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

void MainComponent::applyPreset(int idx) {
    const Preset& p = kPresets[idx];
    loading = true;
    chan[p.ch][0] = p.gain; chan[p.ch][1] = p.bass; chan[p.ch][2] = p.mid; chan[p.ch][3] = p.treble; chan[p.ch][4] = p.vol;
    kMaster.s.setValue(p.master, dontSendNotification); kPres.s.setValue(p.pres, dontSendNotification); kDeep.s.setValue(p.deep, dontSendNotification);
    loading = false;
    selectChannel(p.ch);
}

void MainComponent::loadIR(const File& f) {
    conv.loadImpulseResponse(f, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::yes, 0, dsp::Convolution::Normalise::yes);
    irFile = f; irLoaded = true; irName.setText(f.getFileName(), dontSendNotification);
}

void MainComponent::pushParams() {
    chan[channel][0] = kGain.s.getValue(); chan[channel][1] = kBass.s.getValue(); chan[channel][2] = kMid.s.getValue();
    chan[channel][3] = kTreble.s.getValue(); chan[channel][4] = kVol.s.getValue();
    vh4::AmpParams p; p.channel = channel;
    p.gain = chan[channel][0] / 10.0; p.bass = chan[channel][1] / 10.0; p.mid = chan[channel][2] / 10.0; p.treble = chan[channel][3] / 10.0; p.volume = chan[channel][4] / 10.0;
    p.master = kMaster.s.getValue() / 10.0; p.presence = kPres.s.getValue() / 10.0; p.deep = kDeep.s.getValue() / 10.0;
    p.gate = (kGate.s.getValue() < 0.05) ? -100.0 : (-90.0 + 5.0 * kGate.s.getValue());
    p.cabinet = !(cabSel.getSelectedId() == 2 && irLoaded);
    p.bright = bright; p.inputTrimDb = (kTrim.s.getValue() - 5.0) * 3.0; p.outputDb = (kOut.s.getValue() - 5.0) * 3.0;
    amp.setParams(p);
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    sampleRateNow = sampleRate; bufferNow = samplesPerBlockExpected;
    scratchSize = jmax(samplesPerBlockExpected, 4096); scratch.malloc((size_t)scratchSize);
    amp.prepare(sampleRate, quality.getSelectedId() == 1 ? 4 : 2);
    maxBlock = jmax(samplesPerBlockExpected, 4096);
    conv.prepare(dsp::ProcessSpec{sampleRate, (uint32)maxBlock, 1});
}

void MainComponent::getNextAudioBlock(const AudioSourceChannelInfo& b) {
    ScopedNoDenormals nd;
    auto* buf = b.buffer; int n = b.numSamples;
    if (n > scratchSize) { scratch.malloc((size_t)n); scratchSize = n; }      // di norma non succede: gia' allocato in prepareToPlay
    int inCh = juce::jmin(inputSel.getSelectedId() - 1, buf->getNumChannels() - 1);
    amp.process(buf->getReadPointer(inCh, b.startSample), scratch.get(), n);
    if (irActive.load()) {
        for (int off = 0; off < n; off += maxBlock) {
            float* ptrs[1] = {scratch.get() + off};
            dsp::AudioBlock<float> blk(ptrs, 1, (size_t)jmin(maxBlock, n - off));
            conv.process(dsp::ProcessContextReplacing<float>(blk));
        }
        for (int i = 0; i < n; i++) scratch[i] = jlimit(-1.0f, 1.0f, scratch[i]);
    }
    for (int c = 0; c < buf->getNumChannels(); c++) buf->copyFrom(c, b.startSample, scratch.get(), n);
}

void MainComponent::timerCallback() {
    float pi = amp.inPeak.exchange(0), po = amp.outPeak.exchange(0);
    inLevel = jmax(pi, inLevel * 0.85f); outLevel = jmax(po, outLevel * 0.85f);
    inHold = jmax(pi, inHold * 0.95f); outHold = jmax(po, outHold * 0.95f);
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
    x.setAttribute("out", kOut.s.getValue()); x.setAttribute("trim", kTrim.s.getValue()); x.setAttribute("gate", kGate.s.getValue());
    x.setAttribute("cab", cabSel.getSelectedId()); x.setAttribute("irfile", irFile.getFullPathName());
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
        kGate.s.setValue(x->getDoubleAttribute("gate", 4), dontSendNotification);
        savedIr = File(x->getStringAttribute("irfile")); savedCab = x->getIntAttribute("cab", 1);
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
    auto db = [](float v) { return String(Decibels::gainToDecibels(v, -90.0f), 1) + " dB"; };
    bar(inLevel, 78, "ingresso (picco " + db(inHold) + ")"); bar(outLevel, 112, "uscita (picco " + db(outHold) + ")");
}

void MainComponent::resized() {
    presetSel.setBounds(230, 22, 260, 28);
    chBtn[0].setBounds(24, 100, 120, 34); chBtn[1].setBounds(150, 100, 120, 34); chBtn[2].setBounds(276, 100, 120, 34); chBtn[3].setBounds(402, 100, 120, 34);
    brightBtn.setBounds(540, 104, 90, 26);
    quality.setBounds(700, 20, 170, 28); inputSel.setBounds(700, 50, 110, 22); audioBtn.setBounds(815, 50, 125, 22);
    auto place = [&](Knob& k, int cx, int top) { k.l.setBounds(cx - 55, top, 110, 20); k.s.setBounds(cx - 55, top + 20, 110, 130); };
    int xs[5] = {110, 290, 470, 650, 830};
    place(kGain, xs[0], 160); place(kBass, xs[1], 160); place(kMid, xs[2], 160); place(kTreble, xs[3], 160); place(kVol, xs[4], 160);
    int ys[6] = {100, 250, 400, 550, 700, 850};
    place(kMaster, ys[0], 372); place(kPres, ys[1], 372); place(kDeep, ys[2], 372); place(kGate, ys[3], 372); place(kTrim, ys[4], 372); place(kOut, ys[5], 372);
    cabSel.setBounds(24, 524, 200, 26); irBtn.setBounds(232, 524, 160, 26); irName.setBounds(400, 524, 540, 26);
    info.setBounds(24, 560, 912, 18); hint.setBounds(24, 580, 912, 18);
}
