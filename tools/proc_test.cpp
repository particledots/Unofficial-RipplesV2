// Checks the plug-in as a whole (not just the filter maths): the sidechain bus reaches the FM and the second input, every FM source works,
// Ping fires, the outputs/slopes switch, the state saves and loads.  Usage: ripples2_proc_test   (exit code 0 = all passed)
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <random>

static int failed = 0, total = 0;
#define CHECK(c, ...) do { ++total; if (!(c)) { ++failed; printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

using Proc = UnofficialRipples2Processor;
static void setParam(Proc& p, const char* id, float real) { auto* prm = p.apvts.getParameter(id); prm->setValueNotifyingHost(prm->convertTo0to1(real)); }

struct Opts {
    int fmSrc = 0; float fmAmt = 0.0f; int scMode = 0; float scLevel = 0.0f; float in2 = 0.0f; int output = 0, slope = 1; float freq = 800.0f; float reso = 0.0f;
    bool pingAtStart = false; float decay = 100.0f; float vca = 1.0f; int inputKind = 0;
};
// scMode: 0 = sidechain bus disabled, 1 = DC level scLevel, 2 = burst (1 for the first 2400 samples, then 0), 3 = silent, 4 = sine at 300 Hz of amplitude scLevel
// inputKind: 0 = noise, 1 = silence
static std::vector<float> run(const Opts& o, float* clipMax = nullptr, float* scMeter = nullptr) {
    Proc p;
    juce::AudioProcessor::BusesLayout lay = p.getBusesLayout();
    lay.inputBuses.getReference(1) = o.scMode != 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    CHECK(p.setBusesLayout(lay), "could not set the bus layout (scMode %d)", o.scMode);
    if (o.scMode != 0) CHECK(p.getBus(true, 1)->isEnabled(), "sidechain bus did not switch on");
    const double fs = 48000.0; const int block = 512;
    p.setRateAndBufferSizeDetails(fs, block); p.prepareToPlay(fs, block);
    setParam(p, "freq", o.freq); setParam(p, "reso", o.reso); setParam(p, "output", (float) o.output); setParam(p, "slope", (float) o.slope);
    setParam(p, "fm", o.fmAmt); setParam(p, "fmsrc", (float) o.fmSrc); setParam(p, "in2", o.in2); setParam(p, "vca", o.vca);
    setParam(p, "fmatk", 1.0f); setParam(p, "fmdec", o.decay);
    std::mt19937 rng(7); std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    juce::AudioBuffer<float> buf(4, block); juce::MidiBuffer midi; std::vector<float> out; long pos = 0;
    for (int b = 0; b < 40; ++b) {
        if (o.pingAtStart && b == 2) setParam(p, "ping", 1.0f);
        if (o.pingAtStart && b == 3) setParam(p, "ping", 0.0f);
        buf.clear();
        for (int i = 0; i < block; ++i) { float x = o.inputKind == 1 ? 0.0f : u(rng); buf.setSample(0, i, x); buf.setSample(1, i, x); }
        if (o.scMode != 0) for (int i = 0; i < block; ++i) {
            const long t = pos + i;
            float v = o.scMode == 1 ? o.scLevel : o.scMode == 2 ? (t < 2400 ? 1.0f : 0.0f) : o.scMode == 4 ? o.scLevel * std::sin(6.2831853f * 300.0f * (float) t / 48000.0f) : 0.0f;
            buf.setSample(2, i, v); buf.setSample(3, i, v);
        }
        p.processBlock(buf, midi);
        for (int i = 0; i < block; ++i) out.push_back(buf.getSample(0, i));
        if (clipMax) *clipMax = std::max(*clipMax, p.clipDb.load());
        if (scMeter) *scMeter = std::max(*scMeter, p.scLevel.load());
        pos += block;
    }
    return out;
}
static double rms(const std::vector<float>& y, long from = 5120, long to = -1) { if (to < 0) to = (long) y.size(); double e = 0; for (long i = from; i < to; ++i) e += (double) y[(size_t) i] * y[(size_t) i]; return std::sqrt(e / std::max(1L, to - from)); }

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    { Proc probe; CHECK(probe.getBusCount(true) == 2, "expected a main input and a sidechain input, got %d input buses", probe.getBusCount(true));
      CHECK(probe.getBus(true, 1) != nullptr && !probe.getBus(true, 1)->isEnabledByDefault(), "sidechain should be declared but off by default");
      CHECK(probe.getLatencySamples() == 0, "latency before prepare"); 
      probe.prepareToPlay(48000.0, 512); CHECK(probe.getLatencySamples() == 31, "latency %d", probe.getLatencySamples()); }

    Opts o; o.fmSrc = 0; o.output = 0; const double base = rms(run(o));
    CHECK(base > 0.005, "silent output %g", base);
    // sidechain FM, steady level: the filter opens, so noise comes out louder
    { Opts a = o; a.fmAmt = 0.6f; a.scMode = 1; a.scLevel = 0.8f; CHECK(rms(run(a)) > base * 1.5, "sidechain FM did nothing"); }
    { Opts a = o; a.fmAmt = 0.6f; a.scMode = 0; CHECK(std::fabs(rms(run(a)) - base) < 1e-6, "FM moved with no sidechain"); }
    { Opts a = o; a.fmAmt = 0.6f; a.scMode = 3; CHECK(std::fabs(rms(run(a)) - base) < 1e-6, "silent sidechain changed the output"); }
    { Opts a = o; a.fmAmt = -0.6f; a.scMode = 1; a.scLevel = 0.8f; CHECK(rms(run(a)) < base * 0.7, "negative FM did not close the filter"); }
    { Opts a = o; a.fmAmt = 0.0f; a.scMode = 1; a.scLevel = 0.9f; CHECK(std::fabs(rms(run(a)) - base) < 1e-6, "amount 0 still moved the sound"); }
    // own input
    { Opts a = o; a.fmSrc = 1; a.fmAmt = 0.6f; CHECK(std::fabs(rms(run(a)) - base) > base * 0.02, "own-input FM did nothing"); }
    // envelope from the sidechain burst
    { Opts a = o; a.fmSrc = 2; a.fmAmt = 0.6f; a.scMode = 2; Opts b = a; b.scMode = 3;
      auto ya = run(a), yb = run(b);
      CHECK(rms(ya, 2400, 7200) > rms(yb, 2400, 7200) * 1.6, "envelope FM did not open the filter after the burst: %g vs %g", rms(ya, 2400, 7200), rms(yb, 2400, 7200));
      Opts c = a; c.scMode = 0; CHECK(rms(run(c)) > base * 1.05, "envelope on own input did nothing");
      CHECK(std::fabs(rms(run(b)) - base) < 1e-6, "envelope with silent sidechain moved the sound"); }
    // ping: a click of the Ping parameter opens the filter for a while, then it falls back; nothing without it
    { Opts a = o; a.fmSrc = 3; a.fmAmt = 0.6f; a.pingAtStart = true; a.decay = 40.0f; Opts b = a; b.pingAtStart = false;
      auto ya = run(a), yb = run(b);
      CHECK(std::fabs(rms(yb) - base) < 1e-6, "ping source moved the sound without a ping");
      const long t0 = 2 * 512 + 100;
      CHECK(rms(ya, t0, t0 + 3000) > rms(yb, t0, t0 + 3000) * 1.5, "ping did not open the filter: %g vs %g", rms(ya, t0, t0 + 3000), rms(yb, t0, t0 + 3000));
      CHECK(std::fabs(rms(ya, 15000, 20000) - rms(yb, 15000, 20000)) < 0.02 * rms(yb, 15000, 20000), "ping did not fall back"); }
    // second input: with no main input, the sidechain audio comes through the filter (and not when its level is 0)
    { Opts a = o; a.inputKind = 1; a.scMode = 4; a.scLevel = 0.3f; a.freq = 3000.0f; a.in2 = 1.0f; a.vca = 1.0f;
      const double through = rms(run(a)); Opts b = a; b.in2 = 0.0f; const double off = rms(run(b));
      CHECK(through > 0.02 && off < 1e-6, "second input: %g with level 1, %g with level 0", through, off);
      Opts c = a; c.scMode = 0; CHECK(rms(run(c)) < 1e-6, "second input made sound without a sidechain"); }
    // outputs and slopes all give sound, and differ
    { double r[6]; int k = 0; for (int out = 0; out < 3; ++out) for (int sl = 0; sl < 2; ++sl) { Opts a = o; a.output = out; a.slope = sl; a.freq = 2000.0f; r[k++] = rms(run(a)); }
      for (int i = 0; i < 6; ++i) CHECK(r[i] > 0.001, "output %d is silent", i);
      CHECK(std::fabs(r[0] - r[1]) > 0.1 * r[0], "12 and 24 dB low-pass sound the same");
      CHECK(std::fabs(r[4] - r[5]) < 1e-5, "high-pass changes with slope (it is always 12 dB): %g %g", r[4], r[5]); }
    // low-pass level follows the VCA knob, the high-pass does not
    { Opts a = o; a.vca = 0.2f; Opts b = o; b.vca = 0.8f; CHECK(rms(run(b)) > 2.5 * rms(run(a)), "VCA knob does not change the low-pass level");
      Opts c = o; c.output = 2; c.vca = 0.2f; Opts d = c; d.vca = 0.8f; CHECK(std::fabs(rms(run(c)) - rms(run(d))) < 1e-5, "VCA knob changes the high-pass"); }
    // resonance rings by itself (silence in, tone out) on every output
    for (int out = 0; out < 3; ++out) { Opts a = o; a.output = out; a.inputKind = 1; a.reso = 1.0f; a.fmSrc = 3; a.fmAmt = 0.0f;
        // kick it with a burst on the sidechain audio
        a.scMode = 2; a.in2 = 1.0f; a.freq = 1000.0f; auto y = run(a); CHECK(rms(y, 12000, 19000) > 0.005, "output %d does not self-oscillate (rms %g)", out, rms(y, 12000, 19000)); }
    // meters: the sidechain level is reported, and a loud input lights the clip meter
    { float sc = 0, clip = 0; Opts a = o; a.scMode = 1; a.scLevel = 0.5f; run(a, &clip, &sc); CHECK(std::fabs(sc - 0.5f) < 0.01f, "sidechain meter %g", sc);
      Proc p; p.prepareToPlay(48000.0, 512); setParam(p, "trim", 24.0f); setParam(p, "ingain", 3.5f);
      juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m; for (int i = 0; i < 512; ++i) { buf.setSample(0, i, 0.5f * std::sin(i * 0.1f)); buf.setSample(1, i, 0.5f * std::sin(i * 0.1f)); }
      p.processBlock(buf, m); CHECK(p.clipDb.load() > 3.0f, "clip meter %g dB for a loud input", p.clipDb.load());
      Proc q; q.prepareToPlay(48000.0, 512); juce::AudioBuffer<float> b2(2, 512); for (int i = 0; i < 512; ++i) { b2.setSample(0, i, 0.01f * std::sin(i * 0.1f)); b2.setSample(1, i, 0.01f * std::sin(i * 0.1f)); }
      q.processBlock(b2, m); CHECK(q.clipDb.load() < 0.05f, "clip meter %g dB for a quiet input", q.clipDb.load()); }
    // input stage knob: the dB shown is the stage's gain (pot search)
    for (double g : { 0.25, 0.5, 1.0, 1.4 }) CHECK(std::fabs(ripples2::inputStageGain(Proc::potForGain(g)) / g - 1.0) < 1e-6, "pot search for %g", g);
    // mono layout is accepted, odd ones are not
    { Proc p; juce::AudioProcessor::BusesLayout l = p.getBusesLayout(); l.inputBuses.getReference(0) = juce::AudioChannelSet::mono(); l.outputBuses.getReference(0) = juce::AudioChannelSet::mono(); CHECK(p.isBusesLayoutSupported(l), "mono not supported");
      l.outputBuses.getReference(0) = juce::AudioChannelSet::stereo(); CHECK(!p.isBusesLayoutSupported(l), "mono in / stereo out accepted"); }
    // state: save, change, load
    { Proc p; setParam(p, "freq", 3333.0f); setParam(p, "reso", 0.55f); setParam(p, "output", 2.0f); setParam(p, "fmsrc", 3.0f);
      juce::MemoryBlock mb; p.getStateInformation(mb); Proc q; q.setStateInformation(mb.getData(), (int) mb.getSize());
      CHECK(std::fabs(*q.apvts.getRawParameterValue("freq") - 3333.0f) < 1.0f && std::fabs(*q.apvts.getRawParameterValue("reso") - 0.55f) < 1e-3f && (int) *q.apvts.getRawParameterValue("output") == 2 && (int) *q.apvts.getRawParameterValue("fmsrc") == 3, "state did not come back"); }
    // the panel: builds, draws, at the size it asks for, and the controls are wired to the parameters
    { Proc p; std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); CHECK(ed != nullptr && ed->getWidth() >= 800 && ed->getHeight() >= 560, "editor size");
      auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f); CHECK(img.isValid() && img.getWidth() > 100, "editor did not draw");
      int sliders = 0, buttons = 0; std::function<void(juce::Component*)> walk = [&](juce::Component* c) { if (dynamic_cast<juce::Slider*>(c)) ++sliders; if (dynamic_cast<juce::Button*>(c)) ++buttons; for (auto* ch : c->getChildren()) walk(ch); };
      walk(ed.get()); CHECK(sliders == 10 && buttons == 12, "panel has %d knobs and %d buttons (expected 10 and 12)", sliders, buttons);
      // every child sits inside the window and no two knobs/buttons overlap
      std::vector<juce::Rectangle<int>> r; std::function<void(juce::Component*)> w2 = [&](juce::Component* c) { for (auto* ch : c->getChildren()) { if (dynamic_cast<juce::Slider*>(ch) || dynamic_cast<juce::Button*>(ch)) { CHECK(ed->getLocalBounds().contains(ch->getBounds()), "a control sticks out of the window"); r.push_back(ch->getBounds()); } } };
      w2(ed.get()); for (size_t i = 0; i < r.size(); ++i) for (size_t j = i + 1; j < r.size(); ++j) CHECK(!r[i].intersects(r[j]), "two controls overlap: %d,%d %dx%d and %d,%d %dx%d", r[i].getX(), r[i].getY(), r[i].getWidth(), r[i].getHeight(), r[j].getX(), r[j].getY(), r[j].getWidth(), r[j].getHeight()); }

    printf("%d checks, %d failed\n", total, failed);
    return failed ? 1 : 0;
}
