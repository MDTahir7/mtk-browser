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
// Rules are loaded from a verified updated filter list when present, falling
// back to the bundled native/resources/mtk_filters.txt. Thread-safe hot
// reloading is supported via a shared mutex so signed filter-list updates take
// effect immediately without restarting the browser.

#include <cstdint>
#include <shared_mutex>
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

// Returns the registrable domain (eTLD+1) of `host`, aware of multi-part Public
// Suffixes such as .co.in, .ac.in, .gov.in, .co.uk, .com.au, .co.jp, etc.
std::string EffectiveTldPlusOne(std::string_view host);

class Engine {
 public:
  static Engine& Get();

  // Loads "mtk_filters.txt" from the given directory (or a newer verified
  // filter list if present). Returns the number of rules that were accepted.
  size_t LoadFromDirectory(const std::string& directory);

  // Hot-reloads filter rules from in-memory filter list text (used after a
  // signed filter feed update is verified). Rejects empty inputs and preserves
  // existing rules if parsing produces zero rules.
  size_t ReloadFromText(std::string_view content);

  bool loaded() const;
  size_t network_rule_count() const;
  size_t cosmetic_rule_count() const;

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
    std::string indexed_domain;
    bool anchor_domain = false;
    bool anchor_start = false;
    bool anchor_end = false;
    bool third_party_only = false;
    bool first_party_only = false;
    uint32_t type_mask = 0;  // 0 means "any type"
    std::vector<std::string> include_domains;
    std::vector<std::string> exclude_domains;
  };

  struct State {
    std::vector<NetworkRule> block_rules;
    std::vector<NetworkRule> allow_rules;
    std::unordered_map<std::string, std::vector<size_t>> domain_block_index;
    std::vector<size_t> generic_block_indices;
    std::unordered_map<std::string, std::vector<size_t>> domain_allow_index;
    std::vector<size_t> generic_allow_indices;
    std::vector<std::string> generic_selectors;
    std::unordered_map<std::string, std::vector<std::string>> host_selectors;
    size_t cosmetic_rule_count = 0;
    bool loaded = false;
  };

  static void AddRuleToState(State* state, std::string_view line);
  static bool Matches(const NetworkRule& rule, const std::string& url,
                      const std::string& host,
                      const std::string& initiator_host, RequestType type,
                      bool third_party);

  mutable std::shared_mutex mutex_;
  State state_;
};

}  // namespace mtk::filters
