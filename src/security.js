const BLOCKED_TERMS = [
  'porn', 'porno', 'pornography', 'xxx', 'adult-content', 'adultsite',
  'sex-video', 'sexvideo', 'nude', 'nudity', 'hentai', 'rule34',
  'escort', 'camgirl', 'cam-girl', 'onlyfans', 'redtube', 'xvideos',
  'xnxx', 'brazzers', 'youporn', 'pornhub', 'chaturbate', 'darkweb',
  'dark-web', 'tor2web', 'onion'
];

const BLOCKED_HOSTS = [
  'pornhub.com', 'xvideos.com', 'xnxx.com', 'redtube.com', 'youporn.com',
  'onlyfans.com', 'chaturbate.com', 'stripchat.com', 'livejasmin.com',
  'brazzers.com', 'rule34.xxx', 'thepiratebay.org', '1337x.to',
  'rarbg.to', 'dark.fail'
];

const AD_TRACKER_HOSTS = [
  'doubleclick.net', 'googlesyndication.com', 'googleadservices.com',
  'adservice.google.com', 'amazon-adsystem.com', 'ads-twitter.com',
  'advertising.com', 'adnxs.com', 'criteo.com', 'criteo.net',
  'taboola.com', 'outbrain.com', 'scorecardresearch.com', 'quantserve.com',
  'hotjar.com', 'mouseflow.com', 'fullstory.com', 'facebook.net',
  'connect.facebook.net', 'analytics.twitter.com'
];

function hostMatches(hostname, entries) {
  const host = hostname.toLowerCase().replace(/^www\./, '');
  return entries.some((entry) => host === entry || host.endsWith(`.${entry}`));
}

function containsBlockedTerm(value) {
  let text = String(value).toLowerCase();
  try { text = decodeURIComponent(text); } catch { /* malformed URL */ }
  return BLOCKED_TERMS.some((term) => text.includes(term));
}

function isPrivateHost(hostname) {
  const host = hostname.toLowerCase();
  if (host === 'localhost' || host === '::1' || host.endsWith('.local')) return true;
  if (/^127\./.test(host) || /^10\./.test(host) || /^192\.168\./.test(host)) return true;
  const match = host.match(/^172\.(\d+)\./);
  return Boolean(match && Number(match[1]) >= 16 && Number(match[1]) <= 31);
}

function enforceSafeSearch(rawUrl) {
  const url = new URL(rawUrl);
  if (url.protocol === 'http:') url.protocol = 'https:';
  const host = url.hostname.replace(/^www\./, '').toLowerCase();
  if (host === 'google.com' || host.endsWith('.google.com')) url.searchParams.set('safe', 'active');
  if (host === 'bing.com' || host.endsWith('.bing.com')) url.searchParams.set('adlt', 'strict');
  if (host === 'duckduckgo.com') url.searchParams.set('kp', '1');
  if (host === 'yahoo.com' || host.endsWith('.yahoo.com')) url.searchParams.set('vm', 'r');
  if (host === 'yandex.com' || host.endsWith('.yandex.com')) url.searchParams.set('family', '1');
  if (host === 'ecosia.org') url.searchParams.set('_sf', '1');
  return url.toString();
}

function isSocialPlatformNsfw(urlObj) {
  const host = urlObj.hostname.replace(/^www\./, '').toLowerCase();
  const full = `${urlObj.pathname}${urlObj.search}`.toLowerCase();

  if (host === 'reddit.com' || host.endsWith('.reddit.com')) {
    if (full.includes('/r/nsfw') || full.includes('/r/gonewild') || full.includes('/r/hentai') ||
        full.includes('/r/ecchi') || full.includes('/r/adult') || full.includes('/r/sex') ||
        full.includes('/r/porn') || full.includes('include_over_18=on') || full.includes('nsfw=1')) {
      return true;
    }
  }
  if (host === 'twitter.com' || host.endsWith('.twitter.com') || host === 'x.com' || host.endsWith('.x.com')) {
    if (full.includes('q=nsfw') || full.includes('q=porn') || full.includes('q=sex') || full.includes('q=nude')) {
      return true;
    }
  }
  if (host === 'instagram.com' || host.endsWith('.instagram.com')) {
    if (full.includes('/explore/tags/porn') || full.includes('/explore/tags/nsfw') || full.includes('/explore/tags/xxx') || full.includes('/explore/tags/sex')) {
      return true;
    }
  }
  if (host === 'twitch.tv' || host.endsWith('.twitch.tv')) {
    if (full.includes('pools-bikinis-and-beaches')) return true;
  }
  if (host === 'youtube.com' || host.endsWith('.youtube.com') || host === 'youtu.be') {
    if (full.includes('ageverify') || full.includes('has_verified=1')) return true;
  }
  return false;
}

function inspectRequest(rawUrl, resourceType = 'mainFrame') {
  let url;
  try { url = new URL(rawUrl); } catch { return { block: true, reason: 'Invalid address' }; }
  if (!['https:', 'http:'].includes(url.protocol)) return { block: true, reason: 'Unsupported protocol' };
  if (isPrivateHost(url.hostname)) return { block: true, reason: 'Private network access बंद है' };
  if (hostMatches(url.hostname, BLOCKED_HOSTS)) return { block: true, reason: 'Unsafe domain' };
  if (containsBlockedTerm(`${url.hostname}${url.pathname}${url.search}`)) {
    return { block: true, reason: 'Adult या unsafe content pattern' };
  }
  if (isSocialPlatformNsfw(url)) {
    return { block: true, reason: 'Social media adult/NSFW section blocked' };
  }
  if (resourceType !== 'mainFrame' && (hostMatches(url.hostname, AD_TRACKER_HOSTS) || /\/(ads|adserver|popunder|pagead)\//i.test(url.href))) {
    return { block: true, reason: 'Ad या tracker' };
  }
  return { block: false };
}


function classifyInput(rawInput) {
  const input = String(rawInput || '').trim();
  if (!input) return { blocked: false, url: 'https://www.google.com/?safe=active' };
  const looksLikeUrl = /^https?:\/\//i.test(input) || /^[\w-]+(?:\.[\w-]+)+(?:[/:?#]|$)/i.test(input);
  if (!looksLikeUrl) {
    if (containsBlockedTerm(input)) return { blocked: true, reason: 'Unsafe search रोकी गई' };
    return { blocked: false, url: `https://www.google.com/search?safe=active&q=${encodeURIComponent(input)}` };
  }
  let candidate = /^https?:\/\//i.test(input) ? input : `https://${input}`;
  try { candidate = enforceSafeSearch(candidate); } catch { return { blocked: true, reason: 'Invalid address' }; }
  const decision = inspectRequest(candidate, 'mainFrame');
  return decision.block ? { blocked: true, reason: decision.reason } : { blocked: false, url: candidate };
}

module.exports = { classifyInput, inspectRequest, enforceSafeSearch };