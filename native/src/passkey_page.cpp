#include "passkey_page.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "home_page.h"

namespace mtk::passkeys {
namespace {

std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::string EscapeHtml(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 16);
  for (const char c : value) {
    switch (c) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&#39;"; break;
      default: escaped.push_back(c); break;
    }
  }
  return escaped;
}

}  // namespace

bool IsPasskeyUrl(std::string_view url) {
  if (url == kUrl || url == "mtk://passkeys") return true;
  if (url.starts_with("mtk://passkeys/?") || url.starts_with("mtk://passkeys?")) {
    return true;
  }
  return false;
}

Action ParseActionUrl(std::string_view url) {
  if (url == "mtk://passkeys/action/test-hello") {
    return Action::kTestHello;
  }
  return Action::kNone;
}

std::string BuildHtml(const hello::Result& hello_status,
                      bool light_appearance,
                      bool hindi) {
  const std::string hello_detail = WideToUtf8(hello_status.detail);
  const bool hello_avail = hello_status.available;

  std::string html;
  html.reserve(16384);

  html += R"HTML(<!doctype html>
<html lang=")HTML";
  html += (hindi ? "hi" : "en");
  html += R"HTML(" data-theme=")HTML";
  html += (light_appearance ? "light" : "dark");
  html += R"HTML(">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="referrer" content="no-referrer">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; img-src data:; form-action 'self'; base-uri 'none'; object-src 'none'; frame-src 'none'; connect-src 'none'; font-src 'none';">
<title>)HTML";
  html += (hindi ? "पासकी एवं विंडोज हैलो जांच — MTK Browser"
                 : "Passkey & Windows Hello Diagnostics — MTK Browser");
  html += R"HTML(</title>
<style>
:root {
  color-scheme: dark;
  --bg: #060a18;
  --panel: rgba(16, 23, 45, 0.85);
  --panel-card: rgba(21, 30, 58, 0.72);
  --line: rgba(99, 102, 241, 0.22);
  --line-light: rgba(255, 255, 255, 0.08);
  --text: #f5f7ff;
  --muted: #9da8c3;
  --dim: #687596;
  --accent: #8b5cf6;
  --accent-glow: rgba(139, 92, 246, 0.45);
  --cyan: #38bdf8;
  --ok: #4ade80;
  --ok-bg: rgba(34, 197, 94, 0.16);
  --ok-line: rgba(74, 222, 128, 0.35);
  --warn: #fbbf24;
  --warn-bg: rgba(245, 158, 11, 0.16);
  --warn-line: rgba(251, 191, 36, 0.35);
  --err: #f87171;
  --err-bg: rgba(239, 68, 68, 0.16);
  --err-line: rgba(248, 113, 113, 0.35);
  --card-shadow: 0 12px 32px rgba(2, 6, 23, 0.45);
}

[data-theme="light"] {
  color-scheme: light;
  --bg: #f5f7fb;
  --panel: rgba(255, 255, 255, 0.94);
  --panel-card: #ffffff;
  --line: rgba(99, 102, 241, 0.22);
  --line-light: rgba(0, 0, 0, 0.06);
  --text: #0f172a;
  --muted: #475569;
  --dim: #64748b;
  --accent: #7c3aed;
  --accent-glow: rgba(124, 58, 237, 0.28);
  --cyan: #0284c7;
  --ok: #16a34a;
  --ok-bg: rgba(22, 163, 74, 0.12);
  --ok-line: rgba(22, 163, 74, 0.28);
  --warn: #d97706;
  --warn-bg: rgba(217, 119, 6, 0.12);
  --warn-line: rgba(217, 119, 6, 0.28);
  --err: #dc2626;
  --err-bg: rgba(220, 38, 38, 0.12);
  --err-line: rgba(220, 38, 38, 0.28);
  --card-shadow: 0 10px 25px rgba(100, 116, 139, 0.12);
}

