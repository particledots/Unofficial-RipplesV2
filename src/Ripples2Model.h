// Ripples2Model.h - a circuit model of the 2020 Ripples filter (schematic "ripples_v90", dated 16/12/2019), written from the published
// drawing. No JUCE in here, so it can be tested on its own. Header only.
//
// What is modelled (values read off the drawing; see README for what was NOT checked):
//   * Input stage. IC8D is an inverting amplifier: the input pot (R51, 10k) and R58 100k / R61 1.5k set its input current, and its feedback is
//     R54 + R68 = 151k with a back-to-back zener pair (D5/D6, 3.3 V parts) across it. That is a soft clipper. IC7B and IC8C (both unity, inverting)
//     then add the second input (J4 via R64 68k + R57 30k, so -10.3 dB) and give FILTER_IN.
//   * Four identical stages. Each is a V2164 VCA feeding an op-amp integrator (220 pF) with 33.2k resistors, i.e. an INVERTING one-pole
//     low-pass:  dy/dt = -wc * (y + w),  wc = g / (33.2k * 220p),  g = VCA gain (1 = top of the range, about 21.8 kHz).
//     Stage 1 sees  w1 = FILTER_IN + 33.2k * I_ota ;  stages 2..4 see w = the previous stage's output.
//   * Resonance: an LM13700 OTA (IC5OTA1) turns  (HP(FILTER_IN) - LP4) / 23.1  into a current added at stage 1's input. Its output current is
//     Iabc * tanh(vd / (2 Vt)). Because the input goes through the OTA too, the pass-band level does not fall as the resonance rises.
//   * Outputs (each an inverting op-amp):
//        HP2 = -(51k/33.2k) * (FILTER_IN + LP2 + 2*LP1)                           (high-pass, 12 dB)
//        BP  = -(100k/33.2k) * (LP1 + LP2)             with the 12/24 dB switch down  (band-pass, 6 dB/oct skirts)
//            = -(100k/16.2k) * (LP2 + 2*LP3 + LP4)     with the switch up             (band-pass, 12 dB/oct skirts)
//        LP  = -68k * Iabc_vca * tanh( (HP(LP2 or LP4) / 67.7) / (2 Vt) )        (low-pass through the LM13700 VCA, AC-coupled)
//   * The op-amps stop at +-kRail volts. Audio level: 1.0 in the plug-in = kVoltsPerFS volts.
// The delay-free feedback loop (OTA <-> stages) is solved exactly each sample (Newton on one number), not with a one-sample delay.
#pragma once
#include <cmath>
#include <complex>
#include <vector>
#include <algorithm>

