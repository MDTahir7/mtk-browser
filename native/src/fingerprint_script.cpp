#include "fingerprint_script.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace mtk::fingerprint {
namespace {

constexpr char kOnPrefix[] = "on:";
constexpr char kOffPrefix[] = "off:";

bool IsSafeOriginToken(const std::string& origin) {
  if (origin.empty() || origin.size() > 256) return false;
  if (!origin.starts_with("https://") && !origin.starts_with("http://")) {
    return false;
  }
  return std::all_of(origin.begin(), origin.end(), [](unsigned char c) {
    return std::isalnum(c) != 0 || c == ':' || c == '/' || c == '.' ||
           c == '-';
  });
}

std::string EscapeJsString(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const unsigned char c : value) {
    if (c == '\'' || c == '\\') out.push_back('\\');
    if (c >= 0x20 && c < 0x7f) out.push_back(static_cast<char>(c));
  }
  return out;
}

}  // namespace

bool IsEnabledSwitchValue(const std::string& value) {
  return value.starts_with(kOnPrefix);
}

std::string EncodeSwitchValue(
    bool enabled, uint64_t seed,
    const std::vector<std::string>& exempt_origins) {
  char hex[17]{};
  const auto result = std::to_chars(hex, hex + sizeof(hex) - 1, seed, 16);
  *result.ptr = '\0';
  std::string encoded =
      std::string(enabled ? kOnPrefix : kOffPrefix) + hex;

  std::string joined;
  for (const std::string& origin : exempt_origins) {
    if (!IsSafeOriginToken(origin)) continue;
    if (!joined.empty()) joined.push_back(',');
    joined += origin;
  }
  if (!joined.empty()) {
    encoded.push_back('|');
    encoded += joined;
  }
  return encoded;
}

uint64_t DecodeSeed(const std::string& value) {
  const size_t colon = value.find(':');
  if (colon == std::string::npos) return 0;
  const size_t pipe = value.find('|', colon + 1);
  const size_t end = (pipe == std::string::npos) ? value.size() : pipe;
  if (end <= colon + 1) return 0;
  const std::string_view hex(value.data() + colon + 1, end - (colon + 1));
  uint64_t seed = 0;
  const auto result =
      std::from_chars(hex.data(), hex.data() + hex.size(), seed, 16);
  return (result.ec == std::errc{} && result.ptr == hex.data() + hex.size())
             ? seed
             : 0;
}

