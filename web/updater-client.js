/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit local updater actions. The Rust helper owns parsing and USB writes;
 * the existing application client alone owns current-state backup transport.
 */

export const UPDATE_PACKAGE_MAX_BYTES = 78 + 0x80000;
export const UPDATE_HELPER_PROTOCOL = 'THY666-UPDATER/1';

// The native application's explicit launch hands off an ephemeral token in the
// fragment. Remove it from the address/history before making any request.
export function consumeUpdaterHandoff({location=globalThis.location,history=globalThis.history,fragment=globalThis.window?.__thyUpdateLaunchFragment||location?.hash}={}) {
  if(globalThis.window)delete globalThis.window.__thyUpdateLaunchFragment;
  if(!fragment?.startsWith('#update?'))return null;
  const query=fragment.slice(8);
  try{
    if(typeof history?.replaceState!=='function'){location.hash='update';return null;}
    history.replaceState(history.state||null,'',`${location.pathname||'/'}${location.search||''}#update`);
  }catch{return null;}
  const params=new URLSearchParams(query);
  if(params.getAll('helper').length!==1||params.getAll('session').length!==1||[...params.keys()].some(key=>!['helper','session'].includes(key)))return null;
  const token=params.get('session');let url;try{url=new URL(params.get('helper'));}catch{return null;}
  if(url.protocol!=='http:'||url.hostname!=='127.0.0.1'||!url.port||url.username||url.password||url.pathname!=='/'||url.search||url.hash||!/^[a-f0-9]{64}$/.test(token||''))return null;
  return{endpoint:url.origin,token};
}

export function createUpdaterClient({ fetch: fetcher = globalThis.fetch, timeoutMs = 10000 } = {}) {
  let endpoint = '', token = '', connected = false, busy = false, epoch = 0;
  async function post(path, body, headers = {}) {
    if (!endpoint || !token) throw new Error('Connect the local updater helper first.');
    if (busy) throw new Error('The updater helper already has a pending request.');
    if (typeof fetcher !== 'function') throw new Error('This browser cannot contact the local updater helper.');
    busy = true;
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), timeoutMs);
    try {
      const response = await fetcher(`${endpoint}/v1/${path}`, { method: 'POST',
        headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json', ...headers },
        body: body instanceof Uint8Array ? body : JSON.stringify(body), signal: controller.signal,
        cache: 'no-store', credentials: 'omit', redirect: 'error', referrerPolicy: 'no-referrer', mode: 'cors' });
      const raw = await response.text();
      if (raw.length > 65536) throw new Error('Updater helper response exceeded its size limit.');
      let value; try { value = JSON.parse(raw); } catch { throw new Error('Updater helper returned an invalid response.'); }
      if (value?.protocol !== UPDATE_HELPER_PROTOCOL) throw new Error('The local updater helper uses an unsupported protocol.');
      if (!response.ok) {
        const error = new Error(typeof value.error === 'string' ? value.error : `Updater helper rejected the request (${response.status}).`);
        error.helperRejected = response.status >= 400 && response.status < 500 && typeof value.error === 'string';
        throw error;
      }
      return value;
    } catch (error) {
      if (error?.name === 'AbortError') throw new Error('Updater helper request timed out. No retry was sent.');
      throw error;
    } finally { clearTimeout(timer); busy = false; }
  }
  return {
    get available() { return typeof fetcher === 'function'; }, get connected() { return connected; }, get busy() { return busy; },
    async connect(options = {}) {
      if (connected || busy) throw new Error('The updater helper is already connected or opening.');
      let url; try { url = new URL(String(options.endpoint)); } catch { throw new Error('Enter the helper’s loopback HTTP address.'); }
      if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' || url.username || url.password ||
        (url.pathname !== '/' && url.pathname !== '') || url.search || url.hash)
        throw new Error('The updater address must be an HTTP service on 127.0.0.1 with no path, credentials or query.');
      const secret = String(options.token || '').trim();
      if (!/^[A-Za-z0-9_-]{32,256}$/.test(secret)) throw new Error('Enter the session token printed by the Rust helper.');
      const ticket = ++epoch; endpoint = url.origin; token = secret;
      try {
        const result = await post('status', {});
        if (ticket !== epoch) throw new Error('Updater helper connection was closed.');
        connected = true; return result;
      } catch (error) { if (ticket === epoch) { endpoint = token = ''; connected = false; } throw error; }
    },
    async disconnect() { ++epoch; connected = false; endpoint = token = ''; },
    async status() { if (!connected) throw new Error('Connect the local updater helper first.'); return post('status', {}); },
    async inspectPackage(bytes, { name = '', expected_build = '' } = {}) {
      if (!connected) throw new Error('Connect the local updater helper first.');
      if (!(bytes instanceof Uint8Array) || bytes.length < 142 || bytes.length > UPDATE_PACKAGE_MAX_BYTES) throw new Error('Invalid package size.');
      const headers = { 'Content-Type': 'application/octet-stream', 'X-THY666-Filename': encodeURIComponent(String(name).split(/[\\/]/).at(-1)) };
      if (expected_build) {
        if (!/^[0-9a-f]{16}$/.test(expected_build)) throw new Error('Expected build must contain exactly 16 lowercase hexadecimal characters.');
        headers['X-THY666-Build'] = expected_build;
      }
      return (await post('inspect', bytes, headers)).package;
    },
    async listDevices() { if (!connected) throw new Error('Connect the local updater helper first.'); return (await post('devices', {})).devices; },
    async releases() { if (!connected) throw new Error('Connect the local updater helper first.'); return post('releases', {}); },
    async selectRelease(id) { if (!connected) throw new Error('Connect the local updater helper first.'); return (await post('release-select', { id })).package; },
    async install(request) { if (!connected) throw new Error('Connect the local updater helper first.'); return (await post('install', request)).operation; },
    async progress(operation_id, after_sequence) {
      if (!connected) throw new Error('Connect the local updater helper first.');
      return (await post('progress', { operation_id, after_sequence })).operation;
    },
  };
}

