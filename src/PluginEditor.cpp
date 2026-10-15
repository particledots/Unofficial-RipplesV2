#include "PluginEditor.h"
using namespace ripples2;

Ripples2Look::Ripples2Look() {
    setColour(juce::Slider::textBoxTextColourId, text());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, text());
    setColour(juce::TextButton::textColourOffId, dim());
    setColour(juce::TextButton::textColourOnId, bg());
}

void Ripples2Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider& s) {
    const float d = (float) std::min(w, h) - 8.0f;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, r = d * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float dimmed = s.isEnabled() ? 1.0f : 0.4f;
    juce::Path track; track.addCentredArc(cx, cy, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour(panel().brighter(0.15f)); g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc(cx, cy, r, r, 0.0f, startAngle, angle, true);
    g.setColour((s.isEnabled() ? accent() : dim()).withAlpha(dimmed)); g.strokePath(val, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float br = r - 8.0f;
    g.setColour(panel().brighter(0.05f)); g.fillEllipse(cx - br, cy - br, br * 2, br * 2);
    g.setColour(juce::Colours::black.withAlpha(0.35f)); g.drawEllipse(cx - br, cy - br, br * 2, br * 2, 1.0f);
    juce::Path pt; pt.addRoundedRectangle(-1.5f, -br + 3.0f, 3.0f, br * 0.55f, 1.5f);
    g.setColour(text().withAlpha(dimmed)); g.fillPath(pt, juce::AffineTransform::rotation(angle).translated(cx, cy));
}

void Ripples2Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool) {
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    g.setColour(on ? accent() : (highlighted ? panel().brighter(0.2f) : panel()));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(on ? accent() : panel().brighter(0.3f)); g.drawRoundedRectangle(r, 5.0f, 1.0f);
}

void Ripples2Editor::addKnob(Knob& k, const juce::String& id, const juce::String& name, const juce::String& hintText) {
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 18);
    k.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    auto* prm = proc.apvts.getParameter(id);
    k.slider.setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    k.label.setColour(juce::Label::textColourId, Ripples2Look::dim());
    addAndMakeVisible(k.slider); addAndMakeVisible(k.label);
    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, k.slider);
    hints[&k.slider] = hintText; hints[&k.label] = hintText;
}

void Ripples2Editor::addGroup(Group& gr, const juce::String& id, const juce::String& title, std::vector<juce::String> names, const juce::String& hintText) {
    auto* prm = dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter(id));
    const int count = (int) names.size();
    Group* gp = &gr;
    gr.attach = std::make_unique<juce::ParameterAttachment>(*prm, [gp, count](float v) {
        const int sel = (int) std::lround(v);
        for (int i = 0; i < count; ++i) gp->btns[(size_t) i]->setToggleState(i == sel, juce::dontSendNotification);
    });
    for (int i = 0; i < count; ++i) {
        auto b = std::make_unique<juce::TextButton>(names[(size_t) i]);
        b->onClick = [gp, i] { gp->attach->setValueAsCompleteGesture((float) i); };
        addAndMakeVisible(*b);
        hints[b.get()] = hintText;
        gr.btns.push_back(std::move(b));
    }
    gr.attach->sendInitialUpdate();
    gr.label.setText(title, juce::dontSendNotification);
    gr.label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    gr.label.setColour(juce::Label::textColourId, Ripples2Look::dim());
    addAndMakeVisible(gr.label);
}

