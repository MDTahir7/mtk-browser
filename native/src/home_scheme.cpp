#include "home_scheme.h"

#include <algorithm>
#include <cstring>
#include <string>

#include <vector>

#include "downloads_page.h"
#include "history_page.h"
#include "home_page.h"
#include "passkey_page.h"
#include "preferences.h"
#include "settings_page.h"
#include "strings.h"
#include "windows_hello.h"
#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"

namespace mtk::home {
namespace {

class HomeResourceHandler final : public CefResourceHandler {
 public:
  // Each browser-owned document carries its own policy: the home page may submit
  // a search to Google, the history page may only submit back to itself.
  HomeResourceHandler(std::string html, std::string csp)
      : html_(std::move(html)), csp_(std::move(csp)) {}

  bool Open(CefRefPtr<CefRequest>, bool& handle_request,
            CefRefPtr<CefCallback>) override {
    handle_request = true;
    return true;
  }

  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64_t& response_length, CefString&) override {
    response->SetStatus(200);
    response->SetStatusText("OK");
    response->SetMimeType("text/html");
    response->SetCharset("utf-8");
    CefResponse::HeaderMap headers;
    headers.insert({"Cache-Control", "no-store"});
    headers.insert({"Referrer-Policy", "no-referrer"});
    headers.insert({"X-Content-Type-Options", "nosniff"});
    headers.insert({"Cross-Origin-Opener-Policy", "same-origin"});
    headers.insert({"Content-Security-Policy", csp_});
    response->SetHeaderMap(headers);
    response_length = static_cast<int64_t>(html_.size());
  }

  bool Read(void* data_out, int bytes_to_read, int& bytes_read,
            CefRefPtr<CefResourceReadCallback>) override {
    bytes_read = 0;
    if (!data_out || bytes_to_read <= 0 || offset_ >= html_.size()) return false;
    const size_t count = std::min(static_cast<size_t>(bytes_to_read),
                                  html_.size() - offset_);
    std::memcpy(data_out, html_.data() + offset_, count);
    offset_ += count;
    bytes_read = static_cast<int>(count);
    return true;
  }

  void Cancel() override {}

 private:
  std::string html_;
  std::string csp_;
  size_t offset_ = 0;
  IMPLEMENT_REFCOUNTING(HomeResourceHandler);
};

constexpr char kBaseCsp[] =
    "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; "
    "object-src 'none'; frame-src 'none'; connect-src https://wttr.in https://dummyjson.com; "
    "img-src data:; font-src 'none'; script-src 'unsafe-inline'; form-action ";

class Factory final : public CefSchemeHandlerFactory {
 public:
  explicit Factory(bool private_session) : private_session_(private_session) {}

  CefRefPtr<CefResourceHandler> Create(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& scheme_name,
      CefRefPtr<CefRequest> request) override {
    if (scheme_name != kScheme || !request || request->GetMethod() != "GET" ||
        !IsHomeUrl(request->GetURL().ToString())) return nullptr;
    // A private window is served the same document with no profile rows, so
    // nothing from the on-disk profile is visible inside it.
    const auto prefs = preferences::Store::Get().values();
    const bool light = preferences::ShouldUseLightAppearance(prefs.theme_mode);
    const std::string csp = std::string(kBaseCsp) + "https://www.google.com";
    if (private_session_) {
      return new HomeResourceHandler(BuildHtml({}, {}, {}, light), csp);
    }
    std::string html = SnapshotHtml(light);
    if (html.empty()) html = BuildHtml({}, {}, {}, light);
    return new HomeResourceHandler(std::move(html), csp);
  }

 private:
  const bool private_session_ = false;
  IMPLEMENT_REFCOUNTING(Factory);
};

// The history document renders on demand, because its search box is part of the
// URL. The rendering itself is pure computation over a published copy of the
// store, so it is safe to run on the CEF IO thread.
class HistoryFactory final : public CefSchemeHandlerFactory {
 public:
  explicit HistoryFactory(bool private_session)
      : private_session_(private_session) {}

  CefRefPtr<CefResourceHandler> Create(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& scheme_name,
      CefRefPtr<CefRequest> request) override {
    if (scheme_name != kScheme || !request || request->GetMethod() != "GET") {
      return nullptr;
    }
    const std::string url = request->GetURL().ToString();
    if (!history::IsHistoryUrl(url)) return nullptr;
    std::string html = history::BuildHtml(
        private_session_ ? std::vector<library::Entry>{} : history::Snapshot(),
        history::QueryOf(url), private_session_);
    return new HomeResourceHandler(std::move(html),
                                   std::string(kBaseCsp) + "'self'");
  }