std::vector<std::string> DecodeExemptOrigins(const std::string& value) {
  std::vector<std::string> origins;
  const size_t pipe = value.find('|');
  if (pipe == std::string::npos || pipe + 1 >= value.size()) return origins;
  size_t start = pipe + 1;
  while (start < value.size()) {
    const size_t comma = value.find(',', start);
    const std::string token = value.substr(
        start, (comma == std::string::npos ? value.size() : comma) - start);
    if (IsSafeOriginToken(token)) {
      origins.push_back(token);
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return origins;
}

std::string BuildScript(bool enabled, uint64_t seed,
                        const std::vector<std::string>& exempt_origins) {
  if (!enabled) return {};

  const std::string seed_literal = std::to_string(seed);
  std::string exempt_array_literal = "[";
  bool first = true;
  for (const std::string& origin : exempt_origins) {
    if (!IsSafeOriginToken(origin)) continue;
    if (!first) exempt_array_literal += ",";
    first = false;
    exempt_array_literal += "'" + EscapeJsString(origin) + "'";
  }
  exempt_array_literal += "]";

  static constexpr char kTemplate[] = R"JS(
(() => {
  if (window.__mtkFpGuardApplied) return;
  Object.defineProperty(window, '__mtkFpGuardApplied', {
    value: true, configurable: false, enumerable: false
  });

  const origin = (window.location && window.location.origin
      ? window.location.origin.toLowerCase() : '');
  const host = (window.location && window.location.hostname
      ? window.location.hostname.toLowerCase() : '');
  const path = (window.location && window.location.pathname) || '';

  // User-configured per-site exemptions from sites.v1 (Alt+S Site Settings).
  const exemptOrigins = new Set(__MTK_EXEMPT_ORIGINS__);
  if (origin && exemptOrigins.has(origin)) {
    return;
  }

  // Bot-challenge proof-of-work pages must not have canvas/worker state altered.
  if (host.includes('.within.website') || host.includes('techaro') ||
      path.includes('.within.website')) {
    return;
  }

  const makeNative = (fn, original) => {
    try {
      if (original && original.name) {
        Object.defineProperty(fn, 'name', { value: original.name });
        fn.toString = () => `function ${original.name}() { [native code] }`;
      }
    } catch (_) {}
    return fn;
  };

  const baseSeed = (__MTK_SEED__ >>> 0) || 0x9e3779b9;
  const makePrng = (contentHash) => {
    let state = (baseSeed ^ (contentHash >>> 0)) || 0x9e3779b9;
    return () => {
      state |= 0; state = (state + 0x6D2B79F5) | 0;
      let t = Math.imul(state ^ (state >>> 15), 1 | state);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0);
    };
  };

  // Deterministic content hash so repeated reads of the same canvas in the
  // same session produce identical perturbed bytes (preventing multi-read
  // noise detection) while differing across browser sessions.
  const hashPixels = (data) => {
    let h = 2166136261 >>> 0;
    const step = Math.max(4, ((data.length >> 8) & ~3));
    for (let i = 0; i < data.length; i += step) {
      h ^= data[i] & 0xfe;
      h = Math.imul(h, 16777619) >>> 0;
    }
    return h;
  };

  const perturb = (data) => {
    if (!data || !data.length) return;
    const nextRand = makePrng(hashPixels(data));
    let local = nextRand();
    for (let i = 0; i < data.length; i += 4) {
      local = (local + 0x6D2B79F5) | 0;
      const bump = (local >>> 28) & 1;
      if (data[i] !== undefined) data[i] = (data[i] ^ bump) & 0xff;
      if (data[i + 1] !== undefined) data[i + 1] = (data[i + 1] ^ bump) & 0xff;
      if (data[i + 2] !== undefined) data[i + 2] = (data[i + 2] ^ bump) & 0xff;
    }
  };

  const patchGetImageData = (proto) => {
    if (!proto || !proto.getImageData) return;
    const original = proto.getImageData;
    proto.getImageData = makeNative(function (...args) {
      const result = original.apply(this, args);
      try { perturb(result.data); } catch (_) {}
      return result;
    }, original);
  };
  if (window.CanvasRenderingContext2D) {
    patchGetImageData(CanvasRenderingContext2D.prototype);
  }
  if (window.OffscreenCanvasRenderingContext2D) {
    patchGetImageData(OffscreenCanvasRenderingContext2D.prototype);
  }

  const perturbCanvas2D = (canvas) => {
    try {
      const ctx = canvas && canvas.getContext && canvas.getContext('2d');
      if (ctx && canvas.width > 0 && canvas.height > 0) {
        const img = CanvasRenderingContext2D.prototype.getImageData.call(
            ctx, 0, 0, Math.min(canvas.width, 64), Math.min(canvas.height, 64));
        ctx.putImageData(img, 0, 0);
      }
    } catch (_) {}
  };

  if (window.HTMLCanvasElement) {
    const proto = HTMLCanvasElement.prototype;
    if (proto.toDataURL) {
      const originalToDataURL = proto.toDataURL;
      proto.toDataURL = makeNative(function (...args) {
        perturbCanvas2D(this);
        return originalToDataURL.apply(this, args);
      }, originalToDataURL);
    }
    if (proto.toBlob) {
      const originalToBlob = proto.toBlob;
      proto.toBlob = makeNative(function (...args) {
        perturbCanvas2D(this);
        return originalToBlob.apply(this, args);
      }, originalToBlob);
    }
  }

  if (window.OffscreenCanvas && OffscreenCanvas.prototype.convertToBlob) {
    const originalConvertToBlob = OffscreenCanvas.prototype.convertToBlob;
    OffscreenCanvas.prototype.convertToBlob = makeNative(function (...args) {
      perturbCanvas2D(this);
      return originalConvertToBlob.apply(this, args);
    }, originalConvertToBlob);
  }

  if (window.WebGLRenderingContext || window.WebGL2RenderingContext) {
    const patchGetParameter = (proto) => {
      if (!proto || !proto.getParameter) return;
      const original = proto.getParameter;
      proto.getParameter = makeNative(function (parameter) {
        // 37445/37446: UNMASKED_VENDOR_WEBGL / UNMASKED_RENDERER_WEBGL
        if (parameter === 37445) return 'Google Inc. (Generic)';
        if (parameter === 37446) return 'ANGLE (Generic Renderer)';
        return original.call(this, parameter);
      }, original);
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
    AudioBuffer.prototype.getChannelData = makeNative(function (channel) {
      const data = original.call(this, channel);
      try {
        const nextRand = makePrng((channel | 0) + data.length);
        let local = nextRand();
        for (let i = 0; i < data.length; i += 97) {
          local = (local + 0x6D2B79F5) | 0;
          data[i] += ((local >>> 24) & 1) ? 1e-7 : -1e-7;
        }
      } catch (_) {}
      return data;
    }, original);
  }

  if (window.AnalyserNode && AnalyserNode.prototype.getFloatFrequencyData) {
    const original = AnalyserNode.prototype.getFloatFrequencyData;
    AnalyserNode.prototype.getFloatFrequencyData = makeNative(function (array) {
      const res = original.call(this, array);
      try {
        if (array && array.length) {
          const nextRand = makePrng(array.length);
          let local = nextRand();
          for (let i = 0; i < array.length; i += 16) {
            local = (local + 0x6D2B79F5) | 0;
            array[i] += ((local >>> 24) & 1) ? 1e-4 : -1e-4;
          }
        }
      } catch (_) {}
      return res;
    }, original);
  }

  const patchNavigatorProto = (proto) => {
    if (!proto) return;
    try {
      Object.defineProperty(proto, 'hardwareConcurrency', {
        get: makeNative(function () { return 4; }, { name: 'get hardwareConcurrency' }),
        configurable: true,
        enumerable: true
      });
      Object.defineProperty(proto, 'deviceMemory', {
        get: makeNative(function () { return 8; }, { name: 'get deviceMemory' }),
        configurable: true,
        enumerable: true
      });
    } catch (_) {}
  };
  if (window.Navigator) patchNavigatorProto(Navigator.prototype);
  if (window.WorkerNavigator) patchNavigatorProto(WorkerNavigator.prototype);
})();
)JS";

  std::string script(kTemplate);
  const std::string seed_placeholder = "__MTK_SEED__";
  if (const size_t at = script.find(seed_placeholder); at != std::string::npos) {
    script.replace(at, seed_placeholder.size(), seed_literal);
  }
  const std::string exempt_placeholder = "__MTK_EXEMPT_ORIGINS__";
  if (const size_t at = script.find(exempt_placeholder);
      at != std::string::npos) {
    script.replace(at, exempt_placeholder.size(), exempt_array_literal);
  }
  return script;
}

}  // namespace mtk::fingerprint
