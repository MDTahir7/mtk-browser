#include "policy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

#include "filter_engine.h"

namespace mtk::policy {
namespace {

// Substrings that are unambiguous enough to match anywhere inside a hostname.
constexpr auto kAdultHostSubstrings = std::to_array<std::string_view>({
    "porn", "xxx", "hentai", "xvideo", "xnxx", "xhamster", "redtube",
    "youporn", "brazzers", "spankbang", "chaturbate", "stripchat",
    "camsoda", "livejasmin", "myfreecams", "motherless", "eporner",
    "youjizz", "fapello", "rule34", "camgirl", "camwhore", "onlyfans",
    "milf", "hqporner", "tnaflix", "empflix", "javhd", "sexcam",
    "camsex", "pornstar", "erotica", "nudify", "deepnude"});

// Weak words that must match a whole hostname label or a whole query token,
// so legitimate names such as "essex" or "analytics" are never blocked.
constexpr auto kAdultWords = std::to_array<std::string_view>({
    "sex", "sexy", "nude", "nudes", "nudity", "naked", "boobs", "tits",
    "anal", "bdsm", "fetish", "escort", "escorts", "erotic", "incest",
    "orgy", "orgasm", "blowjob", "cumshot", "creampie", "nsfw", "hookup",
    "adultvideo", "camsite", "onlyfan", "playboy", "hustler"});

// Substrings for piracy, warez and dark-web gateways.
constexpr auto kIllegalHostSubstrings = std::to_array<std::string_view>({
    "torrent", "piratebay", "1337x", "warez", "keygen", "nulled",
    "putlocker", "123movie", "fmovies", "solarmovie", "movierulz",
    "tamilrockers", "filmyzilla", "worldfree4u", "khatrimaza",
    "pirate-proxy", "darkweb", "darknet", "dark-market", "silkroad"});

// Weak illegal-content words matched on label/token boundaries only.
constexpr auto kIllegalWords = std::to_array<std::string_view>({
    "crack", "cracks", "cracked", "serialkey", "activator", "repack",
    "pirated", "piracy", "carding", "cvvshop", "fullz", "botnet",
    "ddos", "ransomware", "stresser", "doxbin"});

// Whole hostnames (with subdomains) that are always blocked.
constexpr auto kBlockedHosts = std::to_array<std::string_view>({
    "pornhub.com", "xvideos.com", "xnxx.com", "redtube.com",
    "youporn.com", "onlyfans.com", "chaturbate.com", "dark.fail",
    "xhamster.com", "brazzers.com", "spankbang.com", "stripchat.com",
    "camsoda.com", "livejasmin.com", "myfreecams.com", "beeg.com",
    "tube8.com", "eporner.com", "youjizz.com", "xvideos2.com",
    "xnxx2.com", "tnaflix.com", "empflix.com", "hqporner.com",
    "onlyfans.co", "fansly.com", "bongacams.com", "cam4.com",
    "camwhores.tv", "porntrex.com", "txxx.com", "upornia.com",
    "hclips.com", "hotmovs.com", "sunporno.com", "porn300.com",
    "youjizz.co", "pornone.com", "sxyprn.com", "nhentai.net",
    "e-hentai.org", "exhentai.org", "hanime.tv", "javhd.com",
    "erome.com", "fapello.com", "coomer.su", "kemono.su",
    "thepiratebay.org", "1337x.to", "yts.mx", "rarbg.to",
    "nyaa.si", "limetorrents.lol", "torrentgalaxy.to", "kickasstorrents.to",
    "fitgirl-repacks.site", "dodi-repacks.site", "steamunlocked.net",
    "ocean-of-games.com", "igg-games.com", "crackwatch.com",
    "sci-hub.se", "libgen.is", "annas-archive.org",
    "fmovies.to", "123movies.net", "putlocker.vip", "soap2day.to",
    "hydraruzxpnew4af.onion", "torrez3market.onion"});

// Suffixes reserved for adult or hidden-service content.
constexpr auto kBlockedTldSuffixes = std::to_array<std::string_view>({
    ".onion", ".xxx", ".adult", ".porn", ".sex", ".sexy", ".cam",
    ".webcam", ".tube", ".casino", ".poker"});

constexpr auto kTrackerHosts = std::to_array<std::string_view>({
    "doubleclick.net", "googlesyndication.com", "googleadservices.com",
    "googletagservices.com", "googletagmanager.com", "google-analytics.com",
    "adservice.google.com", "imasdk.googleapis.com", "2mdn.net",
    "amazon-adsystem.com", "ads-twitter.com", "advertising.com",
    "adnxs.com", "adsrvr.org", "adsafeprotected.com", "adzerk.net",
    "adform.net", "adcolony.com", "adroll.com", "adsterra.com",
    "applovin.com", "betweendigital.com", "bidswitch.net", "buysellads.com",
    "casalemedia.com", "contextweb.com", "criteo.com", "criteo.net",
    "districtm.io", "doubleverify.com", "exoclick.com", "gumgum.com",
    "indexww.com", "inmobi.com", "ironsrc.com", "lijit.com",
    "mathtag.com", "media.net", "mgid.com", "moatads.com", "openx.net",
    "outbrain.com", "popads.net", "popcash.net", "propellerads.com",
    "pubmatic.com", "quantserve.com", "revcontent.com", "rubiconproject.com",
    "scorecardresearch.com", "serving-sys.com", "sharethrough.com",
    "smartadserver.com", "smartclip.net", "sonobi.com", "sovrn.com",
    "spotxchange.com", "taboola.com", "teads.tv", "trafficjunky.com",
    "triplelift.com", "undertone.com", "yieldmanager.com", "yieldmo.com",
    "zedo.com", "33across.com", "juicyads.com", "adskeeper.co.uk",
    "adgebra.co.in", "vserv.mobi", "an.yandex.ru", "ads.yandex.ru",
    "ads.unity.com", "facebook.net", "connect.facebook.net",
    "hotjar.com", "mouseflow.com", "chartbeat.com", "mixpanel.com",
    "segment.io", "amplitude.com", "bugsnag.com", "sentry.io",
    "adsafeprotected.net", "adotmob.com", "adhigh.net", "admatic.com.tr",
    "admob.com", "adnium.com", "adplugg.com", "adsnative.com",
    "adspyglass.com", "adtelligent.com", "adthrive.com", "adtng.com",
    "advangelists.com", "adverticum.net", "aniview.com", "avantisvideo.com",
    "bidtellect.com", "brealtime.com", "clickadu.com", "clicksor.com",
    "cootlogix.com", "cpmstar.com", "dianomi.com", "engageya.com",
    "hilltopads.net", "infolinks.com", "intergi.com", "kargo.com",
    "lockerdome.com", "loopme.me", "luckyorange.com", "mediavine.com",
    "mgid.net", "mopub.com", "nativery.com", "onaudience.com",
    "onclickalgo.com", "onesignal.com", "plugrush.com", "pushnami.com",
    "rtbhouse.com", "servedbyadbutler.com", "sekindo.com", "smaato.net",
    "smilewanted.com", "snigelweb.com", "stpd.cloud", "sundaysky.com",
    "tremorhub.com", "trafficfactory.biz", "vidoomy.com", "voluum.com",
    "waust.at", "yandexadexchange.net", "yieldlab.net", "zemanta.com",
    "zeropark.com", "clarity.ms", "branch.io", "kochava.com",
    "appsflyer.com", "adjust.com", "braze.com", "clevertap.com",
    "fullstory.com", "heap.io", "logrocket.com", "newrelic.com",
    "optimizely.com", "quantcast.com", "statcounter.com", "yieldify.com"});

constexpr auto kAdPaths = std::to_array<std::string_view>({
    "/ads/", "/ad/", "/adserver/", "/advert/", "/advertising/",
    "/sponsor/", "/sponsored/", "/promoted/", "/prebid/", "/gpt/",
    "/vast/", "/vpaid/", "popunder", "ad_frame", "ad-frame", "pagead",
    "/ads.js", "/ad.js", "google_ads", "googleads", "banner_ad",
    "banner-ad", "adunit", "ad-unit", "adslot", "ad-slot"});

constexpr auto kTrackingPaths = std::to_array<std::string_view>({
    "/collect", "/analytics", "/telemetry", "/tracking", "/tracker",
    "/pixel", "/beacon", "/events/batch", "/metrics"});

// Ad and ad-telemetry endpoints inside the YouTube/Google video family.
// Playback, search and thumbnail endpoints are deliberately not listed.
constexpr auto kYouTubeAdPaths = std::to_array<std::string_view>({
    "/pagead/", "/ptracking", "/api/stats/ads", "/get_midroll_info",
    "/get_ad_break", "/ad_companion", "/adservice", "/pcs/activeview",
    "/youtubei/v1/player/ad_break"});

constexpr auto kAdHostLabels = std::to_array<std::string_view>({
    "ad", "ads", "adserver", "adservice", "advertising", "analytics",
    "metrics", "telemetry", "tracker", "tracking"});

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

std::string ExtractHost(std::string_view url) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) return {};
  const auto host_start = scheme_end + 3;
  auto host_end = url.find_first_of("/?#", host_start);
  if (host_end == std::string_view::npos) host_end = url.size();