namespace ripples2 {

constexpr double kPi          = 3.14159265358979323846;
constexpr double kVoltsPerFS  = 10.0;     // ASSUMPTION: 0 dBFS = 10 V peak (not from the drawing); same as the Ripples V1 plug-in after its level fix
constexpr double kRail        = 11.0;     // ASSUMPTION: OPA1679 output swing (rail-to-rail part) on a +12 V rail with a series diode and a -12 V rail
constexpr double kJackClamp   = 12.0;     // ASSUMPTION: the jack protection diodes (D10, D14) limit the input to about +-12 V

// ---- the four stages --------------------------------------------------------------------------------------------------------------------
constexpr double kR_stage     = 33.2e3;   // R7/R2/R8/R15 (feedback) and R20/R1/R5/R13 (inter-stage)
constexpr double kC_int       = 220e-12;  // C14/C13/C8/C9
constexpr double kStageMaxHz  = 1.0 / (2.0 * kPi * kR_stage * kC_int);   // about 21.8 kHz (VCA gain 1)
constexpr double kVt2         = 2.0 * 0.02585;                            // 2 * thermal voltage

// ---- resonance OTA (IC5OTA1) -------------------------------------------------------------------------------------------------------------
constexpr double kOtaDiv      = 1.5e3 / (33.2e3 + 1.5e3);   // R25/R22 on the input side and R26/R27 on the LP4 side: both 1/23.13
constexpr double kOtaHpHz     = 1.0 / (2.0 * kPi * (33.2e3 + 1.5e3) * 4.7e-6);   // C16 4.7u into R25 + R22: about 0.98 Hz
// OTA bias current at which the feedback loop gain is exactly 1 (self-oscillation): K = gm * kOtaDiv * R = 4, with gm = Iabc / (2 Vt).
constexpr double kIThreshold  = 4.0 * kVt2 / (kOtaDiv * kR_stage);      // about 144 uA

// Q control chain: REF_+10 (10.0 V) -> 10k pot R29 -> R33 51k into IC6A's virtual earth -> PNP -> OTA bias. So Iabc = (pot wiper volts) / 51k.
// The 51k loads the pot a little, so the knob is not exactly linear. q = knob position 0..1.
constexpr double kQref = 10.0, kQr = 51e3, kQpot = 10e3;
inline double qCurrentAsDrawn(double q) {
    q = std::max(0.0, std::min(1.0, q));
    const double vw = kQref * q / (1.0 + q * (1.0 - q) * kQpot / kQr);
    return vw / kQr;
}

// "Smooth" Resonance taper (a choice, NOT what the drawing says: the drawing's Q control is linear). Same law as the Ripples V1 plug-in.
// k = loop gain relative to self-oscillation (1 = starts to self-oscillate). Below the onset knob position the distance from
// self-oscillation (1 - k) falls exponentially with the knob, so the peak rises evenly across the knob instead of all at the end;
// above it k rises linearly to 1.15 at the top.
constexpr double kSmoothOnset = 0.88;
inline double smoothLoopGain(double q) {
    q = std::max(0.0, std::min(1.0, q));
    if (q >= kSmoothOnset) return 1.0 + (q - kSmoothOnset) * (0.15 / (1.0 - kSmoothOnset));
    static const double e0 = std::exp(-1.5 * kSmoothOnset);
    return 1.0 - (std::exp(-1.5 * q) - e0) / (1.0 - e0);
}
inline double resoCurrent(double q, bool smooth) { return smooth ? smoothLoopGain(q) * kIThreshold : qCurrentAsDrawn(q); }

// ---- LM13700 VCA for the low-pass output (IC5OTA2) -----------------------------------------------------------------------------------------
// Its bias comes from the VCA CV chain: the jack is normalled to +10 V through R63 16.2k and goes through R34 39k into IC6D, and R35 10M adds -1 uA.
// So with nothing plugged in the bias is about 180 uA; "VCA level" 0..1 scales that (an addition: the hardware has no knob, only the CV input).
constexpr double kIVcaMax     = 10.0 / (16.2e3 + 39e3) - 1e-6;
constexpr double kVcaDiv      = 1.5e3 / (100e3 + 1.5e3);    // R24 100k into the OTA input, R23 1.5k to earth (R28 1.5k on the other input)
constexpr double kVcaFb       = 68e3;                        // R47
constexpr double kVcaHpHz     = 1.0 / (2.0 * kPi * (100e3 + 1.5e3) * 4.7e-6);   // C17 4.7u: about 0.33 Hz

// ---- the other output sums ----------------------------------------------------------------------------------------------------------------
constexpr double kHpGain      = 51e3 / 33.2e3;     // R43 / R46,R45 (and R44 || R42 for LP1, which is why LP1 counts twice)
constexpr double kBp2Gain     = 100e3 / 33.2e3;    // R49 / R17,R18
constexpr double kBp4Gain     = 100e3 / 16.2e3;    // R49 / R14,R12 (and R9 || R10 for LP3, which is why LP3 counts twice)
// The drawing's 12 dB band-pass comes out inverted relative to the input (the 24 dB band-pass and the high-pass and low-pass do not). The plug-in flips it
// so that every output is in phase with the input; set to +1 to follow the drawing.
constexpr double kBp2Sign     = -1.0;

// ---- input stage (IC8D, IC7B, IC8C) ---------------------------------------------------------------------------------------------------------
constexpr double kRfIn        = 100e3 + 51e3;      // R54 + R68 (in series: no tap between them in the drawing)
constexpr double kIn2Gain     = 30e3 / (68e3 + 30e3);  // the second input: R57 30k + R64 68k into IC8C's virtual earth, feedback R60 30k
// Small-signal gain of the input stage for a pot position u (0 = wiper at the end that gives most gain, 1 = least): R58 100k, R61 1.5k, the pot R51 10k.
constexpr double inputStageGain(double u) {
    return kRfIn * 1.5e3 / ((1.5e3 + 10e3 * u) * (100e3 + 1.5e3 * (10e3 * u) / (1.5e3 + 10e3 * u)));
}
constexpr double kInGainMin   = inputStageGain(1.0);    // about 0.194 (-14.2 dB)
constexpr double kInGainMax   = inputStageGain(0.0);    // 1.51 (+3.6 dB)

// The zener pair across IC8D's feedback: I = I1 * exp((|v| - V1) / Vs). ASSUMPTION (the part is only named BZT585B3V3T): a 3.3 V zener's soft knee
// plus one forward diode in series, fitted so that 1 uA flows at 2.30 V and the current rises e-fold per 0.197 V. Output of the input stage therefore
// rounds off from about 2 V and does not go much above 3.3 V.
constexpr double kClampI1 = 1e-6, kClampV1 = 2.30, kClampVs = 0.1965;

// Output voltage of IC8D (magnitude) as a function of the input current into its virtual earth: solves  v / kRfIn + Iclamp(v) = i  by bisection
// once, into a table. eval() takes the signed current and returns the signed voltage with the same sign (the caller supplies the inversion).
struct ClipTable {
    static constexpr int N = 4096;
    static constexpr double kImax = 130e-6;      // enough for +-12 V into the least-loaded input setting (12 V / 100k = 120 uA)
    double v[N + 2];
    ClipTable() {
        for (int k = 0; k <= N + 1; ++k) {
            const double I = kImax * std::min(k, N) / N;
            double lo = 0.0, hi = 8.0;
            for (int it = 0; it < 70; ++it) {
                const double m = 0.5 * (lo + hi);
                const double f = m / kRfIn + kClampI1 * std::exp((m - kClampV1) / kClampVs) - I;
                (f > 0.0 ? hi : lo) = m;
            }
            v[k] = 0.5 * (lo + hi);
        }
    }
    double eval(double i) const {
        const double a = std::min(std::fabs(i), kImax) * (N / kImax);
        const int k = (int) a;
        const double r = v[k] + (a - k) * (v[k + 1] - v[k]);
        return i < 0.0 ? -r : r;
    }
};
inline const ClipTable& clipTable() { static const ClipTable t; return t; }

// The op-amps stop at the rails, but not like a brick wall: linear up to 70% of the rail, then a smooth bend (tanh) that reaches the rail.
inline double clipRail(double v) {
    const double a = 0.7 * kRail, b = kRail - a, m = std::fabs(v);
    if (m <= a) return v;
    const double r = a + b * std::tanh((m - a) / b);
    return v < 0 ? -r : r;
}

// Cheap stand-ins for tan, tanh and 2^x (checked against the real ones in the tests).
// fastTan: Pade [5/4], error below 3e-5 (relative) up to x = 1.0 and below 1% up to x = 1.26 (the filter never goes beyond that: its pole frequency is limited to 0.4 of the sample rate).
inline double fastTan(double x) { const double x2 = x * x; return x * (945.0 - 105.0 * x2 + x2 * x2) / (945.0 - 420.0 * x2 + 15.0 * x2 * x2); }
// fastTanh: Pade [7/6], absolute error below 1e-4 everywhere (worst at the cut-off where it switches to +-1).
inline double fastTanh(double x) {
    const double ax = std::fabs(x);
    if (ax >= 4.97) return x < 0 ? -1.0 : 1.0;
    const double x2 = x * x;
    return x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2))) / (135135.0 + x2 * (62370.0 + x2 * (3150.0 + 28.0 * x2)));
}
// fastExp2: integer part by scaling, fraction by a degree-6 polynomial; relative error below 2e-5.
inline double fastExp2(double x) {
    x = std::max(-60.0, std::min(60.0, x));
    const double fl = std::floor(x), f = x - fl;
    const double p = 1.0 + f * (0.6931471805599453 + f * (0.2402265069591007 + f * (0.0555041086648216 + f * (0.0096181291076285 + f * (0.0013333558146428 + f * 0.0001540353039338)))));
    return std::ldexp(p, (int) fl);
}

