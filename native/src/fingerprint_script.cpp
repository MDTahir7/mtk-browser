#include "fingerprint_script.h"

#include <charconv>

namespace mtk::fingerprint {
namespace {

constexpr char kOnPrefix[] = "on:";
constexpr char kOffPrefix[] = "off:";

}  // namespace

bool IsEnabledSwitchValue(const std::string& value) {
  return value.starts_with(kOnPrefix);
}

std::string EncodeSwitchValue(bool enabled, uint64_t seed) {
  char hex[17]{};
  const auto result = std::to_chars(hex, hex + sizeof(hex) - 1, seed, 16);
  *result.ptr = '\0';
  return std::string(enabled ? kOnPrefix : kOffPrefix) + hex;
}

uint64_t DecodeSeed(const std::string& value) {
  const size_t colon = value.find(':');
  if (colon == std::string::npos) return 0;
  const std::string_view hex(value.data() + colon + 1, value.size() - colon - 1);
  uint64_t seed = 0;
  const auto result =
      std::from_chars(hex.data(), hex.data() + hex.size(), seed, 16);
  return result.ec == std::errc{} ? seed : 0;
}

std::string BuildScript(bool enabled, uint64_t seed) {
  if (!enabled) return {};

  // Decimal literal, substituted into the template below by find/replace
  // rather than printf-style formatting, since the template is JS text that
  // could in principle contain a literal '%' character somewhere and a
  // format-string call would misread that as a directive.
  const std::string seed_literal = std::to_string(seed);

  // mulberry32, seeded from the process-lifetime seed. Deterministic within one
  // run, so repeated reads of the same canvas or WebGL call agree with each
  // other (a page comparing two reads of its own canvas must see the same
  // answer both times, or the noise itself becomes a stronger signal than the
  // fingerprint it was meant to hide), and the exact bytes still differ between
  // one run of the browser and the next.
  //
  // Canvas: 2D readback (getImageData and everything built on it, including
  // toDataURL/toBlob, which call it internally in every real implementation)
  // gets its low bits perturbed per pixel. The visible image is unaffected;
  // only the exact byte values a fingerprinting script would hash change.
  //
  // WebGL: UNMASKED_VENDOR_WEBGL/UNMASKED_RENDERER_WEBGL (read through the
  // WEBGL_debug_renderer_info extension, which exists specifically to expose
  // real GPU model strings) are replaced with generic values. The ordinary
  // VENDOR/RENDERER parameters are left alone, since browsers already report
  // those as "WebKit"/generic strings and changing them would itself be an
  // unusual signal.
  //
  // AudioContext: getChannelData is perturbed the same way as canvas reads,
  // since audio-fingerprinting scripts render a fixed buffer and hash the
  // resulting samples.
  static constexpr char kTemplate[] = R"JS(
(() => {
  if (window.__mtkFingerprint) return;
  Object.defineProperty(window, '__mtkFingerprint', {
    value: true, configurable: false, enumerable: false
  });

  let state = (__MTK_SEED__ >>> 0) || 0x9e3779b9;
  const rand32 = () => {
    state |= 0; state = (state + 0x6D2B79F5) | 0;
    let t = Math.imul(state ^ (state >>> 15), 1 | state);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0);
  };

  const perturb = (data) => {
    // A cheap per-call stream keeps two reads of the same buffer content
    // consistent with each other without caching the whole image.
    let local = rand32();
    for (let i = 0; i < data.length; i += 4) {
      local = (local + 0x6D2B79F5) | 0;
      const bump = (local >>> 28) & 1;
      if (data[i] !== undefined) data[i] = (data[i] + bump) & 0xff;
      if (data[i + 1] !== undefined) data[i + 1] = (data[i + 1] + bump) & 0xff;
      if (data[i + 2] !== undefined) data[i + 2] = (data[i + 2] + bump) & 0xff;
    }
  };

  const patchGetImageData = (proto) => {
    if (!proto || !proto.getImageData) return;
    const original = proto.getImageData;
    proto.getImageData = function (...args) {
      const result = original.apply(this, args);
      try { perturb(result.data); } catch (_) {}
      return result;
    };
  };
  if (window.CanvasRenderingContext2D) {
    patchGetImageData(CanvasRenderingContext2D.prototype);
  }
  if (window.OffscreenCanvasRenderingContext2D) {
    patchGetImageData(OffscreenCanvasRenderingContext2D.prototype);
  }

  const patchToDataUrlFamily = (proto) => {
    if (!proto) return;
    if (proto.toDataURL) {
      const original = proto.toDataURL;
      proto.toDataURL = function (...args) {
        try {
          const ctx = this.getContext && this.getContext('2d');
          if (ctx) perturb(ctx.getImageData(0, 0, this.width, this.height).data);
        } catch (_) {}
        return original.apply(this, args);
      };
    }
  };
  if (window.HTMLCanvasElement) {
    patchToDataUrlFamily(HTMLCanvasElement.prototype);
  }

  if (window.WebGLRenderingContext || window.WebGL2RenderingContext) {
    const patchGetParameter = (proto) => {
      if (!proto || !proto.getParameter) return;
      const original = proto.getParameter;
      proto.getParameter = function (parameter) {
        // 37445/37446 are UNMASKED_VENDOR_WEBGL/UNMASKED_RENDERER_WEBGL from
        // the WEBGL_debug_renderer_info extension; the numeric literals avoid
        // depending on that extension object existing to read the constants.
        if (parameter === 37445) return 'Google Inc. (Generic)';
        if (parameter === 37446) return 'ANGLE (Generic Renderer)';
        return original.call(this, parameter);
      };
    };
    if (window.WebGLRenderingContext) {
      patchGetParameter(WebGLRenderingContext.prototype);
    }
    if (window.WebGL2RenderingContext) {
      patchGetParameter(WebGL2RenderingContext.prototype);
    }
  }

  if (window.AudioBuffer && AudioBuffer.prototype.getChannelData) {
    const original = AudioBuffer.prototype.getChannelData;
    AudioBuffer.prototype.getChannelData = function (channel) {
      const data = original.call(this, channel);
      try {
        let local = rand32();
        for (let i = 0; i < data.length; i += 97) {
          local = (local + 0x6D2B79F5) | 0;
          data[i] += ((local >>> 24) & 1) ? 1e-7 : -1e-7;
        }
      } catch (_) {}
      return data;
    };
  }
})();
)JS";

  std::string script(kTemplate);
  const std::string placeholder = "__MTK_SEED__";
  const size_t at = script.find(placeholder);
  if (at != std::string::npos) script.replace(at, placeholder.size(), seed_literal);
  return script;
}

}  // namespace mtk::fingerprint