  std::string host(url.substr(host_start, host_end - host_start));
  const auto user_info = host.rfind('@');
  if (user_info != std::string::npos) host.erase(0, user_info + 1);
  if (host.starts_with('[')) {
    const auto closing_bracket = host.find(']');
    if (closing_bracket == std::string::npos) return {};
    host = host.substr(1, closing_bracket - 1);
  } else {
    const auto port = host.find(':');
    if (port != std::string::npos) host.resize(port);
  }
  if (!host.empty() && host.back() == '.') host.pop_back();
  return Lower(host);
}

bool HostMatches(std::string_view host, std::string_view entry) {
  return host == entry ||
         (host.size() > entry.size() &&
          host.compare(host.size() - entry.size(), entry.size(), entry) == 0 &&
          host[host.size() - entry.size() - 1] == '.');
}

bool HasHostLabel(std::string_view host, std::string_view label) {
  size_t start = 0;
  while (start < host.size()) {
    const size_t end = host.find('.', start);
    const auto part = host.substr(
        start, end == std::string_view::npos ? host.size() - start : end - start);
    if (part == label) return true;
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return false;
}

// Matches a word against whole hostname labels, also splitting on "-" and "_"
// so "free-sex-cams.example" matches while "essex.gov.uk" does not.
bool HasHostToken(std::string_view host, std::string_view word) {
  size_t start = 0;
  while (start <= host.size()) {
    const size_t end = host.find_first_of(".-_", start);
    const auto part = host.substr(
        start, end == std::string_view::npos ? host.size() - start : end - start);
    if (part == word) return true;
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return false;
}

std::string SiteKey(std::string_view host) {
  const size_t last_dot = host.rfind('.');
  if (last_dot == std::string_view::npos) return std::string(host);
  const size_t previous_dot = host.rfind('.', last_dot - 1);
  if (previous_dot == std::string_view::npos) return std::string(host);
  return std::string(host.substr(previous_dot + 1));
}

bool IsThirdParty(std::string_view host, std::string_view initiator_url) {
  const std::string initiator_host = ExtractHost(initiator_url);
  if (host.empty() || initiator_host.empty()) return false;
  return SiteKey(host) != SiteKey(initiator_host);
}

bool ContainsAny(std::string_view value, const auto& entries) {
  for (const auto entry : entries) {
    if (value.find(entry) != std::string_view::npos) return true;
  }
  return false;
}

bool IsPrivateHost(std::string_view host) {
  if (host == "localhost" || host == "::1" || host.ends_with(".local")) {
    return true;
  }
  const bool private_ipv6 = host.find(':') != std::string_view::npos &&
                            (host.starts_with("fc") || host.starts_with("fd") ||
                             host.starts_with("fe80:"));
  return host.starts_with("127.") || host.starts_with("10.") ||
         host.starts_with("0.") || host.starts_with("169.254.") ||
         host.starts_with("192.168.") || private_ipv6 ||
         host.starts_with("172.16.") ||
         host.starts_with("172.17.") || host.starts_with("172.18.") ||
         host.starts_with("172.19.") || host.starts_with("172.20.") ||
         host.starts_with("172.21.") || host.starts_with("172.22.") ||
         host.starts_with("172.23.") || host.starts_with("172.24.") ||
         host.starts_with("172.25.") || host.starts_with("172.26.") ||
         host.starts_with("172.27.") || host.starts_with("172.28.") ||
         host.starts_with("172.29.") || host.starts_with("172.30.") ||
         host.starts_with("172.31.");
}

bool HasQueryKey(std::string_view url, std::string_view key) {
  const auto query = url.find('?');
  if (query == std::string_view::npos) return false;
  const auto value = url.substr(query + 1);
  const std::string prefixed_key = "&" + std::string(key);
  return value.starts_with(key) || value.find(prefixed_key) !=
                                       std::string_view::npos;
}

bool IsSubresource(ResourceType type) {
  return type != ResourceType::kMainFrame;
}

filters::RequestType ToFilterType(ResourceType type) {
  switch (type) {
    case ResourceType::kMainFrame: return filters::RequestType::kMainFrame;
    case ResourceType::kSubFrame: return filters::RequestType::kSubFrame;
    case ResourceType::kStylesheet: return filters::RequestType::kStylesheet;
    case ResourceType::kScript: return filters::RequestType::kScript;
    case ResourceType::kImage: return filters::RequestType::kImage;
    case ResourceType::kFont: return filters::RequestType::kFont;
    case ResourceType::kXhr: return filters::RequestType::kXhr;
    case ResourceType::kMedia: return filters::RequestType::kMedia;
    case ResourceType::kObject: return filters::RequestType::kObject;
    case ResourceType::kPing: return filters::RequestType::kPing;
    case ResourceType::kWorker: return filters::RequestType::kWorker;
    case ResourceType::kPrefetch: return filters::RequestType::kPrefetch;
    case ResourceType::kFavicon: return filters::RequestType::kFavicon;
    default: return filters::RequestType::kOther;
  }
}

// Decodes percent escapes and "+" separators so filtering sees real words.
std::string DecodeQuery(std::string_view value) {
  std::string decoded;
  decoded.reserve(value.size());
  const auto hex_value = [](char character) -> int {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
  };
  for (size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '+') {
      decoded.push_back(' ');
    } else if (value[index] == '%' && index + 2 < value.size()) {
      const int high = hex_value(value[index + 1]);
      const int low = hex_value(value[index + 2]);
      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
      } else {
        decoded.push_back(value[index]);
      }
    } else {
      decoded.push_back(value[index]);
    }
  }
  return decoded;
}