Ripples2Editor::Ripples2Editor(UnofficialRipples2Processor& pr) : AudioProcessorEditor(&pr), proc(pr) {
    setLookAndFeel(&look);
    defaultHint = "Hover over a control to see what it does.  Double-click a knob to reset it.  Everything can be automated.";
    addKnob(freq, "freq", "FREQUENCY", "FREQUENCY: where the filter acts (the pole frequency of each of the four stages, 5 Hz to 21.7 kHz). The dashed line on the curve marks it.");
    addKnob(reso, "reso", "RESONANCE", "RESONANCE: the peak at the cutoff. It self-oscillates (rings by itself, at the FREQUENCY pitch) near 88% on SMOOTH, or at 77% on AS DRAWN.");
    addKnob(vca, "vca", "LP VCA LEVEL", "LP VCA LEVEL: how loud the low-pass output is. In the real module this is the low-pass VCA's level. It only works for the LOW-PASS output and does nothing for the band-pass and high-pass.");
    addKnob(inGain, "ingain", "INPUT STAGE", "INPUT STAGE: the module's input level knob (-14 to +3.6 dB). Hot signals are rounded off by a soft clipper in the input stage; the INPUT CLIP light shows when that is happening.");
    addKnob(trim, "trim", "TRIM", "TRIM: extra gain before the input stage, for quiet or loud tracks (not on the real module). Raise it to push into the input stage's soft clip.");
    addKnob(outGain, "outgain", "OUTPUT", "OUTPUT: gain after the filter (not on the real module). 0 dB = the module's own level, where 0 dBFS = 10 V.");
    addKnob(fm, "fm", "FM AMOUNT", "FM AMOUNT: how far the FM source moves the frequency (100% = 5 octaves for a full-scale signal). Negative pushes the frequency down instead of up.");
    addKnob(fmAtk, "fmatk", "ATTACK", "ATTACK: rise time of the ENVELOPE and PING ENV. sources.");
    addKnob(fmDec, "fmdec", "DECAY", "DECAY: fall time of the ENVELOPE and PING ENV. sources.");
    addKnob(in2, "in2", "SIDECHAIN IN", "SIDECHAIN IN: mixes the sidechain track into the filter's audio (the module's second input, about -10 dB, no clipping). Separate from FM: the sidechain can do both.");
    for (Knob* k : { &fmAtk, &fmDec }) k->slider.textFromValueFunction = [](double v) { return v < 10.0 ? juce::String(v, 1) + " ms" : v < 1000.0 ? juce::String((int) std::lround(v)) + " ms" : juce::String(v / 1000.0, 2) + " s"; };
    freq.slider.textFromValueFunction = [](double v) { return v < 1000.0 ? juce::String(v, v < 100.0 ? 1 : 0) + " Hz" : juce::String(v / 1000.0, 2) + " kHz"; };
    freq.slider.valueFromTextFunction = [](const juce::String& s) { double v = s.getDoubleValue(); return s.containsIgnoreCase("k") ? v * 1000.0 : v; };
    auto pct = [](double v) { return juce::String((int) std::lround(v * 100.0)) + " %"; };
    auto pctIn = [](const juce::String& s) { return s.getDoubleValue() / 100.0; };
    for (Knob* k : { &reso, &vca, &fm, &in2 }) { k->slider.textFromValueFunction = pct; k->slider.valueFromTextFunction = pctIn; }
    auto dbf = [](double v) { return juce::String(v, 1) + " dB"; };
    for (Knob* k : { &inGain, &trim, &outGain }) k->slider.textFromValueFunction = dbf;
    for (Knob* k : { &freq, &reso, &vca, &inGain, &trim, &outGain, &fm, &fmAtk, &fmDec, &in2 }) k->slider.updateText();

    addGroup(output, "output", "OUTPUT", { "LOW-PASS", "BAND-PASS", "HIGH-PASS" }, "OUTPUT: which of the module's outputs you hear. Low-pass has its own VCA (LP VCA LEVEL). Band-pass is made from two or four stages; the high-pass is always 12 dB.");
    addGroup(slope, "slope", "SLOPE", { "12 dB", "24 dB" }, "SLOPE: 12 dB (two stages) or 24 dB (four stages) per octave. The high-pass is only 12 dB, so this has no effect on HIGH-PASS.");
    addGroup(taper, "taper", "RESONANCE TAPER", { "SMOOTH", "AS DRAWN" }, "RESONANCE TAPER: SMOOTH raises the resonance evenly and self-oscillates near 88%. AS DRAWN follows the schematic, so most of the resonance is packed near the top and it self-oscillates at 77%.");
    addGroup(fmSrc, "fmsrc", "FM SOURCE", { "SIDECHAIN", "OWN INPUT", "ENVELOPE", "PING ENV." },
             "FM SOURCE: SIDECHAIN = the sidechain signal moves the frequency. OWN INPUT = the input does (audio-rate). ENVELOPE = the level of the sidechain (or the input) moves it. PING ENV. = the envelope fired by the FIRE PING button (or by automating it).");

    pingAttach = std::make_unique<juce::ParameterAttachment>(*proc.apvts.getParameter("ping"), [this](float v) { pingButton.setToggleState(v > 0.5f, juce::dontSendNotification); });
    auto wasDownPtr = std::make_shared<bool>(false);
    pingButton.onStateChange = [this, wasDownPtr] {
        bool& wasDown = *wasDownPtr;
        const bool down = pingButton.isDown();
        if (down && !wasDown) { pingAttach->beginGesture(); pingAttach->setValueAsPartOfGesture(1.0f); }
        if (!down && wasDown) {
            juce::Component::SafePointer<Ripples2Editor> self(this);
            juce::Timer::callAfterDelay(90, [self] { if (self != nullptr) { self->pingAttach->setValueAsPartOfGesture(0.0f); self->pingAttach->endGesture(); } });
        }
        wasDown = down;
    };
    addAndMakeVisible(pingButton);
    hints[&pingButton] = "FIRE PING: click to fire the FM envelope once (rises over ATTACK, falls over DECAY). Set FM SOURCE to PING ENV. and raise FM AMOUNT. Like pinging the module with a trigger.";

    hint.setFont(juce::FontOptions(12.0f)); hint.setColour(juce::Label::textColourId, Ripples2Look::dim());
    hint.setJustificationType(juce::Justification::topLeft); hint.setText(defaultHint, juce::dontSendNotification);
    addAndMakeVisible(hint);
    addMouseListener(this, true);
    setSize(860, 600);
    startTimerHz(30);
}

