#include "filter_engine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <mutex>
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

// Common multi-part public suffixes so sites under .co.in, .co.uk, .com.au,
// etc. are never collapsed into a single registrable domain.
constexpr auto kMultiPartPublicSuffixes = std::to_array<std::string_view>({
    "co.in",   "ac.in",   "gov.in",  "org.in",  "net.in",  "res.in",
    "gen.in",  "firm.in", "ind.in",  "nic.in",  "edu.in",  "mil.in",
    "co.uk",   "org.uk",  "ac.uk",   "gov.uk",  "net.uk",  "me.uk",
    "ltd.uk",  "plc.uk",  "sch.uk",  "com.au",  "net.au",  "org.au",
    "edu.au",  "gov.au",  "asn.au",  "id.au",   "co.nz",   "net.nz",
    "org.nz",  "govt.nz", "ac.nz",   "co.jp",   "ne.jp",   "or.jp",
    "ac.jp",   "go.jp",   "ed.jp",   "gr.jp",   "lg.jp",   "com.br",
    "net.br",  "org.br",  "gov.br",  "edu.br",  "co.za",   "org.za",
    "gov.za",  "ac.za",   "net.za",  "com.sg",  "edu.sg",  "gov.sg",
    "net.sg",  "org.sg",  "com.my",  "net.my",  "org.my",  "gov.my",
    "edu.my",  "com.hk",  "edu.hk",  "gov.hk",  "net.hk",  "org.hk",
    "com.tw",  "org.tw",  "gov.tw",  "edu.tw",  "net.tw",  "co.kr",
    "ne.kr",   "or.kr",   "re.kr",   "pe.kr",   "go.kr",   "ac.kr",
    "com.cn",  "net.cn",  "org.cn",  "gov.cn",  "edu.cn",  "ac.cn",
    "com.mx",  "org.mx",  "gob.mx",  "edu.mx",  "net.mx",  "com.tr",
    "org.tr",  "gov.tr",  "edu.tr",  "net.tr",  "co.il",   "org.il",
    "ac.il",   "gov.il",  "net.il",  "co.id",   "or.id",   "ac.id",
    "go.id",   "web.id",  "co.th",   "in.th",   "ac.th",   "go.th",
    "or.th",   "com.vn",  "net.vn",  "org.vn",  "edu.vn",  "gov.vn",
    "com.ph",  "net.ph",  "org.ph",  "gov.ph",  "edu.ph",  "com.pk",
    "net.pk",  "org.pk",  "gov.pk",  "edu.pk",  "com.bd",  "net.bd",
    "org.bd",  "gov.bd",  "edu.bd",  "ac.bd",   "com.np",  "org.np",
    "edu.np",  "gov.np",  "net.np",  "com.ng",  "org.ng",  "gov.ng",
    "edu.ng",  "net.ng",  "co.ke",   "or.ke",   "ac.ke",   "go.ke",
    "com.eg",  "edu.eg",  "gov.eg",  "org.eg",  "com.sa",  "edu.sa",
    "gov.sa",  "org.sa",  "co.ae",   "net.ae",  "org.ae",  "gov.ae",
    "ac.ae",   "com.ar",  "org.ar",  "gob.ar",  "edu.ar",  "net.ar",
    "com.co",  "org.co",  "gov.co",  "edu.co",  "net.co",  "com.pe",
    "org.pe",  "gob.pe",  "edu.pe",  "net.pe",  "com.ua",  "org.ua",
    "gov.ua",  "edu.ua",  "net.ua",  "co.at",   "or.at",   "gv.at",
    "ac.at",   "com.pl",  "net.pl",  "org.pl",  "edu.pl",  "gov.pl",
    "github.io", "gitlab.io", "pages.dev", "vercel.app", "netlify.app",
    "appspot.com", "firebaseapp.com", "web.app", "cloudfront.net",
    "azurewebsites.net", "onrender.com", "workers.dev"
});