// 2x oversampler: linear-phase half-band FIR (windowed sinc, cutoff fs/4 of the 2x rate), written as two phases.
// For a half-band filter every second tap is zero, so one phase is a single tap (a pure delay) and the other has 32 taps.
class Oversampler2x {
public:
    static constexpr int kTaps = 63;                 // odd, symmetric
    static constexpr int kN = 32;                    // taps in the long phase
    Oversampler2x() {
        const int m = (kTaps - 1) / 2;
        double h[kTaps], sum = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            double x = i - m;
            double s = (x == 0.0) ? 0.5 : std::sin(kPi * x * 0.5) / (kPi * x);
            double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kTaps - 1)) + 0.08 * std::cos(4.0 * kPi * i / (kTaps - 1));
            h[i] = s * w;
            if (i != m && ((i - m) % 2 == 0)) h[i] = 0.0;     // exactly zero in a half-band filter (rounding noise otherwise)
            sum += h[i];
        }
        for (int i = 0; i < kTaps; ++i) h[i] *= 2.0 / sum;   // DC gain 2 on the zero-stuffed signal => unity after interpolation
        for (int j = 0; j < kN; ++j) h0[j] = h[2 * j];        // even taps: all non-zero
        hc = h[m]; cj = (m - 1) / 2;                            // odd taps: only the centre one is non-zero (index m = 2*cj+1)
        reset();
    }
    void reset() { for (double& v : xa) v = 0.0; for (double& v : xb) v = 0.0; for (double& v : xd) v = 0.0; pa = pb = pd = 0; }
    static constexpr int latencySamples() { return 31; }   // host-rate samples: up (15.5) + down (15.5)
    // one host sample in -> two oversampled samples out
    void up(double x, double& o0, double& o1) {
        pa = (pa + 1) & (kN - 1); xa[pa] = xa[pa + kN] = x;
        const double* r = &xa[pa + kN];      // r[-j] = x[n-j]
        double a = 0.0;
        for (int j = 0; j < kN; ++j) a += h0[j] * r[-j];
        o0 = a;
        o1 = hc * r[-cj];
    }
    // the same history update without the arithmetic (the output would not be used); the filter then continues as if x had been fed in
    void skip(double x) { pa = (pa + 1) & (kN - 1); xa[pa] = xa[pa + kN] = x; }
    // two oversampled samples in -> one host sample out (the output that lines up with the first of each pair)
    double down(double a, double b) {
        pb = (pb + 1) & (kN - 1); xb[pb] = xb[pb + kN] = a;
        const double* ra = &xb[pb + kN];
        double s = 0.0;
        for (int j = 0; j < kN; ++j) s += h0[j] * ra[-j];
        s += hc * xd[(pd - cj + kN) & (kN - 1)];     // b[n-1-cj]: the history of b before the current one
        pd = (pd + 1) & (kN - 1); xd[pd] = b;
        return 0.5 * s;
    }