Ripples2Editor::~Ripples2Editor() { stopTimer(); setLookAndFeel(nullptr); }

void Ripples2Editor::mouseEnter(const juce::MouseEvent& e) {
    auto it = hints.find(e.eventComponent);
    if (it != hints.end()) hint.setText(it->second, juce::dontSendNotification);
}
void Ripples2Editor::mouseExit(const juce::MouseEvent&) { hint.setText(defaultHint, juce::dontSendNotification); }

void Ripples2Editor::timerCallback() {
    auto fall = [](float& shown, float now, float rate) { shown = now > shown ? now : shown + (now - shown) * rate; };
    fall(clipShown, proc.clipDb.load(), 0.15f);
    fall(scShown, proc.scLevel.load(), 0.2f);
    fall(fmShown, std::fabs(proc.fmNow.load()), 0.2f);
    scConn = proc.scConnected.load();
    vca.slider.setEnabled(p("output") < 0.5);
    slope.btns[0]->setEnabled(true);
    const double sig = p("freq") + 3.1 * p("reso") + 7.7 * p("taper") + 13.0 * p("output") + 17.0 * p("slope") + 23.0 * p("vca") + 29.0 * p("ingain");
    const bool curveMoved = std::fabs(sig - lastSig) > 1e-9; lastSig = sig;
    if (curveMoved) repaint(curveArea); 
    repaint(meterArea);
}