* { box-sizing: border-box; }
html, body { margin: 0; min-height: 100%; }
body {
  background: radial-gradient(circle at 75% 0%, #15224e 0%, transparent 45%),
              radial-gradient(circle at 20% 90%, #170d36 0%, transparent 40%),
              var(--bg);
  color: var(--text);
  font: 14.5px/1.55 'Segoe UI Variable', 'Segoe UI', system-ui, -apple-system, sans-serif;
  transition: background .25s ease, color .25s ease;
}

[data-theme="light"] body {
  background: radial-gradient(circle at 75% 0%, #e0e7ff 0%, transparent 50%),
              radial-gradient(circle at 20% 90%, #f3e8ff 0%, transparent 45%),
              var(--bg);
}

a { color: inherit; text-decoration: none; }

.wrap {
  max-width: 960px;
  margin: 0 auto;
  padding: 34px 22px 56px;
}

/* Header & Breadcrumb */
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 26px;
  flex-wrap: wrap;
}

.crumb-nav {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 12px;
  color: var(--dim);
}

.crumb-nav a {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 5px 11px;
  border-radius: 8px;
  background: var(--panel);
  border: 1px solid var(--line);
  color: var(--cyan);
  font-weight: 550;
  transition: all .2s ease;
}
.crumb-nav a:hover {
  border-color: var(--accent);
  color: #fff;
  transform: translateX(-2px);
}
[data-theme="light"] .crumb-nav a:hover { color: #0f172a; }

.header-controls {
  display: flex;
  align-items: center;
  gap: 8px;
}

.ctrl-btn {
  height: 34px;
  padding: 0 13px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  border-radius: 17px;
  border: 1px solid var(--line);
  background: var(--panel);
  color: var(--muted);
  font-size: 11.5px;
  cursor: pointer;
  outline: none;
  transition: all .2s ease;
  user-select: none;
}
.ctrl-btn:hover {
  border-color: var(--accent);
  color: var(--text);
  box-shadow: 0 0 12px var(--accent-glow);
}

/* Hero Section */
.hero-card {
  background: linear-gradient(135deg, rgba(30, 41, 79, 0.7) 0%, rgba(15, 23, 42, 0.85) 100%);
  border: 1px solid var(--line);
  border-radius: 16px;
  padding: 26px 28px;
  margin-bottom: 24px;
  box-shadow: var(--card-shadow);
  position: relative;
  overflow: hidden;
  backdrop-filter: blur(14px);
}
[data-theme="light"] .hero-card {
  background: linear-gradient(135deg, #ffffff 0%, #f8faff 100%);
}

.hero-card:before {
  content: '';
  position: absolute;
  top: -60px;
  right: -60px;
  width: 220px;
  height: 220px;
  background: radial-gradient(circle, rgba(139, 92, 246, 0.28) 0%, transparent 70%);
  pointer-events: none;
}

.hero-title-row {
  display: flex;
  align-items: center;
  gap: 14px;
  margin-bottom: 8px;
}

.hero-icon {
  width: 44px;
  height: 44px;
  border-radius: 12px;
  display: grid;
  place-items: center;
  background: linear-gradient(135deg, #7c3aed, #4f46e5);
  box-shadow: 0 0 20px rgba(124, 58, 237, 0.55);
  font-size: 22px;
  flex-shrink: 0;
}

.hero-title {
  margin: 0;
  font-size: 23px;
  font-weight: 650;
  letter-spacing: -0.3px;
}

.hero-desc {
  margin: 0;
  color: var(--muted);
  font-size: 13.5px;
  max-width: 720px;
  line-height: 1.5;
}

/* Diagnostic Grid */
.diag-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
  margin-bottom: 24px;
}

@media (max-width: 720px) {
  .diag-grid { grid-template-columns: 1fr; }
}

.card {
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 14px;
  padding: 20px 22px;
  box-shadow: var(--card-shadow);
  backdrop-filter: blur(12px);
  display: flex;
  flex-direction: column;
  transition: transform .2s ease, border-color .2s ease, box-shadow .2s ease;
}
.card:hover {
  border-color: rgba(139, 92, 246, 0.45);
  transform: translateY(-2px);
  box-shadow: var(--card-shadow), 0 0 16px rgba(139, 92, 246, 0.12);
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 12px;
}

.card-title-group {
  display: flex;
  align-items: center;
  gap: 10px;
}

.card-icon {
  width: 30px;
  height: 30px;
  border-radius: 8px;
  background: rgba(139, 92, 246, 0.18);
  border: 1px solid rgba(139, 92, 246, 0.3);
  display: grid;
  place-items: center;
  font-size: 15px;
  flex-shrink: 0;
}

.card-title {
  margin: 0;
  font-size: 14.5px;
  font-weight: 600;
}

/* Badges */
.badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px;
  border-radius: 12px;
  font-size: 11px;
  font-weight: 600;
  white-space: nowrap;
  letter-spacing: 0.2px;
}
.badge-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: currentColor;
}
.badge.ok {
  background: var(--ok-bg);
  color: var(--ok);
  border: 1px solid var(--ok-line);
}
.badge.warn {
  background: var(--warn-bg);
  color: var(--warn);
  border: 1px solid var(--warn-line);
}
.badge.err {
  background: var(--err-bg);
  color: var(--err);
  border: 1px solid var(--err-line);
}
.badge.neutral {
  background: rgba(99, 102, 241, 0.14);
  color: var(--cyan);
  border: 1px solid rgba(56, 189, 248, 0.25);
}

.card-body {
  color: var(--muted);
  font-size: 12.5px;
  line-height: 1.5;
  flex: 1;
}

.card-detail {
  margin-top: 10px;
  padding: 8px 12px;
  border-radius: 8px;
  background: rgba(0, 0, 0, 0.2);
  border: 1px solid var(--line-light);
  font-size: 12px;
  color: var(--dim);
  word-break: break-word;
}
[data-theme="light"] .card-detail {
  background: rgba(0, 0, 0, 0.03);
}

.card-action-bar {
  margin-top: 16px;
  display: flex;
  align-items: center;
  gap: 10px;
}

.btn-action {
  height: 36px;
  padding: 0 16px;
  border-radius: 18px;
  display: inline-flex;
  align-items: center;
  gap: 7px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  border: none;
  background: linear-gradient(135deg, #7c3aed, #4f46e5);
  color: #ffffff;
  box-shadow: 0 4px 14px rgba(124, 58, 237, 0.4);
  transition: all .2s cubic-bezier(0.34, 1.56, 0.64, 1);
  user-select: none;
}
.btn-action:hover {
  transform: scale(1.04);
  box-shadow: 0 6px 20px rgba(124, 58, 237, 0.6);
}
.btn-action:active {
  transform: scale(0.97);
}

.feedback-box {
  display: none;
  margin-top: 12px;
  padding: 10px 14px;
  border-radius: 9px;
  font-size: 12px;
  line-height: 1.45;
  animation: fadeIn .3s ease;
}
@keyframes fadeIn {
  from { opacity: 0; transform: translateY(-4px); }
  to { opacity: 1; transform: translateY(0); }
}
.feedback-box.success {
  background: var(--ok-bg);
  border: 1px solid var(--ok-line);
  color: var(--ok);
}
.feedback-box.error {
  background: var(--err-bg);
  border: 1px solid var(--err-line);
  color: var(--err);
}

/* Feature Capability Pills */
.cap-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
  gap: 8px;
  margin-top: 12px;
}
.cap-pill {
  padding: 6px 10px;
  border-radius: 8px;
  background: rgba(0, 0, 0, 0.18);
  border: 1px solid var(--line-light);
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 11px;
}
[data-theme="light"] .cap-pill {
  background: rgba(0, 0, 0, 0.03);
}
.cap-pill span { color: var(--muted); }
.cap-pill b { font-size: 11px; }

/* Privacy Banner */
.privacy-card {
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 14px;
  padding: 20px 24px;
  box-shadow: var(--card-shadow);
  display: flex;
  align-items: flex-start;
  gap: 16px;
}
.privacy-icon {
  font-size: 26px;
  line-height: 1;
  padding-top: 2px;
  color: var(--cyan);
}
.privacy-content h3 {
  margin: 0 0 6px;
  font-size: 14.5px;
  font-weight: 600;
}
.privacy-content p {
  margin: 0;
  font-size: 12.5px;
  color: var(--muted);
  line-height: 1.55;
}
.privacy-list {
  margin: 8px 0 0;
  padding-left: 18px;
  font-size: 12px;
  color: var(--dim);
  line-height: 1.6;
}
</style>
</head>
<body>
<div class="wrap">

<header>
  <div class="crumb-nav">
    <a href="mtk://home/"><span>←</span> <span data-i18n="backHome">)HTML";
  html += (hindi ? "होम" : "Home");
  html += R"HTML(</span></a>
    <span>/</span>
    <span data-i18n="crumbPasskey">)HTML";
  html += (hindi ? "पासकी डायग्नोस्टिक्स" : "Passkey Diagnostics");
  html += R"HTML(</span>
  </div>
  <div class="header-controls">
    <button id="btn-lang" class="ctrl-btn" type="button" title="Switch Language / भाषा बदलें">
      <span>🌐</span> <span id="lbl-lang">)HTML";
  html += (hindi ? "English" : "हिंदी");
  html += R"HTML(</span>
    </button>
    <button id="btn-theme" class="ctrl-btn" type="button" title="Toggle Theme">
      <span id="theme-ico">)HTML";
  html += (light_appearance ? "🌙" : "🌓");
  html += R"HTML(</span> <span id="lbl-theme">)HTML";
  html += (light_appearance ? (hindi ? "डार्क" : "Dark")
                            : (hindi ? "लाइट" : "Light"));
  html += R"HTML(</span>
    </button>
    <button id="btn-refresh" class="ctrl-btn" type="button" title="Refresh Diagnostics" onclick="location.reload()">
      <span>↻</span>
    </button>
  </div>
