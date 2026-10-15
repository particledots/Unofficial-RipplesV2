// Tests of the Ripples V2 model (no JUCE). Build:  c++ -std=c++17 -O2 -Isrc tests/ripples2_test.cpp -o ripples2_test && ./ripples2_test
// The expected numbers are worked out by hand from the schematic's component values (written out here, not taken from the model's constants where that
// would make the test circular), and the model's own small-signal formula (used by the panel's curve) is checked against the real thing.
#include "Ripples2Model.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
using namespace ripples2;

static int checks = 0, fails = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

// settings used by most tests: the filter wide open unless told otherwise
struct Cfg {
    int src = LP4_VCA; double hz = 1000.0, iReso = 0.0, iVca = 0.3 * 180.159e-6, inGain = 1.0, trim = 1.0, out = 1.0, in2 = 0.0;
    Voice::Settings s() const { Voice::Settings t; t.src = src; t.stageHz = hz; t.iReso = iReso; t.iVca = iVca; t.inGain = inGain; t.trim = trim; t.outGain = out; t.in2 = in2; return t; }
};

// Run a sine through a Voice and measure the gain at the sine's frequency (magnitude), or the signed gain (+ in phase, - opposite; allows for the latency).
static double gainAt(const Cfg& c, double f, double fs, double amp = 0.001, double secs = 0.8) {
    Voice v; v.reset(); const auto s = c.s();
    const int n = (int)(secs * fs); double sr = 0, si = 0, ir = 0, ii = 0;
    for (int i = 0; i < n; ++i) {
        const double ph = 2.0 * kPi * f * i / fs;
        const float x = (float)(amp * std::sin(ph));
        const float y = v.process(x, 0.0f, s, fs, 0.0);
        if (i > n / 2) { sr += y * std::sin(ph); si += y * std::cos(ph); ir += x * std::sin(ph); ii += x * std::cos(ph); }
    }
    return std::hypot(sr, si) / std::max(std::hypot(ir, ii), 1e-30);
}
static double signedGainAt(const Cfg& c, double f, double fs, double amp = 0.001, double secs = 0.8) {
    Voice v; v.reset(); const auto s = c.s(); const int n = (int)(secs * fs); const int lat = Oversampler2x::latencySamples();
    double num = 0, den = 0;
    for (int i = 0; i < n; ++i) {
        const double ph = 2.0 * kPi * f * i / fs; const float x = (float)(amp * std::sin(ph));
        const float y = v.process(x, 0.0f, s, fs, 0.0);
        const float xd = (float)(amp * std::sin(2.0 * kPi * f * (i - lat) / fs));
        if (i > n / 2) { num += y * xd; den += xd * xd; }
    }
    return num / den;
}

static void testOversampler() {
    printf("-- oversampler\n");
    Oversampler2x os; const double fs = 48000.0;
    for (double f : {100.0, 1000.0, 8000.0}) {
        os.reset(); double s = 0, e = 0; const int n = 9600;
        std::vector<double> in(n), outv(n);
        for (int i = 0; i < n; ++i) { in[i] = std::sin(2 * kPi * f * i / fs); double a, b; os.up(in[i], a, b); outv[i] = os.down(a, b); }
        const int lat = Oversampler2x::latencySamples();
        for (int i = n / 2; i < n; ++i) { s += outv[i] * outv[i]; e += in[i - lat] * in[i - lat]; }
        CHECK(std::fabs(db(std::sqrt(s / e))) < 0.5, "gain at %g Hz = %g dB", f, db(std::sqrt(s / e)));
        double d = 0; for (int i = n / 2; i < n; ++i) d += (outv[i] - in[i - lat]) * (outv[i] - in[i - lat]);
        CHECK(std::sqrt(d / e) < 0.05, "shape/latency mismatch at %g Hz: %g", f, std::sqrt(d / e));
    }
    // skip(x) leaves the history exactly as up(x) would
    {
        Oversampler2x a, b; std::mt19937 rng(5); std::uniform_real_distribution<double> u(-1, 1); double worst = 0;
        for (int i = 0; i < 400; ++i) { double x = u(rng), o0, o1; if (i % 3 == 0) a.skip(x); else a.up(x, o0, o1); b.up(x, o0, o1); }
        for (int i = 0; i < 200; ++i) { double x = u(rng), a0, a1, b0, b1; a.up(x, a0, a1); b.up(x, b0, b1); worst = std::max(worst, std::max(std::fabs(a0 - b0), std::fabs(a1 - b1))); }
        CHECK(worst < 1e-13, "skip() changed the history: %g", worst);
    }
}

static void testConstants() {
    printf("-- numbers worked out from the schematic\n");
    CHECK(std::fabs(kStageMaxHz - 1.0 / (2 * 3.14159265358979 * 33.2e3 * 220e-12)) < 1.0 && std::fabs(kStageMaxHz - 21790.0) < 5.0, "stage max %g", kStageMaxHz);
    CHECK(std::fabs(kOtaDiv - 1.5 / 34.7) < 1e-12, "ota divider %g", kOtaDiv);
    CHECK(std::fabs(kIThreshold - 144.1e-6) < 0.3e-6, "self-oscillation current %g", kIThreshold);
    // loop gain 1 at threshold, worked out again here: gm = I / (2 * 25.85 mV);  K = gm * (1.5/34.7) * 33.2k must be 4 (four equal poles give -1/4 at the stage frequency)
    CHECK(std::fabs((kIThreshold / (2 * 0.02585)) * (1.5 / 34.7) * 33.2e3 - 4.0) < 0.01, "K at threshold");
    // Q control: full knob = 10 V / 51k = 196 uA; half way the 51k loading pulls it a few percent below half
    CHECK(std::fabs(qCurrentAsDrawn(1.0) - 10.0 / 51e3) < 1e-9 && qCurrentAsDrawn(0.0) == 0.0, "Q ends");
    { const double lin = 0.5 * 10.0 / 51e3; const double got = qCurrentAsDrawn(0.5); CHECK(got < lin * 0.97 && got > lin * 0.93, "Q mid %g vs linear %g", got, lin); }
    double q = 0.7; while (qCurrentAsDrawn(q) < kIThreshold) q += 0.0005;
    CHECK(q > 0.755 && q < 0.775, "the drawn Q control reaches self-oscillation at %g", q);
    CHECK(std::fabs(kIVcaMax - (10.0 / 55.2e3 - 1e-6)) < 1e-9 && std::fabs(kIVcaMax - 180.2e-6) < 0.2e-6, "VCA bias %g", kIVcaMax);
    CHECK(std::fabs(kVcaDiv - 1.5 / 101.5) < 1e-12, "VCA divider");
    // input stage: gain at the two ends of the pot, worked out directly
    { const double gMin = 151.0 / (10.0 + 1.5) * 1.5 / (100.0 + 1.5 * 10.0 / 11.5); const double gMax = 151.0 / 100.0;
      CHECK(std::fabs(kInGainMin - gMin) < 1e-6 && std::fabs(kInGainMax - gMax) < 1e-9, "input stage gain range %g .. %g", kInGainMin, kInGainMax);
      CHECK(std::fabs(db(kInGainMin) + 14.23) < 0.05 && std::fabs(db(kInGainMax) - 3.58) < 0.05, "in dB %g .. %g", db(kInGainMin), db(kInGainMax));
      double prev = 1e9; for (double u = 0; u <= 1.0001; u += 0.01) { double g = inputStageGain(u); CHECK(g < prev, "input gain not falling at %g", u); prev = g; } }
    CHECK(std::fabs(kIn2Gain - 30.0 / 98.0) < 1e-12 && std::fabs(db(kIn2Gain) + 10.28) < 0.02, "second input gain %g (%g dB)", kIn2Gain, db(kIn2Gain));
}

