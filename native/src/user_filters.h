#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mtk::user_filters {

// Filter rules a person adds themselves, kept apart from the bundled list in
// native/resources/mtk_filters.txt.
//
// They are separate for a concrete reason: the bundled engine is loaded once at
// startup and is then immutable, which is what makes it safe to read from CEF's
// IO thread without locking. These rules can change while the browser is
// running, so they live here behind a lock instead of being merged into it.
//
// The syntax is deliberately smaller than Adblock Plus, because a rule someone
// types has to be predictable:
//
//   ads.example.com          block any request whose address contains this
//   @@cdn.example.com        allow, and win over any block rule
//   # a comment              ignored
//
// Matching is a case-insensitive substring test against the whole address. No
// wildcards, no options, no element hiding.

enum class Decision {
  kNone,
  kBlock,
  kAllow,
};

constexpr size_t kMaxRules = 500;
constexpr size_t kMaxRuleLength = 200;

// The editor calls this while the person types, and Save calls it again before
// touching disk. Keeping syntax in one place means a rule cannot look valid in
// the UI but later be ignored by the request matcher.
struct Validation {
  bool valid = true;
  size_t line = 0;  // One-based when valid is false.
  size_t active_rule_count = 0;
  std::wstring error;
};

Validation Validate(const std::vector<std::wstring>& lines);

// Reads the rules for the active profile. Call once at startup.
void Load();

// Compiles `lines` and makes them current without writing anything. Load() uses
// this after reading the file, and tests use it so they never touch the profile
// directory.
void ApplyWithoutSaving(const std::vector<std::wstring>& lines);

// Replaces the rule set and writes it to the profile directory. Blank lines and
// comments are kept as written; rules that are too long or too many are refused
// and the whole call fails, so nothing is silently dropped.
bool Save(const std::vector<std::wstring>& lines, std::wstring* error);

// Current rules as authored, one per line, for the editor.
std::vector<std::wstring> Lines();

// Number of rules that actually match something, ignoring blanks and comments.
size_t ActiveRuleCount();

// Called from the request path, including CEF's IO thread.
Decision Evaluate(const std::string& lowercase_url);

}  // namespace mtk::user_filters