</header>

<section class="hero-card">
  <div class="hero-title-row">
    <div class="hero-icon">🔑</div>
    <div>
      <h1 class="hero-title" data-i18n="heroTitle">)HTML";
  html += (hindi ? "पासकी एवं विंडोज हैलो जांच"
                 : "Passkey & Windows Hello Diagnostics");
  html += R"HTML(</h1>
    </div>
  </div>
  <p class="hero-desc" data-i18n="heroDesc">)HTML";
  html += (hindi
               ? "MTK Browser में बायोमेट्रिक, प्लेटफॉर्म ऑथेंटिकेटर एवं WebAuthn सुरक्षा क्षमताओं की लाइव जांच।"
               : "Comprehensive capability check for hardware biometrics, platform authenticators, and W3C WebAuthn APIs in MTK Browser.");
  html += R"HTML(</p>
</section>

<div class="diag-grid">

  <!-- Card 1: Windows Hello -->
  <div class="card">
    <div class="card-header">
      <div class="card-title-group">
        <div class="card-icon">🛡️</div>
        <h2 class="card-title" data-i18n="helloTitle">)HTML";
  html += (hindi ? "विंडोज हैलो (ऑपरेटिंग सिस्टम)" : "Windows Hello (Native OS)");
  html += R"HTML(</h2>
      </div>
      <span id="badge-hello" class="badge )HTML";
  html += (hello_avail ? "ok" : "warn");
  html += R"HTML(">
        <span class="badge-dot"></span>
        <span id="text-hello-badge" data-i18n=")HTML";
  html += (hello_avail ? "helloAvail" : "helloNotConfig");
  html += R"HTML(">
          )HTML";
  html += (hello_avail ? (hindi ? "सक्रिय एवं उपलब्ध" : "Available & Configured")
                       : (hindi ? "सेटअप आवश्यक" : "Not Configured / Unavailable"));
  html += R"HTML(
        </span>
      </span>
    </div>
    <div class="card-body">
      <p data-i18n="helloBody">)HTML";
  html += (hindi
               ? "विंडोज हैलो आपके चेहरे, फिंगरप्रिंट या पिन का उपयोग करके सुरक्षित पासकी सत्यापन प्रदान करता है।"
               : "Windows Hello provides hardware-backed biometric (face/fingerprint) or PIN authentication directly integrated with MTK Browser.");
  html += R"HTML(</p>
      <div class="card-detail" id="hello-detail-box">)HTML";
  if (!hello_detail.empty()) {
    html += EscapeHtml(hello_detail);
  } else if (hello_avail) {
    html += (hindi ? "विंडोज हैलो इस डिवाइस पर बायोमेट्रिक या पिन सत्यापन के लिए पूरी तरह तैयार है।"
                   : "Windows Hello is ready for biometric gestures and passkey approvals.");
  } else {
    html += (hindi ? "विंडोज सेटिंग्स &gt; Accounts &gt; Sign-in options में जाकर विंडोज हैलो सेट करें।"
                   : "Set up Windows Hello in Windows Settings &gt; Accounts &gt; Sign-in options.");
  }
  html += R"HTML(</div>
      <div class="card-action-bar">
        <a id="btn-test-hello" class="btn-action" href="mtk://passkeys/action/test-hello">
          <span>👆</span> <span data-i18n="btnTestHello">)HTML";
  html += (hindi ? "विंडोज हैलो जेस्चर टेस्ट करें" : "Test Windows Hello Gesture");
  html += R"HTML(</span>
        </a>
      </div>
      <div id="hello-test-feedback" class="feedback-box"></div>
    </div>
  </div>

  <!-- Card 2: WebAuthn Core API -->
  <div class="card">
    <div class="card-header">
      <div class="card-title-group">
        <div class="card-icon">🌐</div>
        <h2 class="card-title" data-i18n="webauthnTitle">)HTML";
  html += (hindi ? "WebAuthn कोर रनटाइम" : "WebAuthn Core API");
  html += R"HTML(</h2>
      </div>
      <span id="badge-webauthn" class="badge neutral">
        <span class="badge-dot"></span>
        <span id="text-webauthn-badge" data-i18n="checking">)HTML";
  html += (hindi ? "जांच जारी..." : "Checking...");
  html += R"HTML(</span>
      </span>
    </div>
    <div class="card-body">
      <p data-i18n="webauthnBody">)HTML";
  html += (hindi
               ? "W3C WebAuthn Level 3 मानक, जो वेबसाइटों को सुरक्षित पब्लिक-की क्रेडेंशियल्स की अनुमति देता है।"
               : "The W3C WebAuthn specification runtime in MTK Browser that allows passwordless passkey logins on secure websites.");
  html += R"HTML(</p>
      <div class="card-detail" id="webauthn-detail-box" data-i18n="webauthnWaiting">)HTML";
  html += (hindi ? "ब्राउज़र वातावरण का विश्लेषण किया जा रहा है..."
                 : "Evaluating PublicKeyCredential interface...");
  html += R"HTML(</div>
    </div>
  </div>

  <!-- Card 3: Platform Authenticator -->
  <div class="card">
    <div class="card-header">
      <div class="card-title-group">
        <div class="card-icon">💻</div>
        <h2 class="card-title" data-i18n="platformTitle">)HTML";
  html += (hindi ? "प्लेटफ़ॉर्म ऑथेंटिकेटर" : "Platform Authenticator");
  html += R"HTML(</h2>
      </div>
      <span id="badge-platform" class="badge neutral">
        <span class="badge-dot"></span>
        <span id="text-platform-badge" data-i18n="checking">)HTML";
  html += (hindi ? "जांच जारी..." : "Checking...");
  html += R"HTML(</span>
      </span>
    </div>
    <div class="card-body">
      <p data-i18n="platformBody">)HTML";
  html += (hindi
               ? "पुष्टि करता है कि क्या यह पीसी बाहरी यूएसबी डोंगल के बिना आंतरिक टीपीएम (TPM) पासकी का समर्थन करता है।"
               : "Verifies whether this computer can create and sign passkeys using built-in TPM hardware without requiring a USB key.");
  html += R"HTML(</p>
      <div class="card-detail" id="platform-detail-box" data-i18n="platformWaiting">)HTML";
  html += (hindi ? "isUserVerifyingPlatformAuthenticatorAvailable() की जांच हो रही है..."
                 : "Querying isUserVerifyingPlatformAuthenticatorAvailable()...");
  html += R"HTML(</div>
    </div>
  </div>

  <!-- Card 4: Conditional Mediation / Autofill -->
  <div class="card">
    <div class="card-header">
      <div class="card-title-group">
        <div class="card-icon">⚡</div>
        <h2 class="card-title" data-i18n="autofillTitle">)HTML";
  html += (hindi ? "पासकी ऑटोफ़िल (कंडीशनल यूआई)" : "Passkey Autofill (Conditional UI)");
  html += R"HTML(</h2>
      </div>
      <span id="badge-autofill" class="badge neutral">
        <span class="badge-dot"></span>
        <span id="text-autofill-badge" data-i18n="checking">)HTML";
  html += (hindi ? "जांच जारी..." : "Checking...");
  html += R"HTML(</span>
      </span>
    </div>
    <div class="card-body">
      <p data-i18n="autofillBody">)HTML";
  html += (hindi
               ? "वेबसाइट के यूज़रनेम फ़ील्ड में क्लिक करते ही सहेजी गई पासकी सुझाव अपने-आप प्रदर्शित होते हैं।"
               : "Allows web pages to show passkey suggestions directly inside username inputs alongside saved password autofill.");
  html += R"HTML(</p>
      <div class="card-detail" id="autofill-detail-box" data-i18n="autofillWaiting">)HTML";
  html += (hindi ? "कंडीशनल मीडिएशन उपलब्धता का सत्यापन..."
                 : "Querying isConditionalMediationAvailable()...");
  html += R"HTML(</div>
    </div>
  </div>

  <!-- Card 5: Extended Client Capabilities & QR -->
  <div class="card">
    <div class="card-header">
      <div class="card-title-group">
        <div class="card-icon">📱</div>
        <h2 class="card-title" data-i18n="hybridTitle">)HTML";
  html += (hindi ? "क्लाइंट क्षमताएं एवं हाइब्रिड QR" : "Capabilities & Hybrid QR");
  html += R"HTML(</h2>
      </div>
      <span id="badge-hybrid" class="badge ok">
        <span class="badge-dot"></span>
        <span id="text-hybrid-badge" data-i18n="supported">)HTML";
  html += (hindi ? "सक्रिय" : "Supported");
  html += R"HTML(</span>
      </span>
    </div>
    <div class="card-body">
      <p data-i18n="hybridBody">)HTML";
  html += (hindi
               ? "मोबाइल फ़ोन (Android / iOS) के ज़रिए क्यूआर कोड स्कैन करके पासकी से लॉग इन करने की क्षमता।"
               : "Support for cross-device sign-in via FIDO Cross-Device (Hybrid/caBLE) QR scanning and advanced WebAuthn features.");
  html += R"HTML(</p>
      <div class="cap-grid" id="capabilities-grid">
        <div class="cap-pill"><span>Hybrid QR</span><b id="cap-hybrid" style="color:var(--ok)">✓ Yes</b></div>
        <div class="cap-pill"><span>Cond. Get</span><b id="cap-cget" style="color:var(--ok)">✓ Yes</b></div>
        <div class="cap-pill"><span>Cond. Create</span><b id="cap-ccreate" style="color:var(--ok)">✓ Yes</b></div>
        <div class="cap-pill"><span>User Verify</span><b id="cap-uv" style="color:var(--ok)">✓ Yes</b></div>
      </div>
    </div>
  </div>

  <!-- Card 6: Security Keys -->
  <div class="card">
    <div class="card-header">
      <div class="card-title-group">
        <div class="card-icon">🗝️</div>
        <h2 class="card-title" data-i18n="secKeyTitle">)HTML";
  html += (hindi ? "हार्डवेयर सुरक्षा कुंजी (FIDO2 / U2F)" : "Hardware Security Keys (FIDO2/U2F)");
  html += R"HTML(</h2>
      </div>
      <span class="badge ok">
        <span class="badge-dot"></span>
        <span data-i18n="ready">)HTML";
  html += (hindi ? "तैयार" : "Ready");
  html += R"HTML(</span>
      </span>
    </div>
    <div class="card-body">
      <p data-i18n="secKeyBody">)HTML";
  html += (hindi
               ? "यूएसबी (USB) और एनएफसी (NFC) सुरक्षा चाबियां (जैसे YubiKey) किसी भी स्थानीय कॉन्फ़िगरेशन के बिना तुरंत समर्थित हैं।"
               : "USB and NFC security keys (YubiKey, Titan, Feitian) operate through the Windows WebAuthn prompt whenever a site requests them.");
  html += R"HTML(</p>
      <div class="card-detail" data-i18n="secKeyDetail">)HTML";
  html += (hindi
               ? "जब कोई वेबसाइट सुरक्षा कुंजी का अनुरोध करती है, तो विंडोज स्वचालित रूप से चाबी को छूने का संकेत देता है।"
               : "Privacy Note: WebAuthn does not allow background probing of attached USB keys. When requested by a site, Windows prompts for a key touch.");
  html += R"HTML(</div>
    </div>
  </div>

