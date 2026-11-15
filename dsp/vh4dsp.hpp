// VH4 amp simulator - DSP core (header only, no dependencies).
// Preamp (4 channels) + power amp + cabinet. All internal math in double.
#pragma once
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <memory>

namespace vh4 {

static const double kPi = 3.14159265358979323846;
static const double kB  = 367.0;          // preamp supply
inline double taperLog(double r) { return (std::pow(100.0, r) - 1.0) / 99.0; }
inline double clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }

// ------------------------------------------------------------------ fast math (smooth, ~1e-9 relative error)
namespace fm {
inline double exp(double x) {
    if (x > 700) x = 700; if (x < -700) return 0.0;
    double nn = std::floor(x * 1.4426950408889634 + 0.5);
    double r = (x - nn * 0.693145751953125) - nn * 1.42860682030941723212e-6;
    double p = 1.0 + r * (1.0 + r * (0.5 + r * (1.0/6 + r * (1.0/24 + r * (1.0/120 + r * (1.0/720 + r * (1.0/5040 + r * (1.0/40320))))))));
    union { double d; long long i; } u; u.i = (long long)(((int)nn) + 1023) << 52; return p * u.d;
}
inline double log(double x) {   // x > 0
    union { double d; long long i; } u; u.d = x;
    int e = (int)((u.i >> 52) & 0x7ff) - 1023; u.i = (u.i & 0x000fffffffffffffLL) | 0x3ff0000000000000LL;
    double m = u.d; if (m > 1.4142135623730951) { m *= 0.5; e++; }
    double f = m - 1.0, s = f / (2.0 + f), s2 = s * s;
    return e * 0.6931471805599453 + 2.0 * s * (1.0 + s2 * (1.0/3 + s2 * (1.0/5 + s2 * (1.0/7 + s2 * (1.0/9 + s2 * (1.0/11))))));
}
inline double log1p(double x) { return log(1.0 + x); }   // only used for x >= 0 here
inline double pow(double x, double y) { return exp(y * log(x)); }
}
// ------------------------------------------------------------------ triode (Koren 12AX7)
namespace tri {
constexpr double MU = 100.0, EX = 1.4, KG1 = 1060.0, KP = 600.0, KVB = 300.0, KGI = 3.0e-4;
inline double sp(double x, double vs) {
    double t = x / vs;
    if (t > 30) return x;
    if (t < -30) return vs * fm::exp(t);
    return vs * fm::log1p(fm::exp(t));
}
inline double ig(double v)  { double s = sp(v, 0.05); return KGI * s * std::sqrt(s); }
inline double dig(double v) { double s = sp(v, 0.05); double sg = 1.0 / (1.0 + fm::exp(-v / 0.05)); return KGI * 1.5 * std::sqrt(s) * sg; }
inline double Ipl_d(double vpk, double vgk, double* dvp, double* dvg) {
    if (vpk < 1e-9) { *dvp = 0; *dvg = 0; return 0; }
    double q = KVB + vpk * vpk, sq = std::sqrt(q);
    double a = KP * (1.0 / MU + vgk / sq), L, sg;
    if (a > 30) { L = a; sg = 1.0; }
    else if (a < -30) { double e = fm::exp(a); L = e; sg = e; }
    else { double e = fm::exp(a); L = fm::log1p(e); sg = e / (1.0 + e); }
    double e1 = vpk / KP * L;
    if (e1 <= 1e-30) { *dvp = 0; *dvg = 0; return 0; }
    double p = fm::pow(e1, EX - 1.0), I = 2.0 * p * e1 / KG1, dI = 2.0 * EX * p / KG1;
    double da = -KP * vgk * vpk / (q * sq);
    *dvp = dI * (L / KP + vpk / KP * sg * da);
    *dvg = dI * (vpk * sg / sq);
    return I;
}
inline double solveVg(double vin, double vk, double Rs, double g0, double* dvk) {
    if (Rs <= 0 || vin - vk < -0.3) { *dvk = 0; return vin; }
    double lo = ((vin < vk) ? vin : vk) - 1.0, hi = vin, g = g0;
    if (g < lo || g > hi) g = 0.5 * (lo + hi);
    for (int j = 0; j < 40; j++) {
        double v = g - vk, r = g - vin + Rs * ig(v);
        if (r > 0) hi = g; else lo = g;
        double d = 1.0 + Rs * dig(v), gn = g - r / d;
        if (gn <= lo || gn >= hi) gn = 0.5 * (lo + hi);
        if (std::fabs(gn - g) < 1e-10) { g = gn; break; }
        g = gn;
    }
    double dg = Rs * dig(g - vk); *dvk = dg / (1.0 + dg);
    return g;
}
} // namespace tri

