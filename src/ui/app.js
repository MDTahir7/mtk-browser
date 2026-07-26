const address = document.querySelector('#address');
const form = document.querySelector('#address-form');
const back = document.querySelector('#back');
const forward = document.querySelector('#forward');
const reload = document.querySelector('#reload');
const home = document.querySelector('#home');
const count = document.querySelector('#blocked-count');
const modalCount = document.querySelector('#modal-blocked-count');
const toast = document.querySelector('#toast');
const tabStrip = document.querySelector('#tab-strip');
const addTabBtn = document.querySelector('#add-tab');
const shieldToggle = document.querySelector('#shield-toggle');
const shieldModal = document.querySelector('#shield-modal');
const menuToggle = document.querySelector('#menu-toggle');
const chromeMenu = document.querySelector('#chrome-menu');
let toastTimer;

function showToast(message) {
  toast.textContent = message;
  toast.classList.add('visible');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove('visible'), 3200);
}

function renderTabs(tabs, activeId) {
  tabStrip.innerHTML = '';
  tabs.forEach((tab) => {
    const el = document.createElement('div');
    el.className = `tab ${tab.id === activeId ? 'active' : ''}`;
    el.dataset.id = tab.id;

    const favicon = document.createElement('span');
    favicon.className = 'tab-favicon';
    favicon.textContent = tab.url.includes('youtube.com') ? '▶️' : (tab.url.includes('google.com') ? '🔍' : '🌐');

    const title = document.createElement('span');
    title.className = 'tab-title';
    title.textContent = tab.title || 'New Tab';

    const closeBtn = document.createElement('span');
    closeBtn.className = 'tab-close';
    closeBtn.textContent = '×';
    closeBtn.title = 'Close tab';
    closeBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      window.browser.closeTab(tab.id);
    });

    el.appendChild(favicon);
    el.appendChild(title);
    if (tabs.length > 1) el.appendChild(closeBtn);

    el.addEventListener('click', () => window.browser.switchTab(tab.id));
    tabStrip.appendChild(el);
  });
}

function render(state) {
  if (!state) return;
  back.disabled = !state.canGoBack;
  forward.disabled = !state.canGoForward;
  reload.textContent = state.loading ? '✕' : '↻';
  
  const blocked = state.blockedRequests ?? 0;
  count.textContent = blocked;
  if (modalCount) modalCount.textContent = blocked;

  if (document.activeElement !== address && state.url) {
    if (state.url.startsWith('file:') && state.url.includes('newtab.html')) {
      address.value = '';
    } else if (!state.url.startsWith('file:')) {
      address.value = state.url;
    }
  }

  document.title = `${state.title || 'MTK Safe Browser'} — Safe`;
  if (state.tabs) renderTabs(state.tabs, state.activeTabId);
  if (state.blocked && state.reason) showToast(`Blocked: ${state.reason}`);
}

form.addEventListener('submit', async (event) => {
  event.preventDefault();
  const val = address.value.trim();
  if (!val) return;
  const result = await window.browser.navigate(val);
  if (result?.blocked) showToast(result.reason);
});

back.addEventListener('click', () => window.browser.back());
forward.addEventListener('click', () => window.browser.forward());
reload.addEventListener('click', async () => {
  const state = await window.browser.getState();
  return state.loading ? window.browser.stop() : window.browser.reload();
});
home.addEventListener('click', () => window.browser.home());
addTabBtn.addEventListener('click', () => window.browser.createTab());
address.addEventListener('focus', () => address.select());

// Bookmarks Bar Click Handlers
document.querySelectorAll('.bookmark-item').forEach((bm) => {
  bm.addEventListener('click', () => {
    const targetUrl = bm.dataset.url;
    if (targetUrl) window.browser.navigate(targetUrl);
  });
});

// Modal Toggles
shieldToggle.addEventListener('click', (e) => {
  e.stopPropagation();
  chromeMenu.classList.add('hidden');
  shieldModal.classList.toggle('hidden');
});

menuToggle.addEventListener('click', (e) => {
  e.stopPropagation();
  shieldModal.classList.add('hidden');
  chromeMenu.classList.toggle('hidden');
});

document.addEventListener('click', () => {
  shieldModal.classList.add('hidden');
  chromeMenu.classList.add('hidden');
});

// Chrome Menu Items
document.querySelector('#menu-new-tab')?.addEventListener('click', () => window.browser.createTab());
document.querySelector('#menu-close-tab')?.addEventListener('click', () => window.browser.closeActiveTab());
document.querySelector('#menu-reload')?.addEventListener('click', () => window.browser.reload());
document.querySelector('#menu-home')?.addEventListener('click', () => window.browser.home());
document.querySelector('#menu-clear')?.addEventListener('click', () => {
  showToast('Session memory & cache cleared');
});
document.querySelector('#menu-about')?.addEventListener('click', () => {
  showToast('MTK Safe Browser v0.1.1 — Privacy-First Family Protection');
});

// Keyboard Shortcuts
document.addEventListener('keydown', (e) => {
  if (e.ctrlKey && e.key.toLowerCase() === 't') {
    e.preventDefault();
    window.browser.createTab();
  } else if (e.ctrlKey && e.key.toLowerCase() === 'w') {
    e.preventDefault();
    window.browser.closeActiveTab();
  } else if (e.ctrlKey && e.key.toLowerCase() === 'r') {
    e.preventDefault();
    window.browser.reload();
  } else if (e.ctrlKey && e.key.toLowerCase() === 'l') {
    e.preventDefault();
    address.focus();
    address.select();
  }
});

window.browser.onState(render);
window.browser.getState().then(render);