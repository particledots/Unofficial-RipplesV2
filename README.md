# Unofficial Ripples V2 — AU / VST3 (experiment)

A software model of the **2020 revision of the Ripples filter circuit** (schematic `ripples_v90`). The hardware has no firmware, so this is not a port of code:
it is a model of the circuit, written from the drawing. Not a Mutable Instruments product, and not made or endorsed by the module's makers.
It is a separate plug-in from "Unofficial Ripples" (the first, 2019 circuit): different ID, both can be installed together.

**Status, plainly:**
* Built and tested on Linux (model tests, 29 deliberate-break checks, speed). The GitHub build repeats the tests on macOS; see the bottom for that result.
* **Not yet loaded in Logic (M4 or Intel). Not yet listened to. Nobody has compared it with the hardware.** The tests check that the model does what the *drawing* says (gains, slopes, where it self-oscillates, clip). They cannot say whether it sounds like the real module. That is for Bryn to judge by ear.

## What it models

* **Four identical stages** (V2164 VCA feeding an integrator, 33.2k / 220 pF), as inverting one-pole low-passes sharing one control. **Frequency** = the pole frequency of each stage (5 Hz – 21.7 kHz; the top is set by the 33.2k and 220p parts).
* **Resonance:** an LM13700 turns the difference between input and the last stage into a current fed into the first stage; the feedback is solved exactly for every sample (no one-sample delay). In this revision the input is also injected through the OTA, so the pass-band level does not fall as the resonance rises.
* **Outputs:** low-pass (12 dB: two stages, 24 dB: four) through the low-pass VCA, band-pass (12 / 24 dB), high-pass (12 dB, always).
* **Input stage:** the input pot (-14.2 … +3.6 dB), then an op-amp whose feedback has a zener pair, so loud signals are rounded off from about 2 V and never get past about 3.3 V. The **INPUT CLIP** light shows how much it is squashing.
* **Second input** (about -10.3 dB, not clipped): the sidechain track mixed into the filter's audio.
* Op-amps stop at the supply rails: linear to 70%, then a smooth bend. 2x oversampling (63-tap half-band filter, 31 samples latency, reported to the host).

## Things that are guesses, not read from the drawing

1. **Level:** 0 dBFS = 10 V (the module has no such number; I chose it, as in V1 after its level fix).
2. **Rails:** ±11 V, with the soft bend above 70%.
3. **The zener pair** in the input stage is only named `BZT585B3V3T`; its knee (1 µA at 2.30 V, e-fold per 0.197 V) is my fit to a 3.3 V zener plus a diode, not a datasheet curve.
4. **OTA:** a plain tanh (the LM13700's linearising diodes are not modelled).
5. **Resonance taper:** *As drawn* follows the Q-control chain I worked through (self-oscillation at knob ≈ 0.77); *Smooth* (default) is a deliberate departure, the same curve as V1's, because V1's user found the drawn one harsh. Which is closer to the hardware is for Bryn's ears.
6. **Frequency control:** no V/oct input, CV attenuator or temperature network; Frequency is a direct pole frequency.
7. **FM sources:** the hardware has a CV jack, not these. Sidechain / Own input / Envelope / Ping are my own ways of driving it. The scale (100% = 5 octaves for a full-scale signal) is a choice. Ping: linear rise over ATTACK, exponential fall over DECAY.
8. **Low-pass VCA level:** 100% = 180 µA (the drawing's maximum from its divider); the hardware's own CV chain is not modelled.
9. Resistor / capacitor values were read off the PDF picture and have not been checked against a parts list. Part tolerances and chip quirks are not modelled.

## Controls

| Control | What it does |
|---|---|
| OUTPUT | Low-pass / Band-pass / High-pass (switches with a 10 ms fade) |
| SLOPE | 12 or 24 dB (no effect on High-pass) |
| RESONANCE TAPER | Smooth (self-oscillates near 88%) or As drawn (77%) |
| FREQUENCY, RESONANCE | as above |
| LP VCA LEVEL | level of the low-pass output only |
| INPUT STAGE, TRIM, OUTPUT | module's input pot (dB); extra gain before it; gain after the filter |
| FM SOURCE | Sidechain, Own input, Envelope (level of the sidechain, or of the input if none), Ping |
| FM AMOUNT | -100…+100% (negative pushes the frequency down) |
| ATTACK / DECAY | for Envelope and Ping |
| PING | click to fire the envelope once (it is also an automatable parameter) |
| SIDECHAIN IN | how much of the sidechain track is mixed into the filter's audio |

The panel has a curve of the filter's shape at the current settings (small-signal maths, not a measurement), an input-clip light, a sidechain level bar, an FM meter, and a line at the bottom that explains whatever the mouse is over. Every control is a normal parameter, so all can be automated.

## Sidechain in Logic

The sidechain bus is declared from the first install. Use the **Side Chain** menu in the plug-in window's header and pick a **track** (picking "input 1/2" does nothing). The SIDECHAIN LEVEL bar shows when it is really connected. If the menu is missing: Plug-in Manager, select it, **Reset & Rescan Selection**. Raise the version in `CMakeLists.txt` with every build that changes the plug-in's buses.

## Tests

* `tests/ripples2_test.cpp` (no JUCE, ~1,800 checks): oversampler; numbers worked out by hand from the schematic's component values; every output against the circuit equations (and the panel's response curve); slopes and corner levels; sample-rate independence; resonance (threshold, pitch, peak height by hand formula, level bounded); input stage (clip against an independent solve, gain range, second input); VCA; stability with random extreme settings; output switching; FM (+1 octave = double frequency, timing, audio-rate); rail bend; both tapers; envelope and ping timing; silence and reset.
* `tools/mutate.sh`: 29 deliberate breaks (wrong sign, wrong resistor, no warping, wrong gains, no clip, wrong zener, 5 V level, VCA not saturating, FM wrong/upside down/not delayed, taper wrong, ping/envelope timing, …); each must make the tests fail. (Rail clipping inside the four stages cannot be reached by any signal the circuit makes, so it has no test.)
* `tools/proc_test.cpp` (the whole plug-in, in the build): sidechain bus reaches FM, second input and meters; every FM source; Ping; outputs/slopes; self-oscillation on every output; state save/load; mono layout; the panel builds, draws, and no two controls overlap.
* Speed on the Linux test machine: about 2.2% of one core for a stereo instance at 48 kHz (2.5% with FM and the second input). **Not measured on a Mac or in Logic.**

Run the model tests yourself: `c++ -std=c++17 -O2 -Isrc tests/ripples2_test.cpp -o ripples2_test && ./ripples2_test`.

## Build

GitHub Actions, macOS runner, universal (arm64 + x86_64), minimum macOS 11. Download the **Unofficial-RipplesV2-macOS** artifact from the latest passing run, then follow `INSTALL.txt` inside.

## Licence and credit

The model is written from the published hardware design files of the Ripples (design by Émilie Gillet), released under **CC BY-SA 3.0**. A plug-in built from the schematic is, in my reading, an adaptation, so credit is given here and any redistribution should keep CC BY-SA 3.0 for the modelling code. That is my reading, not legal advice; settle it before sharing the plug-in. No firmware code is used (there is none). JUCE is used under its own terms (AGPLv3 / commercial).