static void testSpeedUps() {
    printf("-- the cheaper tan, tanh, 2^x and the two-phase oversampler match the exact versions\n");
    double worstTan = 0, worstTanh = 0, worstExp = 0;
    for (double x = 0.0; x <= 1.0; x += 0.001) worstTan = std::max(worstTan, std::fabs(fastTan(x) / std::tan(x) - 1.0));
    CHECK(worstTan < 5e-5, "fastTan relative error %g", worstTan);
    for (double x = 0.001; x <= 1.26; x += 0.001) worstTan = std::max(worstTan, std::fabs(fastTan(x) / std::tan(x) - 1.0));
    CHECK(worstTan < 0.01, "fastTan relative error up to 1.26: %g", worstTan);
    for (double x = -8.0; x <= 8.0; x += 0.001) worstTanh = std::max(worstTanh, std::fabs(fastTanh(x) - std::tanh(x)));
    CHECK(worstTanh < 1.1e-4, "fastTanh error %g", worstTanh);
    for (double x = -9.0; x <= 9.0; x += 0.0013) worstExp = std::max(worstExp, std::fabs(fastExp2(x) / std::exp2(x) - 1.0));
    CHECK(worstExp < 2e-5, "fastExp2 relative error %g", worstExp);
    CHECK(fastExp2(0.0) == 1.0 && std::fabs(fastExp2(3.0) - 8.0) < 1e-12 && std::fabs(fastExp2(-1.0) - 0.5) < 1e-12, "fastExp2 at whole numbers");
    // the oversampler, against a plain full-length convolution of the same filter
    Oversampler2x os; os.reset();
    const int m = 31; double h[63], sum = 0;
    for (int i = 0; i < 63; ++i) { double x = i - m; double s = (x == 0.0) ? 0.5 : std::sin(kPi * x * 0.5) / (kPi * x); double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / 62) + 0.08 * std::cos(4.0 * kPi * i / 62); h[i] = s * w; if (i != m && ((i - m) % 2 == 0)) h[i] = 0; sum += h[i]; }
    for (double& v : h) v *= 2.0 / sum;
    std::mt19937 rng(3); std::uniform_real_distribution<double> u(-1, 1);
    std::vector<double> in(2000), up(4000, 0.0), refUp(4000), gotUp(4000);
    for (double& v : in) v = u(rng);
    for (int n = 0; n < 2000; ++n) { up[2 * n] = in[n]; double a, b; os.up(in[n], a, b); gotUp[2 * n] = a; gotUp[2 * n + 1] = b; }
    double dU = 0;
    for (int k = 0; k < 4000; ++k) { double r = 0; for (int i = 0; i < 63; ++i) if (k - i >= 0) r += h[i] * up[k - i]; refUp[k] = r; dU = std::max(dU, std::fabs(r - gotUp[k])); }
    CHECK(dU < 1e-12, "up-sampler differs from a plain convolution by %g", dU);
    Oversampler2x os2; os2.reset(); double dD = 0;
    for (int n = 0; n < 2000; ++n) {
        double got = os2.down(gotUp[2 * n], gotUp[2 * n + 1]);
        double r = 0; for (int i = 0; i < 63; ++i) if (2 * n - i >= 0) r += h[i] * gotUp[2 * n - i];
        dD = std::max(dD, std::fabs(got - 0.5 * r));
    }
    CHECK(dD < 1e-12, "down-sampler differs from a plain convolution by %g", dD);
}

// An independent solution of the input stage's feedback equation  v/151k + Iclamp(v) = i  (bisection, written separately from the model's table)
static double clipReference(double i) {
    const double a = std::fabs(i); double lo = 0, hi = 8;
    for (int it = 0; it < 80; ++it) { double m = 0.5 * (lo + hi); double f = m / 151e3 + 1e-6 * std::exp((m - 2.30) / 0.1965) - a; (f > 0 ? hi : lo) = m; }
    const double r = 0.5 * (lo + hi); return i < 0 ? -r : r;
}

