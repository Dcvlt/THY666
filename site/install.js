import { createDeviceClient } from './installer/device-client.js';
import { createBrowserUpdaterClient } from './installer/browser-updater-client.js';
import { initializeUpdaterPanel } from './installer/updater-panel.js';

const mount = document.querySelector('#browser-updater');
const status = document.querySelector('#browser-install-status');
let panel;
const client = createDeviceClient({ onState() { panel?.refresh(); } });
const showNotice = (message, kind = 'info') => {
  status.textContent = message; status.dataset.state = kind; status.hidden = kind !== 'error';
};
panel = initializeUpdaterPanel({
  mount, client,
  runtime: { kind: 'browser-direct', companionLabel: 'Go to Client' },
  createHelper: () => createBrowserUpdaterClient({
    wasmURL: new URL('./installer/browser-updater.wasm', import.meta.url),
    coreManifestURL: new URL('./installer/browser-updater.json', import.meta.url),
    releasesURL: new URL('./releases.json', import.meta.url),
  }),
  notify: showNotice,
  async connectApplication() {
    if (panel.usbBusy) throw new Error('The installer owns USB. Wait for its result before reconnecting.');
    await client.connect();
  },
  onCompanion() {
    void client.disconnect().then(() => { location.href = 'client/index.html'; });
  },
});
document.addEventListener('visibilitychange', () => panel.setHostActive(!document.hidden));
document.addEventListener('click', event => {
  if (!event.target.closest?.('[data-help]')) return;
  event.preventDefault(); window.open('manual.html#updates', '_blank', 'noopener');
});
window.addEventListener('beforeunload', event => {
  if (!panel.usbBusy && !client.busy) return;
  event.preventDefault(); event.returnValue = '';
});
