const path = require('node:path');
const { app, BrowserWindow, WebContentsView, ipcMain, session } = require('electron');
const { classifyInput, inspectRequest, enforceSafeSearch } = require('./security');

const HOME_URL = 'https://www.google.com/?safe=active';
const TOOLBAR_HEIGHT = 76;
const PARTITION = 'safe-session';
let window;
let blockedRequests = 0;
let lastReason = '';

const tabs = new Map();
let activeTabId = null;
let nextTabId = 1;

app.commandLine.appendSwitch('disable-features', [
  'AutofillServerCommunication', 'MediaRouter', 'OptimizationHints',
  'InterestFeedContentSuggestions', 'WebBluetooth'
].join(','));
app.commandLine.appendSwitch('force-dark-mode');

function getTabsArray() {
  return Array.from(tabs.entries()).map(([id, view]) => ({
    id,
    url: view.webContents.getURL(),
    title: view.webContents.getTitle() || 'New Tab'
  }));
}

function sendState(extra = {}) {
  if (!window || window.isDestroyed()) return;
  const activeView = tabs.get(activeTabId);
  const activeWc = activeView ? activeView.webContents : null;
  
  window.webContents.send('browser:state', {
    url: activeWc ? activeWc.getURL() : '',
    title: activeWc ? (activeWc.getTitle() || 'MTK Safe Browser') : 'MTK Safe Browser',
    loading: activeWc ? activeWc.isLoading() : false,
    canGoBack: activeWc ? activeWc.navigationHistory.canGoBack() : false,
    canGoForward: activeWc ? activeWc.navigationHistory.canGoForward() : false,
    blockedRequests,
    lastReason,
    tabs: getTabsArray(),
    activeTabId,
    ...extra
  });
}

function resizeContent() {
  if (!window) return;
  const [width, height] = window.getContentSize();
  const viewHeight = Math.max(0, height - TOOLBAR_HEIGHT);
  
  for (const [id, view] of tabs.entries()) {
    if (id === activeTabId) {
      view.setBounds({ x: 0, y: TOOLBAR_HEIGHT, width, height: viewHeight });
    } else {
      view.setBounds({ x: 0, y: 0, width: 0, height: 0 });
    }
  }
}

function showBlocked(reason, attemptedUrl = '', view = null) {
  const targetView = view || tabs.get(activeTabId);
  if (!targetView) return;
  lastReason = reason;
  const target = path.join(__dirname, 'ui', 'blocked.html');
  targetView.webContents.loadFile(target, { query: { reason, url: attemptedUrl.slice(0, 500) } });
  if (targetView === tabs.get(activeTabId)) {
    sendState({ blocked: true, attemptedUrl, reason });
  }
}

function navigate(input) {
  const result = classifyInput(input);
  const activeView = tabs.get(activeTabId);
  if (!activeView) return result;
  
  if (result.blocked) {
    showBlocked(result.reason, String(input), activeView);
    return result;
  }
  lastReason = '';
  activeView.webContents.loadURL(result.url);
  return result;
}

function setupSession(safeSession) {
  safeSession.setPermissionCheckHandler(() => false);
  safeSession.setPermissionRequestHandler((_wc, _permission, callback) => callback(false));
  safeSession.webRequest.onBeforeRequest({ urls: ['*://*/*'] }, (details, callback) => {
    const decision = inspectRequest(details.url, details.resourceType);
    if (!decision.block) return callback({ cancel: false });
    blockedRequests += 1;
    lastReason = decision.reason;
    callback({ cancel: true });
    if (details.resourceType === 'mainFrame') {
      setImmediate(() => {
        // Try to find which tab this request belongs to.
        // It's hard to map webRequest to WebContents without webContentsId in onBeforeRequest (available in newer electron)
        // Fallback to active tab for now
        showBlocked(decision.reason, details.url);
      });
    } else if (blockedRequests % 10 === 0) {
      setImmediate(() => sendState());
    }
  });
  safeSession.webRequest.onBeforeSendHeaders({ urls: ['*://*/*'] }, (details, callback) => {
    const headers = { ...details.requestHeaders, DNT: '1', 'Sec-GPC': '1' };
    if (/\.youtube\.com$|^https:\/\/youtube\.com/i.test(details.url)) {
      headers['YouTube-Restrict'] = 'Strict';
    }
    callback({ requestHeaders: headers });
  });
  safeSession.on('will-download', (event) => {
    event.preventDefault();
    blockedRequests += 1;
    lastReason = 'Downloads बंद हैं';
    sendState({ blocked: true, reason: lastReason });
  });
}

function securePageContents(wc, view) {
  wc.setWindowOpenHandler(({ url }) => {
    createTab(url);
    return { action: 'deny' };
  });
  wc.on('will-navigate', (event, url) => {
    const decision = inspectRequest(url, 'mainFrame');
    if (decision.block) {
      event.preventDefault();
      return showBlocked(decision.reason, url, view);
    }
    const safeUrl = enforceSafeSearch(url);
    if (safeUrl !== url) {
      event.preventDefault();
      wc.loadURL(safeUrl);
    }
  });
}

