#pragma once

// MTK Shields filter engine.
//
// Implements the subset of the Adblock Plus filter syntax that MTK Browser
// needs for browser-wide network and cosmetic filtering:
//
//   ||example.com^$third-party,script      network block rule
//   @@||example.com/allowed.js             exception rule
//   /banner/*/img^                         substring/wildcard rule
//   ##.ad-container                         generic element hiding
//   example.com##.sponsored                 site-specific element hiding
//
// Rules are authored in this repository (see native/resources/mtk_filters.txt)
// and are loaded once in the browser process before any browser is created.
// After loading the engine is immutable, so lookups are safe from the CEF IO
// thread.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mtk::filters {

enum class RequestType : uint32_t {
  kOther = 1u << 0,
  kMainFrame = 1u << 1,
  kSubFrame = 1u << 2,
  kStylesheet = 1u << 3,
  kScript = 1u << 4,
  kImage = 1u << 5,
  kFont = 1u << 6,
  kXhr = 1u << 7,
  kMedia = 1u << 8,
  kObject = 1u << 9,
  kPing = 1u << 10,
  kWorker = 1u << 11,
  kPrefetch = 1u << 12,
  kFavicon = 1u << 13,
};

class Engine {
 public:
  static Engine& Get();

  // Loads "mtk_filters.txt" from the given directory. Returns the number of
  // rules that were accepted. Safe to call once during startup.
  size_t LoadFromDirectory(const std::string& directory);

  bool loaded() const { return loaded_; }
  size_t network_rule_count() const {
    return block_rules_.size() + allow_rules_.size();
  }
  size_t cosmetic_rule_count() const { return cosmetic_rule_count_; }

  // True when the request matches a block rule and no exception rule.
  bool ShouldBlock(const std::string& lowercase_url,
                   const std::string& host,
                   const std::string& initiator_host,
                   RequestType type) const;

  // Comma-separated CSS selector list that applies to the given host.
  std::string CssSelectorsForHost(const std::string& host) const;

 private:
  struct NetworkRule {
    std::string pattern;
    bool anchor_domain = false;
    bool anchor_start = false;
    bool anchor_end = false;
    bool third_party_only = false;
    bool first_party_only = false;
    uint32_t type_mask = 0;  // 0 means "any type"
    std::vector<std::string> include_domains;
    std::vector<std::string> exclude_domains;
  };

  void AddRule(std::string_view line);
  bool Matches(const NetworkRule& rule, const std::string& url,
               const std::string& host, const std::string& initiator_host,
               RequestType type, bool third_party) const;

  std::vector<NetworkRule> block_rules_;
  std::vector<NetworkRule> allow_rules_;
  std::vector<std::string> generic_selectors_;
  std::unordered_map<std::string, std::vector<std::string>> host_selectors_;
  size_t cosmetic_rule_count_ = 0;
  bool loaded_ = false;
};

}  // namespace mtk::filters