// True when any whole word of the decoded query/path matches a blocked word,
// or contains one of the unambiguous adult/illegal substrings.
bool HasBlockedSearchTerm(std::string_view raw_url) {
  const auto query_start = raw_url.find('?');
  const std::string text = DecodeQuery(
      query_start == std::string_view::npos ? std::string_view{}
                                           : raw_url.substr(query_start + 1));

  size_t start = 0;
  while (start < text.size()) {
    size_t end = start;
    while (end < text.size() &&
           (std::isalnum(static_cast<unsigned char>(text[end])) != 0)) {
      ++end;
    }
    if (end > start) {
      const std::string_view token(text.data() + start, end - start);
      for (const auto word : kAdultWords) {
        if (token == word) return true;
      }
      for (const auto word : kIllegalWords) {
        if (token == word) return true;
      }
      for (const auto fragment : kAdultHostSubstrings) {
        if (token.find(fragment) != std::string_view::npos) return true;
      }
      for (const auto fragment : kIllegalHostSubstrings) {
        if (token.find(fragment) != std::string_view::npos) return true;
      }
      start = end;
    } else {
      ++start;
    }
  }
  return false;
}

// Host-level adult/illegal classification, applied to every resource type so
// blocked material cannot load inside frames, images or scripts either.
const wchar_t* ClassifyBlockedHost(std::string_view host) {
  for (const auto suffix : kBlockedTldSuffixes) {
    if (host.size() > suffix.size() && host.ends_with(suffix)) {
      return L"Adult or restricted domain suffix";
    }
  }
  for (const auto blocked_host : kBlockedHosts) {
    if (HostMatches(host, blocked_host)) return L"Blocked site";
  }
  for (const auto fragment : kAdultHostSubstrings) {
    if (host.find(fragment) != std::string_view::npos) {
      return L"Adult content domain";
    }
  }
  for (const auto word : kAdultWords) {
    if (HasHostToken(host, word)) return L"Adult content domain";
  }
  for (const auto fragment : kIllegalHostSubstrings) {
    if (host.find(fragment) != std::string_view::npos) {
      return L"Piracy or illegal content domain";
    }
  }
  for (const auto word : kIllegalWords) {
    if (HasHostToken(host, word)) {
      return L"Piracy or illegal content domain";
    }
  }
  return nullptr;
}

}  // namespace