static void testInputStage() {
    printf("-- input stage: gain, soft clip, second input\n");
    const double fs = 96000.0;
    // table against an independent solution
    double worst = 0;
    for (double i = -130e-6; i <= 130e-6; i += 0.37e-6) worst = std::max(worst, std::fabs(clipTable().eval(i) - clipReference(i)));
    CHECK(worst < 2e-4, "clip table differs from the reference by %g V", worst);
    // small signal: 151k per ampere; odd; rising; continuous slope (no kinks); never beyond ~3.3 V
    CHECK(std::fabs(clipTable().eval(1e-6) / 1e-6 / 151e3 - 1.0) < 0.003, "small-signal slope");
    CHECK(std::fabs(clipTable().eval(-37e-6) + clipTable().eval(37e-6)) < 1e-12 && std::fabs(clipTable().eval(0.0)) < 1e-12, "not odd");
    { double prev = -1, prevSlope = 1e9; bool mono = true, smooth = true;
      for (double i = 0.5e-6; i <= 130e-6; i += 0.5e-6) { double v = clipTable().eval(i); if (v <= prev) mono = false; double sl = (v - prev) / 0.5e-6; if (prev >= 0 && sl > prevSlope * 1.001 + 1.0) smooth = false; if (prev >= 0) prevSlope = sl; prev = v; }
      CHECK(mono, "clip curve not rising"); CHECK(smooth, "clip curve's slope increases somewhere (a kink)"); }
    CHECK(clipTable().eval(130e-6) < 3.3 && clipTable().eval(130e-6) > 3.0, "limit %g V", clipTable().eval(130e-6));
    CHECK(clipTable().eval(20e-6) < 0.9 * 20e-6 * 151e3, "no compression at 20 uA");
    // whole voice: the gain of the input stage, seen through the high-pass output well above the corner (HP2 -> 51k/33.2k * G there) and through the low-pass output at low frequency
    for (double G : {kInGainMin, 0.4, 1.0, kInGainMax}) {
        Cfg c; c.src = HP2; c.hz = 100.0; c.inGain = G;
        const double hp = gainAt(c, 15000.0, fs) / (51.0 / 33.2) / G;
        CHECK(std::fabs(hp - 1.0) < 0.025, "HP2 pass-band gain / (1.536 G) = %g at G = %g", hp, G);   // 2% of it is the stage's own response at 15 kHz
        Cfg l; l.src = LP4_VCA; l.hz = kStageMaxHz; l.inGain = G;
        const double vcaGain = 68e3 * l.iVca / (2 * 0.02585) / 67.6667;      // small signal: Rf * Iabc / 2Vt * (1.5k / 101.5k)
        const double lp = gainAt(l, 100.0, 48000.0) / (vcaGain * G);
        CHECK(std::fabs(lp - 1.0) < 0.03, "LP via VCA pass-band gain / (VCA gain * G) = %g at G = %g", lp, G);
    }
    // in phase with the input at the pass-band of every output (the drawing's 12 dB band-pass is flipped on purpose)
    for (int src = 0; src < 5; ++src) {
        Cfg c; c.src = src; c.hz = 1000.0; const double f = src <= 1 ? 100.0 : src == 4 ? 15000.0 : 1000.0;
        const double sg = signedGainAt(c, f, 96000.0);
        CHECK(sg > 0.05, "output %d is not in phase with the input (%g)", src, sg);
    }
    // the soft clip: a big signal comes out limited and rounded, a quadruple signal is nowhere near four times bigger
    {
        Cfg c; c.src = HP2; c.hz = 100.0; c.inGain = kInGainMax;
        const double a = gainAt(c, 3000.0, fs, 0.2, 0.3) * 0.2, b = gainAt(c, 3000.0, fs, 0.8, 0.3) * 0.8;
        CHECK(b < 1.6 * a, "four times the input gave %g times the output (no soft clip?)", b / a);
        Voice v; v.reset(); const auto s = c.s(); float pk = 0;
        for (int i = 0; i < 48000; ++i) { float y = v.process((float)(1.0 * std::sin(2 * kPi * 1000.0 * i / 48000.0)), 0.0f, s, 48000.0, 0.0); if (i > 24000) pk = std::max(pk, std::fabs(y)); }
        CHECK(pk * kVoltsPerFS < 1.536 * 3.4 * 1.05 && pk * kVoltsPerFS > 1.536 * 2.6, "full-scale peak at the HP output %g V (clip limit 3.3 V * 1.536)", pk * kVoltsPerFS);
        // odd harmonics appear (the clipper is symmetric)
        Cfg d; d.src = LP4_VCA; d.hz = kStageMaxHz; d.inGain = kInGainMax; const auto sd = d.s(); Voice w; w.reset(); std::vector<float> y(48000);
        for (int i = 0; i < 48000; ++i) y[i] = w.process((float)(0.5 * std::sin(2 * kPi * 500.0 * i / 48000.0)), 0.0f, sd, 48000.0, 0.0);
        auto gz = [&](double f) { double sr = 0, si = 0; for (int i = 24000; i < 48000; ++i) { sr += y[i] * std::sin(2 * kPi * f * i / 48000.0); si += y[i] * std::cos(2 * kPi * f * i / 48000.0); } return std::hypot(sr, si) / 24000.0; };
        CHECK(gz(1500.0) > 0.01 * gz(500.0) && gz(1000.0) < 0.05 * gz(500.0), "harmonics: 3rd %g, 2nd %g, 1st %g", gz(1500.0), gz(1000.0), gz(500.0));
    }
    // second input: no clip, -10.28 dB (30k / 98k) relative to a full-gain main input, and nothing when its level is 0
    {
        Cfg c; c.src = HP2; c.hz = 100.0; c.inGain = 1.0;
        auto run = [&](float mainAmp, float scAmp, double in2, bool toggle = false) {
            Voice v; v.reset(); Cfg k = c; k.in2 = in2; auto s = k.s(); double sr = 0, si = 0; const int n = 48000; (void) toggle;
            for (int i = 0; i < n; ++i) { double ph = 2 * kPi * 12000.0 * i / 96000.0; float y = v.process(mainAmp * (float) std::sin(ph), scAmp * (float) std::sin(ph), s, 96000.0, 0.0);
                if (i > n / 2) { sr += y * std::sin(2 * kPi * 12000.0 * (i - 31) / 96000.0); si += y * std::cos(2 * kPi * 12000.0 * (i - 31) / 96000.0); } }
            return sr / (n / 4.0); };
        const double main1 = run(0.001f, 0.0f, 0.0), sc1 = run(0.0f, 0.001f, 1.0);
        CHECK(std::fabs(sc1 / main1 - 30.0 / 98.0) < 0.01, "second input gain relative to the main input: %g (expected %g)", sc1 / main1, 30.0 / 98.0);
        CHECK(std::fabs(run(0.0f, 0.001f, 0.0)) < 1e-9, "second input leaks at level 0");
        const double half = run(0.0f, 0.001f, 0.5); CHECK(std::fabs(half / sc1 - 0.5) < 0.01, "level 0.5 gives %g of full", half / sc1);
        // both inputs add
        const double both = run(0.001f, 0.001f, 1.0); CHECK(std::fabs(both - (main1 + sc1)) < 0.01 * (main1 + sc1), "inputs do not add: %g vs %g", both, main1 + sc1);
        // the second input is not clipped by the input stage: a big one at the HP output stays linear up to the op-amp rails
        const double big = run(0.0f, 0.6f, 1.0) / 0.6, small = run(0.0f, 0.001f, 1.0) / 0.001;
        CHECK(std::fabs(big / small - 1.0) < 0.05, "second input compresses: %g vs %g", big, small);
    }
    // switching the second input on in the middle of a run leaves no click
    {
        Cfg c; c.src = LP4_VCA; c.hz = 2000.0; Voice v; v.reset(); double last = 0, maxStep = 0, toneStep = 0;
        for (int i = 0; i < 48000; ++i) { Cfg k = c; k.in2 = i < 24000 ? 0.0 : 1.0; float y = v.process(0.0f, (float)(0.2 * std::sin(2 * kPi * 300.0 * i / 48000.0)), k.s(), 48000.0, 0.0);
            if (i > 2000) { double st = std::fabs(y - last); (i < 23000 ? toneStep : maxStep) = std::max(i < 23000 ? toneStep : maxStep, st); } last = y; }
        CHECK(maxStep < 0.1, "switching the second input on made a jump of %g", maxStep);
    }
}