private:
    double h0[kN], hc = 0.0; int cj = 0;
    double xa[2 * kN], xb[2 * kN], xd[kN];
    int pa = 0, pb = 0, pd = 0;
};

// One channel of the filter proper (the four stages and the resonance loop), running at the (oversampled) rate it is given.
class Channel {
public:
    void reset() { s[0] = s[1] = s[2] = s[3] = 0.0; hpIn = hpV2 = hpV4 = 0.0; iOta = 0.0; vcaU2 = vcaU4 = 0.0; }
    // x = FILTER_IN in volts; stageHz = pole frequency of each stage (<= kStageMaxHz); iReso = resonance OTA bias current (amps); fs = rate this runs at.
    // y[0..3] = LP1..LP4 (volts, inverting stages, so LP1 = -H*w, LP2 = +H^2*w, ...). vcaU2 / vcaU4 = LP2 / LP4 after the 0.33 Hz AC-coupling, for the VCA output.
    void step(double x, double stageHz, double iReso, double fs, double* y) {
        if (std::fabs(fs - lastFs) > 1e-9) { lastFs = fs; aIn = 1.0 - std::exp(-2.0 * kPi * kOtaHpHz / fs); aV = 1.0 - std::exp(-2.0 * kPi * kVcaHpHz / fs); }
        const double wcT = std::min(stageHz, 0.4 * fs);
        const double g = fastTan(kPi * wcT / fs);
        const double G = g / (1.0 + g);
        // 0.98 Hz high-pass in front of the OTA's + input (C16)
        hpIn += aIn * (x - hpIn);
        const double vplus = (x - hpIn) * kOtaDiv;
        // linear part of each stage:  y_k = c_k - G * w_k  with  c_k = s_k * (1 - G);  then each y is p_k + q_k * I (I = the OTA current into stage 1's input)
        const double G1 = 1.0 - G;
        double p[4], q[4];
        p[0] = s[0] * G1 - G * x;      q[0] = -G * kR_stage;
        for (int k = 1; k < 4; ++k) { p[k] = s[k] * G1 - G * p[k - 1]; q[k] = -G * q[k - 1]; }
        // solve  I = Iabc * tanh((vplus - (p3 + q3*I) * kOtaDiv) / (2Vt))  for I (monotone: Newton, started from the last answer)
        double I = iOta;
        const double dvdI = q[3] * kOtaDiv / kVt2;
        const double v0 = (vplus - p[3] * kOtaDiv) / kVt2;
        for (int it = 0; it < 6; ++it) {
            const double th = fastTanh(v0 - dvdI * I);
            const double F  = I - iReso * th;
            const double dF = 1.0 + iReso * (1.0 - th * th) * dvdI;
            const double step = F / dF;
            I -= step;
            if (std::fabs(step) < 1e-10) break;
        }
        iOta = I;
        for (int k = 0; k < 4; ++k) {
            y[k] = clipRail(p[k] + q[k] * I);        // the op-amps stop at the supply rails
            s[k] = 2.0 * y[k] - s[k];                // trapezoidal state update
        }
        hpV2 += aV * (y[1] - hpV2); hpV4 += aV * (y[3] - hpV4);
        vcaU2 = y[1] - hpV2; vcaU4 = y[3] - hpV4;
    }
    double vcaU2 = 0.0, vcaU4 = 0.0;
private:
    double s[4] = {0, 0, 0, 0};
    double hpIn = 0, hpV2 = 0, hpV4 = 0, iOta = 0;
    double lastFs = 0, aIn = 0, aV = 0;
};

