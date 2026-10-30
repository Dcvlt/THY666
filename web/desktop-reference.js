/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native window presentation for the read-only component reference. Nothing
 * here requests a Client, updater, device list or USB connection.
 */
import {connectDesktopRuntime} from './desktop-runtime.js';
import {initializeDesktopChrome} from './desktop-chrome.js';

// Only native staging adds this module to the reference document. The shared
// browser reference remains outside the desktop runtime's module graph.
if (globalThis.window?.__TAURI__?.core?.invoke) {
  void initializeDesktopReference().catch(error => console.error('The component reference window could not initialize.', error));
}

export async function initializeDesktopReference({scope=globalThis.window,document:doc=scope?.document,
  connect=connectDesktopRuntime,mountChrome=initializeDesktopChrome}={}) {
  if (!scope?.__TAURI__?.core?.invoke || !doc?.body?.classList.contains('design-reference')) return null;
  const bridge = await connect({scope});
  if (bridge?.runtime?.kind !== 'desktop') throw new Error('The native component reference needs the desktop window.');
  if (doc.querySelector('.desktop-window-chrome')) return null;

  const stylesheets = ['desktop-chrome.css','desktop-reference.css'].map(name => {
    const link = doc.createElement('link');link.rel = 'stylesheet';link.href = `./${name}`;doc.head.append(link);return link;
  });
  const shell = doc.createElement('div');shell.className = 'app-shell';
  const content = [...doc.body.children].filter(node => node.tagName !== 'SCRIPT');
  doc.body.append(shell);shell.append(...content);
  try {
    return mountChrome({runtime:bridge.runtime,document:doc});
  } catch (error) {
    shell.before(...content);shell.remove();for (const link of stylesheets) link.remove();throw error;
  }
}
