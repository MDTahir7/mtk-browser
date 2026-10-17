#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mtk::sites {

// Choices remembered for one site. A site is identified by its scheme and host
// ("https://example.com"), which is how the web itself scopes permissions and
// storage. The port is deliberately ignored: the product has no use for two
// different rule sets on two ports of the same host, and folding them keeps the
// stored key predictable.

enum class Permission {
  // Default. Requests are refused without prompting; a site only gets access
  // once someone chooses Allow in the site settings dialog.
  kAsk,
  kAllow,
  // An explicit one-browser-run Allow. Store::Set keeps this only in memory;
  // serialised rules always become kAsk, so a restart fails closed.
  kSession,
  kBlock,
};

// Zoom is stored as a whole percentage so the file stays readable, and is
// converted to Chromium's logarithmic zoom level when applied.
constexpr int kDefaultZoomPercent = 100;
constexpr int kMinZoomPercent = 25;
constexpr int kMaxZoomPercent = 500;

struct Rule {
  std::string origin;
  int zoom_percent = kDefaultZoomPercent;
  Permission camera = Permission::kAsk;
  Permission microphone = Permission::kAsk;
  Permission location = Permission::kAsk;
  Permission notifications = Permission::kAsk;
  // Ad and tracker filtering for this site. Turning it off never affects the
  // safety rules, which are not per-site adjustable.
  bool ad_filtering = true;
  // Cookies and other site data. Blocking is enforced through CEF's cookie
  // access filter, so it applies to both sending and storing.
  bool cookies = true;
  // Exempts this site from the global HTTPS-only refusal of its own plain-HTTP
  // subresources. For the rare page that has no https at all; it never affects
  // the main-frame upgrade, which HTTPS-only leaves alone regardless.
  bool allow_plain_http = false;
  // Exempts this site from renderer-side Canvas/WebGL/Audio/Navigator
  // fingerprinting perturbations when a trusted web app breaks under noise.
  bool allow_fingerprinting = false;
};

// Scheme and host of `url`, lowercased, or an empty string when the URL is not
// an ordinary web address.
std::string OriginOf(const std::string& url);

// Zoom percent to Chromium zoom level and back. Chromium uses a factor of 1.2
// per step, so this is a logarithm, not a linear scale.
double ZoomLevelForPercent(int percent);
int ZoomPercentForLevel(double level);

class Store final {
 public:
  static Store& Get();

  void Load();
  bool Flush();

  // Effective rule for `origin`; defaults when nothing is stored.
  Rule For(const std::string& origin) const;
  bool HasRule(const std::string& origin) const;
  // Stores `rule`, or forgets the site when the rule is entirely default.
  bool Set(const Rule& rule);
  bool Forget(const std::string& origin);
  bool Clear();

  const std::vector<Rule>& rules() const;
  // Returns all origins that have explicitly opted out of fingerprinting
  // protection (`allow_fingerprinting == true`).
  std::vector<std::string> FingerprintingExemptOrigins() const;

 private:
  std::vector<Rule> rules_;
  // Overlay of kSession decisions. It is intentionally never written to
  // sites.v1 and applies only to the current browser process.
  std::vector<Rule> temporary_rules_;
  bool dirty_ = false;
};

}  // namespace mtk::sites