bool IsAdOrTracker(const std::string& raw_url, ResourceType type,
                   const std::string& request_initiator) {
  const std::string url = Lower(raw_url);
  const std::string host = ExtractHost(url);
  for (const auto tracker_host : kTrackerHosts) {
    if (HostMatches(host, tracker_host)) return true;
  }

  // MTK Shields filter list runs for every resource type, like a dedicated
  // adblock engine, and takes precedence over the heuristics below.
  const auto& engine = filters::Engine::Get();
  if (engine.loaded() &&
      engine.ShouldBlock(url, host, ExtractHost(Lower(request_initiator)),
                         ToFilterType(type))) {
    return true;
  }

  if (!IsSubresource(type)) return false;

  const auto scheme_end = url.find("://");
  const size_t path_start =
      scheme_end == std::string::npos
          ? std::string::npos
          : url.find('/', scheme_end + 3);
  const size_t path_end = path_start == std::string::npos
                              ? std::string::npos
                              : url.find_first_of("?#", path_start);
  const std::string_view path =
      path_start == std::string::npos
          ? std::string_view{}
          : std::string_view(url).substr(
                path_start, path_end == std::string::npos
                                ? std::string::npos
                                : path_end - path_start);

  // YouTube serves ads from its own functional domains, so only the known
  // ad/ad-telemetry endpoints are dropped there.
  const bool youtube_family = HostMatches(host, "youtube.com") ||
                              HostMatches(host, "youtube-nocookie.com") ||
                              HostMatches(host, "googlevideo.com") ||
                              HostMatches(host, "ytimg.com");
  if (youtube_family) return ContainsAny(path, kYouTubeAdPaths);

  const bool functional_host = HostMatches(host, "googleapis.com") ||
                               HostMatches(host, "gstatic.com");
  if (functional_host) return false;

  if (!IsThirdParty(host, request_initiator)) return false;

  for (const auto label : kAdHostLabels) {
    if (HasHostLabel(host, label)) return true;
  }

  const bool filterable_type =
      type == ResourceType::kSubFrame || type == ResourceType::kScript ||
      type == ResourceType::kImage || type == ResourceType::kXhr ||
      type == ResourceType::kPing || type == ResourceType::kOther;
  if (filterable_type && ContainsAny(path, kAdPaths)) return true;
  return filterable_type && ContainsAny(path, kTrackingPaths);
}

