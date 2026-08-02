// Tests for the password store. The rules being checked here are the ones
// docs/PASSWORD_MANAGER.md commits to: https only, exact origin match, no
// guessing between several accounts, and a real encryption round trip.
//
// Save() and Reveal() only touch memory and DPAPI, so nothing here calls
// Flush(): no file is written and the developer's own profile is untouched.

#include <string>

#include "password_store.h"
#include "test_support.h"

namespace {

void ClearStore() { mtk::passwords::Store::Get().Clear(); }

}  // namespace

MTK_TEST(PasswordsRoundTripThroughEncryption) {
  ClearStore();
  auto& store = mtk::passwords::Store::Get();
  const std::wstring secret = L"Correct horse! 42 \u0939\u093f";
  MTK_EXPECT_TRUE(store.Save("https://example.com", L"someone@example.com",
                             secret));

  std::wstring revealed;
  MTK_EXPECT_TRUE(store.Reveal("https://example.com", L"someone@example.com",
                               &revealed));
  MTK_EXPECT_EQ_WSTR(revealed, secret);
  mtk::passwords::Scrub(&revealed);
  ClearStore();
}

MTK_TEST(PasswordsRefuseNonHttpsAndBadOrigins) {
  ClearStore();
  auto& store = mtk::passwords::Store::Get();
  // http is refused: a password sent in the clear is already exposed, and
  // remembering it would encourage repeating that.
  MTK_EXPECT_FALSE(store.Save("http://example.com", L"user", L"secret"));
  // A full URL or a bare host is not an origin, so it cannot be a key.
  MTK_EXPECT_FALSE(store.Save("https://example.com/login", L"user", L"secret"));
  MTK_EXPECT_FALSE(store.Save("example.com", L"user", L"secret"));
  MTK_EXPECT_FALSE(store.Save("", L"user", L"secret"));
  MTK_EXPECT_EQ_INT(store.credentials().size(), 0);
}

MTK_TEST(PasswordsRefuseEmptyAndOversizedFields) {
  ClearStore();
  auto& store = mtk::passwords::Store::Get();
  MTK_EXPECT_FALSE(store.Save("https://example.com", L"", L"secret"));
  MTK_EXPECT_FALSE(store.Save("https://example.com", L"user", L""));
  MTK_EXPECT_FALSE(store.Save(
      "https://example.com",
      std::wstring(mtk::passwords::kMaxUserNameLength + 1, L'u'), L"secret"));
  MTK_EXPECT_FALSE(store.Save(
      "https://example.com", L"user",
      std::wstring(mtk::passwords::kMaxPasswordLength + 1, L'p')));
  MTK_EXPECT_EQ_INT(store.credentials().size(), 0);
}

MTK_TEST(PasswordsSaveReplacesTheSameAccount) {
  ClearStore();
  auto& store = mtk::passwords::Store::Get();
  MTK_EXPECT_TRUE(store.Save("https://example.com", L"user", L"first"));
  MTK_EXPECT_TRUE(store.Save("https://example.com", L"user", L"second"));
  MTK_EXPECT_EQ_INT(store.credentials().size(), 1);

  std::wstring revealed;
  MTK_EXPECT_TRUE(store.Reveal("https://example.com", L"user", &revealed));
  MTK_EXPECT_EQ_WSTR(revealed, L"second");
  mtk::passwords::Scrub(&revealed);
  ClearStore();
}

MTK_TEST(PasswordsFillNothingWhenSeveralAccountsAreSaved) {
  ClearStore();
  auto& store = mtk::passwords::Store::Get();
  MTK_EXPECT_TRUE(store.Save("https://example.com", L"first", L"a-secret"));
  mtk::passwords::Credential credential;
  MTK_EXPECT_TRUE(store.SingleFor("https://example.com", &credential));
  MTK_EXPECT_EQ_WSTR(credential.user_name, L"first");

  // With a second account, autofill would have to guess, so it declines.
  MTK_EXPECT_TRUE(store.Save("https://example.com", L"second", L"b-secret"));
  MTK_EXPECT_FALSE(store.SingleFor("https://example.com", &credential));
  ClearStore();
}

MTK_TEST(PasswordsMatchOriginExactly) {
  ClearStore();
  auto& store = mtk::passwords::Store::Get();
  MTK_EXPECT_TRUE(store.Save("https://example.com", L"user", L"secret"));
  mtk::passwords::Credential credential;
  // A lookalike or a subdomain gets nothing, which is what stops a filled
  // credential from leaking to a neighbouring host.
  MTK_EXPECT_FALSE(store.SingleFor("https://examp1e.com", &credential));
  MTK_EXPECT_FALSE(store.SingleFor("https://login.example.com", &credential));
  MTK_EXPECT_FALSE(store.SingleFor("https://example.com.evil.test", &credential));
  MTK_EXPECT_FALSE(store.SingleFor("http://example.com", &credential));
  MTK_EXPECT_TRUE(store.SingleFor("https://example.com", &credential));
  ClearStore();
}

MTK_TEST(PasswordsRemoveAndClearForgetEverything) {
  ClearStore();
  auto& store = mtk::passwords::Store::Get();
  MTK_EXPECT_TRUE(store.Save("https://example.com", L"user", L"secret"));
  MTK_EXPECT_TRUE(store.Save("https://other.example", L"user", L"secret"));
  MTK_EXPECT_EQ_INT(store.credentials().size(), 2);
  MTK_EXPECT_TRUE(store.Remove("https://example.com", L"user"));
  MTK_EXPECT_EQ_INT(store.credentials().size(), 1);
  MTK_EXPECT_FALSE(store.Remove("https://example.com", L"user"));

  std::wstring revealed;
  MTK_EXPECT_FALSE(store.Reveal("https://example.com", L"user", &revealed));
  MTK_EXPECT_TRUE(store.Clear());
  MTK_EXPECT_EQ_INT(store.credentials().size(), 0);
}

MTK_TEST(PasswordsStorableUrlFollowsTheSafetyPolicy) {
  MTK_EXPECT_TRUE(mtk::passwords::IsStorableUrl("https://example.com/login"));
  // Plain http, private network and adult hosts can hold no credential.
  MTK_EXPECT_FALSE(mtk::passwords::IsStorableUrl("http://example.com/login"));
  MTK_EXPECT_FALSE(mtk::passwords::IsStorableUrl("https://192.168.1.1/login"));
  MTK_EXPECT_FALSE(mtk::passwords::IsStorableUrl("https://www.pornhub.com/login"));
  MTK_EXPECT_FALSE(mtk::passwords::IsStorableUrl("file:///C:/Windows/win.ini"));
  MTK_EXPECT_FALSE(mtk::passwords::IsStorableUrl("about:blank"));
}

MTK_TEST(PasswordsScrubEmptiesTheBuffer) {
  std::wstring secret = L"not-a-real-password";
  mtk::passwords::Scrub(&secret);
  MTK_EXPECT_TRUE(secret.empty());
  // Scrubbing an already empty string must not misbehave.
  mtk::passwords::Scrub(&secret);
  MTK_EXPECT_TRUE(secret.empty());
}