static void testGainsAgainstTheory() {
    printf("-- every output against the circuit equations (and against the panel's response curve)\n");
    const double fs = 96000.0; double worstRatio = 0; int n = 0;
    for (int src = 0; src < 5; ++src) for (double hz : {300.0, 1500.0}) for (double kk : {0.0, 0.5, 0.8}) for (double G : {0.4, 1.0}) {
        Cfg c; c.src = src; c.hz = hz; c.iReso = kk * kIThreshold; c.inGain = G;
        for (double mul : {0.05, 0.3, 1.0, 3.0, 6.0}) {
            const double f = hz * mul; if (f > 9000.0) continue;
            const double got = gainAt(c, f, fs), want = std::abs(response(src, f, hz, c.iReso, c.iVca, G));
            if (want < 0.01) continue;
            // the gain at 1 mV in is far below any clipping, so this must agree closely (a few % close to a high peak, where a tiny frequency difference matters)
            const double r = got / want; ++n;
            const double tol = kk > 0.7 && mul > 0.8 && mul < 1.25 ? 0.06 : 0.02;
            CHECK(std::fabs(r - 1.0) < tol, "src %d hz %g k %g G %g f %g: simulated %g, formula %g (ratio %g)", src, hz, kk, G, f, got, want, r);
            worstRatio = std::max(worstRatio, std::fabs(r - 1.0));
        }
    }
    printf("   %d comparisons, worst difference %.2f %%\n", n, 100.0 * worstRatio);
    // hand-worked peak values (stage 1 kHz, no resonance): at the stage frequency H = (1-j)/2, H(1-H) = 1/2, so
    //   BP 12 dB: 100k/33.2k * 1/2 = 1.506 x G,   BP 24 dB: 100k/16.2k * (1/2)^2 = 1.543 x G,   HP 12 dB: 51k/33.2k * 1/2 = 0.768 x G
    { Cfg c; c.hz = 1000.0; c.src = BP2; CHECK(std::fabs(gainAt(c, 1000.0, fs) - 1.506) < 0.02, "BP 12 dB peak %g", gainAt(c, 1000.0, fs));
      c.src = BP4; CHECK(std::fabs(gainAt(c, 1000.0, fs) - 1.543) < 0.02, "BP 24 dB peak %g", gainAt(c, 1000.0, fs));
      c.src = HP2; CHECK(std::fabs(gainAt(c, 1000.0, fs) - 0.768) < 0.01, "HP corner gain %g", gainAt(c, 1000.0, fs)); }
    // band-pass outputs have no bass: LP1 and LP2 cancel
    { Cfg c; c.hz = 4000.0; c.src = BP2; CHECK(gainAt(c, 30.0, fs) < 0.03 && gainAt(c, 30.0, fs) > 0.015, "BP 12 dB at 30 Hz (6 dB/oct skirt: 3 x 30/4000 = 0.0226) %g", gainAt(c, 30.0, fs)); c.src = BP4; CHECK(gainAt(c, 30.0, fs) < 0.001, "BP 24 dB at 30 Hz %g", gainAt(c, 30.0, fs)); }
    // the 24 dB low-pass and band-pass are the 12 dB ones with two more poles
    { Cfg a, b; a.src = LP2_VCA; b.src = LP4_VCA; a.hz = b.hz = 2000.0; CHECK(gainAt(b, 2000.0, fs) < 0.6 * gainAt(a, 2000.0, fs), "LP4 not below LP2 at the stage frequency"); }
    // response curve: limits and shape
    for (int src = 0; src < 5; ++src) { for (double f : {1.0, 20.0, 1000.0, 20000.0}) { auto r = response(src, f, 1000.0, 0.3 * kIThreshold, 50e-6, 1.0); CHECK(std::isfinite(r.real()) && std::isfinite(r.imag()), "response not finite"); } }
    CHECK(std::abs(response(HP2, 20000.0, 100.0, 0.0, 0.0, 1.0)) > 1.4 && std::abs(response(HP2, 20000.0, 100.0, 0.0, 0.0, 1.0)) < 1.55, "HP2 high-frequency level in the formula");
}

static void testCutoffAndSlope() {
    printf("-- cutoff and slopes\n");
    const double fs = 96000.0;
    // low-pass through the VCA: two poles = 12 dB/oct, four = 24 dB/oct (measured far above the stage frequency, 8 -> 16 kHz with the stages at 1 kHz)
    for (auto spec : { std::make_pair((int) LP2_VCA, 12.0), std::make_pair((int) LP4_VCA, 24.0) }) {
        Cfg c; c.src = spec.first; c.hz = 1000.0;
        double slope = db(gainAt(c, 6000.0, fs, 0.01)) - db(gainAt(c, 12000.0, fs, 0.01));
        CHECK(std::fabs(slope - spec.second) < 1.0, "low-pass slope %g dB/oct (expected %g)", slope, spec.second);
        // -6 dB (two poles) / -12 dB (four) at the stage frequency, relative to the pass-band
        const double rel = db(gainAt(c, 1000.0, fs) / gainAt(c, 20.0, fs));
        CHECK(std::fabs(rel + 3.0103 * (spec.second / 6.0)) < 0.3, "low-pass at the stage frequency %g dB", rel);
    }
    // high-pass: +12 dB/oct below the corner; -6 dB at the stage frequency (H = 1/sqrt2 per pole, (1-H)^2 magnitude 1/2)
    { Cfg c; c.src = HP2; c.hz = 5000.0; double slope = db(gainAt(c, 100.0, fs)) - db(gainAt(c, 50.0, fs)); CHECK(std::fabs(slope - 12.0) < 0.6, "high-pass slope %g dB/oct", slope);
      c.hz = 1000.0; const double rel = db(gainAt(c, 1000.0, fs) / gainAt(c, 20000.0, fs)); CHECK(std::fabs(rel + 6.02) < 0.5, "high-pass at the stage frequency %g dB", rel); }
    // band-pass skirts: 6 dB/oct (12 dB version) and 12 dB/oct (24 dB version), both sides
    for (auto spec : { std::make_pair((int) BP2, 6.0), std::make_pair((int) BP4, 12.0) }) {
        Cfg c; c.src = spec.first; c.hz = 2000.0;
        double lo = db(gainAt(c, 40.0, fs, 0.01)) - db(gainAt(c, 20.0, fs, 0.01)), hi = db(gainAt(c, 12000.0, fs, 0.01)) - db(gainAt(c, 24000.0 > 40000.0 ? 40000.0 : 24000.0, fs, 0.01));
        CHECK(std::fabs(lo - spec.second) < 0.8, "band-pass %d low skirt %g dB/oct (expected %g)", spec.first, lo, spec.second);
        const double hi1 = db(gainAt(c, 10000.0, fs, 0.01)) - db(gainAt(c, 20000.0, fs, 0.01));
        CHECK(std::fabs(hi1 - spec.second) < 1.2, "band-pass %d high skirt %g dB/oct (expected %g)", spec.first, hi1, spec.second); (void) hi;
    }
    // the frequency control moves everything by octaves
    { Cfg a, b; a.hz = 1000.0; b.hz = 2000.0; double r = gainAt(a, 1000.0, fs) / gainAt(a, 20.0, fs) - gainAt(b, 2000.0, fs) / gainAt(b, 20.0, fs); CHECK(std::fabs(r) < 0.01, "doubling the control does not double the corner: %g", r); }
}

static void testHighCutoffAndOverload() {
    printf("-- high cutoff settings at 44.1 kHz, and recovery after an overload\n");
    const double fs = 44100.0;
    { Cfg c; c.src = LP2_VCA; c.hz = 15000.0; const double rel = db(gainAt(c, 15000.0, fs) / gainAt(c, 100.0, fs)); CHECK(std::fabs(rel + 6.02) < 1.0, "15 kHz stage setting at 44.1 kHz: %g dB (expected -6.0)", rel); }
    { // the filter on its own at 48 kHz, 10 kHz stage: four poles = -12 dB there (frequency warping)
        auto gainOf = [](double f) { Channel c; c.reset(); double sr = 0, si = 0, ir = 0, ii = 0; const double fs = 48000.0; const int n = 48000;
            for (int i = 0; i < n; ++i) { double ph = 2 * kPi * f * i / fs, x = 0.5 * std::sin(ph); double y[4]; c.step(x, 10000.0, 0.0, fs, y);
                if (i > n / 2) { sr += y[3] * std::sin(ph); si += y[3] * std::cos(ph); ir += x * std::sin(ph); ii += x * std::cos(ph); } }
            return std::hypot(sr, si) / std::hypot(ir, ii); };
        const double rel = db(gainOf(10000.0) / gainOf(50.0)); CHECK(std::fabs(rel + 12.04) < 0.4, "LP4 at a 10 kHz stage, 48 kHz: %g dB (expected -12.0)", rel); }
    // a huge input for a while must not wind the filter up: the stages stop at the rails, so a slow filter recovers sooner
    Cfg c; c.src = LP4_VCA; c.hz = 5.0; c.inGain = kInGainMax; c.trim = 40.0; Voice v; v.reset(); const auto s = c.s(); float y = 0;
    for (int i = 0; i < 48000; ++i) v.process(1.0f, 0.0f, s, 48000.0, 0.0);
    for (int i = 0; i < 14400; ++i) y = v.process(0.0f, 0.0f, s, 48000.0, 0.0);
    CHECK(std::fabs(y) < 0.25, "output %g, 0.3 s after a big overload ended (stage 5 Hz)", y);
}