// One triode stage with cathode RC, coupling cap and grid current.
// mode 0: voltage across Rout from coupling current; 1: absolute plate voltage; 2: cathode voltage.
// Grid-current solve as a table: v = vg - vk satisfies v + Rs*ig(v) = u (u = vin - vk). u(v) is explicit and monotonic.
struct GridTab {
    static constexpr int N = 1600;
    double u[N], v[N], s[N]; double Rs = -1; int idx = 0;
    void build(double Rs_) {
        if (Rs_ == Rs) return; Rs = Rs_; if (Rs <= 0) return;
        for (int i = 0; i < N; i++) {
            double vv = -1.0 + 9.0 * i / (N - 1);
            v[i] = vv; u[i] = vv + Rs * tri::ig(vv); s[i] = 1.0 / (1.0 + Rs * tri::dig(vv));
        }
        idx = 0;
    }
    // returns v for a given u, and the slope dv/du
    inline double eval(double uu, double* sl) {
        if (uu >= u[N - 1]) { *sl = s[N - 1]; return v[N - 1] + s[N - 1] * (uu - u[N - 1]); }
        if (uu <= u[0]) { *sl = 1.0; return uu; }
        int i = idx; if (i > N - 2) i = N - 2;
        while (i > 0 && u[i] > uu) i--;
        while (i < N - 2 && u[i + 1] <= uu) i++;
        idx = i;
        double h = u[i + 1] - u[i], t = (uu - u[i]) / h, t2 = t * t, t3 = t2 * t;
        double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
        double dh00 = 6 * t2 - 6 * t, dh10 = 3 * t2 - 4 * t + 1, dh01 = -6 * t2 + 6 * t, dh11 = 3 * t2 - 2 * t;
        *sl = (dh00 * v[i] + dh10 * h * s[i] + dh01 * v[i + 1] + dh11 * h * s[i + 1]) / h;
        return h00 * v[i] + h10 * h * s[i] + h01 * v[i + 1] + h11 * h * s[i + 1];
    }
};
#ifdef VH4_STATS
inline long long g_it[16], g_n[16], g_vg[16]; 
#endif
struct Stage {
    int id = 0;
    double B = kB, Rp = 120e3, Rk = 680, Ck = 0, Cc = 0, Rg = 1, Rout = 0, Rs = 0, fs = 192000;
    int mode = 1;
    double dt = 0, ak = 0, ac = 0, kc = 0, gk = 0, gp = 0, bp = 0, ipmax = 0;
    double vk0 = 0, vcap = 0, vg = 0, ip = 1e-3, ipp = 1e-3, vp = 0, vkn = 0;
    GridTab gt;
    inline double gridV(double vin, double vk, double* dvk) {
        double u = vin - vk;
        if (Rs <= 0 || u < -0.3) { *dvk = 0; return vin; }
        double sl, v = gt.eval(u, &sl); *dvk = 1.0 - sl; return vk + v;
    }
    void configure() {
        if (Rs > 0) gt.build(Rs);
        dt = 1.0 / fs;
        ak = (Ck > 0) ? Ck / dt : 0.0; ac = (Cc > 0) ? Cc / dt : 0.0;
        kc = (Cc > 0) ? ac / (ac * Rg + 1.0) : 0.0;
        gk = 1.0 / (ak + 1.0 / Rk); gp = 1.0 / (1.0 / Rp + kc); bp = B / Rp; ipmax = B / (Rp + Rk);
    }
    void set(double B_, double Rp_, double Rk_, double Ck_, double Cc_, double Rg_, double Rout_, double Rs_, double fs_, int mode_) {
        B = B_; Rp = Rp_; Rk = Rk_; Ck = Ck_; Cc = Cc_; Rg = Rg_; Rout = Rout_; Rs = Rs_; fs = fs_; mode = mode_; configure();
    }
    // change only the pot-dependent parts, keep the state
    void retune(double Rg_, double Rout_, double Rs_) { Rg = Rg_; Rout = Rout_; Rs = Rs_; configure(); }
    // steady state for a constant input voltage vin (DC)
    void settle(double vin) {
        double lo = 0, hi = ipmax;
        for (int j = 0; j < 200; j++) {
            double x = 0.5 * (lo + hi), vk = x * Rk, vpp = B - x * Rp, d1, d2;
            double F = x - tri::Ipl_d(vpp - vk, vin - vk, &d1, &d2);
            if (F > 0) hi = x; else lo = x;
        }
        double x = 0.5 * (lo + hi);
        ip = ipp = x; vk0 = vkn = x * Rk; vp = B - x * Rp; vcap = vp; vg = vin;
    }
    double step(double vin) {
#ifdef VH4_STATS
        g_n[id]++;
#endif
        double x, c1 = vk0 * ak, c2 = bp + kc * vcap, vk, vpp, vgg, lo = 0.0, hi = ipmax;
        x = 2.0 * ip - ipp; if (!(x > lo && x < hi)) x = ip; if (!(x > lo && x < hi)) x = 0.5 * (lo + hi);
        for (int it = 0; it < 60; it++) {
#ifdef VH4_STATS
            g_it[id]++;
#endif
            vk = (c1 + x) * gk; vpp = (c2 - x) * gp;
            double dgk; vgg = gridV(vin, vk, &dgk);
            double dvp, dvg; double I = tri::Ipl_d(vpp - vk, vgg - vk, &dvp, &dvg);
            double F = x - I;
            if (F > 0) hi = x; else lo = x;
            double dF = 1.0 - (dvp * (-gp - gk) + dvg * (dgk * gk - gk));
            double xn = x - F / dF;
            if (!(xn > lo && xn < hi)) xn = 0.5 * (lo + hi);
            double st = std::fabs(xn - x); x = xn;
            if (st < 1e-12 + 1e-10 * x) break;
        }
        vk = (c1 + x) * gk; vpp = (c2 - x) * gp; double dd; vgg = gridV(vin, vk, &dd);
        vk0 = vk; vg = vgg; vkn = vk; vp = vpp; ipp = ip; ip = x;
        double icur = kc * (vpp - vcap);
        if (Cc > 0) vcap += icur / ac;
        return (mode == 0) ? icur * Rout : ((mode == 1) ? vpp : vkn);
    }
};

