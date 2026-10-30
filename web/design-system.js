/* Read-only component examples. No application, protocol or hardware imports. */
import {UI_ICON_NAMES, createUIIcon, createUIIconCorners, initializeUIComponents} from './ui-system.js';

/** Local selection only. Shared classes, native focus and listbox semantics. */
export function initializeReferencePicker({root, choices, onChange = () => {}}) {
  const doc = root.ownerDocument, trigger = root.querySelector('button'), list = root.querySelector('[role="listbox"]');
  const label = trigger.querySelector('[data-picker-label]'), swatch = trigger.querySelector('.theme-swatch');
  const listeners = [], options = []; let value = choices[0].value, active = 0;
  const listen = (node, type, callback) => { node.addEventListener(type, callback);listeners.push(() => node.removeEventListener(type, callback)); };
  const focus = index => { active = Math.max(0, Math.min(choices.length - 1, index));options[active].focus(); };
  const close = (restoreFocus = true) => { list.hidden = true;trigger.setAttribute('aria-expanded', 'false');if (restoreFocus) trigger.focus(); };
  const setValue = next => {
    const choice = choices.find(item => item.value === next); if (!choice) return false;
    value = next;label.textContent = choice.label;swatch.dataset.accent = value;
    options.forEach((node, index) => { const selected = choices[index].value === value;node.setAttribute('aria-selected', String(selected));node.querySelector('.app-accent-check').hidden = !selected; });return true;
  };
  const open = () => { list.hidden = false;trigger.setAttribute('aria-expanded', 'true');focus(choices.findIndex(item => item.value === value)); };
  const choose = next => { if (setValue(next)) { close();onChange(value); } };
  for (const choice of choices) {
    const option = doc.createElement('button');option.type = 'button';option.className = 'app-accent-option hud-choice hud-choice--nav';option.tabIndex = -1;option.setAttribute('role', 'option');
    const color = doc.createElement('span');color.className = 'theme-swatch';color.dataset.accent = choice.value;color.setAttribute('aria-hidden', 'true');
    const text = doc.createElement('span');text.className = 'app-accent-label';text.textContent = choice.label;
    const checked = doc.createElement('span');checked.className = 'app-accent-check';checked.setAttribute('aria-hidden', 'true');checked.append(createUIIcon('check', {document: doc}));option.append(color, text, checked);list.append(option);options.push(option);
    listen(option, 'click', () => choose(choice.value));
  }
  listen(trigger, 'click', () => list.hidden ? open() : close());
  listen(root, 'keydown', event => {
    if (!['ArrowDown','ArrowUp','Home','End','Enter',' ','Escape','Tab'].includes(event.key)) return;
    if (event.key === 'Tab') { if (!list.hidden) close(false);return; }
    event.preventDefault();
    if (event.key === 'Escape') { close();return; }
    if (event.key === 'Enter' || event.key === ' ') { if (list.hidden) open();else choose(choices[active].value);return; }
    if (list.hidden) open();
    else focus(event.key === 'Home' ? 0 : event.key === 'End' ? choices.length - 1 : active + (event.key === 'ArrowDown' ? 1 : -1));
  });
  listen(doc, 'click', event => { if (!list.hidden && !root.contains(event.target)) close(false); });
  setValue(value);
  return {setValue, get value() { return value; }, destroy() { listeners.forEach(remove => remove()); }};
}

if (globalThis.document?.body?.classList.contains('design-reference')) initializeDesignReference();