bool IsSocialPlatformNsfw(const std::string& raw_url) {
  const std::string url = Lower(raw_url);
  const std::string host = ExtractHost(url);

  if (host == "reddit.com" || host.ends_with(".reddit.com")) {
    if (url.find("/r/nsfw") != std::string::npos ||
        url.find("/r/gonewild") != std::string::npos ||
        url.find("/r/realgirls") != std::string::npos ||
        url.find("/r/hentai") != std::string::npos ||
        url.find("/r/ecchi") != std::string::npos ||
        url.find("/r/adult") != std::string::npos ||
        url.find("/r/sex") != std::string::npos ||
        url.find("/r/porn") != std::string::npos ||
        url.find("include_over_18=on") != std::string::npos ||
        url.find("nsfw=1") != std::string::npos) {
      return true;
    }
  }

  if (host == "twitter.com" || host.ends_with(".twitter.com") ||
      host == "x.com" || host.ends_with(".x.com")) {
    if (url.find("q=nsfw") != std::string::npos ||
        url.find("q=porn") != std::string::npos ||
        url.find("q=sex") != std::string::npos ||
        url.find("q=xxx") != std::string::npos ||
        url.find("q=nude") != std::string::npos) {
      return true;
    }
  }

  if (host == "instagram.com" || host.ends_with(".instagram.com")) {
    if (url.find("/explore/tags/porn") != std::string::npos ||
        url.find("/explore/tags/nsfw") != std::string::npos ||
        url.find("/explore/tags/xxx") != std::string::npos ||
        url.find("/explore/tags/sex") != std::string::npos ||
        url.find("/explore/tags/nude") != std::string::npos) {
      return true;
    }
  }

  if (host == "twitch.tv" || host.ends_with(".twitch.tv")) {
    if (url.find("pools-bikinis-and-beaches") != std::string::npos) {
      return true;
    }
  }

  if (host == "youtube.com" || host.ends_with(".youtube.com") ||
      host == "youtu.be") {
    if (url.find("ageverify") != std::string::npos ||
        url.find("has_verified=1") != std::string::npos) {
      return true;
    }
  }

  return false;
}