// The five things the plug-in can send out. LP is the low-pass through the VCA; BP and LP come in 12 and 24 dB versions (the drawing's 12/24 dB switch).
enum Source { LP2_VCA = 0, LP4_VCA = 1, BP2 = 2, BP4 = 3, HP2 = 4 };
inline int sourceIndex(int output /*0 LP, 1 BP, 2 HP*/, int slope /*0 = 12 dB, 1 = 24 dB*/) { return output == 0 ? slope : output == 1 ? 2 + slope : 4; }

// Envelope follower for the FM "Envelope" source: rises towards the rectified input with the attack time, falls with the decay time.
// Output is in the same units as the input (1.0 = full scale), never negative.
struct EnvFollower {
    double e = 0.0, aA = 0.0, aD = 0.0;
    void reset() { e = 0.0; }
    void setTimes(double attackMs, double decayMs, double fs) {
        aA = 1.0 - std::exp(-1.0 / (std::max(0.01, attackMs) * 0.001 * fs));
        aD = 1.0 - std::exp(-1.0 / (std::max(0.1, decayMs) * 0.001 * fs));
    }
    double run(double x) { x = std::fabs(x); e += (x > e ? aA : aD) * (x - e); return e; }
};

// The FM "Ping" source: a one-shot envelope. trigger() starts a linear rise to 1.0 over the attack time, then it falls exponentially (time constant = decay).
struct PingEnv {
    double e = 0.0, up = 0.0, aD = 0.0; bool rising = false;
    void reset() { e = 0.0; rising = false; }
    void setTimes(double attackMs, double decayMs, double fs) {
        up = 1.0 / std::max(1.0, std::max(0.01, attackMs) * 0.001 * fs);
        aD = std::exp(-1.0 / (std::max(0.1, decayMs) * 0.001 * fs));
    }
    void trigger() { rising = true; }
    double run() {
        if (rising) { e += up; if (e >= 1.0) { e = 1.0; rising = false; } }
        else e *= aD;
        return e;
    }
};