 private:
  const bool private_session_ = false;
  IMPLEMENT_REFCOUNTING(HistoryFactory);
};

// The passkeys document renders diagnostics for WebAuthn and Windows Hello.
class PasskeysFactory final : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& scheme_name,
      CefRefPtr<CefRequest> request) override {
    if (scheme_name != kScheme || !request || request->GetMethod() != "GET") {
      return nullptr;
    }
    const std::string url = request->GetURL().ToString();
    if (!passkeys::IsPasskeyUrl(url)) return nullptr;

    const auto prefs = preferences::Store::Get().values();
    const bool light = preferences::ShouldUseLightAppearance(prefs.theme_mode);
    const bool hindi = strings::IsHindi();
    const auto hello_status = hello::CheckAvailability();
    std::string html = passkeys::BuildHtml(hello_status, light, hindi);
    return new HomeResourceHandler(std::move(html), passkeys::kPasskeyCsp);
  }

 private:
  IMPLEMENT_REFCOUNTING(PasskeysFactory);
};

class DownloadsFactory final : public CefSchemeHandlerFactory {
 public:
  explicit DownloadsFactory(bool private_session)
      : private_session_(private_session) {}

  CefRefPtr<CefResourceHandler> Create(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& scheme_name,
      CefRefPtr<CefRequest> request) override {
    if (scheme_name != kScheme || !request || request->GetMethod() != "GET") {
      return nullptr;
    }
    const std::string url = request->GetURL().ToString();
    if (!downloads_page::IsDownloadsUrl(url)) return nullptr;

    const auto prefs = preferences::Store::Get().values();
    const bool light = preferences::ShouldUseLightAppearance(prefs.theme_mode);
    const auto& items = private_session_ ? std::vector<downloads::Item>{}
                                         : downloads::Manager::Get().items();
    std::string html = downloads_page::BuildHtml(
        items, downloads_page::QueryOf(url), downloads_page::CategoryOf(url),
        light, private_session_);
    return new HomeResourceHandler(std::move(html),
                                   std::string(kBaseCsp) + "'self'");
  }

 private:
  const bool private_session_ = false;
  IMPLEMENT_REFCOUNTING(DownloadsFactory);
};

class SettingsFactory final : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& scheme_name,
      CefRefPtr<CefRequest> request) override {
    if (scheme_name != kScheme || !request || request->GetMethod() != "GET") {
      return nullptr;
    }
    const std::string url = request->GetURL().ToString();
    if (!settings_page::IsSettingsUrl(url)) return nullptr;

    const auto prefs = preferences::Store::Get().values();
    const bool light = preferences::ShouldUseLightAppearance(prefs.theme_mode);
    const bool hindi = strings::IsHindi();
    std::string html = settings_page::BuildHtml(
        prefs, settings_page::QueryOf(url), light, hindi);
    return new HomeResourceHandler(std::move(html),
                                   std::string(kBaseCsp) + "'self'");
  }

 private:
  IMPLEMENT_REFCOUNTING(SettingsFactory);
};

}  // namespace

void RegisterCustomScheme(CefRawPtr<CefSchemeRegistrar> registrar) {
  if (!registrar) return;
  registrar->AddCustomScheme(
      kScheme, CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                   CEF_SCHEME_OPTION_DISPLAY_ISOLATED);
}

bool RegisterSchemeHandler(CefRefPtr<CefRequestContext> context,
                           bool private_session) {
  if (!context) context = CefRequestContext::GetGlobalContext();
  if (!context) return false;
  return context->RegisterSchemeHandlerFactory(kScheme, kHost,
                                               new Factory(private_session)) &&
         context->RegisterSchemeHandlerFactory(
             kScheme, history::kHost, new HistoryFactory(private_session)) &&
         context->RegisterSchemeHandlerFactory(
             kScheme, passkeys::kHost, new PasskeysFactory()) &&
         context->RegisterSchemeHandlerFactory(
             kScheme, downloads_page::kHost, new DownloadsFactory(private_session)) &&
         context->RegisterSchemeHandlerFactory(
             kScheme, settings_page::kHost, new SettingsFactory());
}

}  // namespace mtk::home