function createTab(url = HOME_URL) {
  const id = nextTabId++;
  const view = new WebContentsView({
    webPreferences: {
      partition: PARTITION,
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      devTools: false,
      spellcheck: false
    }
  });
  
  const wc = view.webContents;
  securePageContents(wc, view);
  
  wc.on('did-start-loading', () => { if (activeTabId === id) sendState(); });
  wc.on('did-stop-loading', () => sendState()); 
  wc.on('did-navigate', () => sendState());
  wc.on('did-navigate-in-page', () => sendState());
  wc.on('page-title-updated', () => sendState());
  wc.on('before-input-event', (event, input) => {
    if (input.key === 'F12' || (input.control && input.shift && input.key.toLowerCase() === 'i')) event.preventDefault();
  });
  wc.on('render-process-gone', () => {
    if (activeTabId === id) showBlocked('Page process सुरक्षित रूप से बंद हुआ', '', view);
  });
  
  tabs.set(id, view);
  window.contentView.addChildView(view);
  
  switchTab(id);
  if (url) wc.loadURL(url);
  return id;
}

function switchTab(id) {
  if (!tabs.has(id)) return;
  activeTabId = id;
  resizeContent();
  sendState();
}

function closeTab(id) {
  if (!tabs.has(id)) return;
  const view = tabs.get(id);
  window.contentView.removeChildView(view);
  view.webContents.close();
  tabs.delete(id);
  
  if (tabs.size === 0) {
    app.quit();
    return;
  }
  
  if (activeTabId === id) {
    const remainingIds = Array.from(tabs.keys());
    switchTab(remainingIds[remainingIds.length - 1]);
  } else {
    sendState();
  }
}

function closeActiveTab() {
  if (activeTabId) closeTab(activeTabId);
}

function createWindow() {
  const safeSession = session.fromPartition(PARTITION, { cache: false });
  setupSession(safeSession);
  window = new BrowserWindow({
    width: 1280,
    height: 820,
    minWidth: 760,
    minHeight: 500,
    backgroundColor: '#10131a',
    title: 'MTK Safe Browser',
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      devTools: false
    }
  });
  
  window.loadFile(path.join(__dirname, 'ui', 'index.html'));
  window.on('resize', resizeContent);
  window.on('maximize', resizeContent);
  window.on('unmaximize', resizeContent);
  window.on('closed', () => { window = null; });
  
  createTab(HOME_URL);
}

function stateSnapshot() {
  const activeView = tabs.get(activeTabId);
  const wc = activeView ? activeView.webContents : null;
  return {
    url: wc ? wc.getURL() : '', 
    title: wc ? wc.getTitle() : '', 
    loading: wc ? wc.isLoading() : false, 
    blockedRequests, 
    lastReason,
    canGoBack: wc ? wc.navigationHistory.canGoBack() : false,
    canGoForward: wc ? wc.navigationHistory.canGoForward() : false,
    tabs: getTabsArray(),
    activeTabId
  };
}

ipcMain.handle('browser:navigate', (_event, input) => navigate(input));
ipcMain.handle('browser:back', () => {
  const v = tabs.get(activeTabId);
  if (v && v.webContents.navigationHistory.canGoBack()) v.webContents.navigationHistory.goBack();
});
ipcMain.handle('browser:forward', () => {
  const v = tabs.get(activeTabId);
  if (v && v.webContents.navigationHistory.canGoForward()) v.webContents.navigationHistory.goForward();
});
ipcMain.handle('browser:reload', () => {
  const v = tabs.get(activeTabId);
  if (v) v.webContents.reload();
});
ipcMain.handle('browser:stop', () => {
  const v = tabs.get(activeTabId);
  if (v) v.webContents.stop();
});
ipcMain.handle('browser:home', () => navigate(HOME_URL));
ipcMain.handle('browser:get-state', () => stateSnapshot());

ipcMain.handle('browser:create-tab', (_event, url) => createTab(url || 'file://' + path.join(__dirname, 'ui', 'newtab.html')));
ipcMain.handle('browser:switch-tab', (_event, id) => switchTab(id));
ipcMain.handle('browser:close-tab', (_event, id) => closeTab(id));
ipcMain.handle('browser:close-active-tab', () => closeActiveTab());

app.on('certificate-error', (event, _wc, _url, _error, _cert, callback) => {
  event.preventDefault();
  callback(false);
});

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.whenReady().then(createWindow);
  app.on('second-instance', () => {
    if (!window) return;
    if (window.isMinimized()) window.restore();
    window.focus();
  });
  app.on('window-all-closed', () => app.quit());
}