// What the plug-in uses: one channel at 2x oversampling, with the plug-in's units (Hz, amps, linear gains) and 1.0 = kVoltsPerFS volts.
// Changing the output fades between the old and the new one over about 10 ms (so the switch does not click).
// FM: fmOct is a frequency offset in octaves (positive = higher), applied to all four stages (exponential, like the hardware's V/oct summing).
// It is delayed by the oversampler's latency so that it lines up with the audio it belongs to.
class Voice {
public:
    static constexpr int kFmDelay = 31;     // = Oversampler2x::latencySamples()
    static_assert(kFmDelay == Oversampler2x::latencySamples(), "FM delay must equal the oversampler latency");
    struct Settings {
        int src = LP4_VCA;           // which Source
        double stageHz = 1000.0;     // pole frequency of each stage
        double iReso = 0.0;          // resonance OTA bias current, amps
        double iVca = 0.0;           // VCA OTA bias current, amps
        double inGain = 1.0;         // small-signal gain of the input stage (kInGainMin..kInGainMax)
        double trim = 1.0;           // linear gain in front of the input (the plug-in's Input trim)
        double outGain = 1.0;        // linear gain after the filter
        double in2 = 0.0;            // level of the second input, 0..1 (linear)
    };
    void reset() { os.reset(); os2.reset(); ch0.reset(); curSrc = prevSrc = LP4_VCA; xf = 1.0; for (double& d : fmBuf) d = 0.0; fmPos = 0; fmPrev = 0.0; peakIin = 0.0; }
    // in = main input, in2sig = second input (the plug-in's sidechain), both in plug-in units (1.0 = full scale)
    float process(float in, float in2sig, const Settings& s, double hostFs, double fmOct = 0.0) {
        if (s.src != curSrc) { prevSrc = curSrc; curSrc = s.src; xf = 0.0; }
        // FM, delayed to match the audio path
        fmBuf[fmPos] = std::max(-8.0, std::min(8.0, fmOct));
        const int readPos = (fmPos - kFmDelay + kFmBufLen) % kFmBufLen;
        const double fmCur = fmBuf[readPos];
        fmPos = (fmPos + 1) % kFmBufLen;
        const double fmA = 0.5 * (fmPrev + fmCur), fmB = fmCur;
        fmPrev = fmCur;
        // volts at the jacks (the protection diodes limit them), then up to 2x
        const double vin  = std::max(-kJackClamp, std::min(kJackClamp, (double) in * s.trim * kVoltsPerFS));
        double a, b, a2 = 0.0, b2 = 0.0;
        os.up(vin, a, b);
        const bool useIn2 = s.in2 > 0.0;
        if (useIn2) os2.up(std::max(-kJackClamp, std::min(kJackClamp, (double) in2sig * s.in2 * kVoltsPerFS)), a2, b2);
        else os2.skip(0.0);
        const double fs2 = 2.0 * hostFs;
        const double hzA = std::max(1.0, std::min(fmA == 0.0 ? s.stageHz : s.stageHz * fastExp2(fmA), kStageMaxHz));
        const double hzB = std::max(1.0, std::min(fmB == 0.0 ? s.stageHz : s.stageHz * fastExp2(fmB), kStageMaxHz));
        const unsigned mask = (1u << curSrc) | (xf < 1.0 ? (1u << prevSrc) : 0u);
        double oa[5], ob[5];
        oneSample(a, a2, hzA, s, fs2, mask, oa);
        oneSample(b, b2, hzB, s, fs2, mask, ob);
        double ya, yb;
        if (xf >= 1.0) { ya = oa[curSrc]; yb = ob[curSrc]; }
        else { ya = xf * oa[curSrc] + (1.0 - xf) * oa[prevSrc]; yb = xf * ob[curSrc] + (1.0 - xf) * ob[prevSrc]; xf = std::min(1.0, xf + 1.0 / (0.010 * hostFs)); }
        return (float) (os.down(ya, yb) * s.outGain / kVoltsPerFS);
    }
    double peakIin = 0.0;     // largest current into the input stage since the caller last cleared it (amps); the caller turns it into a "clipping" light
private:
    void oneSample(double v, double v2, double hz, const Settings& s, double fs2, unsigned mask, double* o) {
        const double iin = v * s.inGain / kRfIn;                 // amps into IC8D's virtual earth
        peakIin = std::max(peakIin, std::fabs(iin));
        const double vo = clipTable().eval(iin);                  // IC8D swings the other way: -vo
        const double x = clipRail(-vo - kIn2Gain * v2);           // FILTER_IN (IC8C inverts once more, so the drawing's input path is inverting overall)
        double y[4];
        ch0.step(x, hz, s.iReso, fs2, y);
        if (mask & (1u << LP2_VCA)) o[LP2_VCA] = clipRail(-kVcaFb * s.iVca * fastTanh(ch0.vcaU2 * kVcaDiv / kVt2));
        if (mask & (1u << LP4_VCA)) o[LP4_VCA] = clipRail(-kVcaFb * s.iVca * fastTanh(ch0.vcaU4 * kVcaDiv / kVt2));
        if (mask & (1u << BP2))     o[BP2] = kBp2Sign * clipRail(-kBp2Gain * (y[0] + y[1]));
        if (mask & (1u << BP4))     o[BP4] = clipRail(-kBp4Gain * (y[1] + 2.0 * y[2] + y[3]));
        if (mask & (1u << HP2))     o[HP2] = clipRail(-kHpGain * (x + y[1] + 2.0 * y[0]));
    }
    static constexpr int kFmBufLen = 64;
    Oversampler2x os, os2;
    Channel ch0;
    int curSrc = LP4_VCA, prevSrc = LP4_VCA;
    double xf = 1.0;
    double fmBuf[kFmBufLen] = {};
    int fmPos = 0;
    double fmPrev = 0.0;
};