// Memoryless function tabulated with cubic Hermite interpolation (for stages without any state).
struct StaticMap {
    int N = 0; double x0 = 0, dx = 1, inv = 1; std::vector<double> y, d;
    // builds y = f(x) on [x0, x1] with n points; f is called with increasing x
    template <class F> void build(double xa, double xb, int n, F f) {
        N = n; x0 = xa; dx = (xb - xa) / (n - 1); inv = 1.0 / dx; y.assign(n, 0.0); d.assign(n, 0.0);
        for (int i = 0; i < n; i++) y[i] = f(x0 + i * dx);
        for (int i = 0; i < n; i++) {
            int a = i > 0 ? i - 1 : 0, b = i < n - 1 ? i + 1 : n - 1; d[i] = (y[b] - y[a]) / ((b - a) * dx);
        }
    }
    inline double eval(double x) const {
        double t = (x - x0) * inv;
        if (t <= 0) return y[0] + d[0] * (x - x0);
        if (t >= N - 1) return y[N - 1] + d[N - 1] * (x - (x0 + (N - 1) * dx));
        int i = (int)t; double f = t - i, f2 = f * f, f3 = f2 * f;
        return (2 * f3 - 3 * f2 + 1) * y[i] + (f3 - 2 * f2 + f) * dx * d[i] + (-2 * f3 + 3 * f2) * y[i + 1] + (f3 - f2) * dx * d[i + 1];
    }
};

// ------------------------------------------------------------------ small dense linear algebra
constexpr int kMaxN = 12;
// solve A X = Bm (n x n, n x m), result in Bm
inline void solveMat(double A[kMaxN][kMaxN], double Bm[kMaxN][kMaxN], int n, int m) {
    for (int c = 0; c < n; c++) {
        int p = c; for (int r = c + 1; r < n; r++) if (std::fabs(A[r][c]) > std::fabs(A[p][c])) p = r;
        if (p != c) for (int k = 0; k < kMaxN; k++) { std::swap(A[p][k], A[c][k]); std::swap(Bm[p][k], Bm[c][k]); }
        double d = A[c][c];
        for (int r = c + 1; r < n; r++) {
            double f = A[r][c] / d; if (f == 0) continue;
            for (int k = c; k < n; k++) A[r][k] -= f * A[c][k];
            for (int k = 0; k < m; k++) Bm[r][k] -= f * Bm[c][k];
        }
    }
    for (int c = n - 1; c >= 0; c--)
        for (int k = 0; k < m; k++) {
            double s = Bm[c][k]; for (int j = c + 1; j < n; j++) s -= A[c][j] * Bm[j][k];
            Bm[c][k] = s / A[c][c];
        }
}

// Linear RC network simulated in time (trapezoid), v[n+1] = Phi v[n] + gam (u[n]+u[n+1]).
struct LinNet {
    int n = 0, node = 0;
    double G[kMaxN][kMaxN], C[kMaxN][kMaxN], b[kMaxN], Phi[kMaxN][kMaxN], gam[kMaxN], v[kMaxN], uprev = 0;
    void clear(int n_) { n = n_; std::memset(G, 0, sizeof G); std::memset(C, 0, sizeof C); std::memset(b, 0, sizeof b); }
    void R(int a, int bb, double r) { double g = 1.0 / r; G[a][a] += g; if (bb >= 0) { G[bb][bb] += g; G[a][bb] -= g; G[bb][a] -= g; } }
    void Cap(int a, int bb, double c) { C[a][a] += c; C[bb][bb] += c; C[a][bb] -= c; C[bb][a] -= c; }
    void reset() { std::memset(v, 0, sizeof v); uprev = 0; }
    void copyMatrices(const LinNet& o) { n = o.n; node = o.node; std::memcpy(Phi, o.Phi, sizeof Phi); std::memcpy(gam, o.gam, sizeof gam); }
    // keeps the state when only the component values change
    void discretize(double fs) {
        double dt = 1.0 / fs, A[kMaxN][kMaxN], M[kMaxN][kMaxN], g[kMaxN][kMaxN];
        std::memset(M, 0, sizeof M); std::memset(g, 0, sizeof g);
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
            double c = C[i][j] + (i == j ? 1e-12 : 0.0);   // 1 pF parasitic: damps the Nyquist mode of resistive-only nodes
            A[i][j] = c / dt + G[i][j] / 2; M[i][j] = c / dt - G[i][j] / 2;
        }
        double A2[kMaxN][kMaxN]; std::memcpy(A2, A, sizeof A);
        for (int i = 0; i < n; i++) g[i][0] = b[i];
        solveMat(A, M, n, n);                      // Phi = A^-1 M
        solveMat(A2, g, n, 1);                     // gam = A^-1 b / 2
        for (int i = 0; i < n; i++) { for (int j = 0; j < n; j++) Phi[i][j] = M[i][j]; gam[i] = g[i][0] / 2; }
    }
    inline double step(double u) {
        double s = u + uprev, w[kMaxN]; uprev = u;
        for (int r = 0; r < n; r++) { double a = gam[r] * s; const double* p = Phi[r]; for (int c = 0; c < n; c++) a += p[c] * v[c]; w[r] = a; }
        for (int r = 0; r < n; r++) v[r] = w[r];
        return v[node];
    }
};