static void testSampleRates() {
    printf("-- same answer at 44.1 / 48 / 96 kHz\n");
    for (int src = 0; src < 5; ++src) for (double f : { 500.0, 2000.0, 6000.0 }) {
        Cfg c; c.src = src; c.hz = 3000.0; c.iReso = 0.3 * kIThreshold;
        double a = gainAt(c, f, 44100.0), b = gainAt(c, f, 48000.0), d = gainAt(c, f, 96000.0);
        if (d < 0.01) continue;
        CHECK(std::fabs(db(a / d)) < 0.6 && std::fabs(db(b / d)) < 0.6, "src %d f %g: %g, %g, %g", src, f, a, b, d);
    }
}

// frequency of a sustained oscillation: count positive zero crossings
static double oscFreq(const std::vector<float>& y, double fs, int from) {
    int cross = 0; int first = -1, last = -1;
    for (size_t i = from + 1; i < y.size(); ++i) if (y[i - 1] < 0 && y[i] >= 0) { if (first < 0) first = (int) i; last = (int) i; ++cross; }
    return cross < 3 ? 0.0 : (cross - 1) * fs / (double)(last - first);
}
static double tailRms(const std::vector<float>& y, int from) { double s = 0; for (size_t i = from; i < y.size(); ++i) s += y[i] * y[i]; return std::sqrt(s / (y.size() - from)); }
static std::vector<float> ring(double q, bool smooth, double hz, double fs, double secs, int src = LP4_VCA, double iVcaFrac = 0.3) {
    Voice v; v.reset(); Cfg c; c.src = src; c.hz = hz; c.iReso = resoCurrent(q, smooth); c.iVca = iVcaFrac * kIVcaMax; const auto s = c.s();
    const int n = (int)(secs * fs); std::vector<float> y(n);
    for (int i = 0; i < n; ++i) y[i] = v.process(i < 20 ? 0.5f : 0.0f, 0.0f, s, fs, 0.0);   // a short click, then silence
    return y;
}

static void testResonance() {
    printf("-- resonance and self-oscillation\n");
    const double fs = 48000.0;
    // as drawn: threshold at the knob position worked out from the Q chain (0.765)
    { auto lo = ring(0.74, false, 1000.0, fs, 4.0), hi = ring(0.80, false, 1000.0, fs, 4.0);
      CHECK(tailRms(lo, (int)(3.5 * fs)) < 1e-4, "as drawn: still ringing at 0.74: %g", tailRms(lo, (int)(3.5 * fs)));
      CHECK(tailRms(hi, (int)(3.5 * fs)) > 0.02, "as drawn: no self-oscillation at 0.80: %g", tailRms(hi, (int)(3.5 * fs)));
      double thr = -1; for (double q = 0.70; q <= 0.85; q += 0.01) { auto y = ring(q, false, 1000.0, fs, 3.0); if (tailRms(y, (int)(2.5 * fs)) > 0.005) { thr = q; break; } }
      CHECK(thr > 0.75 && thr < 0.79, "as drawn: threshold found at %g", thr);
      printf("   as drawn: self-oscillation starts at knob %.2f\n", thr); }
    // pitch of the oscillation: four equal poles give 180 degrees at the stage frequency
    for (double f : { 200.0, 1000.0, 5000.0 }) {
        auto y = ring(0.95, true, f, fs, 3.0);
        double of = oscFreq(y, fs, (int)(2.0 * fs));
        CHECK(std::fabs(of / f - 1.0) < 0.05, "stage %g Hz oscillates at %g Hz", f, of);
    }
    // the peak: sinusoids at the stage frequency against the formula  |H^4 (1+K) / (1+K H^4)|  (H^4 = -1/4 there) through each output
    for (double kk : {0.3, 0.6, 0.8}) {
        Cfg c; c.src = BP4; c.hz = 2000.0; c.iReso = kk * kIThreshold;
        const double K = 4.0 * kk;
        // BP4 = 6.173 * w1 H^2 (1-H)^2 with w1 = (1+K)/(1+K H^4) (hp ~ 1): at the stage frequency H^2(1-H)^2 = 1/4 and H^4 = -1/4
        const double want = (100.0 / 16.2) * 0.25 * (1.0 + K) / (1.0 - K / 4.0);
        const double got = gainAt(c, 2000.0, 96000.0);
        CHECK(std::fabs(got / want - 1.0) < 0.03, "k %g: peak %g (hand-worked %g)", kk, got, want);
    }
    // a resonant peak: grows with the knob, for both tapers
    for (bool smooth : {false, true}) { Cfg a, b, d; a.src = b.src = d.src = BP4; a.hz = b.hz = d.hz = 2000.0;
        a.iReso = resoCurrent(0.2, smooth); b.iReso = resoCurrent(0.5, smooth); d.iReso = resoCurrent(0.7, smooth);
        CHECK(gainAt(b, 2000.0, 48000.0) > gainAt(a, 2000.0, 48000.0) && gainAt(d, 2000.0, 48000.0) > gainAt(b, 2000.0, 48000.0), "peak does not grow with the resonance (smooth %d)", (int) smooth); }
    // the input is also sent through the OTA, so the pass-band level does not fall with the resonance (the 2020 design's point): within 0.5 dB up to 90% of threshold
    for (double kk : {0.3, 0.6, 0.9}) { Cfg a, b; a.src = b.src = LP4_VCA; a.hz = b.hz = 3000.0; b.iReso = kk * kIThreshold;
        const double d = db(gainAt(b, 100.0, 48000.0) / gainAt(a, 100.0, 48000.0)); CHECK(std::fabs(d) < 0.5, "pass-band level changes by %g dB at k = %g", d, kk); }
    // the oscillation level is limited (by the OTA and the rails)
    { auto y = ring(1.0, true, 1000.0, fs, 2.0, LP4_VCA, 1.0); double pk = 0; for (float s : y) pk = std::max(pk, (double) std::fabs(s)); CHECK(pk < 1.15, "oscillation peak %g of full scale", pk); }
    // oscillation can be heard on every output
    for (int src = 0; src < 5; ++src) { auto y = ring(0.95, true, 1000.0, fs, 3.0, src); CHECK(tailRms(y, (int)(2.5 * fs)) > 0.005, "output %d is silent while self-oscillating", src); }
}

