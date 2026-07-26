const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('browser', {
  navigate: (input) => ipcRenderer.invoke('browser:navigate', input),
  back: () => ipcRenderer.invoke('browser:back'),
  forward: () => ipcRenderer.invoke('browser:forward'),
  reload: () => ipcRenderer.invoke('browser:reload'),
  stop: () => ipcRenderer.invoke('browser:stop'),
  home: () => ipcRenderer.invoke('browser:home'),
  createTab: (url) => ipcRenderer.invoke('browser:create-tab', url),
  switchTab: (id) => ipcRenderer.invoke('browser:switch-tab', id),
  closeTab: (id) => ipcRenderer.invoke('browser:close-tab', id),
  closeActiveTab: () => ipcRenderer.invoke('browser:close-active-tab'),
  getState: () => ipcRenderer.invoke('browser:get-state'),
  onState: (callback) => {
    const listener = (_event, state) => callback(state);
    ipcRenderer.on('browser:state', listener);
    return () => ipcRenderer.removeListener('browser:state', listener);
  }
});