#pragma once

#include <cstdint>
#include <string>

// Renderer-side fingerprinting mitigations: session-consistent noise on Canvas
// and Web Audio readback, and generic WebGL vendor/renderer strings. Injected
// once per V8 context, from CefRenderProcessHandler::OnContextCreated, which
// runs before any page script (see mtk_app.cpp). This file has no CEF
// dependency of its own, so the pure parts are testable without a browser.
//
// The seed is generated once in the browser process at startup and carried to
// every renderer through a command-line switch (preferences are only loaded
// in the browser process, and there is no other IPC channel for this yet). It
// stays the same for every tab and every site for the life of one running
// browser, and changes on the next launch: enough to break cross-session
// correlation without making two tabs of the same site disagree with each
// other, which would be its own kind of unusual and detectable signal.
namespace mtk::fingerprint {

// Name of the command-line switch carrying "<on|off>:<16 hex digit seed>",
// e.g. "on:1a2b3c4d5e6f7890". Read the same way in every process.
inline constexpr char kSwitchName[] = "mtk-fingerprint-protection";

// True when `value` (the switch's value, not the whole "name=value" pair)
// requests protection, i.e. it starts with "on:". Anything else, including a
// missing or malformed value, is treated as off, which is the fail-safe
// direction for a switch value produced outside this file's own encoder.
bool IsEnabledSwitchValue(const std::string& value);

// Encodes on/off and a seed into the single string passed as the switch
// value. Kept separate from the parsing side purely so both directions are
// covered by the same round-trip test.
std::string EncodeSwitchValue(bool enabled, uint64_t seed);

// Seed encoded in `value` (the switch's value), or 0 when it cannot be parsed.
// A seed of 0 still produces valid, deterministic noise; it is not a sentinel
// for "disabled" (that is IsEnabledSwitchValue's job).
uint64_t DecodeSeed(const std::string& value);

// The script text itself, deterministic for a given seed. Returns an empty
// string when `enabled` is false, so a caller can inject the result
// unconditionally without a separate branch.
std::string BuildScript(bool enabled, uint64_t seed);

}  // namespace mtk::fingerprint