static void testVca() {
    printf("-- low-pass VCA\n");
    const double fs = 48000.0;
    Cfg z; z.src = LP4_VCA; z.hz = kStageMaxHz; z.iVca = 0.0;
    CHECK(gainAt(z, 500.0, fs) < 1e-6, "VCA at 0 is not silent: %g", gainAt(z, 500.0, fs));
    Cfg a = z, b = z; a.iVca = 0.2 * kIVcaMax; b.iVca = 0.4 * kIVcaMax;
    const double g1 = gainAt(a, 500.0, fs), g2 = gainAt(b, 500.0, fs);
    CHECK(g1 > 0.1 && std::fabs(g2 / g1 - 2.0) < 0.05, "VCA does not scale with its control: %g %g", g1, g2);
    // the hardware's normalled maximum: gain 68k * 180 uA / 51.7 mV / 67.7 = 3.5
    { Cfg m = z; m.iVca = kIVcaMax; const double g = gainAt(m, 500.0, fs); CHECK(std::fabs(g - 3.5) < 0.15, "VCA at full: %g", g); }
    // the VCA's own tanh, on its own: through the second input (which the input stage's clip does not touch) a loud sine comes out compressed by about 1.5 dB or more
    { Cfg m = z; m.iVca = kIVcaMax; m.in2 = 1.0; auto run2 = [&](float amp) { Voice v; v.reset(); auto st = m.s(); double sr = 0, si = 0; const int n = 24000;
          for (int i = 0; i < n; ++i) { double ph = 2 * kPi * 500.0 * i / fs; float y = v.process(0.0f, amp * (float) std::sin(ph), st, fs, 0.0);
              if (i > n / 2) { sr += y * std::sin(2 * kPi * 500.0 * (i - 31) / fs); si += y * std::cos(2 * kPi * 500.0 * (i - 31) / fs); } }
          return std::hypot(sr, si) / (n / 4.0) / amp; };
      const double lo = run2(0.01f), hi = run2(1.0f); CHECK(hi < 0.85 * lo && hi > 0.4 * lo, "VCA tanh through the second input: %g vs %g", hi, lo); }
    // AC-coupled: a DC step produces no lasting output
    { Voice v; v.reset(); auto s = b.s(); float y = 0; for (int i = 0; i < 48000 * 3; ++i) y = v.process(0.05f, 0.0f, s, fs, 0.0); CHECK(std::fabs(y) < 0.02, "DC passes through the VCA output: %g", y); }
    // saturates (tanh): a big signal comes out less than proportionally
    { Cfg m = z; m.iVca = kIVcaMax; m.inGain = kInGainMax; const double lo = gainAt(m, 500.0, fs, 0.01), hi = gainAt(m, 500.0, fs, 1.0, 0.3); CHECK(hi < 0.85 * lo, "VCA does not saturate: %g vs %g", hi, lo); }
}

static void testStability() {
    printf("-- stability with extreme and random settings\n");
    std::mt19937 rng(7); std::uniform_real_distribution<double> u(0.0, 1.0);
    for (double fs : { 44100.0, 96000.0, 192000.0 }) {
        Voice v; v.reset(); bool bad = false; double pk = 0; Cfg c; Voice::Settings st = c.s();
        for (int i = 0; i < (int)(fs * 4); ++i) {
            if (i % 2000 == 0) {
                st.stageHz = std::exp(std::log(5.0) + u(rng) * std::log(30000.0 / 5.0));
                const double q = u(rng) > 0.5 ? 1.0 : u(rng); st.iReso = resoCurrent(q, u(rng) > 0.5);
                st.iVca = u(rng) * kIVcaMax; st.src = (int)(rng() % 5); st.inGain = kInGainMin + u(rng) * (kInGainMax - kInGainMin);
                st.trim = std::pow(10.0, (u(rng) * 48.0 - 24.0) / 20.0); st.in2 = u(rng) > 0.5 ? u(rng) : 0.0;
            }
            float x = (float)((u(rng) * 2 - 1) * (u(rng) > 0.99 ? 4.0 : 1.0));
            float y = v.process(x, (float)((u(rng) * 2 - 1) * 0.5), st, fs, (u(rng) - 0.5) * 6.0);
            if (!std::isfinite(y)) { bad = true; break; }
            pk = std::max(pk, (double) std::fabs(y));
        }
        CHECK(!bad, "non-finite output at %g Hz", fs);
        CHECK(pk <= 1.6 * kRail / kVoltsPerFS, "output far beyond the rails: %g", pk);   // the anti-alias filter rings a little on hard-clipped edges
    }
}

static void testOutputSwitch() {
    printf("-- switching the output does not click\n");
    const double fs = 48000.0; Voice v; v.reset(); Cfg c; c.hz = 4000.0; c.iReso = 0.2 * kIThreshold; c.iVca = 0.4 * kIVcaMax;
    // a steady 200 Hz tone; walk through all five outputs; the biggest sample-to-sample jump must stay near the tone's own
    double maxStep = 0, last = 0, maxToneStep = 0; int cur = LP2_VCA;
    for (int i = 0; i < 48000 * 3; ++i) {
        if (i == 24000) cur = LP4_VCA;
        if (i == 48000) cur = BP2;
        if (i == 72000) cur = BP4;
        if (i == 96000) cur = HP2;
        if (i == 120000) cur = LP2_VCA;
        c.src = cur;
        float y = v.process((float)(0.2 * std::sin(2 * kPi * 200.0 * i / fs)), 0.0f, c.s(), fs, 0.0);
        if (i > 4800) { double st = std::fabs(y - last); if (i < 23000) maxToneStep = std::max(maxToneStep, st); maxStep = std::max(maxStep, st); }
        last = y;
    }
    CHECK(maxStep < 4.0 * maxToneStep + 0.01, "a jump of %g when switching (the tone's own biggest step is %g)", maxStep, maxToneStep);
    CHECK(sourceIndex(0, 0) == LP2_VCA && sourceIndex(0, 1) == LP4_VCA && sourceIndex(1, 0) == BP2 && sourceIndex(1, 1) == BP4 && sourceIndex(2, 0) == HP2 && sourceIndex(2, 1) == HP2, "source mapping");
}

