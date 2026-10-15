#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <map>

// A plain, original look: dark panel, round knobs with a value arc, button groups, a curve showing the filter's shape.
class Ripples2Look : public juce::LookAndFeel_V4 {
public:
    Ripples2Look();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::FontOptions(13.0f, juce::Font::bold); }
    static juce::Colour bg()     { return juce::Colour(0xff1c1f24); }
    static juce::Colour panel()  { return juce::Colour(0xff262a31); }
    static juce::Colour accent() { return juce::Colour(0xffe8a33d); }
    static juce::Colour text()   { return juce::Colour(0xffe6e8eb); }
    static juce::Colour dim()    { return juce::Colour(0xff8b929c); }
};

class Ripples2Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Ripples2Editor(UnofficialRipples2Processor&);
    ~Ripples2Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    struct Group {
        std::vector<std::unique_ptr<juce::TextButton>> btns;
        std::unique_ptr<juce::ParameterAttachment> attach;
        juce::Label label;
    };
    void addKnob(Knob&, const juce::String& id, const juce::String& name, const juce::String& hintText);
    void addGroup(Group&, const juce::String& id, const juce::String& title, std::vector<juce::String> names, const juce::String& hintText);
    void timerCallback() override;
    void drawCurve(juce::Graphics&, juce::Rectangle<int>) const;
    double p(const char* id) const { return (double) *proc.apvts.getRawParameterValue(id); }

    UnofficialRipples2Processor& proc;
    Ripples2Look look;
    Knob freq, reso, vca, inGain, trim, outGain, fm, fmAtk, fmDec, in2;
    Group output, slope, taper, fmSrc;
    juce::TextButton pingButton { "FIRE PING" };
    std::unique_ptr<juce::ParameterAttachment> pingAttach;
    juce::Label hint;
    juce::String defaultHint;
    std::map<juce::Component*, juce::String> hints;
    juce::Rectangle<int> curveArea, meterArea;
    float clipShown = 0.0f, scShown = 0.0f, fmShown = 0.0f;
    bool scConn = false;
    double lastSig = -1.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ripples2Editor)
};