RequestDecision EvaluateRequest(const std::string& raw_url,
                                ResourceType resource_type,
                                const std::string& request_initiator) {
  const std::string url = Lower(raw_url);
  if (url == "about:blank") return {true, {}};
  if (!url.starts_with("https://") && !url.starts_with("http://")) {
    return {false, L"Unsupported protocol"};
  }

  const std::string host = ExtractHost(url);
  if (host.empty()) return {false, L"Invalid address"};
  if (IsPrivateHost(host)) {
    return {false, L"Private network access is disabled"};
  }
  if (const wchar_t* host_reason = ClassifyBlockedHost(host)) {
    return {false, host_reason};
  }
  if (resource_type == ResourceType::kMainFrame && HasBlockedSearchTerm(url)) {
    return {false, L"Adult or illegal search term blocked"};
  }
  if (IsSocialPlatformNsfw(url)) {
    return {false, L"Social platform adult/NSFW section blocked"};
  }
  if (IsAdOrTracker(url, resource_type, request_initiator)) {
    return {false, L"Ad or tracker blocked"};
  }
  return {true, {}};
}

NavigationDecision EvaluateNavigation(const std::string& raw_url) {
  const auto decision = EvaluateRequest(raw_url, ResourceType::kMainFrame);
  if (!decision.allow) return {false, {}, decision.reason};
  return {true, EnforceSafeSearch(raw_url), {}};
}

NavigationDecision ResolveInput(const std::string& raw_input) {
  std::string input = raw_input;
  input.erase(input.begin(), std::find_if(input.begin(), input.end(), [](unsigned char c) {
                return !std::isspace(c);
              }));
  input.erase(std::find_if(input.rbegin(), input.rend(), [](unsigned char c) {
                return !std::isspace(c);
              }).base(), input.end());
  if (input.empty()) return EvaluateNavigation("https://www.google.com/?safe=active");

  const bool has_scheme = input.find("://") != std::string::npos;
  const bool looks_like_host = input.find(' ') == std::string::npos &&
                               input.find('.') != std::string::npos;
  if (has_scheme || looks_like_host) {
    return EvaluateNavigation(has_scheme ? input : "https://" + input);
  }

  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string encoded;
  for (const unsigned char character : input) {
    if (std::isalnum(character) || character == '-' || character == '_' ||
        character == '.' || character == '~') {
      encoded.push_back(static_cast<char>(character));
    } else if (character == ' ') {
      encoded.push_back('+');
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex[character >> 4]);
      encoded.push_back(kHex[character & 0x0F]);
    }
  }
  return EvaluateNavigation("https://www.google.com/search?safe=active&q=" + encoded);
}

std::vector<std::pair<std::string, std::string>> PrivacyRequestHeaders(
    const std::string&) {
  return {{"DNT", "1"}, {"Sec-GPC", "1"}};
}

std::string HostOf(const std::string& raw_url) {
  return ExtractHost(Lower(raw_url));
}

std::string EnforceSafeSearch(std::string url) {
  const std::string lower_url = Lower(url);
  if (lower_url.starts_with("http://")) url.replace(0, 7, "https://");

  const std::string host = Lower(ExtractHost(url));
  const auto add_query = [&url](std::string_view key_value) {
    url += url.find('?') == std::string::npos ? "?" : "&";
    url += key_value;
  };

  if ((host == "google.com" || host.ends_with(".google.com")) &&
      !HasQueryKey(url, "safe=")) {
    add_query("safe=active");
  } else if ((host == "bing.com" || host.ends_with(".bing.com")) &&
             !HasQueryKey(url, "adlt=")) {
    add_query("adlt=strict");
  } else if (host == "duckduckgo.com" && !HasQueryKey(url, "kp=")) {
    add_query("kp=1");
  } else if ((host == "yahoo.com" || host.ends_with(".yahoo.com")) &&
             !HasQueryKey(url, "vm=")) {
    add_query("vm=r");
  } else if ((host == "yandex.com" || host.ends_with(".yandex.com")) &&
             !HasQueryKey(url, "family=")) {
    add_query("family=1");
  } else if (host == "ecosia.org" && !HasQueryKey(url, "_sf=")) {
    add_query("_sf=1");
  }
  return url;
}

}  // namespace mtk::policy

