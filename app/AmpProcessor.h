// Collega il motore DSP al thread audio senza bloccarlo: i parametri e il motore nuovo
// vengono preparati nel thread dell'interfaccia e "consegnati" con un mutex che il thread audio
// prova soltanto (try_lock), quindi non aspetta mai.
#pragma once
#include "../dsp/vh4dsp.hpp"
#include <atomic>
#include <mutex>

class AmpProcessor {
public:
    // thread dell'interfaccia: (ri)crea il motore con la frequenza e l'oversampling dati
    void prepare(double sampleRate, int oversampling) {
        sr = sampleRate; osf = oversampling;
        auto a = std::make_unique<vh4::Amp>(); a->prepare(sr, osf);
        auto c = a->makeConfig(params);
        std::lock_guard<std::mutex> g(m);
        if (!active) { a->applyConfig(*c); active = std::move(a); activeCfg = std::move(c); }
        else { pendingAmp = std::move(a); pendingCfg = std::move(c); hasAmp = true; hasCfg = true; }
    }
    // thread dell'interfaccia
    void setParams(const vh4::AmpParams& p) {
        params = p;
        if (sr <= 0) return;
        vh4::Amp* any;
        { std::lock_guard<std::mutex> g(m); any = active ? active.get() : nullptr; }
        if (!any) return;
        // la configurazione si costruisce senza toccare lo stato audio (makeConfig e' const)
        auto c = any->makeConfig(p);
        std::lock_guard<std::mutex> g(m);
        pendingCfg = std::move(c); hasCfg = true;
    }
    // thread audio
    void process(const float* in, float* out, int n) {
        if (m.try_lock()) {
            if (hasAmp) { std::swap(active, pendingAmp); hasAmp = false; }      // il vecchio motore viene liberato dal thread dell'interfaccia
            if (hasCfg) { std::swap(activeCfg, pendingCfg); active->applyConfig(*activeCfg); hasCfg = false; }
            m.unlock();
        }
        if (!active) { for (int i = 0; i < n; i++) out[i] = 0; return; }
        float pi = 0, po = 0;
        for (int i = 0; i < n; i++) {
            float x = in[i]; pi = std::max(pi, std::fabs(x));
            double y = active->process(x);
            float yf = (float)std::max(-1.0, std::min(1.0, y)); po = std::max(po, std::fabs(yf)); out[i] = yf;
        }
        inPeak.store(std::max(inPeak.load(), pi)); outPeak.store(std::max(outPeak.load(), po));
    }
    double dspLatencySeconds() const { return active ? active->latencySeconds() : 0.0; }
    std::atomic<float> inPeak{0}, outPeak{0};
private:
    std::mutex m;
    std::unique_ptr<vh4::Amp> active, pendingAmp;
    std::unique_ptr<vh4::AmpConfig> activeCfg, pendingCfg;
    bool hasAmp = false, hasCfg = false;
    vh4::AmpParams params; double sr = 0; int osf = 4;
};
