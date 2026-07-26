#include "filter_engine.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mtk::filters {
namespace {

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

std::string Trim(std::string_view value) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

// "^" in ABP syntax means "separator": end of URL or a non-alphanumeric,
// non-(-._%) character.
bool IsSeparator(char character) {
  const auto value = static_cast<unsigned char>(character);
  if (std::isalnum(value) != 0) return false;
  return character != '-' && character != '.' && character != '_' &&
         character != '%';
}

bool HostMatchesEntry(std::string_view host, std::string_view entry) {
  if (host == entry) return true;
  return host.size() > entry.size() &&
         host.compare(host.size() - entry.size(), entry.size(), entry) == 0 &&
         host[host.size() - entry.size() - 1] == '.';
}

std::string SiteKey(std::string_view host) {
  const size_t last_dot = host.rfind('.');
  if (last_dot == std::string_view::npos) return std::string(host);
  const size_t previous_dot = host.rfind('.', last_dot - 1);
  if (previous_dot == std::string_view::npos) return std::string(host);
  return std::string(host.substr(previous_dot + 1));
}

uint32_t TypeMaskFromOption(std::string_view option) {
  if (option == "script") return static_cast<uint32_t>(RequestType::kScript);
  if (option == "image") return static_cast<uint32_t>(RequestType::kImage);
  if (option == "stylesheet") {
    return static_cast<uint32_t>(RequestType::kStylesheet);
  }
  if (option == "xmlhttprequest") {
    return static_cast<uint32_t>(RequestType::kXhr);
  }
  if (option == "subdocument") {
    return static_cast<uint32_t>(RequestType::kSubFrame);
  }
  if (option == "media") return static_cast<uint32_t>(RequestType::kMedia);
  if (option == "font") return static_cast<uint32_t>(RequestType::kFont);
  if (option == "object") return static_cast<uint32_t>(RequestType::kObject);
  if (option == "ping" || option == "beacon") {
    return static_cast<uint32_t>(RequestType::kPing);
  }
  if (option == "websocket" || option == "other") {
    return static_cast<uint32_t>(RequestType::kOther);
  }
  if (option == "document") {
    return static_cast<uint32_t>(RequestType::kMainFrame);
  }
  return 0;
}

// Wildcard-aware substring match starting at the given offset.
bool MatchesAt(const std::string& url, const std::string& pattern,
               size_t url_index, bool anchor_end) {
  size_t pattern_index = 0;
  while (pattern_index < pattern.size()) {
    const char token = pattern[pattern_index];
    if (token == '*') {
      ++pattern_index;
      if (pattern_index == pattern.size()) return !anchor_end || true;
      for (size_t next = url_index; next <= url.size(); ++next) {
        if (MatchesAt(url, pattern.substr(pattern_index), next, anchor_end)) {
          return true;
        }
      }
      return false;
    }
    if (token == '^') {
      if (url_index == url.size()) {
        ++pattern_index;
        continue;
      }
      if (!IsSeparator(url[url_index])) return false;
      ++url_index;
      ++pattern_index;
      continue;
    }
    if (url_index >= url.size() || url[url_index] != token) return false;
    ++url_index;
    ++pattern_index;
  }
  return !anchor_end || url_index == url.size();
}

}  // namespace

Engine& Engine::Get() {
  static Engine engine;
  return engine;
}

void Engine::AddRule(std::string_view raw_line) {
  const std::string line = Trim(raw_line);
  if (line.empty() || line[0] == '!' || line[0] == '#') {
    // "#" alone starts a comment; element hiding always uses "##".
    if (line.find("##") == std::string::npos) return;
  }
  if (line.starts_with("[")) return;

  // Cosmetic rules: [domains]##selector
  const size_t hide_separator = line.find("##");
  if (hide_separator != std::string::npos) {
    const std::string domains = Lower(line.substr(0, hide_separator));
    const std::string selector = Trim(line.substr(hide_separator + 2));
    if (selector.empty()) return;
    if (domains.empty()) {
      generic_selectors_.push_back(selector);
    } else {
      std::stringstream stream(domains);
      std::string domain;
      while (std::getline(stream, domain, ',')) {
        const std::string trimmed = Trim(domain);
        if (!trimmed.empty()) host_selectors_[trimmed].push_back(selector);
      }
    }
    ++cosmetic_rule_count_;
    return;
  }

  std::string body = line;
  bool exception = false;
  if (body.starts_with("@@")) {
    exception = true;
    body.erase(0, 2);
  }

  NetworkRule rule;
  const size_t options_separator = body.find('$');
  std::string options;
  if (options_separator != std::string::npos) {
    options = Lower(body.substr(options_separator + 1));
    body.erase(options_separator);
  }

  if (body.starts_with("||")) {
    rule.anchor_domain = true;
    body.erase(0, 2);
  } else if (body.starts_with("|")) {
    rule.anchor_start = true;
    body.erase(0, 1);
  }
  if (body.ends_with("|")) {
    rule.anchor_end = true;
    body.pop_back();
  }
  rule.pattern = Lower(body);
  if (rule.pattern.empty()) return;

  if (!options.empty()) {
    std::stringstream stream(options);
    std::string option;
    while (std::getline(stream, option, ',')) {
      const std::string value = Trim(option);
      if (value == "third-party" || value == "3p") {
        rule.third_party_only = true;
      } else if (value == "~third-party" || value == "first-party" ||
                 value == "1p") {
        rule.first_party_only = true;
      } else if (value.starts_with("domain=")) {
        std::stringstream domains(value.substr(7));
        std::string domain;
        while (std::getline(domains, domain, '|')) {
          if (domain.empty()) continue;
          if (domain[0] == '~') rule.exclude_domains.push_back(domain.substr(1));
          else rule.include_domains.push_back(domain);
        }
      } else {
        rule.type_mask |= TypeMaskFromOption(value);
      }
    }
  }

  if (exception) allow_rules_.push_back(std::move(rule));
  else block_rules_.push_back(std::move(rule));
}