static void testFm() {
    printf("-- FM (frequency modulation in octaves)\n");
    const double fs = 48000.0;
    // a constant +1 octave must give the same sound as doubling the frequency setting
    for (double f : { 700.0, 2000.0 }) for (int src : {(int) LP4_VCA, (int) HP2}) {
        Voice a, b; a.reset(); b.reset(); double d = 0, e = 0; Cfg ca, cb; ca.src = cb.src = src; ca.hz = 1000.0; cb.hz = 2000.0; ca.iReso = cb.iReso = 0.4 * kIThreshold;
        for (int i = 0; i < 48000; ++i) {
            float x = (float)(0.1 * std::sin(2 * kPi * f * i / fs));
            float ya = a.process(x, 0.0f, ca.s(), fs, 1.0), yb = b.process(x, 0.0f, cb.s(), fs, 0.0);
            if (i > 24000) { d += (ya - yb) * (ya - yb); e += yb * yb; }
        }
        CHECK(std::sqrt(d / e) < 0.01, "+1 octave is not the same as doubling the frequency (difference %g)", std::sqrt(d / e));
    }
    { Voice a, b; a.reset(); b.reset(); double d = 0, e = 0; Cfg ca, cb; ca.src = cb.src = LP2_VCA; ca.hz = 4000.0; cb.hz = 2000.0;
      for (int i = 0; i < 48000; ++i) { float x = (float)(0.1 * std::sin(2 * kPi * 900.0 * i / fs));
          float ya = a.process(x, 0.0f, ca.s(), fs, -1.0), yb = b.process(x, 0.0f, cb.s(), fs, 0.0);
          if (i > 24000) { d += (ya - yb) * (ya - yb); e += yb * yb; } }
      CHECK(std::sqrt(d / e) < 0.01, "-1 octave is not the same as halving it (difference %g)", std::sqrt(d / e)); }
    // timing: an FM step at input sample N must act on the audio that arrived at N (the audio is delayed by the oversampler, so the FM must be too)
    {
        const int N = 12000, lat = Oversampler2x::latencySamples();
        Voice a, b; a.reset(); b.reset(); double d = 0, e = 0; Cfg ca, cb; ca.src = cb.src = LP4_VCA; ca.hz = 500.0; ca.iReso = cb.iReso = 0.3 * kIThreshold;
        for (int i = 0; i < 24000; ++i) {
            float x = (float)(0.1 * std::sin(2 * kPi * 1500.0 * i / fs));
            cb.hz = i >= N + lat ? 4000.0 : 500.0;
            float ya = a.process(x, 0.0f, ca.s(), fs, i >= N ? 3.0 : 0.0);
            float yb = b.process(x, 0.0f, cb.s(), fs, 0.0);
            if (i > N - 50 && i < N + lat + 300) { d += (ya - yb) * (ya - yb); e += yb * yb; }
        }
        CHECK(std::sqrt(d / e) < 0.02, "FM step is not lined up with the audio (difference %g)", std::sqrt(d / e));
    }
    // audio-rate FM: stays finite, bounded, and makes new frequencies (side-bands) that were not in the input
    {
        Voice v; v.reset(); double pk = 0; bool bad = false; std::vector<float> y(48000); Cfg c; c.src = LP4_VCA; c.hz = 3000.0; c.iReso = 0.5 * kIThreshold;
        for (int i = 0; i < 48000; ++i) {
            float x = (float)(0.1 * std::sin(2 * kPi * 440.0 * i / fs));
            double m = 2.0 * std::sin(2 * kPi * 330.0 * i / fs);
            y[i] = v.process(x, 0.0f, c.s(), fs, m);
            if (!std::isfinite(y[i])) bad = true;
            pk = std::max(pk, (double) std::fabs(y[i]));
        }
        auto gz = [&](double f) { double sr = 0, si = 0; for (int i = 24000; i < 48000; ++i) { sr += y[i] * std::sin(2 * kPi * f * i / fs); si += y[i] * std::cos(2 * kPi * f * i / fs); } return std::hypot(sr, si) / 24000.0; };
        CHECK(!bad && pk <= 1.2, "audio-rate FM gave bad output (peak %g)", pk);
        CHECK(gz(440.0) > 0.002 && gz(110.0) > 0.0005, "audio-rate FM: 440 Hz tone %g, side-band at 110 Hz %g", gz(440.0), gz(110.0));
    }
}

static void testRailKnee() {
    printf("-- the rails: linear up to 70%%, smooth bend, never past the rail\n");
    for (double v : { 0.0, 1.0, -3.0, 7.0, -7.6 }) CHECK(std::fabs(clipRail(v) - v) < 1e-12, "not linear at %g V: %g", v, clipRail(v));
    double prev = clipRail(7.75);
    for (double v = 7.8; v < 60.0; v += 0.05) {
        double c = clipRail(v);
        CHECK(c >= prev - 1e-12 && c <= kRail + 1e-9 && c - prev < 0.06, "knee misbehaves at %g V: %g (before %g)", v, c, prev);
        prev = c;
    }
    CHECK(std::fabs(clipRail(-5.0) + clipRail(5.0)) < 1e-12 && std::fabs(clipRail(-30.0) + clipRail(30.0)) < 1e-12, "not symmetric");
    CHECK(clipRail(1000.0) > 0.999 * kRail, "does not reach the rail: %g", clipRail(1000.0));
}

static void testSmoothTaper() {
    printf("-- smooth resonance taper\n");
    CHECK(std::fabs(smoothLoopGain(0.0)) < 1e-12, "k(0) = %g", smoothLoopGain(0.0));
    CHECK(std::fabs(smoothLoopGain(kSmoothOnset) - 1.0) < 1e-9, "k(onset) = %g", smoothLoopGain(kSmoothOnset));
    CHECK(std::fabs(smoothLoopGain(1.0) - 1.15) < 1e-9, "k(1) = %g", smoothLoopGain(1.0));
    double prev = -1; for (double q = 0; q <= 1.0001; q += 0.005) { double k = smoothLoopGain(q); CHECK(k > prev, "not rising at %g", q); prev = k; }
    CHECK(resoCurrent(0.5, true) == smoothLoopGain(0.5) * kIThreshold && resoCurrent(0.5, false) == qCurrentAsDrawn(0.5), "resoCurrent does not use the chosen taper");
    const double fs = 48000.0;
    // self-oscillation starts where the taper says (near 0.88), and not before
    auto ringS = [&](double q) { auto y = ring(q, true, 1000.0, fs, 4.0); return tailRms(y, (int)(3.5 * fs)); };
    CHECK(ringS(0.84) < 1e-4, "smooth taper: still ringing at 0.84: %g", ringS(0.84));
    CHECK(ringS(0.93) > 0.005, "smooth taper: no self-oscillation at 0.93: %g", ringS(0.93));
    // the peak rises evenly: each step of 0.2 adds between 4 and 20 dB between 0.2 and 0.8
    auto pk = [&](double q, bool smooth) { Cfg c; c.src = BP4; c.hz = 2000.0; c.iReso = resoCurrent(q, smooth); return db(gainAt(c, 2000.0, 96000.0, 0.0003, 1.5)); };
    double p0 = pk(0.0, true), p2 = pk(0.2, true), p4 = pk(0.4, true), p6 = pk(0.6, true), p8 = pk(0.8, true);
    CHECK(p2 - p0 > 4 && p2 - p0 < 20 && p4 - p2 > 4 && p4 - p2 < 20 && p6 - p4 > 4 && p6 - p4 < 20 && p8 - p6 > 4 && p8 - p6 < 20, "uneven rise: %g %g %g %g %g dB", p0, p2, p4, p6, p8);
    auto want = [&](double q, bool smooth) { const double K = 4.0 * resoCurrent(q, smooth) / kIThreshold; return db((1.0 + K) / (1.0 - K / 4.0)); };   // peak rise worked out by hand
    CHECK(std::fabs((p2 - p0) - want(0.2, true)) < 0.6 && std::fabs((p4 - p0) - want(0.4, true)) < 0.6 && std::fabs((p6 - p0) - want(0.6, true)) < 0.8, "smooth peak rise %g %g %g, hand-worked %g %g %g", p2 - p0, p4 - p0, p6 - p0, want(0.2, true), want(0.4, true), want(0.6, true));
    // the drawn taper packs it into the end of the knob
    double d0 = pk(0.0, false), d5 = pk(0.5, false), d7 = pk(0.7, false);
    CHECK(std::fabs((d5 - d0) - want(0.5, false)) < 0.6 && std::fabs((d7 - d0) - want(0.7, false)) < 1.0, "drawn peak rise %g %g, hand-worked %g %g", d5 - d0, d7 - d0, want(0.5, false), want(0.7, false));
    CHECK(d7 - d5 > 10.0 && p6 - p0 < d7 - d0, "drawn taper is not steeper than smooth near the top");
}