bool IsMultiPartSuffix(std::string_view suffix) {
  return std::any_of(kMultiPartPublicSuffixes.begin(),
                     kMultiPartPublicSuffixes.end(),
                     [suffix](std::string_view entry) {
                       return suffix == entry;
                     });
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

// Fast iterative O(N) wildcard-and-separator matcher starting at `url_start`.
// Replaces recursive backtracking so pathological patterns with multiple `*`
// tokens never stall the CEF IO thread.
bool MatchesAtIterative(std::string_view url, std::string_view pattern,
                        size_t url_start, bool anchor_end) {
  size_t u = url_start;
  size_t p = 0;
  size_t star_p = std::string_view::npos;
  size_t star_u = 0;

  while (u < url.size()) {
    if (p < pattern.size()) {
      const char token = pattern[p];
      if (token == '*') {
        star_p = p++;
        star_u = u;
        continue;
      }
      if (token == '^') {
        if (IsSeparator(url[u])) {
          ++p;
          ++u;
          continue;
        }
      } else if (token == url[u]) {
        ++p;
        ++u;
        continue;
      }
    }
    // If we reached the end of the pattern and don't need to anchor to the end
    // of the URL, we have a match!
    if (p == pattern.size() && !anchor_end) return true;

    if (star_p != std::string_view::npos) {
      p = star_p + 1;
      u = ++star_u;
    } else {
      return false;
    }
  }

  while (p < pattern.size()) {
    if (pattern[p] == '*' || pattern[p] == '^') {
      ++p;
    } else {
      break;
    }
  }
  return p == pattern.size();
}

// Extracts a leading hostname from a domain-anchored pattern like
// "doubleclick.net/pagead/*" or "ads.example.co.in^" so the rule can be bucketed
// in an O(1) hash map by hostname.
std::string ExtractLeadingDomain(std::string_view pattern) {
  size_t end = 0;
  while (end < pattern.size()) {
    const unsigned char c = static_cast<unsigned char>(pattern[end]);
    if (std::isalnum(c) != 0 || c == '.' || c == '-') {
      ++end;
    } else {
      break;
    }
  }
  if (end == 0) return {};
  // Only index as an exact domain when terminated by end-of-pattern, '^', '/',
  // or ':' (not '*' or a partial label prefix like "||ad").
  if (end < pattern.size()) {
    const char next = pattern[end];
    if (next != '^' && next != '/' && next != ':' && next != '?') {
      return {};
    }
  }
  std::string_view candidate = pattern.substr(0, end);
  if (candidate.find('.') == std::string_view::npos ||
      candidate.front() == '.' || candidate.back() == '.') {
    return {};
  }
  return std::string(candidate);
}

}  // namespace

std::string EffectiveTldPlusOne(std::string_view raw_host) {
  std::string host = Lower(std::string(raw_host));
  while (!host.empty() && host.back() == '.') host.pop_back();
  if (host.empty()) return {};

  const size_t last_dot = host.rfind('.');
  if (last_dot == std::string::npos) return host;
  const size_t second_last_dot =
      last_dot > 0 ? host.rfind('.', last_dot - 1) : std::string::npos;
  if (second_last_dot == std::string::npos) return host;

  const std::string_view last_two =
      std::string_view(host).substr(second_last_dot + 1);
  if (IsMultiPartSuffix(last_two)) {
    const size_t third_last_dot =
        second_last_dot > 0 ? host.rfind('.', second_last_dot - 1)
                            : std::string::npos;
    if (third_last_dot == std::string::npos) return host;
    return host.substr(third_last_dot + 1);
  }
  return host.substr(second_last_dot + 1);
}

Engine& Engine::Get() {
  static Engine engine;
  return engine;
}

void Engine::AddRuleToState(State* state, std::string_view raw_line) {
  const std::string line = Trim(raw_line);
  if (line.empty() || line[0] == '!' || line[0] == '#') {
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
      state->generic_selectors.push_back(selector);
    } else {
      std::stringstream stream(domains);
      std::string domain;
      while (std::getline(stream, domain, ',')) {
        const std::string trimmed = Trim(domain);
        if (!trimmed.empty()) state->host_selectors[trimmed].push_back(selector);
      }
    }
    ++state->cosmetic_rule_count;
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

  if (rule.anchor_domain) {
    rule.indexed_domain = ExtractLeadingDomain(rule.pattern);
  }

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

  if (exception) {
    const size_t idx = state->allow_rules.size();
    if (!rule.indexed_domain.empty()) {
      state->domain_allow_index[rule.indexed_domain].push_back(idx);
    } else {
      state->generic_allow_indices.push_back(idx);
    }
    state->allow_rules.push_back(std::move(rule));
  } else {
    const size_t idx = state->block_rules.size();
    if (!rule.indexed_domain.empty()) {
      state->domain_block_index[rule.indexed_domain].push_back(idx);
    } else {
      state->generic_block_indices.push_back(idx);
    }
    state->block_rules.push_back(std::move(rule));
  }
}

