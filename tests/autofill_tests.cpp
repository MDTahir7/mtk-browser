#include "autofill_store.h"

#include <string>
#include <vector>

#include "test_support.h"

namespace {

using mtk::autofill::AddressProfile;
using mtk::autofill::Store;

AddressProfile CreateSampleProfile(const std::string& id = "addr_test_1") {
  AddressProfile profile;
  profile.id = id;
  profile.label = L"Home";
  profile.full_name = L"Mohammad Tahir";
  profile.email = L"tahir@example.com";
  profile.phone = L"+91 9876543210";
  profile.street_address = L"123 Connaught Place, Suite 4";
  profile.city = L"New Delhi";
  profile.state = L"Delhi";
  profile.postal_code = L"110001";
  profile.country = L"India";
  profile.consent_granted = false;
  profile.last_used_timestamp = 1000;
  return profile;
}

MTK_TEST(AutofillStoreSavesAndRetrievesProfile) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = Store::Get();
  store.Clear();
  MTK_EXPECT_EQ_INT(store.Count(), 0);

  AddressProfile profile = CreateSampleProfile();
  MTK_EXPECT_TRUE(store.SaveProfile(profile));
  MTK_EXPECT_EQ_INT(store.Count(), 1);

  AddressProfile loaded;
  MTK_EXPECT_TRUE(store.GetProfile("addr_test_1", &loaded));
  MTK_EXPECT_EQ_STR(loaded.id, "addr_test_1");
  MTK_EXPECT_EQ_WSTR(loaded.label, L"Home");
  MTK_EXPECT_EQ_WSTR(loaded.full_name, L"Mohammad Tahir");
  MTK_EXPECT_EQ_WSTR(loaded.email, L"tahir@example.com");
  MTK_EXPECT_EQ_WSTR(loaded.phone, L"+91 9876543210");
  MTK_EXPECT_EQ_WSTR(loaded.street_address, L"123 Connaught Place, Suite 4");
  MTK_EXPECT_EQ_WSTR(loaded.city, L"New Delhi");
  MTK_EXPECT_EQ_WSTR(loaded.state, L"Delhi");
  MTK_EXPECT_EQ_WSTR(loaded.postal_code, L"110001");
  MTK_EXPECT_EQ_WSTR(loaded.country, L"India");
  MTK_EXPECT_FALSE(loaded.consent_granted);
}

MTK_TEST(AutofillConsentRequirementAndToggle) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = Store::Get();
  store.Clear();

  AddressProfile profile = CreateSampleProfile();
  MTK_EXPECT_TRUE(store.SaveProfile(profile));

  // Without explicit consent, CanAutofill must return false
  MTK_EXPECT_FALSE(store.CanAutofill("addr_test_1"));

  // Granting consent allows autofill
  MTK_EXPECT_TRUE(store.SetConsent("addr_test_1", true));
  MTK_EXPECT_TRUE(store.CanAutofill("addr_test_1"));

  // Revoking consent immediately prevents autofill
  MTK_EXPECT_TRUE(store.SetConsent("addr_test_1", false));
  MTK_EXPECT_FALSE(store.CanAutofill("addr_test_1"));
}

MTK_TEST(AutofillProfileUpdatesAndRemovals) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = Store::Get();
  store.Clear();

  AddressProfile profile1 = CreateSampleProfile("addr_1");
  AddressProfile profile2 = CreateSampleProfile("addr_2");
  profile2.label = L"Work";
  profile2.email = L"work@example.com";

  MTK_EXPECT_TRUE(store.SaveProfile(profile1));
  MTK_EXPECT_TRUE(store.SaveProfile(profile2));
  MTK_EXPECT_EQ_INT(store.Count(), 2);

  // Update profile 1
  profile1.city = L"Mumbai";
  profile1.state = L"Maharashtra";
  profile1.postal_code = L"400001";
  MTK_EXPECT_TRUE(store.SaveProfile(profile1));
  MTK_EXPECT_EQ_INT(store.Count(), 2);

  AddressProfile loaded;
  MTK_EXPECT_TRUE(store.GetProfile("addr_1", &loaded));
  MTK_EXPECT_EQ_WSTR(loaded.city, L"Mumbai");
  MTK_EXPECT_EQ_WSTR(loaded.postal_code, L"400001");

  // Remove profile 1
  MTK_EXPECT_TRUE(store.RemoveProfile("addr_1"));
  MTK_EXPECT_EQ_INT(store.Count(), 1);
  MTK_EXPECT_FALSE(store.GetProfile("addr_1", &loaded));
  MTK_EXPECT_TRUE(store.GetProfile("addr_2", &loaded));
}

MTK_TEST(AutofillValidationRejectsEmptyOrOversizedFields) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = Store::Get();
  store.Clear();

  // Completely blank profile must be rejected
  AddressProfile empty_profile;
  empty_profile.id = "bad_1";
  MTK_EXPECT_FALSE(store.SaveProfile(empty_profile));

  // Profile with oversized field (>256 chars) must be rejected
  AddressProfile oversized = CreateSampleProfile("bad_2");
  oversized.full_name = std::wstring(300, L'A');
  MTK_EXPECT_FALSE(store.SaveProfile(oversized));

  // Non-existent profile query fails safely
  AddressProfile dummy;
  MTK_EXPECT_FALSE(store.GetProfile("does_not_exist", &dummy));
  MTK_EXPECT_FALSE(store.CanAutofill("does_not_exist"));
  MTK_EXPECT_FALSE(store.SetConsent("does_not_exist", true));
}

MTK_TEST(AutofillStoreRoundtripsThroughEncryptionAndReload) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = Store::Get();
  store.Clear();

  AddressProfile profile = CreateSampleProfile("addr_persist");
  profile.consent_granted = true;
  profile.phone = L"+91 99999 88888";
  profile.street_address = L"Line 1 with | pipe and \\ backslash";
  MTK_EXPECT_TRUE(store.SaveProfile(profile));

  // Flush and reload from disk
  MTK_EXPECT_TRUE(store.Flush());
  store.Load();

  MTK_EXPECT_EQ_INT(store.Count(), 1);
  AddressProfile loaded;
  MTK_EXPECT_TRUE(store.GetProfile("addr_persist", &loaded));
  MTK_EXPECT_EQ_WSTR(loaded.full_name, L"Mohammad Tahir");
  MTK_EXPECT_EQ_WSTR(loaded.phone, L"+91 99999 88888");
  MTK_EXPECT_EQ_WSTR(loaded.street_address, L"Line 1 with | pipe and \\ backslash");
  MTK_EXPECT_TRUE(loaded.consent_granted);
  MTK_EXPECT_TRUE(store.CanAutofill("addr_persist"));

  // Clear removes from memory and disk
  MTK_EXPECT_TRUE(store.Clear());
  store.Load();
  MTK_EXPECT_EQ_INT(store.Count(), 0);
}

}  // namespace