static void testEnvelopes() {
    printf("-- envelope follower and ping envelope\n");
    const double fs = 48000.0; EnvFollower e; e.setTimes(5.0, 100.0, fs);
    double v = 0; int tA = -1; for (int i = 0; i < 48000 && tA < 0; ++i) { v = e.run(1.0); if (v >= 1.0 - std::exp(-1.0)) tA = i; }
    CHECK(std::fabs(tA / fs * 1000.0 - 5.0) < 0.3, "attack time constant %g ms (expected 5)", tA / fs * 1000.0);
    for (int i = 0; i < 48000; ++i) e.run(1.0);
    CHECK(std::fabs(e.e - 1.0) < 1e-6, "does not settle at the input level: %g", e.e);
    int tD = -1; for (int i = 0; i < 48000 * 2 && tD < 0; ++i) { v = e.run(0.0); if (v <= std::exp(-1.0)) tD = i; }
    CHECK(std::fabs(tD / fs * 1000.0 - 100.0) < 2.0, "decay time constant %g ms (expected 100)", tD / fs * 1000.0);
    EnvFollower f; f.setTimes(1.0, 50.0, fs); double mn = 1e9, mx = -1e9;
    for (int i = 0; i < 48000; ++i) { double y = f.run(0.5 * std::sin(2 * kPi * 440.0 * i / fs)); if (i > 24000) { mn = std::min(mn, y); mx = std::max(mx, y); } }
    CHECK(mn > 0.25 && mx < 0.5 + 1e-9, "follower on a 0.5 sine runs between %g and %g", mn, mx);
    EnvFollower z; z.setTimes(1.0, 50.0, fs); bool neg = false; for (int i = 0; i < 1000; ++i) if (z.run(-0.7) < 0) neg = true;
    CHECK(!neg, "follower went negative");
    // ping: nothing until triggered; linear rise over the attack time to exactly 1; falls with the decay time constant; retrigger works; never leaves 0..1
    PingEnv p; p.setTimes(10.0, 200.0, fs); double mx2 = 0; bool quiet = true; for (int i = 0; i < 1000; ++i) if (p.run() != 0.0) quiet = false;
    CHECK(quiet, "ping envelope moves without a trigger");
    p.trigger(); int tPeak = -1; double halfway = -1; std::vector<double> y;
    for (int i = 0; i < 48000 * 3; ++i) { double r = p.run(); y.push_back(r); mx2 = std::max(mx2, r); if (tPeak < 0 && r >= 1.0) tPeak = i; if (halfway < 0 && r >= 0.5) halfway = i; }
    CHECK(mx2 == 1.0 && std::fabs(tPeak / fs * 1000.0 - 10.0) < 0.1, "ping peak %g reached at %g ms (expected 1.0 at 10 ms)", mx2, tPeak / fs * 1000.0);
    CHECK(std::fabs(halfway / fs * 1000.0 - 5.0) < 0.1, "ping is not linear: half height at %g ms", halfway / fs * 1000.0);
    { int tFall = -1; for (int i = tPeak; i < (int) y.size() && tFall < 0; ++i) if (y[i] <= std::exp(-1.0)) tFall = i - tPeak; CHECK(std::fabs(tFall / fs * 1000.0 - 200.0) < 3.0, "ping decay time constant %g ms (expected 200)", tFall / fs * 1000.0); }
    CHECK(y.back() < 1e-5 && *std::min_element(y.begin(), y.end()) >= 0.0, "ping does not die away cleanly");
    // retrigger part-way down: goes up again
    { PingEnv q; q.setTimes(1.0, 100.0, fs); q.trigger(); for (int i = 0; i < 4800; ++i) q.run(); const double low = q.e; q.trigger(); double hi = 0; for (int i = 0; i < 200; ++i) hi = std::max(hi, q.run()); CHECK(low < 0.5 && hi == 1.0, "retrigger: %g -> %g", low, hi); }
}

static void testClipMeter() {
    printf("-- input-stage current (for the clip light)\n");
    Voice v; v.reset(); Cfg c; c.inGain = 1.0; auto s = c.s();
    v.peakIin = 0.0; for (int i = 0; i < 4800; ++i) v.process((float)(0.05 * std::sin(i * 0.05)), 0.0f, s, 48000.0, 0.0);
    // 0.05 FS = 0.5 V; into 151k per gain 1.0: 0.5 V / 151k = 3.3 uA; (the stage sees the oversampled signal, the peak is a bit above the sine's)
    CHECK(v.peakIin > 3.0e-6 && v.peakIin < 3.7e-6, "peak current %g uA for 0.5 V", v.peakIin * 1e6);
    v.peakIin = 0.0; CHECK(v.peakIin == 0.0, "cannot clear");
    // compression in dB at a given peak current, as the panel will show it
    const double lin20 = 20e-6 * 151e3, got20 = clipTable().eval(20e-6);
    CHECK(std::fabs(db(lin20 / got20) - 1.5) < 0.2, "compression at 20 uA %g dB", db(lin20 / got20));
    CHECK(db(5e-6 * 151e3 / clipTable().eval(5e-6)) < 0.05, "compression at 5 uA");
}

static void testSilenceAndReset() {
    printf("-- silence in, silence out; reset clears the state\n");
    Voice v; v.reset(); Cfg c; c.iReso = 0.9 * kIThreshold; c.iVca = 0.5 * kIVcaMax; c.in2 = 1.0; auto s = c.s();
    float worst = 0; for (int i = 0; i < 48000; ++i) worst = std::max(worst, std::fabs(v.process(0.0f, 0.0f, s, 48000.0, 0.0)));
    CHECK(worst < 1e-6, "silence in gave %g", worst);
    for (int i = 0; i < 4800; ++i) v.process(0.5f, 0.3f, s, 48000.0, 0.0);
    v.reset();
    float after = 0; for (int i = 0; i < 100; ++i) after = std::max(after, std::fabs(v.process(0.0f, 0.0f, s, 48000.0, 0.0)));
    CHECK(after < 1e-6, "state survived reset: %g", after);
}

int main() {
    testOversampler(); testConstants(); testSpeedUps(); testInputStage(); testGainsAgainstTheory(); testCutoffAndSlope(); testHighCutoffAndOverload(); testSampleRates();
    testResonance(); testVca(); testStability(); testOutputSwitch(); testFm(); testRailKnee(); testSmoothTaper(); testEnvelopes(); testClipMeter(); testSilenceAndReset();
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
