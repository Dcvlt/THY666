import { initializeHelp, resolveHelpTopic } from './guide/help.js';
import { getHelpTopic } from './guide/help-topics.js';
import { initializeUIComponents } from './ui-system.js';
import { initializeSiteNavigation } from './site-navigation.js';

initializeUIComponents();
initializeSiteNavigation();

let navigating = false;
const help = initializeHelp({
  mount: document.querySelector('#main'),
  onNavigate(id) {
    document.title = `THY666 · ${getHelpTopic(id).title} · User guide`;
    if (!navigating) history.replaceState(null, '', `#${id}`);
  },
});
function openHash({ focus = false } = {}) {
  // The accessibility skip link moves focus without replacing the article.
  if (location.hash === '#main') return;
  navigating = true;
  let requested;
  try { requested = decodeURIComponent(location.hash.slice(1)); } catch { requested = ''; }
  const id = resolveHelpTopic(requested || 'getting-started');
  help.open(id, { focus });
  history.replaceState(null, '', `#${id}`);
  navigating = false;
}
// Preserve the incoming deep link: initializeHelp does not invoke onNavigate.
openHash();
addEventListener('hashchange', () => openHash({ focus: true }));
