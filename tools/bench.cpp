// Speed check: how much of one core a stereo instance would use at 48 kHz (this machine, not a Mac).
#include "Ripples2Model.h"
#include <chrono>
#include <cstdio>
using namespace ripples2;
int main() {
    const double fs = 48000.0; const int n = 48000 * 20;
    for (int mode = 0; mode < 3; ++mode) {
        Voice v[2]; Voice::Settings s; s.src = mode == 2 ? BP4 : LP4_VCA; s.stageHz = 1500.0; s.iReso = 0.6 * kIThreshold; s.iVca = 0.3 * kIVcaMax; s.in2 = mode == 1 ? 1.0 : 0.0;
        float acc = 0; auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i) for (int c = 0; c < 2; ++c) acc += v[c].process((float) (0.3 * std::sin(i * 0.05)), (float) (0.2 * std::sin(i * 0.011)), s, fs, mode == 1 ? 0.5 * std::sin(i * 0.001) : 0.0);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("%s: %.2f%% of a core (stereo, 48 kHz)   [%g]\n", mode == 0 ? "plain" : mode == 1 ? "with FM and second input" : "band-pass 24 dB", 100.0 * sec / (n / fs), (double) acc);
    }
}
