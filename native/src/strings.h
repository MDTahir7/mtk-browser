#pragma once

#include <string>

namespace mtk::strings {

// UI text in English and Hindi. Both translations of a string are written on one
// line of the table below, so a new string cannot be added to one language and
// forgotten in the other: the enum and both tables are generated from this list.
//
// The UI language follows the language choice already in Settings, which reads
// "English (United States)" or "Hindi and English (United States)". A separate
// UI-language setting would be a second question about the same thing.

#define MTK_STRING_TABLE(V)                                                    \
  /* Browser menu */                                                           \
  V(kAddBookmark, L"Add bookmark", L"बुकमार्क जोड़ें")                          \
  V(kRemoveBookmark, L"Remove bookmark", L"बुकमार्क हटाएँ")                     \
  V(kBookmarks, L"Bookmarks", L"बुकमार्क")                                      \
  V(kShowBookmarksBar, L"Show bookmarks bar", L"बुकमार्क बार दिखाएँ")            \
  V(kHideBookmarksBar, L"Hide bookmarks bar", L"बुकमार्क बार छिपाएँ")            \
  V(kImportBookmarks, L"Import bookmarks from another browser...",              \
    L"दूसरे ब्राउज़र से बुकमार्क आयात करें...")                                  \
  V(kHistory, L"History", L"इतिहास")                                            \
  V(kDownloads, L"Downloads", L"डाउनलोड")                                       \
  V(kSavedPasswords, L"Saved passwords...", L"सहेजे गए पासवर्ड...")               \
  V(kDownloadsActive, L"Downloads (%d active)", L"डाउनलोड (%d चल रहे)")          \
  V(kNewWindow, L"New window", L"नई विंडो")                                     \
  V(kNewPrivateWindow, L"New private window", L"नई निजी विंडो")                  \
  V(kReopenClosedTab, L"Reopen closed tab", L"बंद किया गया टैब फिर खोलें")        \
  V(kFullScreen, L"Full screen", L"पूर्ण स्क्रीन")                                \
  V(kExitFullScreen, L"Exit full screen", L"पूर्ण स्क्रीन से बाहर निकलें")          \
  V(kPrint, L"Print...", L"प्रिंट करें...")                                       \
  V(kKeyboardShortcuts, L"Keyboard shortcuts", L"कीबोर्ड शॉर्टकट")                \
  V(kSiteSettings, L"Settings for this site...", L"इस साइट की सेटिंग...")         \
  V(kClearBrowsingData, L"Clear browsing data...", L"ब्राउज़िंग डेटा हटाएँ...")    \
  V(kCheckForUpdates, L"Check for updates...", L"अपडेट देखें...")                 \
  V(kCheckingForUpdates, L"Checking for updates...", L"अपडेट देखे जा रहे हैं...")  \
  V(kMakeDefaultBrowser, L"Make MTK Browser the default...",                     \
    L"MTK Browser को डिफ़ॉल्ट बनाएँ...")                                          \
  V(kIsDefaultBrowser, L"MTK Browser is the default browser",                     \
    L"MTK Browser पहले से डिफ़ॉल्ट ब्राउज़र है")                                    \
  V(kSettings, L"Settings", L"सेटिंग")                                           \
  V(kProfilePrefix, L"Profile: ", L"प्रोफ़ाइल: ")                                \
  V(kNewProfile, L"New profile...", L"नई प्रोफ़ाइल...")                           \
  /* Tab menu */                                                               \
  V(kDuplicateTab, L"Duplicate tab", L"टैब की कॉपी बनाएँ")                       \
  V(kPinTab, L"Pin tab", L"टैब पिन करें")                                        \
  V(kUnpinTab, L"Unpin tab", L"टैब अनपिन करें")                                  \
  V(kMuteSite, L"Mute site", L"साइट म्यूट करें")                                  \
  V(kUnmuteSite, L"Unmute site", L"साइट अनम्यूट करें")                            \
  V(kCloseTab, L"Close tab", L"टैब बंद करें")                                     \
  V(kCloseOtherTabs, L"Close other tabs", L"अन्य टैब बंद करें")                    \
  V(kCloseTabsToRight, L"Close tabs to the right", L"दाईं ओर के टैब बंद करें")      \
  /* Downloads panel */                                                        \
  V(kNothingDownloaded, L"Nothing downloaded yet.",                             \
    L"अभी तक कुछ डाउनलोड नहीं हुआ।")                                            \
  V(kPause, L"Pause", L"रोकें")                                                  \
  V(kResume, L"Resume", L"फिर शुरू करें")                                        \
  V(kCancel, L"Cancel", L"रद्द करें")                                            \
  V(kOpen, L"Open", L"खोलें")                                                    \
  V(kFolder, L"Folder", L"फ़ोल्डर")                                              \
  V(kCopy, L"Copy", L"कॉपी करें")                                                \
  V(kRemove, L"Remove", L"हटाएँ")                                                \
  V(kAllDownloads, L"All downloads", L"सभी डाउनलोड")                             \
  V(kClearFinished, L"Clear finished", L"पूरे हुए हटाएँ")                          \
  /* Toolbar accessible names */                                               \
  V(kBack, L"Back", L"पीछे")                                                     \
  V(kForward, L"Forward", L"आगे")                                                \
  V(kReload, L"Reload", L"फिर लोड करें")                                          \
  V(kHomePage, L"Home page", L"होम पेज")                                         \
  V(kShields, L"MTK Shields", L"MTK शील्ड्स")                                     \
  V(kBrowserMenu, L"Browser menu", L"ब्राउज़र मेन्यू")                             \
  V(kFindPrevious, L"Find previous", L"पिछला खोजें")                              \
  V(kFindNext, L"Find next", L"अगला खोजें")                                       \
  V(kCloseFindBar, L"Close find bar", L"खोज बार बंद करें")                         \
  V(kAddressPlaceholder, L"Search or enter address",                             \
    L"खोजें या पता लिखें")

enum class Id {
#define MTK_STRING_ENUM(name, english, hindi) name,
  MTK_STRING_TABLE(MTK_STRING_ENUM)
#undef MTK_STRING_ENUM
      kCount,
};

// True when the UI is showing Hindi.
bool IsHindi();

// Reads the language preference. Called once the preference store is loaded;
// calling it again after the preference changes picks up the new value, which
// only matters at startup because a language change asks for a restart.
void Refresh();

const wchar_t* Get(Id id);

// Same lookup with the language given explicitly. Tests use this so they do not
// depend on a loaded preference file, and so both translations of every string
// can be checked.
const wchar_t* GetFor(Id id, bool hindi);

}  // namespace mtk::strings