size_t Engine::LoadFromDirectory(const std::string& directory) {
  const std::string path = directory + "\\mtk_filters.txt";
  std::ifstream file(path);
  if (!file.is_open()) return 0;

  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    AddRule(line);
  }
  loaded_ = network_rule_count() > 0 || cosmetic_rule_count_ > 0;
  return network_rule_count() + cosmetic_rule_count_;
}

bool Engine::Matches(const NetworkRule& rule, const std::string& url,
                     const std::string& host,
                     const std::string& initiator_host, RequestType type,
                     bool third_party) const {
  if (rule.type_mask != 0 &&
      (rule.type_mask & static_cast<uint32_t>(type)) == 0) {
    return false;
  }
  if (rule.third_party_only && !third_party) return false;
  if (rule.first_party_only && third_party) return false;

  if (!rule.include_domains.empty()) {
    const bool included = std::any_of(
        rule.include_domains.begin(), rule.include_domains.end(),
        [&](const std::string& domain) {
          return HostMatchesEntry(initiator_host, domain);
        });
    if (!included) return false;
  }
  for (const auto& domain : rule.exclude_domains) {
    if (HostMatchesEntry(initiator_host, domain)) return false;
  }

  if (rule.anchor_domain) {
    // Anchor at the start of the hostname or any of its parent labels.
    const size_t host_position = url.find(host);
    if (host_position == std::string::npos) return false;
    for (size_t offset = 0; offset <= host.size(); ++offset) {
      const bool label_start =
          offset == 0 || (offset > 0 && host[offset - 1] == '.');
      if (!label_start) continue;
      if (MatchesAt(url, rule.pattern, host_position + offset,
                    rule.anchor_end)) {
        return true;
      }
    }
    return false;
  }
  if (rule.anchor_start) {
    return MatchesAt(url, rule.pattern, 0, rule.anchor_end);
  }
  for (size_t offset = 0; offset <= url.size(); ++offset) {
    if (MatchesAt(url, rule.pattern, offset, rule.anchor_end)) return true;
  }
  return false;
}

bool Engine::ShouldBlock(const std::string& lowercase_url,
                         const std::string& host,
                         const std::string& initiator_host,
                         RequestType type) const {
  if (block_rules_.empty()) return false;
  const bool third_party = !initiator_host.empty() && !host.empty() &&
                           SiteKey(host) != SiteKey(initiator_host);

  const bool blocked = std::any_of(
      block_rules_.begin(), block_rules_.end(), [&](const NetworkRule& rule) {
        return Matches(rule, lowercase_url, host, initiator_host, type,
                       third_party);
      });
  if (!blocked) return false;

  return !std::any_of(
      allow_rules_.begin(), allow_rules_.end(), [&](const NetworkRule& rule) {
        return Matches(rule, lowercase_url, host, initiator_host, type,
                       third_party);
      });
}

std::string Engine::CssSelectorsForHost(const std::string& host) const {
  std::vector<std::string> selectors = generic_selectors_;
  for (const auto& [domain, domain_selectors] : host_selectors_) {
    if (HostMatchesEntry(host, domain)) {
      selectors.insert(selectors.end(), domain_selectors.begin(),
                       domain_selectors.end());
    }
  }
  if (selectors.empty()) return {};

  std::string result;
  for (size_t index = 0; index < selectors.size(); ++index) {
    if (index > 0) result += ",";
    result += selectors[index];
  }
  return result;
}

}  // namespace mtk::filters
