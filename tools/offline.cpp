// Offline renderer + speed test:  offline in.wav out.wav [channel gain bass mid treble volume master presence deep osf]
#include "../dsp/vh4dsp.hpp"
#include "wavio.hpp"
#include <chrono>
#include <cstdlib>
int main(int argc, char** argv) {
    if (argc < 3) { printf("uso: offline in.wav out.wav [canale gain bass mid treble vol master pres deep osf]\n"); return 1; }
    std::vector<double> x; int sr; if (!readWav(argv[1], x, sr)) { printf("file non letto\n"); return 1; }
    vh4::AmpParams p; int osf = 4;
    double* f[] = {&p.gain, &p.bass, &p.mid, &p.treble, &p.volume, &p.master, &p.presence, &p.deep};
    if (argc > 3) p.channel = atoi(argv[3]);
    for (int i = 0; i < 8 && argc > 4 + i; i++) *f[i] = atof(argv[4 + i]);
    if (argc > 12) osf = atoi(argv[12]);
    vh4::Amp amp; auto t0 = std::chrono::steady_clock::now();
    amp.prepare(sr, osf); amp.setParams(p);
    auto t1 = std::chrono::steady_clock::now();
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); i++) y[i] = amp.process(x[i]);
    auto t2 = std::chrono::steady_clock::now();
    double dtp = std::chrono::duration<double>(t1 - t0).count(), dtr = std::chrono::duration<double>(t2 - t1).count(), dur = x.size() / double(sr);
    printf("prepare %.2f s, rendering %.2f s per %.2f s di audio => %.2fx il tempo reale (osf %d)\n", dtp, dtr, dur, dtr / dur, osf);
    double pk = 0, e = 0; for (double v : y) { pk = std::max(pk, std::fabs(v)); e += v * v; }
    printf("picco %.3f  rms %.4f  B+ finale %.0f V\n", pk, std::sqrt(e / y.size()), amp.supply());
    writeWavF32(argv[2], y, sr); return 0;
}