// ------------------------------------------------------------------ preamp
struct PreampControls { int channel = 3; double gain = 0.6, bass = 0.55, mid = 0.5, treble = 0.6, vol = 0.5; bool bright = false; };

class Preamp {
public:
    // node indices of the input network
    enum { P, N1A, N1B, N1C, N1D, N1E, N2A, N2B, N3A, N3B, N4A, N4B, NN };
    // everything that depends on the knobs. Built off the audio thread, then applied (cheap).
    struct Config {
        int ch = 3; double volGain = 0.25, rh = 1e5;
        LinNet net, stack; StaticMap map4;
    };
    void prepare(double fs_) {
        fs = fs_;
        v1a.set(kB, 120e3, 2.2e3, 47e-6, 0, 1, 0, 10e3, fs, 1); v1a.settle(0.0); v1aDc = v1a.vp;
        // Thevenin resistance seen at the plate of V1a (R3 || rp)
        double h = 0.05, d1, d2, vk = v1a.vk0, vpk = v1a.vp - vk;
        double ia = tri::Ipl_d(vpk + h, -vk, &d1, &d2), ib = tri::Ipl_d(vpk - h, -vk, &d1, &d2);
        double rp = 1.0 / ((ia - ib) / (2 * h)); rth = rp * 120e3 / (rp + 120e3);
        v2b.set(kB, 120e3, 680, 1e-6, 4.7e-9, 1.47e6, 1e6, 200e3, fs, 0); v2b.settle(0);
        v2a.set(kB, 120e3, 680, 1e-6, 4.7e-9, 1e6, 1e6, 100e3, fs, 0); v2a.settle(0);
        v1b.set(kB, 120e3, 3.3e3, 1e-6, 2.2e-9, 690e3, 220e3, 100e3, fs, 0); v1b.settle(0);
        v2a4.set(kB, 120e3, 680, 1e-6, 4.7e-9, 1e6, 1e6, 197e3, fs, 0); v2a4.settle(0);
        v3a.set(kB, 120e3, 680, 2.2e-6, 0, 1, 0, 470e3, fs, 1); v3a.settle(0);
        // V3b (cathode follower, no memory): table of cathode voltage vs. V3a plate voltage
        Stage f; f.set(kB, 1.0, 120e3, 0, 0, 1, 0, 120e3, fs, 2); f.settle(v3a.vp); double dc = f.vkn;
        map3b.build(0.0, 380.0, 4096, [&](double x) { return f.step(x) - dc; });
        net.reset(); stack.reset();
        Config c0; buildConfig(PreampControls(), c0); apply(c0);
    }
    void buildConfig(const PreampControls& c, Config& o) const {
        int ch_ = c.channel;
        double wg = taperLog(c.gain), rh = std::max(wg, 0.002) * kRpot, others = taperLog(0.5), ro = std::max(others, 0.002) * kRpot;
        o.ch = ch_; o.rh = rh;
        LinNet& n = o.net; n.clear(NN);
        double rhc[5] = {0, ro, ro, ro, ro}; rhc[ch_] = rh;
        n.R(P, -1, rth);
        n.Cap(P, N1A, 220e-9);
        n.R(N1A, N1B, 470e3); n.Cap(N1A, N1B, 2.2e-9); n.R(N1B, N1C, 470e3); n.Cap(N1B, N1C, 220e-12);
        n.R(N1C, N1D, 470e3); n.Cap(N1C, N1D, 220e-12);
        n.R(N1B, -1, 220e3); n.Cap(N1A, N1E, 68e-12); n.R(N1E, N1D, 10e6);
        if (c.bright && ch_ == 1) n.R(N1E, N1D, 1.0);
        n.R(N1D, -1, rhc[1]); n.R(N1D, -1, 1e6);
        n.Cap(P, N2A, 10e-9); n.R(N2A, -1, 220e3); n.R(N2A, N2B, 3.3e6); n.Cap(N2A, N2B, 680e-12); n.R(N2B, -1, rhc[2]);
        n.Cap(P, N3A, 1e-9); n.R(N3A, N3B, 220e3); n.R(N3B, -1, rhc[3]); n.R(N3B, -1, 3.35e6);
        n.Cap(P, N4A, 1e-9); n.R(N4A, N4B, 220e3); n.R(N4B, -1, rhc[4]); n.R(N4B, -1, 220e3);
        n.b[P] = 1.0 / rth;
        n.node = (ch_ == 1) ? N1D : (ch_ == 2) ? N2B : (ch_ == 3) ? N3B : N4B;
        n.discretize(fs);
        double ft = clampd(c.treble, 0.002, 0.998), fb = clampd(taperLog(c.bass), 0.002, 0.998), fm_ = clampd(c.mid, 0.002, 0.998);
        LinNet& st = o.stack; st.clear(8);
        const double RT = 220e3, RB = 1e6, RM = 22e3;
        st.R(0, -1, 10e3); st.Cap(0, 1, 560e-12); st.R(0, 2, 39e3);
        st.R(1, 3, (1 - ft) * RT); st.R(3, 4, ft * RT); st.R(4, 5, (1 - fb) * RB); st.R(5, 6, fb * RB);
        st.R(6, 7, (1 - fm_) * RM); st.R(7, -1, fm_ * RM);
        st.Cap(2, 5, 22e-9); st.Cap(2, 7, 22e-9); st.R(3, -1, 940e3);
        st.b[0] = 1.0 / 10e3; st.node = 3; st.discretize(fs);
        double wv = taperLog(c.vol);
        static const double RMIX[5] = {0, 56e3, 680e3, 680e3, 680e3};
        o.volGain = wv * 470e3 / 940e3;
        // V4a + V4b: memoryless, tabulated
        Stage a, b; a.set(kB, 120e3, 680, 0, 0, 1, 0, RMIX[ch_] + wv * (1 - wv) * 470e3, fs, 1); a.settle(0);
        b.set(kB, 1.0, 120e3, 0, 0, 1, 0, 120e3, fs, 2); b.settle(a.vp); double dc = b.vkn;
        o.map4.build(-100.0, 100.0, 8001, [&](double x) { return b.step(a.step(x)) - dc; });
    }
    void apply(const Config& c) {
        cfg = &c; ch = c.ch; volGain = c.volGain;
        net.copyMatrices(c.net); stack.copyMatrices(c.stack);
        double rh = c.rh;
        if (ch == 2) v2b.retune(470e3 + 1e6, 1e6, std::min(rh, 200e3));
        if (ch == 3) v2a.retune(1e6, 1e6, 47e3 + rh * 220e3 / (rh + 220e3));
        if (ch == 4) { v1b.retune(690e3, 220e3, rh * 220e3 / (rh + 220e3)); v2a4.retune(1e6, 1e6, 47e3 + 150e3); }
    }
    void setControls(const PreampControls& c) { buildConfig(c, own); apply(own); }
    // x: volts at the internal rate. Returns the voltage at the output of V4b, before the FX send divider.
    inline double process(double x) {
        double y = v1a.step(x) - v1aDc;
        y = net.step(y);
        switch (ch) {
            case 2: y = v2b.step(y); break;
            case 3: y = v2a.step(y); break;
            case 4: y = v1b.step(y); y = v2a4.step(y); break;
            default: break;
        }
        y = v3a.step(y);
        y = map3b.eval(y);
        y = stack.step(y) * volGain;
        return cfg->map4.eval(y);
    }
    void tagIds() { v1a.id = 1; v2b.id = 2; v2a.id = 3; v1b.id = 4; v2a4.id = 5; v3a.id = 6; }
    double fs = 192000;
private:
    static constexpr double kRpot = 250e3;
    int ch = 3; double rth = 40e3, volGain = 0.25, v1aDc = 0;
    Stage v1a, v2b, v2a, v1b, v2a4, v3a;
    LinNet net, stack; StaticMap map3b; Config own; const Config* cfg = nullptr;
};