void Ripples2Editor::drawCurve(juce::Graphics& g, juce::Rectangle<int> r) const {
    g.setColour(Ripples2Look::bg()); g.fillRoundedRectangle(r.toFloat(), 6.0f);
    const float fLo = 20.0f, fHi = 20000.0f, dbLo = -48.0f, dbHi = 24.0f;
    auto X = [&](double f) { return r.getX() + (float) (std::log(f / fLo) / std::log(fHi / fLo)) * r.getWidth(); };
    auto Y = [&](double d) { return r.getBottom() - (float) ((juce::jlimit<double>(dbLo, dbHi, d) - dbLo) / (dbHi - dbLo)) * r.getHeight(); };
    g.setFont(juce::FontOptions(10.0f)); 
    for (double f : { 100.0, 1000.0, 10000.0 }) { g.setColour(Ripples2Look::panel().brighter(0.1f)); g.drawVerticalLine((int) X(f), (float) r.getY(), (float) r.getBottom());
        g.setColour(Ripples2Look::dim()); g.drawText(f >= 1000.0 ? juce::String((int) (f / 1000.0)) + " kHz" : juce::String((int) f) + " Hz", (int) X(f) + 3, r.getBottom() - 14, 50, 12, juce::Justification::left); }
    for (double d : { -36.0, -24.0, -12.0, 0.0, 12.0 }) { g.setColour(d == 0.0 ? Ripples2Look::panel().brighter(0.35f) : Ripples2Look::panel().brighter(0.1f)); g.drawHorizontalLine((int) Y(d), (float) r.getX(), (float) r.getRight());
        g.setColour(Ripples2Look::dim()); g.drawText(juce::String((int) d) + " dB", r.getX() + 3, (int) Y(d) - 12, 44, 12, juce::Justification::left); }
    const double hz = p("freq"), q = p("reso"); const bool smooth = p("taper") < 0.5;
    const int src = sourceIndex((int) std::lround(p("output")), (int) std::lround(p("slope")));
    const double iReso = resoCurrent(q, smooth), iVca = p("vca") * kIVcaMax;
    const double ing = inputStageGain(UnofficialRipples2Processor::potForGain(std::pow(10.0, p("ingain") / 20.0)));
    g.setColour(Ripples2Look::dim()); { juce::Path d; const float x = X(hz); const float dash[] = { 4.0f, 4.0f }; g.setColour(Ripples2Look::dim().withAlpha(0.7f));
        juce::Line<float> l(x, (float) r.getY(), x, (float) r.getBottom()); g.drawDashedLine(l, dash, 2, 1.0f); }
    juce::Path path; bool started = false;
    const int W = r.getWidth();
    for (int i = 0; i <= W; i += 2) {
        const double f = fLo * std::pow((double) fHi / fLo, (double) i / W);
        const double mag = std::abs(response(src, f, hz, iReso, iVca, ing));
        const float y = Y(20.0 * std::log10(std::max(mag, 1e-6)));
        const float x = (float) (r.getX() + i);
        if (!started) { path.startNewSubPath(x, y); started = true; } else path.lineTo(x, y);
    }
    g.saveState(); g.reduceClipRegion(r);
    g.setColour(Ripples2Look::accent()); g.strokePath(path, juce::PathStrokeType(2.0f));
    g.restoreState();
    g.setColour(Ripples2Look::dim()); g.setFont(juce::FontOptions(10.0f));
    g.drawText("Shape of the filter at the current settings (small signals; not a measurement of the real module)", r.getX() + 50, r.getY() + 3, r.getWidth() - 60, 12, juce::Justification::right);
}

