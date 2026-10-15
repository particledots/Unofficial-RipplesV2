#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Ripples2Model.h"
#include <atomic>

class UnofficialRipples2Processor : public juce::AudioProcessor {
public:
    UnofficialRipples2Processor();
    ~UnofficialRipples2Processor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Unofficial Ripples V2"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }   // it can ring (self-oscillate) when the input stops

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // read by the panel (about 30 times a second)
    std::atomic<float> clipDb { 0.0f };        // how much the input stage is squashing right now, in dB (0 = clean)
    std::atomic<float> scLevel { 0.0f };       // peak level of the sidechain input, 0..1 (full scale = 1)
    std::atomic<bool> scConnected { false };
    std::atomic<float> fmNow { 0.0f };         // the FM currently applied, in octaves (for the panel's light)

    // Gain of the input stage for a given pot position, and the pot position that gives a wanted gain (bisection)
    static double potForGain(double g);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ripples2::Voice voices[2];
    ripples2::EnvFollower env;
    ripples2::PingEnv ping;
    double smHzLog = std::log(1000.0), smQ = 0.0, smVca = 0.3, smIn2 = 0.0;
    double fs = 44100.0;
    bool first = true, pingWas = false;
    float peakSc = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UnofficialRipples2Processor)
};
