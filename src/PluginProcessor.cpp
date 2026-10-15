#include "PluginProcessor.h"
#include "PluginEditor.h"

UnofficialRipples2Processor::UnofficialRipples2Processor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                      .withInput("Sidechain (FM / IN 2)", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "STATE", createLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout UnofficialRipples2Processor::createLayout() {
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto F = [](const char* id, const char* name, NormalisableRange<float> r, float def, const char* label = "") {
        return std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel(label)); };
    p.push_back(F("freq", "Frequency", NormalisableRange<float>(5.0f, 21700.0f, 0.0f, 0.25f), 1000.0f, "Hz"));
    p.push_back(F("reso", "Resonance", NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "taper", 1 }, "Resonance taper", StringArray{ "Smooth", "As drawn" }, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "output", 1 }, "Output", StringArray{ "Low-pass", "Band-pass", "High-pass" }, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "slope", 1 }, "Slope", StringArray{ "12 dB", "24 dB" }, 1));
    p.push_back(F("vca", "Low-pass VCA level", NormalisableRange<float>(0.0f, 1.0f), 0.3f));
    p.push_back(F("ingain", "Input stage gain", NormalisableRange<float>((float) (20.0 * std::log10(ripples2::kInGainMin)), (float) (20.0 * std::log10(ripples2::kInGainMax))), 0.0f, "dB"));
    p.push_back(F("trim", "Input trim", NormalisableRange<float>(-24.0f, 24.0f), 0.0f, "dB"));
    p.push_back(F("outgain", "Output gain", NormalisableRange<float>(-24.0f, 24.0f), 0.0f, "dB"));
    p.push_back(F("in2", "Sidechain audio (IN 2)", NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(F("fm", "FM amount", NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "fmsrc", 1 }, "FM source", StringArray{ "Sidechain", "Own input", "Envelope", "Ping" }, 0));
    p.push_back(F("fmatk", "FM attack", NormalisableRange<float>(0.1f, 100.0f, 0.0f, 0.4f), 1.0f, "ms"));
    p.push_back(F("fmdec", "FM decay", NormalisableRange<float>(5.0f, 3000.0f, 0.0f, 0.4f), 150.0f, "ms"));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "ping", 1 }, "Ping", false));
    return { p.begin(), p.end() };
}

double UnofficialRipples2Processor::potForGain(double g) {
    double lo = 0.0, hi = 1.0;                       // gain falls as u rises
    for (int i = 0; i < 40; ++i) { const double m = 0.5 * (lo + hi); (ripples2::inputStageGain(m) > g ? lo : hi) = m; }
    return 0.5 * (lo + hi);
}

void UnofficialRipples2Processor::prepareToPlay(double sampleRate, int) {
    fs = sampleRate;
    for (auto& v : voices) v.reset();
    env.reset(); ping.reset();
    first = true; pingWas = false;
    setLatencySamples(ripples2::Oversampler2x::latencySamples());
}