// ------------------------------------------------------------------ power amp
struct PowerAmpParams {
    double Bidle = 430, vg2fac = 0.97, Ibias = 0.035, Zaa = 2400, RL = 8, Rsup = 70, tau = 0.04, gpre = 15, betag = 0.083,
           fblp = 18000, presFc = 4000, deepFc = 115, presMax = 0.8, deepMax = 0.8, Cc = 22e-9, Rsrc = 35e3, Rleak = 220e3,
           Bpi = 330, Rppi = 82e3, Rkpi = 1000, kvb = 115;
};

class PowerAmp {
public:
    void prepare(double fs_, const PowerAmpParams& pp = PowerAmpParams()) {
        P = pp; fs = fs_; dt = 1.0 / fs;
        Rh = P.Zaa / 4.0; Lm = Rh / (2 * kPi * 38.0); kout = std::sqrt(P.RL / Rh);
        double lo = -300, hi = 0;
        for (int j = 0; j < 80; j++) { double m = 0.5 * (lo + hi); if (Ipent(P.Bidle, P.vg2fac * P.Bidle, m) > P.Ibias) hi = m; else lo = m; }
        vb = 0.5 * (lo + hi);
        pi.set(P.Bpi, P.Rppi, P.Rkpi, 0, 0, 1, 0, 0, fs, 1);
        for (int i = 0; i < 600; i++) vdc = pi.step(0.0);
        a = P.Cc / dt; kc = a / (1.0 + a * P.Rsrc);
        vcold1 = vcold2 = vdc - vb; vg1s = vg2s = vb;
        iidle = 4.0 * P.Ibias * 1.1; ifilt = iidle; im = 0; vd = 0; fbsig = 0; Beff = P.Bidle;
        kL = dt / Lm;
        afb = 1 - std::exp(-2 * kPi * P.fblp / fs); ap = 1 - std::exp(-2 * kPi * P.presFc / fs); ad = 1 - std::exp(-2 * kPi * P.deepFc / fs);
        lpfb = lpP = lpD = 0;
        for (int i = 0; i < int(0.125 * 192000 * fs / 192000); i++) process(0.0);   // warm-up
    }
    void setControls(double presence, double deep) { pres = presence; dp = deep; }
    // x: send voltage * master. returns speaker voltage (8 ohm)
    inline double process(double x) {
        double vpi = pi.step(x * P.gpre - fbsig);
        double s1 = 2.0 * vdc - vpi, s2 = vpi;
        double vg1 = gridSolve(vcold1, vg1s, s1), vgB = gridSolve(vcold2, vg2s, s2);
        vcold1 += kc * (s1 - vg1 - vcold1) / a; vcold2 += kc * (s2 - vgB - vcold2) / a;
        double vg2 = std::max(P.vg2fac * Beff, 1.0);
        // plate current = C * atan(vak/kvb); C is constant during the solve of vd
        double C1 = 2.0 * pentC(vg2, vg1), C2 = 2.0 * pentC(vg2, vgB), ik = 1.0 / P.kvb;
        double l = -Beff, h = Beff, xv = vd;
        for (int it = 0; it < 40; it++) {
            double ua = (Beff - xv) * ik, ub = (Beff + xv) * ik;
            double ia = ua > 0 ? C1 * std::atan(ua) : 0.0, ib = ub > 0 ? C2 * std::atan(ub) : 0.0;
            double f = ia - ib - xv * (1.0 / Rh + kL) - im;
            if (f > 0) l = xv; else h = xv;
            double d = -((ua > 0 ? C1 * ik / (1 + ua * ua) : 0.0) + (ub > 0 ? C2 * ik / (1 + ub * ub) : 0.0)) - 1.0 / Rh - kL;
            double xn = xv - f / d;
            if (xn <= l || xn >= h) xn = 0.5 * (l + h);
            double st = std::fabs(xn - xv); xv = xn;
            if (st < 1e-7) break;
        }
        vd = xv; im += kL * vd;
        double ua = (Beff - vd) * ik, ub = (Beff + vd) * ik;
        double i1 = ua > 0 ? C1 * std::atan(ua) : 0.0, i2 = ub > 0 ? C2 * std::atan(ub) : 0.0;
        double itot = (i1 + i2) * 1.1; ifilt += (itot - ifilt) * dt / P.tau;
        Beff = P.Bidle - P.Rsup * (ifilt - iidle);
        double vout = kout * vd;
        double v1 = (lpfb += afb * (vout - lpfb));
        lpP += ap * (v1 - lpP); double hpP = v1 - lpP; lpD += ad * (v1 - lpD);
        fbsig = P.betag * (v1 - pres * P.presMax * hpP - dp * P.deepMax * lpD);
        return vout;
    }
    double supply() const { return Beff; }
private:
    static constexpr double MU2 = 11.0, EX2 = 1.35, KG12 = 650.0, KP2 = 60.0, KGIP = 6.0e-4;
    double Ipent(double vak, double vg2, double vgk) const {
        if (vak < 0) vak = 0; if (vg2 < 1) vg2 = 1;
        double aa = KP2 * (1.0 / MU2 + vgk / vg2);
        double s = (aa > 30) ? aa : ((aa < -30) ? std::exp(aa) : std::log1p(std::exp(aa)));
        double e1 = vg2 / KP2 * s;
        return 2.0 * std::pow(e1, EX2) / KG12 * std::atan(vak / P.kvb);
    }
    double pentC(double vg2, double vgk) const {      // Ipent = pentC * atan(vak/kvb)
        double aa = KP2 * (1.0 / MU2 + vgk / vg2);
        double sft = (aa > 30) ? aa : ((aa < -30) ? fm::exp(aa) : fm::log1p(fm::exp(aa)));
        double e1 = vg2 / KP2 * sft;
        return e1 <= 1e-30 ? 0.0 : 2.0 * fm::pow(e1, EX2) / KG12;
    }
    static double igp(double v) { double s = tri::sp(v, 0.05); return KGIP * s * std::sqrt(s); }
    static double digp(double v) { double s = tri::sp(v, 0.05); double sg = 1.0 / (1.0 + std::exp(-v / 0.05)); return KGIP * 1.5 * std::sqrt(s) * sg; }
    double gridSolve(double vcold, double& vgs, double vs) const {
        double lin = (kc * (vs - vcold) + vb / P.Rleak) / (kc + 1.0 / P.Rleak);   // no grid current
        if (lin < -1.5) { vgs = lin; return lin; }
        double lo = -3000, hi = 3000, x = vgs;
        for (int j = 0; j < 80; j++) {
            double f = kc * (vs - x - vcold) - (x - vb) / P.Rleak - igp(x);
            if (f > 0) lo = x; else hi = x;
            double d = -kc - 1.0 / P.Rleak - digp(x);
            double xn = x - f / d;
            if (xn <= lo || xn >= hi) xn = 0.5 * (lo + hi);
            if (std::fabs(xn - x) < 1e-8) { x = xn; break; }
            x = xn;
        }
        vgs = x; return x;
    }
    double Fvd(double vdd, double B, double vg2, double vg1, double vgB) const {
        double i1 = 2.0 * Ipent(B - vdd, vg2, vg1), i2 = 2.0 * Ipent(B + vdd, vg2, vgB);
        return i1 - i2 - vdd / Rh - im - kL * vdd;
    }
    PowerAmpParams P; double fs = 192000, dt = 0, Rh = 600, Lm = 0, kout = 0, vb = 0, vdc = 0, a = 0, kc = 0;
    double vcold1 = 0, vcold2 = 0, vg1s = 0, vg2s = 0, iidle = 0, ifilt = 0, im = 0, vd = 0, fbsig = 0, Beff = 430, kL = 0;
    double afb = 0, ap = 0, ad = 0, lpfb = 0, lpP = 0, lpD = 0, pres = 0.5, dp = 0.85;
    Stage pi;
};

