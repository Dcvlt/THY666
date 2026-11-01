import { HELP_FIGURES, HELP_TOPICS } from './guide/help-topics.js';
import { initializeUIComponents } from './ui-system.js';
import { initializeSiteNavigation } from './site-navigation.js';

initializeUIComponents();
initializeSiteNavigation();

// Read-only, synthetic documentation figures. This site has no device adapter.
export const siteFigures = {
  chordEntry: HELP_FIGURES.navigation,
  simple: HELP_TOPICS.find(topic => topic.id === 'views').sections
    .flatMap(section => section.figures || []).find(figure => figure.label.startsWith('Simple ·')),
  update: HELP_FIGURES.update,
};
for (const mount of document.querySelectorAll('[data-site-figure]')) {
  const figure = siteFigures[mount.dataset.siteFigure];
  if (!figure) continue;
  const image = document.createElement('img');
  image.src = figure.src; image.alt = figure.alt;
  image.width = figure.width; image.height = figure.height;
  image.decoding = 'async';
  mount.append(image);
}
