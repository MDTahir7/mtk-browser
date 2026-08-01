#pragma once

#include <string>
#include <utility>
#include <vector>

namespace mtk::policy {

enum class ResourceType {
  kMainFrame,
  kSubFrame,
  kScript,
  kImage,
  kStylesheet,
  kXhr,
  kFont,
  kMedia,
  kObject,
  kPing,
  kWorker,
  kPrefetch,
  kFavicon,
  kOther,
};

struct NavigationDecision {
  bool allow;
  std::string url;
  std::wstring reason;
};

struct RequestDecision {
  bool allow;
  std::wstring reason;
};

NavigationDecision EvaluateNavigation(const std::string& raw_url);
NavigationDecision ResolveInput(const std::string& raw_input);
RequestDecision EvaluateRequest(const std::string& raw_url,
                                ResourceType resource_type,
                                const std::string& request_initiator = {});
std::vector<std::pair<std::string, std::string>> PrivacyRequestHeaders(
    const std::string& raw_url);
std::string EnforceSafeSearch(std::string url);
// Replacement address for a browse surface that must not load, or an empty
// string when the URL needs no substitution.
std::string SafeSurfaceRedirect(const std::string& raw_url);
// Extracts the lowercase hostname of a URL, or an empty string when absent.
std::string HostOf(const std::string& raw_url);
bool IsAdOrTracker(const std::string& raw_url, ResourceType type,
                   const std::string& request_initiator = {});
bool IsSocialPlatformNsfw(const std::string& raw_url);

}  // namespace mtk::policy