// ---- small-signal response, for the panel's curve (and for the tests, which compare it with the real thing) ---------------------------------
// Gain from the main input (volts) to the chosen output (volts) at frequency f, for the given settings; excludes the Input trim and Output gain.
// Linear theory: each stage H = 1/(1 + j f/f0); with K = gm * kOtaDiv * R (gm = iReso / 2Vt):  w1 = x (1 + K hp) / (1 + K H^4),  LP1..LP4 = -H w1, H^2 w1, -H^3 w1, H^4 w1.
inline std::complex<double> response(int src, double f, double stageHz, double iReso, double iVca, double inGain) {
    using C = std::complex<double>;
    const C j(0.0, 1.0);
    const C H = 1.0 / (1.0 + j * (f / stageHz));
    const double K = iReso * kOtaDiv * kR_stage / kVt2;
    const C hp = (j * f) / (j * f + kOtaHpHz);
    const C H2 = H * H, H4 = H2 * H2;
    const C w1 = (1.0 + K * hp) / (1.0 + K * H4);        // per volt of FILTER_IN
    const C l1 = -H * w1, l2 = H2 * w1, l3 = -H2 * H * w1, l4 = H4 * w1;
    const C x = -inGain;                                  // FILTER_IN per volt at the input
    const C hv = (j * f) / (j * f + kVcaHpHz);
    const double vca = -kVcaFb * iVca * kVcaDiv / kVt2;   // small-signal gain of the VCA stage (from the AC-coupled LP to the output)
    C out;
    switch (src) {
        case LP2_VCA: out = vca * hv * l2; break;
        case LP4_VCA: out = vca * hv * l4; break;
        case BP2:     out = kBp2Sign * (-kBp2Gain) * (l1 + l2); break;
        case BP4:     out = -kBp4Gain * (l2 + 2.0 * l3 + l4); break;
        default:      out = -kHpGain * (1.0 + l2 + 2.0 * l1); break;
    }
    return out * x;
}

} // namespace ripples2