// ------------------------------------------------------------------ resampling (halfband FIR, polyphase-free simple version)
class HalfBand {
public:
    void design(int taps, double beta) {
        // Kaiser windowed sinc, cutoff = 1/4 of the high rate (halfband)
        n = taps | 1; h.assign(n, 0.0);
        int m = n / 2; double i0b = bessel0(beta);
        for (int i = 0; i < n; i++) {
            int k = i - m; double s = (k == 0) ? 0.5 : std::sin(kPi * k / 2.0) / (kPi * k);
            double r = double(k) / m; double w = bessel0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0b;
            h[i] = s * w;
        }
        // normalize DC gain of the full filter to 1
        double sum = 0; for (double v : h) sum += v; for (double& v : h) v /= sum;
        hist.assign(n, 0.0); pos = 0;
    }
    int delay() const { return n / 2; }   // in high-rate samples
    // push one high-rate sample
    inline double push(double x) {
        hist[pos] = x; double acc = 0; int idx = pos; int m = n / 2;
        acc += h[m] * hist[(pos - m + n) % n];
        for (int k = 1; k < m; k += 2) { acc += h[m - k] * (hist[(pos - m + k + n) % n] + hist[(pos - m - k + n) % n]); }
        pos = (pos + 1) % n; (void)idx; return acc;
    }
    int n = 0; std::vector<double> h, hist; int pos = 0;
private:
    static double bessel0(double x) { double s = 1, t = 1; for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }
};
// Note: halfband symmetric taps - only odd offsets from the center and the center tap are nonzero.