function initializeDesignReference() {

for (const node of document.querySelectorAll('[data-icon]')) node.append(createUIIcon(node.dataset.icon));
for (const node of document.querySelectorAll('[data-demo-icon]')) node.append(createUIIcon(node.dataset.demoIcon), createUIIconCorners());
for (const name of UI_ICON_NAMES) {
  const sample = document.createElement('div');sample.className = 'reference-icon-specimen';
  const label = document.createElement('code');label.textContent = name;sample.append(createUIIcon(name), label);document.querySelector('#icon-catalog').append(sample);
}
const collapseExample = document.createElement('button');collapseExample.type = 'button';collapseExample.className = 'hud-icon-button';collapseExample.setAttribute('aria-label', 'Collapse sidebar');collapseExample.append(createUIIcon('arrowRight'), createUIIconCorners());document.querySelector('.reference-icons').append(collapseExample);
document.querySelector('#reference-appearance').addEventListener('change', event => {
  document.body.dataset.appearance = event.target.value;
  document.documentElement.style.colorScheme = event.target.value;
});
document.querySelector('#reference-accent').addEventListener('change', event => { document.body.dataset.theme = event.target.value; });
const accents = document.querySelector('#reference-accent');
const picker = initializeReferencePicker({root: document.querySelector('#reference-picker'), choices: [...accents.options].map(option => ({value: option.value, label: option.textContent})), onChange(value) {
  document.body.dataset.theme = value;accents.value = value;document.querySelector('#picker-result').textContent = `${accents.selectedOptions[0].textContent} selected`;
}});
accents.addEventListener('change', () => picker.setValue(accents.value));
document.querySelector('#example-mixed').indeterminate = true;
for (const toggle of document.querySelectorAll('input.hud-toggle')) toggle.parentElement.classList.add('hud-toggle-label');
for (const name of ['layout','task']) {
  const nodes = [...document.querySelectorAll(`[data-${name}]`)];
  for (const node of nodes) node.addEventListener('click', () => {
    for (const item of nodes) item.setAttribute('aria-pressed', String(item === node));
    document.querySelector(`#${name}-result`).textContent = `${node.dataset[name]} selected${name === 'task' ? ' · demonstration only' : ''}`;
  });
}
for (const [attribute, result] of [['data-nav-choice','nav-result'], ['data-sidebar-choice','sidebar-result']]) {
  const nodes = [...document.querySelectorAll(`[${attribute}]`)];
  for (const node of nodes) node.addEventListener('click', () => {
    for (const item of nodes) { if (item === node) item.setAttribute('aria-current', 'page');else item.removeAttribute('aria-current'); }
    document.getElementById(result).textContent = `${node.getAttribute(attribute)} selected · example only`;
  });
}
const probe = document.querySelector('[data-state-probe]');let hovered = false, pressed = false;
const state = () => { document.querySelector('#combined-state').textContent = [probe.getAttribute('aria-pressed') === 'true' && 'Selected', hovered && 'Hover', document.activeElement === probe && 'Focus', pressed && 'Pressed'].filter(Boolean).join(' + ') || 'Rest'; };
for (const event of ['pointerenter','pointerleave','pointerdown','pointerup','pointercancel','focus','blur','keydown','keyup']) probe.addEventListener(event, input => {
  if (input.type === 'pointerenter') hovered = true;
  if (input.type === 'pointerleave') { hovered = false;pressed = false; }
  if (input.type === 'pointerdown' || input.type === 'keydown' && [' ','Enter'].includes(input.key)) pressed = true;
  if (['pointerup','pointercancel','keyup','blur'].includes(input.type)) pressed = false;state();
});
probe.addEventListener('click', () => { probe.setAttribute('aria-pressed', String(probe.getAttribute('aria-pressed') !== 'true'));state(); });
const notice = document.querySelector('[data-tone="success"]');
document.querySelector('[data-dismiss]').addEventListener('click', () => { notice.hidden = true; document.querySelector('#restore-notice').focus(); });
document.querySelector('#restore-notice').addEventListener('click', () => { notice.hidden = false; });
const restoreBanners = document.querySelector('#restore-banners');
for (const dismiss of document.querySelectorAll('[data-dismiss-banner]')) dismiss.addEventListener('click', () => { dismiss.closest('[data-banner-example]').hidden = true;restoreBanners.focus(); });
restoreBanners.addEventListener('click', () => { for (const banner of document.querySelectorAll('[data-banner-example]')) banner.hidden = false; });
const card = document.querySelector('#motion-card'), reduced = matchMedia('(prefers-reduced-motion: reduce)');
const preference = () => { document.querySelector('#motion-preference').textContent = reduced.matches ? 'Reduced motion is enabled. Entrances and hover fades are immediate.' : 'Your system allows motion. Reduced motion removes the entrance and hover fades.'; };
preference(); reduced.addEventListener('change', preference);
document.querySelector('#replay-motion').addEventListener('click', () => { if (!reduced.matches && !card.classList.contains('hud-enter')) card.classList.add('hud-enter'); });
card.addEventListener('animationend', event => { if (event.animationName === 'hud-panel-enter') card.classList.remove('hud-enter'); });
const contract = JSON.parse(document.querySelector('#component-contract').textContent);
const make = (tag, text) => { const node = document.createElement(tag);if (text !== undefined) node.textContent = text;return node; };
for (const family of contract.families) {
  const row = make('tr');row.dataset.componentFamily = family.id;
  const name = make('th', family.title);name.scope = 'row';
  const selectors = make('td'), production = make('td'), example = make('td');
  for (const selector of family.selectors) selectors.append(make('code', selector), make('br'));
  for (const file of family.production) production.append(make('code', file), make('br'));
  const link = make('a', 'View example');link.href = `#${family.example}`;example.append(link);row.append(name, selectors, production, example);document.querySelector('#inventory-rows').append(row);
}
for (const exception of contract.specialized) {
  const section = make('section');section.append(make('h3', exception.title), make('p', exception.reason), make('code', exception.selectors.join(' · ')));document.querySelector('#specialized-inventory').append(section);
}
const components = initializeUIComponents({root: document.body});
window.addEventListener('pagehide', event => { if (!event.persisted) { picker.destroy();components.destroy();reduced.removeEventListener('change', preference); } });
}
