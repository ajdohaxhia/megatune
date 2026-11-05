#pragma once
#include <algorithm>
#include <cmath>
// Original registration: 0xfa1758, 0xfa1940, 0xfa1d24, 0xfa2228, 0xfa2408.
namespace originalControls {
// 0xf9a84c: exact double constants for the two normalized branches.
inline constexpr double speedSplit = 0.784;
inline constexpr double speedNegativeWeight = 0.21599999999999997;
inline constexpr float speedDefault = 25.f;
inline constexpr float amountDefault = 100.f;
inline constexpr float sustainDefault = 0.f;
inline constexpr float doublerDefault = 0.f;
inline constexpr float widthDefault = 0.f;
}
// The original's 0xf18b00 messages establish direction and held-note scope only.
// Timing magnitude, one-second ramp and 1ms floor remain independent estimates.
inline float megaTuneSmoothingSeconds(float speed, float sustain, float heldSeconds) {
 const float held = std::clamp(heldSeconds, 0.f, 1.f);
 return std::max(.001f, std::max(0.f, speed) * .001f
     * (1 - std::clamp(sustain, -100.f, 100.f) * .005f * held));
}

// Derived control mapping: 0xf9a798, 0xf9a7f8, 0xf9a84c,
// 0x13f3588 and 0x13f3658, corroborated against ARM64 instructions.
inline float megaTuneSpeedFromNormalized(float normalized) {
 const double n = std::clamp((double)normalized, 0., 1.);
 if (n >= originalControls::speedSplit) return (float)(-3. * (n - originalControls::speedSplit) / originalControls::speedNegativeWeight);
 return (float)((30625. * std::pow(49., -n / originalControls::speedSplit) - 625.) / 150.);
}
inline float megaTuneSpeedToNormalized(float speed) {
 const double v = std::clamp((double)speed, -3., 200.);
 return (float)(v <= 0. ? originalControls::speedSplit - originalControls::speedNegativeWeight * v / 3.
      : originalControls::speedSplit * std::log((150. * v + 625.) / 30625.) / std::log(1. / 49.));
}
// Amount curve midpoint 75: 0xf9a8c8; the linear UI approximation was incorrect.
inline float megaTuneAmountFromNormalized(float normalized) {
 const double n = std::clamp((double)normalized, 0., 1.);
 return (float)(112.5 * (1. - std::pow(9., -n)));
}
inline float megaTuneAmountToNormalized(float amount) {
 const double v = std::clamp((double)amount, 0., 100.);
 return (float)(std::log(1. - v / 112.5) / std::log(1. / 9.));
}
// Editor meter: positive means the input is sharp of the target.
inline float megaTuneCentsBetween(float inputMidi, float targetMidi) {
 return (inputMidi - targetMidi) * 100.f;
}
// -100..+100 cents maps to left..right; values outside stay on the end stops.
inline float megaTuneMeterFraction(float cents) {
 return std::clamp((cents + 100.f) / 200.f, 0.f, 1.f);
}
