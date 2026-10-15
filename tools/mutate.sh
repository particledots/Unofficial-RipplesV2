#!/bin/bash
# Deliberate breaks: each one changes a COPY of the model, and the tests must then fail. Usage: tools/mutate.sh   (exit 0 = every break was caught)
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d); mkdir -p "$W/src" "$W/tests"; cp tests/ripples2_test.cpp "$W/tests/"
caught=0; missed=0
try() { # name, old, new
  cp src/Ripples2Model.h "$W/src/Ripples2Model.h"
  python3 - "$W/src/Ripples2Model.h" "$2" "$3" <<'PY' || { echo "  (mutation text not found: $1)"; missed=$((missed+1)); return; }
import sys
f,a,b=sys.argv[1:4]; s=open(f).read()
if a not in s: sys.exit(1)
open(f,'w').write(s.replace(a,b,1))
PY
  if ! c++ -std=c++17 -O1 -I"$W/src" "$W/tests/ripples2_test.cpp" -o "$W/t" 2>"$W/err"; then echo "  (does not compile: $1)"; missed=$((missed+1)); return; fi
  if "$W/t" >"$W/out" 2>&1; then echo "MISSED  $1"; missed=$((missed+1)); else echo "caught  $1  ($(grep -c FAIL "$W/out") failing checks)"; caught=$((caught+1)); fi
}
try "resonance feedback sign flipped" "const double F  = I - iReso * th;" "const double F  = I + iReso * th;"
try "wrong stage resistor" "kR_stage     = 33.2e3" "kR_stage     = 39.2e3"
try "no frequency warping" "const double g = fastTan(kPi * wcT / fs);" "const double g = kPi * wcT / fs;"
try "OTA divider wrong" "kOtaDiv      = 1.5e3 / (33.2e3 + 1.5e3)" "kOtaDiv      = 1.5e3 / (24.2e3 + 1.5e3)"
try "high-pass gain wrong" "kHpGain      = 51e3 / 33.2e3" "kHpGain      = 33.2e3 / 33.2e3"
try "24 dB band-pass: LP3 counted once" "(y[1] + 2.0 * y[2] + y[3])" "(y[1] + y[2] + y[3])"
try "high-pass: LP1 counted once" "(x + y[1] + 2.0 * y[0])" "(x + y[1] + y[0])"
try "12 dB band-pass missing LP2" "(y[0] + y[1]));" "(y[0]));"
try "band-pass 12 dB polarity not flipped" "kBp2Sign     = -1.0;" "kBp2Sign     = 1.0;"
try "second input gain wrong" "30e3 / (68e3 + 30e3);" "30e3 / (30e3 + 30e3);"
try "second input not added" "clipRail(-vo - kIn2Gain * v2)" "clipRail(-vo)"
try "input clip too soft/high (zener knee)" "kClampV1 = 2.30" "kClampV1 = 3.60"
try "input stage ignores clip" "const double vo = clipTable().eval(iin);" "const double vo = iin * kRfIn;"
# (rail clipping inside the four stages cannot be reached by any signal the circuit can make - the OTA current limits them to under 6 V - so it has no test and no mutation)
try "hard-wrong rail knee (starts at 40%)" "const double a = 0.7 * kRail" "const double a = 0.4 * kRail"
try "level 5 V instead of 10 V" "kVoltsPerFS  = 10.0" "kVoltsPerFS  = 5.0"
try "VCA output not saturating" "fastTanh(ch0.vcaU4 * kVcaDiv / kVt2)" "(ch0.vcaU4 * kVcaDiv / kVt2)"
try "VCA output not AC coupled" "vcaU2 = y[1] - hpV2; vcaU4 = y[3] - hpV4;" "vcaU2 = y[1]; vcaU4 = y[3];"
try "wrong trapezoid state update" "s[k] = 2.0 * y[k] - s[k];" "s[k] = y[k];"
try "FM not delayed to match audio" "const int readPos = (fmPos - kFmDelay + kFmBufLen) % kFmBufLen;" "const int readPos = fmPos;"
try "FM upside down" "s.stageHz * fastExp2(fmA)" "s.stageHz * fastExp2(-fmA)"
try "FM too weak (half octave scale)" "s.stageHz * fastExp2(fmB)" "s.stageHz * fastExp2(0.5 * fmB)"
try "no fade when the output changes" "xf = std::min(1.0, xf + 1.0 / (0.010 * hostFs));" "xf = 1.0;"
try "smooth taper onset wrong" "kSmoothOnset = 0.88" "kSmoothOnset = 0.70"
try "smooth taper too steep" "std::exp(-1.5 * q)" "std::exp(-3.0 * q)"
try "taper switch ignored" "inline double resoCurrent(double q, bool smooth) { return smooth ?" "inline double resoCurrent(double q, bool smooth) { return true ?"
try "ping rises too fast" "up = 1.0 / std::max(1.0, std::max(0.01, attackMs) * 0.001 * fs);" "up = 4.0 / std::max(1.0, std::max(0.01, attackMs) * 0.001 * fs);"
try "ping does not decay right" "aD = std::exp(-1.0 / (std::max(0.1, decayMs) * 0.001 * fs));" "aD = std::exp(-0.5 / (std::max(0.1, decayMs) * 0.001 * fs));"
try "envelope follower attack wrong" "aA = 1.0 - std::exp(-1.0 / (std::max(0.01, attackMs) * 0.001 * fs));" "aA = 1.0 - std::exp(-0.3 / (std::max(0.01, attackMs) * 0.001 * fs));"
try "response curve formula: wrong resonance gain" "const double K = iReso * kOtaDiv * kR_stage / kVt2;" "const double K = 0.8 * iReso * kOtaDiv * kR_stage / kVt2;"
rm -rf "$W"
echo "$caught caught, $missed missed"
[ "$missed" -eq 0 ]