</div>

<!-- Privacy Architecture Notice -->
<section class="privacy-card">
  <div class="privacy-icon">🔒</div>
  <div class="privacy-content">
    <h3 data-i18n="privacyTitle">)HTML";
  html += (hindi ? "शून्य-ज्ञान एवं सुरक्षा गारंटी (Zero-Knowledge Architecture)"
                 : "Zero-Knowledge & Privacy Guarantees");
  html += R"HTML(</h3>
    <p data-i18n="privacyDesc">)HTML";
  html += (hindi
               ? "यह डायग्नोस्टिक पृष्ठ केवल आपके स्थानीय कंप्यूटर की क्षमताओं का परीक्षण करता है:"
               : "This diagnostic document queries local system capabilities without collecting or transmitting data:");
  html += R"HTML(</p>
    <ul class="privacy-list">
      <li data-i18n="priv1">)HTML";
  html += (hindi
               ? "कोई भी क्रेडेंशियल या निजी कुंजी नहीं बनाई, पढ़ी या भेजी जाती है।"
               : "No credentials, passkeys, or private keys are created, stored, or sent.");
  html += R"HTML(</li>
      <li data-i18n="priv2">)HTML";
  html += (hindi
               ? "बायोमेट्रिक डेटा (चेहरा/फ़िंगरप्रिंट) कभी भी विंडोज़ के हार्डवेयर टीपीएम (TPM) से बाहर नहीं निकलता।"
               : "Biometric templates never leave the Windows TPM/secure enclave; MTK Browser only receives cryptographic verification confirmations.");
  html += R"HTML(</li>
      <li data-i18n="priv3">)HTML";
  html += (hindi
               ? "यह पृष्ठ पूरी तरह से स्थानीय रूप से चलता है (Content-Security-Policy: connect-src &#39;none&#39;) जिससे नेटवर्क ट्रैफ़िक पूर्णतः अवरुद्ध है।"
               : "Strict local Content-Security-Policy (connect-src 'none') ensures zero external network telemetry.");
  html += R"HTML(</li>
    </ul>
  </div>