size_t Engine::LoadFromDirectory(const std::string& directory) {
  const std::string path = directory + "\\mtk_filters.txt";
  std::ifstream file(path);
  if (!file.is_open()) return 0;

  State next_state;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    AddRuleToState(&next_state, line);
  }
  const size_t total = next_state.block_rules.size() +
                       next_state.allow_rules.size() +
                       next_state.cosmetic_rule_count;
  next_state.loaded = total > 0;

  std::unique_lock<std::shared_mutex> lock(mutex_);
  state_ = std::move(next_state);
  return total;
}

size_t Engine::ReloadFromText(std::string_view content) {
  if (content.empty()) return 0;
  State next_state;
  size_t offset = 0;
  while (offset < content.size()) {
    const size_t end = content.find('\n', offset);
    std::string_view line = content.substr(
        offset, (end == std::string_view::npos ? content.size() : end) - offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    AddRuleToState(&next_state, line);
    if (end == std::string_view::npos) break;
    offset = end + 1;
  }
  const size_t total = next_state.block_rules.size() +
                       next_state.allow_rules.size() +
                       next_state.cosmetic_rule_count;
  if (total == 0) return 0;
  next_state.loaded = true;

  std::unique_lock<std::shared_mutex> lock(mutex_);
  state_ = std::move(next_state);
  return total;
}

bool Engine::loaded() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return state_.loaded;
}

size_t Engine::network_rule_count() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return state_.block_rules.size() + state_.allow_rules.size();
}

size_t Engine::cosmetic_rule_count() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return state_.cosmetic_rule_count;
}

bool Engine::Matches(const NetworkRule& rule, const std::string& url,
                     const std::string& host,
                     const std::string& initiator_host, RequestType type,
                     bool third_party) {
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
    const size_t host_position = url.find(host);
    if (host_position == std::string::npos) return false;
    for (size_t offset = 0; offset <= host.size(); ++offset) {
      const bool label_start =
          offset == 0 || (offset > 0 && host[offset - 1] == '.');
      if (!label_start) continue;
      if (MatchesAtIterative(url, rule.pattern, host_position + offset,
                             rule.anchor_end)) {
        return true;
      }
    }
    return false;
  }
  if (rule.anchor_start) {
    return MatchesAtIterative(url, rule.pattern, 0, rule.anchor_end);
  }
  for (size_t offset = 0; offset <= url.size(); ++offset) {
    if (MatchesAtIterative(url, rule.pattern, offset, rule.anchor_end)) {
      return true;
    }
  }
  return false;
}

bool Engine::ShouldBlock(const std::string& lowercase_url,
                         const std::string& host,
                         const std::string& initiator_host,
                         RequestType type) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (state_.block_rules.empty()) return false;

  const bool third_party =
      !initiator_host.empty() && !host.empty() &&
      EffectiveTldPlusOne(host) != EffectiveTldPlusOne(initiator_host);

  auto matches_indexed_or_generic =
      [&](const std::vector<NetworkRule>& rules,
          const std::unordered_map<std::string, std::vector<size_t>>& domain_index,
          const std::vector<size_t>& generic_indices) {
        if (!host.empty() && !domain_index.empty()) {
          size_t offset = 0;
          while (offset < host.size()) {
            const std::string sub = host.substr(offset);
            const auto it = domain_index.find(sub);
            if (it != domain_index.end()) {
              for (const size_t idx : it->second) {
                if (Matches(rules[idx], lowercase_url, host, initiator_host,
                            type, third_party)) {
                  return true;
                }
              }
            }
            const size_t dot = host.find('.', offset);
            if (dot == std::string::npos) break;
            offset = dot + 1;
          }
        }
        for (const size_t idx : generic_indices) {
          if (Matches(rules[idx], lowercase_url, host, initiator_host, type,
                      third_party)) {
            return true;
          }
        }
        return false;
      };

  if (!matches_indexed_or_generic(state_.block_rules, state_.domain_block_index,
                                  state_.generic_block_indices)) {
    return false;
  }

  return !matches_indexed_or_generic(
      state_.allow_rules, state_.domain_allow_index, state_.generic_allow_indices);
}

std::string Engine::CssSelectorsForHost(const std::string& host) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::vector<std::string> selectors = state_.generic_selectors;
  for (const auto& [domain, domain_selectors] : state_.host_selectors) {
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
