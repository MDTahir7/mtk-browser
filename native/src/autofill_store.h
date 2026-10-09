#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mtk::autofill {

struct AddressProfile {
  std::string id;              // Alphanumeric identifier, e.g. "addr_1"
  std::wstring label;          // e.g. "Home", "Work"
  std::wstring full_name;      // e.g. "Mohammad Tahir"
  std::wstring email;          // e.g. "user@example.com"
  std::wstring phone;          // e.g. "+91 9876543210"
  std::wstring street_address; // e.g. "123 Main St, Apt 4B"
  std::wstring city;           // e.g. "New Delhi"
  std::wstring state;          // e.g. "Delhi"
  std::wstring postal_code;    // e.g. "110001"
  std::wstring country;        // e.g. "India"
  bool consent_granted = false;// Consent must be explicitly granted before autofilling
  uint64_t last_used_timestamp = 0;
};

constexpr size_t kMaxProfiles = 50;
constexpr size_t kMaxFieldLength = 256;
constexpr size_t kMaxStreetAddressLength = 512;

class Store final {
 public:
  static Store& Get();

  void Load();
  bool Flush();

  // Adds or replaces an address profile. Validates fields and generates an id if empty.
  bool SaveProfile(const AddressProfile& profile);

  // Retrieves an address profile by id. Returns false if not found.
  bool GetProfile(const std::string& id, AddressProfile* profile) const;

  // Returns all stored address profiles in order of appearance.
  std::vector<AddressProfile> ListProfiles() const;

  // Returns true if profile exists AND user has explicitly granted consent for autofill.
  bool CanAutofill(const std::string& id) const;

  // Updates the consent flag for a specific profile.
  bool SetConsent(const std::string& id, bool consent);

  // Removes the profile with the given id.
  bool RemoveProfile(const std::string& id);

  // Clears all stored profiles.
  bool Clear();

  // Number of stored profiles.
  size_t Count() const;

 private:
  std::vector<AddressProfile> profiles_;
  bool dirty_ = false;
};

}  // namespace mtk::autofill