</section>

</div>

<script>
(function() {
  const i18nDict = {
    en: {
      backHome: "Home",
      crumbPasskey: "Passkey Diagnostics",
      heroTitle: "Passkey & Windows Hello Diagnostics",
      heroDesc: "Comprehensive capability check for hardware biometrics, platform authenticators, and W3C WebAuthn APIs in MTK Browser.",
      helloTitle: "Windows Hello (Native OS)",
      helloAvail: "Available & Configured",
      helloNotConfig: "Not Configured / Unavailable",
      helloBody: "Windows Hello provides hardware-backed biometric (face/fingerprint) or PIN authentication directly integrated with MTK Browser.",
      btnTestHello: "Test Windows Hello Gesture",
      webauthnTitle: "WebAuthn Core API",
      checking: "Checking...",
      supported: "Supported",
      notSupported: "Not Available",
      ready: "Ready",
      webauthnBody: "The W3C WebAuthn specification runtime in MTK Browser that allows passwordless passkey logins on secure websites.",
      platformTitle: "Platform Authenticator",
      platformBody: "Verifies whether this computer can create and sign passkeys using built-in TPM hardware without requiring a USB key.",
      autofillTitle: "Passkey Autofill (Conditional UI)",
      autofillBody: "Allows web pages to show passkey suggestions directly inside username inputs alongside saved password autofill.",
      hybridTitle: "Capabilities & Hybrid QR",
      hybridBody: "Support for cross-device sign-in via FIDO Cross-Device (Hybrid/caBLE) QR scanning and advanced WebAuthn features.",
      secKeyTitle: "Hardware Security Keys (FIDO2/U2F)",
      secKeyBody: "USB and NFC security keys (YubiKey, Titan, Feitian) operate through the Windows WebAuthn prompt whenever a site requests them.",
      secKeyDetail: "Privacy Note: WebAuthn does not allow background probing of attached USB keys. When requested by a site, Windows prompts for a key touch.",
      privacyTitle: "Zero-Knowledge & Privacy Guarantees",
      privacyDesc: "This diagnostic document queries local system capabilities without collecting or transmitting data:",
      priv1: "No credentials, passkeys, or private keys are created, stored, or sent.",
      priv2: "Biometric templates never leave the Windows TPM/secure enclave; MTK Browser only receives cryptographic verification confirmations.",
      priv3: "Strict local Content-Security-Policy (connect-src 'none') ensures zero external network telemetry."
    },
    hi: {
      backHome: "होम",
      crumbPasskey: "पासकी डायग्नोस्टिक्स",
      heroTitle: "पासकी एवं विंडोज हैलो जांच",
      heroDesc: "MTK Browser में बायोमेट्रिक, प्लेटफॉर्म ऑथेंटिकेटर एवं WebAuthn सुरक्षा क्षमताओं की लाइव जांच।",
      helloTitle: "विंडोज हैलो (ऑपरेटिंग सिस्टम)",
      helloAvail: "सक्रिय एवं उपलब्ध",
      helloNotConfig: "सेटअप आवश्यक",
      helloBody: "विंडोज हैलो आपके चेहरे, फिंगरप्रिंट या पिन का उपयोग करके सुरक्षित पासकी सत्यापन प्रदान करता है।",
      btnTestHello: "विंडोज हैलो जेस्चर टेस्ट करें",
      webauthnTitle: "WebAuthn कोर रनटाइम",
      checking: "जांच जारी...",
      supported: "सक्रिय",
      notSupported: "अनुपलब्ध",
      ready: "तैयार",
      webauthnBody: "W3C WebAuthn Level 3 मानक, जो वेबसाइटों को सुरक्षित पब्लिक-की क्रेडेंशियल्स की अनुमति देता है।",
      platformTitle: "प्लेटफ़ॉर्म ऑथेंटिकेटर",
      platformBody: "पुष्टि करता है कि क्या यह पीसी बाहरी यूएसबी डोंगल के बिना आंतरिक टीपीएम (TPM) पासकी का समर्थन करता है।",
      autofillTitle: "पासकी ऑटोफ़िल (कंडीशनल यूआई)",
      autofillBody: "वेबसाइट के यूज़रनेम फ़ील्ड में क्लिक करते ही सहेजी गई पासकी सुझाव अपने-आप प्रदर्शित होते हैं।",
      hybridTitle: "क्लाइंट क्षमताएं एवं हाइब्रिड QR",
      hybridBody: "मोबाइल फ़ोन (Android / iOS) के ज़रिए क्यूआर कोड स्कैन करके पासकी से लॉग इन करने की क्षमता।",
      secKeyTitle: "हार्डवेयर सुरक्षा कुंजी (FIDO2 / U2F)",
      secKeyBody: "यूएसबी (USB) और एनएफसी (NFC) सुरक्षा चाबियां (जैसे YubiKey) किसी भी स्थानीय कॉन्फ़िगरेशन के बिना तुरंत समर्थित हैं।",
      secKeyDetail: "गोपनीयता नोट: WebAuthn जुड़ी हुई यूएसबी कुंजियों की पृष्ठभूमि में जांच की अनुमति नहीं देता। साइट द्वारा अनुरोध करने पर विंडो चाबी छूने को कहती है।",
      privacyTitle: "शून्य-ज्ञान एवं सुरक्षा गारंटी (Zero-Knowledge Architecture)",
      privacyDesc: "यह डायग्नोस्टिक पृष्ठ केवल आपके स्थानीय कंप्यूटर की क्षमताओं का परीक्षण करता है:",
      priv1: "कोई भी क्रेडेंशियल या निजी कुंजी नहीं बनाई, पढ़ी या भेजी जाती है।",
      priv2: "बायोमेट्रिक डेटा (चेहरा/फ़िंगरप्रिंट) कभी भी विंडोज़ के हार्डवेयर टीपीएम (TPM) से बाहर नहीं निकलता।",
      priv3: "यह पृष्ठ पूरी तरह से स्थानीय रूप से चलता है (Content-Security-Policy: connect-src 'none') जिससे नेटवर्क ट्रैफ़िक पूर्णतः अवरुद्ध है।"
    }
  };

  let curLang = document.documentElement.lang === 'hi' ? 'hi' : 'en';

  function applyLanguage(lang) {
    curLang = lang;
    document.documentElement.lang = lang;
    const dict = i18nDict[lang] || i18nDict.en;
    document.querySelectorAll('[data-i18n]').forEach(function(el) {
      const key = el.getAttribute('data-i18n');
      if (dict[key]) el.textContent = dict[key];
    });
    const lblLang = document.getElementById('lbl-lang');
    if (lblLang) lblLang.textContent = lang === 'hi' ? 'English' : 'हिंदी';
    const lblTheme = document.getElementById('lbl-theme');
    const isLight = document.documentElement.getAttribute('data-theme') === 'light';
    if (lblTheme) {
      lblTheme.textContent = isLight ? (lang === 'hi' ? 'डार्क' : 'Dark') : (lang === 'hi' ? 'लाइट' : 'Light');
    }
  }

  const btnLang = document.getElementById('btn-lang');
  if (btnLang) {
    btnLang.addEventListener('click', function() {
      applyLanguage(curLang === 'hi' ? 'en' : 'hi');
    });
  }

  const btnTheme = document.getElementById('btn-theme');
  if (btnTheme) {
    btnTheme.addEventListener('click', function() {
      const isCurrentlyLight = document.documentElement.getAttribute('data-theme') === 'light';
      const nextTheme = isCurrentlyLight ? 'dark' : 'light';
      document.documentElement.setAttribute('data-theme', nextTheme);
      document.getElementById('theme-ico').textContent = nextTheme === 'light' ? '🌙' : '🌓';
      document.getElementById('lbl-theme').textContent = nextTheme === 'light'
        ? (curLang === 'hi' ? 'डार्क' : 'Dark')
        : (curLang === 'hi' ? 'लाइट' : 'Light');
    });
  }

  // Windows Hello test feedback handler
  window.onHelloVerified = function(success, message) {
    const box = document.getElementById('hello-test-feedback');
    if (!box) return;
    box.style.display = 'block';
    const timeStr = new Date().toLocaleTimeString();
    if (success) {
      box.className = 'feedback-box success';
      box.innerHTML = '<strong>' + (curLang === 'hi' ? '✓ सत्यापन सफल (' : '✓ Verification Successful (') + timeStr + '):</strong> ' +
        (curLang === 'hi' ? 'विंडोज हैलो जेस्चर की पुष्टि हो गई है।' : 'Windows Hello biometric / PIN gesture verified successfully.');
    } else {
      box.className = 'feedback-box error';
      box.innerHTML = '<strong>' + (curLang === 'hi' ? '✕ सत्यापन असफल (' : '✕ Verification Not Completed (') + timeStr + '):</strong> ' +
        (message || (curLang === 'hi' ? 'जेस्चर पूरा नहीं हुआ।' : 'User canceled or verification could not complete.'));
    }
  };

  const btnTestHello = document.getElementById('btn-test-hello');
  if (btnTestHello) {
    btnTestHello.addEventListener('click', function() {
      const box = document.getElementById('hello-test-feedback');
      if (box) {
        box.style.display = 'block';
        box.className = 'feedback-box';
        box.style.background = 'rgba(99, 102, 241, 0.15)';
        box.style.border = '1px solid rgba(99, 102, 241, 0.3)';
        box.style.color = 'var(--text)';
        box.textContent = curLang === 'hi' ? 'विंडोज हैलो संकेत प्रारंभ किया जा रहा है...' : 'Prompting Windows Hello secure verification...';
      }
    });
  }

  // Diagnostic Runner
  async function runDiagnostics() {
    // 1. WebAuthn Core
    const badgeWebAuthn = document.getElementById('badge-webauthn');
    const textWebAuthn = document.getElementById('text-webauthn-badge');
    const boxWebAuthn = document.getElementById('webauthn-detail-box');

    const hasWebAuthn = typeof window.PublicKeyCredential !== 'undefined';
    if (hasWebAuthn) {
      badgeWebAuthn.className = 'badge ok';
      textWebAuthn.textContent = curLang === 'hi' ? 'सक्रिय' : 'Supported';
      boxWebAuthn.textContent = curLang === 'hi'
        ? 'window.PublicKeyCredential API लोड हो गया है। सुरक्षित ओरिजिन (mtk:// एवं https://) समर्थित हैं।'
        : 'window.PublicKeyCredential is fully exposed on this secure standard origin (mtk://passkeys).';
    } else {
      badgeWebAuthn.className = 'badge err';
      textWebAuthn.textContent = curLang === 'hi' ? 'अनुपलब्ध' : 'Unavailable';
      boxWebAuthn.textContent = curLang === 'hi'
        ? 'इस रनटाइम में WebAuthn API अनुपलब्ध है।'
        : 'WebAuthn runtime is not exposed in this context.';
    }

    if (!hasWebAuthn) return;

    // 2. Platform Authenticator
    const badgePlatform = document.getElementById('badge-platform');
    const textPlatform = document.getElementById('text-platform-badge');
    const boxPlatform = document.getElementById('platform-detail-box');

    try {
      const isPlatformAvail = await PublicKeyCredential.isUserVerifyingPlatformAuthenticatorAvailable();
      if (isPlatformAvail) {
        badgePlatform.className = 'badge ok';
        textPlatform.textContent = curLang === 'hi' ? 'उपलब्ध (TPM/Hello)' : 'Available (TPM/Hello)';
        boxPlatform.textContent = curLang === 'hi'
          ? 'इस डिवाइस पर प्लेटफ़ॉर्म ऑथेंटिकेटर (TPM 2.0 / Windows Hello) पासकी बनाने के लिए तैयार है।'
          : 'User-verifying platform authenticator is active. Websites can create hardware-backed passkeys directly.';
      } else {
        badgePlatform.className = 'badge warn';
        textPlatform.textContent = curLang === 'hi' ? 'उपलब्ध नहीं' : 'Not Available';
        boxPlatform.textContent = curLang === 'hi'
          ? 'डिवाइस पर कोई सक्रिय प्लेटफ़ॉर्म ऑथेंटिकेटर नहीं मिला। बाहरी सुरक्षा कुंजी या फ़ोन का उपयोग किया जा सकता है।'
          : 'No local platform authenticator reported. External security keys or cross-device QR can still be used.';
      }
    } catch (err) {
      badgePlatform.className = 'badge err';
      textPlatform.textContent = curLang === 'hi' ? 'जांच त्रुटि' : 'Check Error';
      boxPlatform.textContent = (err && err.message) ? err.message : 'Error checking platform authenticator.';
    }

    // 3. Conditional Mediation (Passkey Autofill)
    const badgeAutofill = document.getElementById('badge-autofill');
    const textAutofill = document.getElementById('text-autofill-badge');
    const boxAutofill = document.getElementById('autofill-detail-box');

    try {
      if (typeof PublicKeyCredential.isConditionalMediationAvailable === 'function') {
        const isMediationAvail = await PublicKeyCredential.isConditionalMediationAvailable();
        if (isMediationAvail) {
          badgeAutofill.className = 'badge ok';
          textAutofill.textContent = curLang === 'hi' ? 'सक्रिय' : 'Supported';
          boxAutofill.textContent = curLang === 'hi'
            ? 'पासकी कंडीशनल ऑटोफ़िल समर्थित है। इनपुट फ़ील्ड में पासकी सुझाव सीधे दिखेंगे।'
            : 'Conditional mediation is available. Sign-in forms can present passkey suggestions in autocomplete dropdowns.';
        } else {
          badgeAutofill.className = 'badge warn';
          textAutofill.textContent = curLang === 'hi' ? 'अनुपलब्ध' : 'Unavailable';
          boxAutofill.textContent = curLang === 'hi'
            ? 'इस ब्राउज़र प्रोफ़ाइल में कंडीशनल ऑटोफ़िल उपलब्ध नहीं है।'
            : 'Conditional mediation is not supported in this runtime.';
        }
      } else {
        badgeAutofill.className = 'badge neutral';
        textAutofill.textContent = 'API Absent';
        boxAutofill.textContent = 'isConditionalMediationAvailable API is not present on this Chromium version.';
      }
    } catch (err) {
      badgeAutofill.className = 'badge err';
      textAutofill.textContent = 'Error';
      boxAutofill.textContent = (err && err.message) ? err.message : 'Error evaluating mediation.';
    }

    // 4. Extended Capabilities
    if (typeof PublicKeyCredential.getClientCapabilities === 'function') {
      try {
        const caps = await PublicKeyCredential.getClientCapabilities();
        if (caps) {
          const setCap = (id, val) => {
            const el = document.getElementById(id);
            if (el) {
              el.textContent = val ? '✓ Yes' : '✕ No';
              el.style.color = val ? 'var(--ok)' : 'var(--err)';
            }
          };
          if ('hybridTransport' in caps) setCap('cap-hybrid', caps.hybridTransport);
          if ('conditionalGet' in caps) setCap('cap-cget', caps.conditionalGet);
          if ('conditionalCreate' in caps) setCap('cap-ccreate', caps.conditionalCreate);
          if ('userVerifyingPlatformAuthenticator' in caps) setCap('cap-uv', caps.userVerifyingPlatformAuthenticator);
        }
      } catch (_) {}
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', runDiagnostics);
  } else {
    runDiagnostics();
  }
})();
</script>
</body>
</html>
)HTML";

  return html;
}

}  // namespace mtk::passkeys
