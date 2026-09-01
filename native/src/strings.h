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
  V(kSiteList, L"Sites with stored settings...", L"संग्रहित सेटिंग वाली साइटें...")\
  V(kUserFilters, L"Your filter rules...", L"आपके फ़िल्टर नियम...")                \
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
    L"खोजें या पता लिखें")                                                       \
  /* Shared dialog buttons */                                                  \
  V(kOk, L"OK", L"ठीक है")                                                       \
  V(kSave, L"Save", L"सहेजें")                                                    \
  V(kClose, L"Close", L"बंद करें")                                                \
  /* Bookmarks dialog */                                                       \
  V(kBookmarksTitle, L"Bookmarks - MTK Browser", L"बुकमार्क — MTK Browser")       \
  V(kBookmarksHint, L"Select an item to open it in the current tab.",            \
    L"किसी आइटम को चुनें, वह इसी टैब में खुलेगा।")                                 \
  V(kSearch, L"Search", L"खोजें")                                                \
  V(kRemoveAll, L"Remove all", L"सभी हटाएँ")                                     \
  V(kRemoveAllBookmarksQuestion,                                                \
    L"Remove all saved bookmarks?\n\nBookmark folders are removed as well.",     \
    L"सभी सहेजे गए बुकमार्क हटाएँ?\n\nबुकमार्क फ़ोल्डर भी हट जाएँगे।")             \
  /* Downloads dialog */                                                       \
  V(kDownloadsTitle, L"Downloads - MTK Browser", L"डाउनलोड — MTK Browser")        \
  V(kOpenFolder, L"Open folder", L"फ़ोल्डर खोलें")                                \
  V(kOpenFile, L"Open file", L"फ़ाइल खोलें")                                      \
  V(kShowInFolder, L"Show in folder", L"फ़ोल्डर में दिखाएँ")                       \
  /* Clear browsing data dialog */                                             \
  V(kClearDataTitle, L"Clear browsing data - MTK Browser",                       \
    L"ब्राउज़िंग डेटा हटाएँ — MTK Browser")                                       \
  V(kClearDataIntro,                                                            \
    L"Choose what to remove from this computer. This cannot be undone.",         \
    L"चुनें कि इस कंप्यूटर से क्या हटाना है। यह वापस नहीं लाया जा सकता।")           \
  V(kClearHistoryItem, L"Browsing history", L"ब्राउज़िंग इतिहास")                 \
  V(kClearDownloadsItem, L"Download history (files on disk are kept)",           \
    L"डाउनलोड इतिहास (डिस्क पर फ़ाइलें रहेंगी)")                                  \
  V(kClearCookiesItem, L"Cookies and site data (signs you out of sites)",        \
    L"कुकीज़ और साइट डेटा (साइटों से साइन आउट हो जाएँगे)")                          \
  V(kClearCacheItem, L"Cached files and images",                                 \
    L"कैश की गई फ़ाइलें और चित्र")                                                \
  V(kClearSessionItem, L"Saved tabs from the last session",                      \
    L"पिछले सत्र के सहेजे गए टैब")                                                \
  V(kClearSiteSettingsItem,                                                     \
    L"Per-site settings (permissions, cookies, zoom)",                           \
    L"प्रति-साइट सेटिंग (अनुमतियाँ, कुकीज़, ज़ूम)")                                \
  V(kClearPasswordsItem, L"Saved passwords", L"सहेजे गए पासवर्ड")                 \
  V(kClearDataNote,                                                             \
    L"Settings, the parent PIN and the download mode are never changed here.",   \
    L"सेटिंग, पैरेंट पिन और डाउनलोड मोड यहाँ से कभी नहीं बदलते।")                  \
  V(kClearNow, L"Clear now", L"अभी हटाएँ")                                       \
  /* Parent PIN dialogs */                                                     \
  V(kParentPinTitle, L"Parent PIN - MTK Browser", L"पैरेंट पिन — MTK Browser")     \
  V(kParentPin, L"Parent PIN", L"पैरेंट पिन")                                     \
  V(kUnlock, L"Unlock", L"अनलॉक करें")                                           \
  V(kPinSetupIntro,                                                             \
    L"A parent PIN stops safety settings from being changed by someone else. "    \
    L"Use at least 4 characters.",                                               \
    L"पैरेंट पिन सुरक्षा सेटिंग को किसी और के बदलने से रोकता है। कम से कम 4 अक्षर "  \
    L"रखें।")                                                                    \
  V(kCurrentPin, L"Current PIN", L"मौजूदा पिन")                                   \
  V(kNewPin, L"New PIN", L"नया पिन")                                             \
  V(kConfirmNewPin, L"Confirm new PIN", L"नया पिन दोबारा लिखें")                  \
  V(kRemovePin, L"Remove PIN", L"पिन हटाएँ")                                      \
  V(kSavePin, L"Save PIN", L"पिन सहेजें")                                          \
  V(kCurrentPinNoneSet, L"Current PIN (none set yet)",                            \
    L"मौजूदा पिन (अभी कोई सेट नहीं है)")                                          \
  V(kEnterParentPin, L"Enter the parent PIN to continue.",                        \
    L"आगे बढ़ने के लिए पैरेंट पिन लिखें।")                                        \
  /* Settings dialog */                                                        \
  V(kSettingsTitle, L"MTK Browser Settings", L"MTK Browser सेटिंग")               \
  V(kGeneralHeading, L"General", L"सामान्य")                                     \
  V(kHomePageLabel, L"Home page", L"होम पेज")                                    \
  V(kOnStartup, L"On startup", L"शुरू होने पर")                                   \
  V(kWebsiteLanguage, L"Website language preference",                             \
    L"वेबसाइट भाषा प्राथमिकता")                                                  \
  V(kDownloadsNote,                                                             \
    L"Safe downloads save only known file types and ask every time. Standard "    \
    L"downloads work like other browsers and save any file type.",                \
    L"सुरक्षित डाउनलोड सिर्फ़ जानी-पहचानी फ़ाइल क़िस्में सहेजते हैं और हर बार "     \
    L"पूछते हैं। सामान्य डाउनलोड दूसरे ब्राउज़र की तरह काम करते हैं और कोई भी "     \
    L"फ़ाइल क़िस्म सहेजते हैं।")                                                  \
  V(kSafetyStaysOnNote,                                                         \
    L"Site safety, shields and SafeSearch stay enabled in both modes.",           \
    L"साइट सुरक्षा, शील्ड्स और SafeSearch दोनों मोड में चालू रहते हैं।")            \
  V(kLanguageRestartNote,                                                       \
    L"Language preference takes effect after you restart MTK Browser.",           \
    L"भाषा की पसंद MTK Browser दोबारा शुरू करने पर लागू होगी।")                    \
  V(kConnectionHeading, L"Connection", L"कनेक्शन")                               \
  V(kEncryptedDns, L"Encrypted DNS", L"एन्क्रिप्टेड DNS")                          \
  V(kEncryptedDnsRestartNote,                                                   \
    L"Encrypted DNS takes effect after you restart MTK Browser.",                 \
    L"एन्क्रिप्टेड DNS MTK Browser दोबारा शुरू करने पर लागू होगा।")                 \
  V(kParentControlsHeading, L"Parent controls", L"पैरेंट नियंत्रण")                \
  V(kParentPinButton, L"Parent PIN...", L"पैरेंट पिन...")                          \
  V(kHttpsOnlyOption,                                                           \
    L"Block plain HTTP content on pages (HTTPS-only)",                            \
    L"पेजों पर सादा HTTP सामग्री रोकें (सिर्फ़ HTTPS)")                            \
  V(kAutoUpdateOption,                                                          \
    L"Check for signed MTK Browser updates automatically",                        \
    L"हस्ताक्षरित MTK Browser अपडेट अपने आप देखें")                               \
  V(kStartupHomePage, L"Open the home page", L"होम पेज खोलें")                    \
  V(kStartupContinue, L"Continue where you left off",                             \
    L"जहाँ छोड़ा था वहीं से शुरू करें")                                            \
  V(kSafeDownloadsOption, L"Safe downloads (recommended)",                        \
    L"सुरक्षित डाउनलोड (सुझाया गया)")                                            \
  V(kStandardDownloadsOption, L"Standard downloads (like other browsers)",        \
    L"सामान्य डाउनलोड (दूसरे ब्राउज़र की तरह)")                                   \
  V(kPinRequiredState, L"A parent PIN is required to open Settings.",             \
    L"सेटिंग खोलने के लिए पैरेंट पिन ज़रूरी है।")                                  \
  V(kPinMissingState, L"No parent PIN. Anyone can change these settings.",        \
    L"कोई पैरेंट पिन नहीं। कोई भी ये सेटिंग बदल सकता है।")                         \
  /* Encrypted DNS providers. The operator names stay as they are; only the      \
     description beside each one is translated. */                              \
  V(kDohCloudflareFamily,                                                       \
    L"Cloudflare for Families (blocks malware and adult domains)",                \
    L"Cloudflare for Families (मैलवेयर और वयस्क डोमेन रोकता है)")                  \
  V(kDohCloudflare, L"Cloudflare (1.1.1.1)", L"Cloudflare (1.1.1.1)")             \
  V(kDohQuad9, L"Quad9 (blocks known malicious domains)",                         \
    L"Quad9 (जाने-पहचाने नुक़सानदेह डोमेन रोकता है)")                              \
  V(kDohGoogle, L"Google Public DNS", L"Google Public DNS")                       \
  V(kUseWindowsDns, L"Use the Windows DNS setting",                               \
    L"Windows की DNS सेटिंग इस्तेमाल करें")                                       \
  /* Your filter rules dialog */                                               \
  V(kUserFiltersTitle, L"Your filter rules - MTK Browser",                        \
    L"आपके फ़िल्टर नियम — MTK Browser")                                          \
  V(kFiltersLine1,                                                              \
    L"One rule per line. A rule blocks any address that contains it.",            \
    L"हर पंक्ति में एक नियम। नियम उस पते को रोकता है जिसमें वह मौजूद हो।")          \
  V(kFiltersLine2,                                                              \
    L"Start a line with @@ to allow instead of block, or with # for a note.",      \
    L"रोकने की जगह अनुमति देने के लिए पंक्ति @@ से शुरू करें, टिप्पणी के लिए # से।") \
  V(kFiltersNote,                                                               \
    L"Your rules cannot switch off site safety, SafeSearch or adult-content "     \
    L"blocking.",                                                                 \
    L"आपके नियम साइट सुरक्षा, SafeSearch या वयस्क सामग्री की रोक बंद नहीं कर "      \
    L"सकते।")                                                                    \
  /* Saved passwords dialog */                                                  \
  V(kPasswordsTitle, L"Saved passwords - MTK Browser",                            \
    L"सहेजे गए पासवर्ड — MTK Browser")                                           \
  V(kPasswordsIntro,                                                            \
    L"Saved sign-ins are encrypted for this Windows account and this profile.",    \
    L"सहेजे गए साइन-इन इस Windows खाते और इस प्रोफ़ाइल के लिए एन्क्रिप्ट किए "      \
    L"जाते हैं।")                                                                \
  V(kShowPassword, L"Show password", L"पासवर्ड दिखाएँ")                           \
  V(kCopyPassword, L"Copy password", L"पासवर्ड कॉपी करें")                        \
  /* Site settings dialog */                                                    \
  V(kSiteSettingsTitle, L"Site settings - MTK Browser",                           \
    L"साइट सेटिंग — MTK Browser")                                                \
  V(kSettingsForLabel, L"Settings for", L"इसकी सेटिंग")                           \
  V(kSiteScopeNote, L"These choices apply only to this site.",                    \
    L"ये चुनाव सिर्फ़ इस साइट पर लागू होते हैं।")                                  \
  V(kCamera, L"Camera", L"कैमरा")                                                \
  V(kMicrophone, L"Microphone", L"माइक्रोफ़ोन")                                   \
  V(kLocation, L"Location", L"स्थान")                                            \
  V(kNotifications, L"Notifications", L"सूचनाएँ")                                 \
  V(kBlockAdsOnSite, L"Block ads and trackers on this site",                      \
    L"इस साइट पर विज्ञापन और ट्रैकर रोकें")                                       \
  V(kAllowCookiesOnSite, L"Allow cookies and site data",                          \
    L"कुकीज़ और साइट डेटा की अनुमति दें")                                          \
  V(kAllowPlainHttpOnSite, L"Allow plain HTTP on this site",                      \
    L"इस साइट पर सादा HTTP की अनुमति दें")                                        \
  V(kSiteSafetyNote,                                                            \
    L"Site safety, SafeSearch and adult-content blocking stay on for every site "  \
    L"and cannot be switched off here.",                                          \
    L"साइट सुरक्षा, SafeSearch और वयस्क सामग्री की रोक हर साइट पर चालू रहती है और " \
    L"यहाँ से बंद नहीं की जा सकती।")                                             \
  V(kZoom, L"Zoom", L"ज़ूम")                                                     \
  V(kResetZoom, L"Reset zoom", L"ज़ूम रीसेट करें")                                \
  V(kForgetSite, L"Forget site", L"साइट भुला दें")                                \
  V(kPermissionBlockedDefault, L"Blocked (default)", L"रोका गया (डिफ़ॉल्ट)")       \
  V(kPermissionAllowed, L"Allowed", L"अनुमति है")                                 \
  V(kPermissionBlockedNeverAsk, L"Blocked, and never ask",                        \
    L"रोका गया, और कभी न पूछें")                                                  \
                                                                                  \
  /* Site list dialog */                                                        \
  V(kSiteListTitle, L"Sites with stored settings - MTK Browser",                  \
    L"संग्रहित सेटिंग वाली साइटें — MTK Browser")                                  \
  V(kSiteListIntro,                                                              \
    L"Every site with a permission, filtering, cookie or zoom choice that is "    \
    L"not the default.",                                                          \
    L"हर वह साइट जिसकी अनुमति, फ़िल्टरिंग, कुकी या ज़ूम की पसंद डिफ़ॉल्ट से अलग है।") \
  V(kSiteListOpen, L"Change...", L"बदलें...")                                     \
  V(kSiteListClear, L"Clear this site's data", L"इस साइट का डेटा हटाएँ")           \
  V(kSiteListEmptyNote, L"No site has a stored setting yet.",                     \
    L"अभी किसी साइट की कोई सेटिंग संग्रहित नहीं है।")                              \
  V(kSiteListClearConfirm,                                                        \
    L"Clear this site's stored settings, browsing history and saved "             \
    L"passwords?\n\nThis cannot be undone.",                                       \
    L"इस साइट की संग्रहित सेटिंग, ब्राउज़िंग इतिहास और सहेजे गए पासवर्ड हटाएँ?\n\n"  \
    L"इसे वापस नहीं किया जा सकता।")

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
