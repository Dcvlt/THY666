import {
  HELP_GROUPS, HELP_TOPICS, getHelpTopic, getHelpContext,
  getHelpReference, resolveHelpTopic, searchHelpTopics,
} from './help-topics.js';

export { HELP_GROUPS, HELP_TOPICS, resolveHelpTopic, searchHelpTopics };

let instanceSequence = 0;

/** Read-only help UI. No connection, engine or device adapter is accepted. */
export function initializeHelp({ mount, onNavigate = () => {}, onContextChange = () => {} } = {}) {
  if (!mount?.ownerDocument) throw new TypeError('Help requires a DOM mount.');
  const doc = mount.ownerDocument, prefix = `thy-help-${++instanceSequence}`;
  const listeners = [];
  let disposed = false, selected = 'getting-started', query = '', overlay = null, restoreFocus = null;
  let contextButtons = [];
  const el = (tag, className = '', text) => {
    const node = doc.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = String(text);
    return node;
  };
  const listen = (node, event, handler, options) => {
    node.addEventListener(event, handler, options);
    listeners.push(() => node.removeEventListener(event, handler, options));
  };
  const button = (label, className = '') => {
    const node = el('button', className, label);
    node.type = 'button';
    return node;
  };
  const root = el('section', 'thy-help');
  root.dataset.helpRoot = '';
  const rail = el('nav', 'thy-help-rail');
  rail.setAttribute('aria-label', 'Documentation topics');
  rail.append(el('p', 'thy-help-eyebrow', 'THY666 GUIDE'), el('h2', 'thy-help-title', 'Documentation'));
  const searchLabel = el('label', 'thy-help-search-label', 'Find a feature or control');
  searchLabel.htmlFor = `${prefix}-search`;
  const searchInput = el('input', 'thy-help-search');
  searchInput.id = `${prefix}-search`;
  searchInput.type = 'search';
  searchInput.placeholder = 'Try “track channel” or “aftertouch”';
  searchInput.autocomplete = 'off';
  const resultStatus = el('p', 'thy-help-result-count');
  resultStatus.setAttribute('role', 'status');
  resultStatus.setAttribute('aria-live', 'polite');
  const links = el('div', 'thy-help-topic-list');
  rail.append(searchLabel, searchInput, resultStatus, links);
  const article = el('article', 'thy-help-article');
  article.tabIndex = -1;
  article.setAttribute('aria-label', 'Guide article');
  root.append(rail, article);
  mount.replaceChildren(root);

  function renderNavigation() {
    const matches = searchHelpTopics(query), ids = new Set(matches.map(item => item.id));
    links.replaceChildren();
    resultStatus.textContent = query.trim() ? `${matches.length} ${matches.length === 1 ? 'guide' : 'guides'} found` : 'Workflows, controls and settings';
    if (!matches.length) {
      links.append(el('p', 'thy-help-empty', 'No matching guide. Try a feature name, a physical control or a MIDI term.'));
      return matches;
    }
    for (const group of HELP_GROUPS) {
      const entries = HELP_TOPICS.filter(item => item.group === group.id && ids.has(item.id));
      if (!entries.length) continue;
      const groupNode = el('section', 'thy-help-nav-group');
      groupNode.append(el('h3', '', group.title));
      for (const item of entries) {
        const link = button(item.title, 'thy-help-topic-link');
        link.dataset.helpTopic = item.id;
        if (item.id === selected) link.setAttribute('aria-current', 'page');
        groupNode.append(link);
      }
      links.append(groupNode);
    }
    return matches;
  }

  function renderArticle(item) {
    article.replaceChildren();
    const group = HELP_GROUPS.find(value => value.id === item.group);
    article.append(el('p', 'thy-help-eyebrow', group?.title || 'Guide'));
    const title = el('h1', '', item.title);
    title.id = `${prefix}-article-title`;
    article.setAttribute('aria-labelledby', title.id);
    article.append(title, el('p', 'thy-help-lead', item.summary));
    for (const part of item.sections) {
      const section = el('section', 'thy-help-section');
      section.append(el('h2', '', part.title));
      for (const paragraph of part.paragraphs || []) section.append(el('p', '', paragraph));
      if (part.figures?.length) {
        const gallery = el('div', 'thy-help-figures');
        for (const illustration of part.figures) {
          if (!['screen', 'surface'].includes(illustration.kind) ||
              !illustration.src.startsWith('data:image/svg+xml;charset=utf-8,')) continue;
          const figure = el('figure', `thy-help-figure thy-help-figure-${illustration.kind}`);
          figure.dataset.helpFigure = illustration.kind;
          figure.append(el('h3', 'thy-help-figure-label', illustration.label));
          const viewport = el('div', 'thy-help-figure-viewport'), image = el('img');
          image.src = illustration.src; image.alt = illustration.alt;
          image.width = illustration.width; image.height = illustration.height;
          image.loading = 'lazy'; image.decoding = 'async';
          viewport.append(image); figure.append(viewport, el('figcaption', '', illustration.caption));
          gallery.append(figure);
        }
        section.append(gallery);
      }
      for (const [key, tag] of [['bullets', 'ul'], ['steps', 'ol']]) {
        if (!part[key]?.length) continue;
        const list = el(tag);
        for (const value of part[key]) list.append(el('li', '', value));
        section.append(list);
      }
      if (part.table) {
        const scroll = el('div', 'thy-help-table-scroll'), table = el('table');
        const head = el('thead'), header = el('tr');
        for (const label of part.table.columns) {
          const cell = el('th', '', label); cell.setAttribute('scope', 'col'); header.append(cell);
        }
        head.append(header);
        const body = el('tbody');
        for (const row of part.table.rows) {
          const tr = el('tr');
          for (const value of row) tr.append(el('td', '', value));
          body.append(tr);
        }
        table.append(head, body); scroll.append(table); section.append(scroll);
      }
      article.append(section);
    }
    const reference = getHelpReference(item.id);
    if (reference.length) {
      const details = el('details', 'thy-help-reference');
      details.append(el('summary', '', `Setting reference · ${reference.length} controls`));
      const list = el('dl');
      for (const row of reference) {
        list.append(el('dt', '', row.title));
        const text = el('dd');
        text.append(el('p', '', row.description));
        if (row.choices.length) text.append(el('p', 'thy-help-choices', `Choices: ${row.choices.join(' · ')}`));
        list.append(text);
      }
      details.append(list); article.append(details);
    }
    if (item.related.length) {
      const related = el('section', 'thy-help-related');
      related.append(el('h2', '', 'Continue with'));
      const list = el('div', 'thy-help-related-links');
      for (const id of item.related) {
        const destination = getHelpTopic(id), link = button(destination.title, 'thy-help-related-link');
        link.dataset.helpTopic = destination.id;
        list.append(link);
      }
      related.append(list); article.append(related);
    }
  }

  function closeContext({ focus = true } = {}) {
    if (!overlay) return false;
    overlay.remove(); overlay = null; contextButtons = [];
    const previous = restoreFocus; restoreFocus = null;
    onContextChange(null);
    if (focus && previous?.isConnected !== false && typeof previous?.focus === 'function') previous.focus();
    return true;
  }

  function open(id, { focus = true } = {}) {
    if (disposed) return null;
    closeContext({ focus: false });
    const item = getHelpTopic(id);
    selected = item.id;
    renderNavigation(); renderArticle(item);
    onNavigate(selected);
    if (focus) article.focus();
    return item;
  }

  function search(value = '') {
    if (disposed) return [];
    query = String(value); searchInput.value = query;
    return renderNavigation();
  }

  function context(id, { anchor } = {}) {
    if (disposed) return null;
    closeContext({ focus: false });
    const info = getHelpContext(id);
    restoreFocus = anchor || doc.activeElement;
    overlay = el('div', 'thy-help-overlay');
    overlay.dataset.helpOverlay = '';
    const dialog = el('section', 'thy-help-dialog');
    dialog.setAttribute('role', 'dialog'); dialog.setAttribute('aria-modal', 'true');
    dialog.setAttribute('aria-labelledby', `${prefix}-context-title`);
    dialog.setAttribute('aria-describedby', `${prefix}-context-summary`);
    const top = el('div', 'thy-help-dialog-top');
    top.append(el('p', 'thy-help-eyebrow', 'CONTROL GUIDE'));
    const close = button('Close', 'thy-help-close');
    close.setAttribute('aria-label', 'Close help'); close.dataset.helpAction = 'close';
    top.append(close);
    const title = el('h2', '', info.title); title.id = `${prefix}-context-title`;
    const summary = el('p', 'thy-help-context-summary', info.summary); summary.id = `${prefix}-context-summary`;
    dialog.append(top, title, summary);
    if (info.options.length) dialog.append(el('p', 'thy-help-choices', `Choices: ${info.options.join(' · ')}`));
    if (info.unavailable) dialog.append(el('p', 'thy-help-boundary', 'This control is planned or unavailable in the current implementation.'));
    const articleHint = el('p', 'thy-help-context-related', `Full guide: ${info.topic.title}`);
    const read = button('Open full guide', 'thy-help-read'); read.dataset.helpAction = 'read';
    read.dataset.helpDestination = info.topic.id;
    dialog.append(articleHint, read, el('p', 'thy-help-escape', 'Esc closes this guide. Reading help never changes the instrument.'));
    overlay.append(dialog); doc.body.append(overlay);
    contextButtons = [close, read];
    onContextChange(info.topic.id);
    close.focus();
    return info;
  }

  listen(root, 'click', event => {
    const link = event.target.closest?.('[data-help-topic]');
    if (!link || !root.contains(link)) return;
    event.preventDefault(); open(link.dataset.helpTopic);
  });
  listen(searchInput, 'input', () => search(searchInput.value));
  listen(doc, 'click', event => {
    if (overlay?.contains(event.target)) {
      const action = event.target.closest?.('[data-help-action]');
      if (action || event.target === overlay) { event.preventDefault(); event.stopPropagation(); }
      if (action?.dataset.helpAction === 'close' || event.target === overlay) closeContext();
      else if (action?.dataset.helpAction === 'read') open(action.dataset.helpDestination);
      return;
    }
    const trigger = event.target.closest?.('[data-help]');
    if (!trigger || trigger.disabled) return;
    event.preventDefault(); event.stopPropagation();
    context(trigger.dataset.help, { anchor: trigger });
  }, true);
  listen(doc, 'keydown', event => {
    if (!overlay) return;
    if (event.key === 'Escape') {
      event.preventDefault(); event.stopPropagation(); closeContext();
    } else if (event.key === 'Tab') {
      const first = contextButtons[0], last = contextButtons.at(-1), current = doc.activeElement;
      if (event.shiftKey && (current === first || !overlay.contains(current))) {
        event.preventDefault(); last.focus();
      } else if (!event.shiftKey && (current === last || !overlay.contains(current))) {
        event.preventDefault(); first.focus();
      }
    }
  }, true);
  renderNavigation(); renderArticle(getHelpTopic(selected));
  return {
    open, search, context, closeContext,
    get selected() { return selected; },
    get contextOpen() { return Boolean(overlay); },
    destroy() {
      if (disposed) return;
      closeContext(); disposed = true;
      for (const remove of listeners.splice(0)) remove();
      mount.replaceChildren();
    },
  };
}
