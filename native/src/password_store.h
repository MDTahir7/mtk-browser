#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mtk::passwords {

// Saved sign-in for one origin and user name. The password itself is never held
// here: it lives only in the encrypted blob and is decrypted on demand.
//
// The threat model and the reasoning behind every rule in this file are in
// docs/PASSWORD_MANAGER.md. Read that before changing anything here.
struct Credential {
  // Scheme and host, e.g. "https://example.com". Always https.
  std::string origin;
  std::wstring user_name;
};

constexpr size_t kMaxCredentials = 500;
constexpr size_t kMaxUserNameLength = 256;
constexpr size_t kMaxPasswordLength = 256;

// True when a credential may be saved for `url` at all: https only, and the
// navigation policy must allow it.
bool IsStorableUrl(const std::string& url);

class Store final {
 public:
  static Store& Get();

  void Load();
  bool Flush();

  // Saves or replaces the password for (origin, user_name). Returns false when
  // the origin is unusable, a field is too long, or encryption failed. The
  // caller is expected to have asked the person first.
  bool Save(const std::string& origin, const std::wstring& user_name,
            const std::wstring& password);

  // Credential for `origin`, if exactly one is stored. Autofill deliberately
  // does nothing when several accounts are saved for a site, rather than
  // guessing which one is wanted.
  bool SingleFor(const std::string& origin, Credential* credential) const;

  // Every credential saved for `origin`, in storage order. Used to offer an
  // account picker when SingleFor finds more than one and filling would
  // otherwise be a guess.
  std::vector<Credential> AllFor(const std::string& origin) const;

  // Decrypts one password. `password` is overwritten on failure and the caller
  // is responsible for zeroing it after use.
  bool Reveal(const std::string& origin, const std::wstring& user_name,
              std::wstring* password) const;

  bool Remove(const std::string& origin, const std::wstring& user_name);
  // Removes every credential saved for `origin`, regardless of user name.
  // Returns how many were removed.
  size_t RemoveAllForOrigin(const std::string& origin);
  bool Clear();

  const std::vector<Credential>& credentials() const;

 private:
  struct Entry {
    Credential credential;
    // DPAPI blob, user scoped, with profile-specific extra entropy.
    std::string encrypted;
  };

  std::vector<Entry> entries_;
  bool dirty_ = false;
};

// Overwrites the characters of `text` before releasing them, so a decrypted
// password does not linger in freed memory.
void Scrub(std::wstring* text);

}  // namespace mtk::passwords
