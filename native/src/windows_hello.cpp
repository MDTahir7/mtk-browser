#include "windows_hello.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.UI.h>

namespace mtk::hello {
namespace {

using winrt::Windows::Security::Credentials::UI::UserConsentVerificationResult;
using winrt::Windows::Security::Credentials::UI::UserConsentVerifier;
using winrt::Windows::Security::Credentials::UI::UserConsentVerifierAvailability;

class Apartment final {
 public:
  Apartment() {
    try {
      winrt::init_apartment(winrt::apartment_type::single_threaded);
      initialized_ = true;
    } catch (const winrt::hresult_error&) {
      // CEF may already have selected a different apartment model for its UI
      // thread. Windows Runtime calls remain valid in that existing apartment.
    }
  }
  ~Apartment() {
    if (initialized_) winrt::uninit_apartment();
  }

 private:
  bool initialized_ = false;
};

std::wstring AvailabilityDetail(UserConsentVerifierAvailability value) {
  switch (value) {
    case UserConsentVerifierAvailability::DeviceNotPresent:
      return L"Windows Hello is not available on this device.";
    case UserConsentVerifierAvailability::NotConfiguredForUser:
      return L"Set up Windows Hello in Windows Settings before turning this on.";
    case UserConsentVerifierAvailability::DisabledByPolicy:
      return L"Windows Hello is disabled by a Windows policy.";
    case UserConsentVerifierAvailability::DeviceBusy:
      return L"Windows Hello is busy. Try again in a moment.";
    case UserConsentVerifierAvailability::Available:
      break;
  }
  return L"Windows Hello is not available right now.";
}

std::wstring VerificationDetail(UserConsentVerificationResult value) {
  switch (value) {
    case UserConsentVerificationResult::DeviceNotPresent:
      return L"Windows Hello is no longer available on this device.";
    case UserConsentVerificationResult::NotConfiguredForUser:
      return L"Windows Hello is not set up for this Windows account.";
    case UserConsentVerificationResult::DisabledByPolicy:
      return L"Windows Hello is disabled by a Windows policy.";
    case UserConsentVerificationResult::DeviceBusy:
      return L"Windows Hello is busy. Try again in a moment.";
    case UserConsentVerificationResult::RetriesExhausted:
      return L"Too many Windows Hello attempts were made. Try again later.";
    case UserConsentVerificationResult::Canceled:
      return L"Windows Hello verification was canceled.";
    case UserConsentVerificationResult::Verified:
      break;
  }
  return L"Windows Hello could not verify this request.";
}

}  // namespace

Result CheckAvailability() {
  Apartment apartment;
  try {
    const auto availability = UserConsentVerifier::CheckAvailabilityAsync().get();
    if (availability == UserConsentVerifierAvailability::Available) {
      return {true, false, {}};
    }
    return {false, false, AvailabilityDetail(availability)};
  } catch (const winrt::hresult_error&) {
    return {false, false, L"Windows Hello could not be reached on this PC."};
  }
}

Result Verify(const std::wstring& purpose) {
  Result availability = CheckAvailability();
  if (!availability.available) return availability;

  Apartment apartment;
  try {
    const auto response = UserConsentVerifier::RequestVerificationAsync(
        winrt::hstring(purpose)).get();
    if (response == UserConsentVerificationResult::Verified) {
      return {true, true, {}};
    }
    return {true, false, VerificationDetail(response)};
  } catch (const winrt::hresult_error&) {
    return {true, false, L"Windows Hello could not complete this request."};
  }
}

}  // namespace mtk::hello