bool UnofficialRipples2Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    if (!(out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())) return false;
    const auto sc = l.getChannelSet(true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void UnofficialRipples2Processor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    auto mainBuf = getBusBuffer(buf, false, 0);
    const int chans = std::min(mainBuf.getNumChannels(), 2);
    const float* sc[2] = { nullptr, nullptr }; int scChans = 0;
    if (auto* scBus = getBus(true, 1)) if (scBus->isEnabled()) {
        auto scBuf = getBusBuffer(buf, true, 1);
        scChans = std::min(scBuf.getNumChannels(), 2);
        for (int c = 0; c < scChans; ++c) sc[c] = scBuf.getReadPointer(c);
    }
    auto P = [this](const char* id) { return (double) *apvts.getRawParameterValue(id); };
    auto C = [&](const char* id, int hi) { return juce::jlimit(0, hi, (int) std::lround(P(id))); };
    const double targetHz = P("freq"), targetQ = P("reso"), targetVca = P("vca"), targetIn2 = P("in2");
    const bool smooth = C("taper", 1) == 0;
    const int slope = C("slope", 1), outSel = C("output", 2), fmSrc = C("fmsrc", 3);
    const double fmScale = P("fm") * 5.0;      // octaves per unit of FM signal: 100% = 5 octaves for a full-scale signal
    env.setTimes(P("fmatk"), P("fmdec"), fs);
    ping.setTimes(P("fmatk"), P("fmdec"), fs);
    const bool pingNow = P("ping") > 0.5f;
    if (pingNow && !pingWas) ping.trigger();
    pingWas = pingNow;
    ripples2::Voice::Settings st;
    st.src = ripples2::sourceIndex(outSel, slope);
    st.inGain = ripples2::inputStageGain(potForGain(std::pow(10.0, P("ingain") / 20.0)));
    st.trim = std::pow(10.0, P("trim") / 20.0);
    st.outGain = std::pow(10.0, P("outgain") / 20.0);
    if (first) { smHzLog = std::log(std::max(1.0, targetHz)); smQ = targetQ; smVca = targetVca; smIn2 = targetIn2; first = false; }
    const double a = 1.0 - std::exp(-1.0 / (0.005 * fs));
    const double tl = std::log(std::max(1.0, targetHz));
    float* d[2] = { mainBuf.getWritePointer(0), chans > 1 ? mainBuf.getWritePointer(1) : nullptr };
    float scPk = 0.0f, fmLast = 0.0f;
    for (auto& v : voices) v.peakIin = 0.0;
    for (int i = 0; i < n; ++i) {
        smHzLog += a * (tl - smHzLog); smQ += a * (targetQ - smQ); smVca += a * (targetVca - smVca); smIn2 += a * (targetIn2 - smIn2);
        st.stageHz = std::exp(smHzLog);
        st.iReso = ripples2::resoCurrent(smQ, smooth);
        st.iVca = smVca * ripples2::kIVcaMax;
        st.in2 = smIn2 < 1e-4 ? 0.0 : smIn2;
        double fmSide = 0.0, scMono = 0.0;
        if (scChans == 1) scMono = sc[0][i]; else if (scChans == 2) scMono = 0.5 * ((double) sc[0][i] + (double) sc[1][i]);
        fmSide = scMono;
        if (scChans > 0) scPk = std::max(scPk, std::fabs((float) scMono));
        double ownMono = chans > 1 ? 0.5 * ((double) d[0][i] + (double) d[1][i]) : (double) d[0][i];
        double envLevel = 0.0, pingLevel = 0.0;
        if (fmSrc == 2) envLevel = env.run(scChans > 0 ? scMono : ownMono);
        if (fmSrc == 3) pingLevel = ping.run();
        for (int c = 0; c < chans; ++c) {
            const float x = d[c][i];
            const double src = fmSrc == 1 ? (double) x : fmSrc == 2 ? envLevel : fmSrc == 3 ? pingLevel : fmSide;
            const double fm = src * fmScale;
            const float in2sig = scChans == 0 ? 0.0f : (scChans == 1 ? sc[0][i] : sc[c][i]);
            d[c][i] = voices[c].process(x, in2sig, st, fs, fm);
            if (c == 0) fmLast = (float) fm;
        }
    }
    double pk = 0.0; for (int c = 0; c < chans; ++c) pk = std::max(pk, voices[c].peakIin);
    if (pk > 0.0) {
        const double lin = pk * ripples2::kRfIn, got = ripples2::clipTable().eval(pk);
        const float db = (float) (20.0 * std::log10(std::max(1.0, lin / std::max(got, 1e-9))));
        clipDb.store(db);
    } else clipDb.store(0.0f);
    scLevel.store(scPk); scConnected.store(scChans > 0); fmNow.store(fmLast);
}

juce::AudioProcessorEditor* UnofficialRipples2Processor::createEditor() { return new Ripples2Editor(*this); }

void UnofficialRipples2Processor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
}
void UnofficialRipples2Processor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UnofficialRipples2Processor(); }
