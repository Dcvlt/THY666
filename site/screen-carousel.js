import { HELP_TOPICS } from './guide/help-topics.js';

// Synthetic examples rendered by the same C renderer as the instrument.
export const screenViews = HELP_TOPICS.find(topic => topic.id === 'views').sections
  .flatMap(section => section.figures || []);
export const wrapView = index => (index + screenViews.length) % screenViews.length;

for (const mount of document.querySelectorAll('[data-screen-carousel]')) {
  const image = mount.querySelector('img');
  const caption = mount.querySelector('figcaption');
  const count = mount.querySelector('[data-view-count]');
  const dots = mount.querySelector('[data-view-dots]');
  let current = 0;
  const buttons = screenViews.map((view, index) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'hud-action hud-action--compact';
    button.textContent = String(index + 1).padStart(2, '0');
    button.setAttribute('aria-label', view.label.split(' · ')[0]);
    button.addEventListener('click', () => show(index));
    dots.append(button);
    return button;
  });
  function show(index) {
    current = wrapView(index);
    const view = screenViews[current];
    image.src = view.src; image.alt = view.alt;
    image.width = view.width; image.height = view.height;
    caption.textContent = view.label;
    count.textContent = `${current + 1} / ${screenViews.length}`;
    buttons.forEach((button, i) => button.setAttribute('aria-pressed', String(i === current)));
  }
  mount.querySelector('[data-view-prev]').addEventListener('click', () => show(current - 1));
  mount.querySelector('[data-view-next]').addEventListener('click', () => show(current + 1));
  mount.addEventListener('keydown', event => {
    if (!['ArrowLeft', 'ArrowRight'].includes(event.key)) return;
    event.preventDefault(); show(current + (event.key === 'ArrowRight' ? 1 : -1));
  });
  show(0);
}