class Resampler2x {
public:
    void design(int taps, double beta) { up.design(taps, beta); dn.design(taps, beta); }
    // up: one input sample -> two output samples (zero stuffed, gain 2)
    inline void upsample(double x, double* out) { out[0] = 2.0 * up.push(x); out[1] = 2.0 * up.push(0.0); }
    // down: two input samples -> one output sample (filter then keep first)
    inline double downsample(const double* in) { double a = dn.push(in[0]); dn.push(in[1]); return a; }
    HalfBand up, dn;
};

// ------------------------------------------------------------------ biquads for the cabinet
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double process(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    static Biquad butterHP(double fc, double fs, double Q) {
        double w = std::tan(kPi * fc / fs), k = w * w, n = 1 / (1 + w / Q + k); Biquad q;
        q.b0 = n; q.b1 = -2 * n; q.b2 = n; q.a1 = 2 * (k - 1) * n; q.a2 = (1 - w / Q + k) * n; return q;
    }
    static Biquad butterLP(double fc, double fs, double Q) {
        double w = std::tan(kPi * fc / fs), k = w * w, n = 1 / (1 + w / Q + k); Biquad q;
        q.b0 = k * n; q.b1 = 2 * k * n; q.b2 = k * n; q.a1 = 2 * (k - 1) * n; q.a2 = (1 - w / Q + k) * n; return q;
    }
    static Biquad peak(double f0, double Q, double fs) {   // same as scipy.signal.iirpeak
        double w0 = f0 / (fs / 2) * kPi, bw = (f0 / (fs / 2)) / Q * kPi, gb = 1 / std::sqrt(2.0);
        double beta = (std::sqrt(1 - gb * gb) / gb) * std::tan(bw / 2), gain = 1 / (1 + beta); Biquad q;
        q.b0 = 1 - gain; q.b1 = 0; q.b2 = -(1 - gain); q.a1 = -2 * gain * std::cos(w0); q.a2 = 2 * gain - 1; return q;
    }
};