void Ripples2Editor::paint(juce::Graphics& g) {
    g.fillAll(Ripples2Look::bg());
    g.setColour(Ripples2Look::panel()); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 10.0f);
    g.setColour(Ripples2Look::text()); g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("UNOFFICIAL RIPPLES V2", 28, 18, 360, 30, juce::Justification::centredLeft);
    g.setColour(Ripples2Look::dim()); g.setFont(juce::FontOptions(12.0f));
    g.drawText("four-pole filter, modelled from a published circuit", 28, 46, 420, 16, juce::Justification::centredLeft);
    drawCurve(g, curveArea);
    g.setColour(Ripples2Look::panel().brighter(0.2f));
    g.drawHorizontalLine(380, 28.0f, (float) getWidth() - 28.0f);
    g.setColour(Ripples2Look::dim()); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("FM  AND  SIDECHAIN", 28, 386, 200, 14, juce::Justification::centredLeft);
    // clip light and sidechain meter (top right)
    auto m = meterArea;
    const float clip = juce::jlimit(0.0f, 1.0f, clipShown / 3.0f);
    const bool lit = clipShown > 0.3f;
    g.setColour(lit ? juce::Colour(0xffe5484d).interpolatedWith(juce::Colour(0xffff7a59), clip) : Ripples2Look::bg()); g.fillEllipse((float) m.getX(), (float) m.getY() + 2, 12.0f, 12.0f);
    g.setColour(Ripples2Look::panel().brighter(0.3f)); g.drawEllipse((float) m.getX(), (float) m.getY() + 2, 12.0f, 12.0f, 1.0f);
    g.setColour(Ripples2Look::dim()); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(lit ? "INPUT CLIP  " + juce::String(clipShown, 1) + " dB" : "INPUT CLIP", m.getX() + 18, m.getY(), 130, 16, juce::Justification::centredLeft);
    const int bx = m.getX(), by = m.getY() + 20, bw = 150;
    g.setColour(Ripples2Look::bg()); g.fillRoundedRectangle((float) bx, (float) by, (float) bw, 6.0f, 3.0f);
    g.setColour(Ripples2Look::accent()); g.fillRoundedRectangle((float) bx, (float) by, bw * juce::jlimit(0.0f, 1.0f, scShown), 6.0f, 3.0f);
    g.setColour(Ripples2Look::dim());
    g.drawText(scConn ? "SIDECHAIN LEVEL" : "SIDECHAIN: none connected", bx, by + 8, 190, 12, juce::Justification::centredLeft);
    g.setColour(Ripples2Look::bg()); g.fillRoundedRectangle((float) bx, (float) by + 24, (float) bw, 6.0f, 3.0f);
    g.setColour(Ripples2Look::accent().darker(0.2f)); g.fillRoundedRectangle((float) bx, (float) by + 24, bw * juce::jlimit(0.0f, 1.0f, fmShown / 5.0f), 6.0f, 3.0f);
    g.setColour(Ripples2Look::dim()); g.drawText("FM NOW (full bar = 5 octaves)", bx, by + 32, 190, 12, juce::Justification::centredLeft);
}

void Ripples2Editor::resized() {
    const int W = getWidth();
    meterArea = { W - 28 - 172, 12, 172, 58 };
    // row of button groups
    auto place = [](Group& g, int x, int y, int bw, int gap) {
        g.label.setBounds(x, y, 160, 13);
        for (size_t i = 0; i < g.btns.size(); ++i) g.btns[i]->setBounds(x + (int) i * (bw + gap), y + 14, bw, 26);
    };
    place(output, 28, 66, 96, 6);
    place(slope, 28 + 3 * 102 + 18, 66, 66, 6);
    place(taper, 28 + 3 * 102 + 18 + 2 * 72 + 18, 66, 92, 6);
    curveArea = { 28, 114, W - 56, 136 };
    // six main knobs
    Knob* big[6] = { &freq, &reso, &vca, &inGain, &trim, &outGain };
    const int kw = (W - 56) / 6;
    for (int i = 0; i < 6; ++i) {
        auto cell = juce::Rectangle<int>(28 + i * kw, 254, kw, 122);
        big[i]->label.setBounds(cell.removeFromTop(16));
        big[i]->slider.setBounds(cell);
    }
    // FM section
    place(fmSrc, 28, 404, 100, 6);
    pingButton.setBounds(28 + 4 * 106 + 12, 418, 90, 26);
    Knob* fk[4] = { &fm, &fmAtk, &fmDec, &in2 };
    for (int i = 0; i < 4; ++i) {
        auto cell = juce::Rectangle<int>(28 + i * 150, 452, 150, 100);
        fk[i]->label.setBounds(cell.removeFromTop(16));
        fk[i]->slider.setBounds(cell);
    }
    hint.setBounds(28, 556, W - 56, 34);
}
