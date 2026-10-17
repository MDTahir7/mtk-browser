#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Renderer-side fingerprinting mitigations: session-consistent noise on Canvas
// and Web Audio readback, generic WebGL/WebGL2 vendor/renderer strings,
// standardized Navigator/WorkerNavigator concurrency and memory values, and
// per-site exemption support. Injected once per V8 context, from
// CefRenderProcessHandler::OnContextCreated, which runs before any page script
// (see mtk_app.cpp). This file has no CEF dependency of its own, so the pure
// parts are testable without a browser.
namespace mtk::fingerprint {

// Name of the command-line switch carrying "<on|off>:<16 hex digit seed>[:<exempt_origins>]",
// e.g. "on:1a2b3c4d5e6f7890:https://example.com,https://trusted.org".
inline constexpr char kSwitchName[] = "mtk-fingerprint-protection";

// True when `value` (the switch's value, not the whole "name=value" pair)
// requests protection, i.e. it starts with "on:". Anything else, including a
// missing or malformed value, is treated as off.
bool IsEnabledSwitchValue(const std::string& value);

// Encodes on/off, a 64-bit seed, and optional per-site exempt origins into the
// single string passed as the command-line switch value.
std::string EncodeSwitchValue(
    bool enabled, uint64_t seed,
    const std::vector<std::string>& exempt_origins = {});

// Seed encoded in `value` (the switch's value), or 0 when it cannot be parsed.
uint64_t DecodeSeed(const std::string& value);

// Decodes the list of per-site exempt origins encoded in `value`.
std::vector<std::string> DecodeExemptOrigins(const std::string& value);

// The script text itself, deterministic for a given seed and exempt origins list.
// Returns an empty string when `enabled` is false.
std::string BuildScript(
    bool enabled, uint64_t seed,
    const std::vector<std::string>& exempt_origins = {});

}  // namespace mtk::fingerprint