class Cabinet {
public:
    void prepare(double fs) {
        hp = Biquad::butterHP(70, fs, 0.70710678); p105 = Biquad::peak(105, 2.5, fs); p250 = Biquad::peak(250, 1.2, fs);
        lpa = Biquad::butterLP(5000, fs, 0.5411961); lpb = Biquad::butterLP(5000, fs, 1.3065630); p2500 = Biquad::peak(2500, 2.0, fs);
    }
    inline double process(double x) {
        double y = hp.process(x);
        y += 0.6 * p105.process(y);
        y += 0.25 * p250.process(y);
        y = lpb.process(lpa.process(y));
        y += 0.5 * p2500.process(y);
        return y;
    }
private:
    Biquad hp, p105, p250, lpa, lpb, p2500;
};

// ------------------------------------------------------------------ the whole amp
struct AmpParams {
    int channel = 3; double gain = 0.6, bass = 0.55, mid = 0.5, treble = 0.6, volume = 0.5, master = 0.5, presence = 0.5, deep = 0.85;
    bool bright = false; double inputTrimDb = 0.0, outputDb = 0.0;
};

struct AmpConfig {            // everything that depends on the knobs; built off the audio thread
    AmpParams p; Preamp::Config pre; double masterGain = 0.1, inGain = 1, outGain = 1;
};

class Amp {
public:
    // sampleRate: host rate; oversampling: 1, 2 or 4
    void prepare(double sampleRate, int oversampling) {
        sr = sampleRate; osf = (oversampling == 1 || oversampling == 2) ? oversampling : 4; fs = sr * osf;
        if (osf >= 2) rs1.design(69, 9.0);
        if (osf >= 4) rs2.design(15, 7.0);
        pre.prepare(fs); pa.prepare(fs); cab.prepare(sr);
        smoothK = 1.0 - std::exp(-1.0 / (0.02 * sr));
        setParams(AmpParams()); mg = ownCfg.masterGain; og = ownCfg.outGain; ig = ownCfg.inGain;
    }
    std::unique_ptr<AmpConfig> makeConfig(const AmpParams& p) const {
        std::unique_ptr<AmpConfig> c(new AmpConfig()); fill(*c, p); return c;
    }
    void applyConfig(const AmpConfig& c) {            // cheap, safe on the audio thread
        pre.apply(c.pre); pa.setControls(c.p.presence, c.p.deep);
        tMg = c.masterGain; tIg = c.inGain; tOg = c.outGain;
    }
    void setParams(const AmpParams& p) { fill(ownCfg, p); applyConfig(ownCfg); }
    // x: guitar signal, full scale = 1 V. Mono in, mono out.
    inline double process(double x) {
        mg += (tMg - mg) * smoothK; ig += (tIg - ig) * smoothK; og += (tOg - og) * smoothK;
        masterGain = mg;
        double in = x * ig, out;
        if (osf == 1) out = chain(in);
        else {
            double a[2], b[4], o[4], o2[2];
            rs1.upsample(in, a);
            if (osf == 2) { o2[0] = chain(a[0]); o2[1] = chain(a[1]); out = rs1.downsample(o2); }
            else {
                rs2.upsample(a[0], b); rs2.upsample(a[1], b + 2);
                for (int i = 0; i < 4; i++) o[i] = chain(b[i]);
                double d[2] = {rs2.downsample(o), rs2.downsample(o + 2)};
                out = rs1.downsample(d);
            }
        }
        return cab.process(out) * og;
    }
    double supply() const { return pa.supply(); }
    double latencySeconds() const { return osf >= 2 ? (rs1.up.delay() / (2.0 * sr) + rs1.dn.delay() / (2.0 * sr)) : 0.0; }
    static constexpr double kDiv = 4.7e3 / (100e3 + 4.7e3), kCal = 0.272;
    static constexpr double kOutScale = 1.0 / 150.0;     // about -10 dBFS rms at master 5
private:
    void fill(AmpConfig& c, const AmpParams& p) const {
        c.p = p;
        PreampControls pc; pc.channel = p.channel; pc.gain = p.gain; pc.bass = p.bass; pc.mid = p.mid; pc.treble = p.treble; pc.vol = p.volume; pc.bright = p.bright;
        pre.buildConfig(pc, c.pre);
        c.masterGain = taperLog(p.master) * kDiv * kCal;
        c.inGain = std::pow(10.0, p.inputTrimDb / 20.0); c.outGain = kOutScale * std::pow(10.0, p.outputDb / 20.0);
    }
    inline double chain(double v) { return pa.process(pre.process(v) * masterGain); }
    double sr = 48000, fs = 192000, masterGain = 0.1, mg = 0.1, ig = 1, og = 1, tMg = 0.1, tIg = 1, tOg = 1, smoothK = 0.001; int osf = 4;
    Preamp pre; PowerAmp pa; Cabinet cab; Resampler2x rs1, rs2; AmpConfig ownCfg;
};

} // namespace vh4
