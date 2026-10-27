# Project site and public user guide

The GitHub Pages site contains a project overview, installation guidance, browser Client and the
same searchable, illustrated user guide as the Client. The guide is read-only;
the separate Install page offers an explicit WebHID updater with configuration
backup/restore over Web Serial. Loading any page opens no device. No private
musical corpus is loaded. The desktop Client remains a separate app.

## Preview locally

From the repository root, with Python 3.11+, Node.js 20+ and Rust 1.89+:

```sh
rustup target add wasm32-unknown-unknown
npm run setup
npm run build:core
npm run build:browser-updater
npm run build:site
python -m http.server 3331 --bind 127.0.0.1 --directory build/site
```

Open `http://127.0.0.1:3331/`. Use HTTP rather than `file://` so JavaScript modules
load normally. The Client is at `client/index.html`, opening disconnected with explicit Web Serial
connection in desktop Chrome or Edge over HTTPS or localhost. The installer releases
its connection before **Go to Client** navigates there; reconnect explicitly in the Client.
The guide is at `manual.html`; a topic link such as
`manual.html#sequencer-editor` opens that topic directly.

The site needs no npm packages, firmware, device or `.env`. Its portable Rust
updater is built first and verified against its source/build manifest. It stages
an explicit file list into `build/site`, with bundled font licenses and a SHA256
asset manifest. It does not publish the repository root, `web/generated`, local
backups, captures or desktop release bundles. The Client reuses the desktop public asset boundary: checked core WASM, public menu
metadata and OLED profile, source modules and licensed assets. Private chord data,
local releases and backups are excluded. Never copy generated output wholesale.

## Publish on GitHub Pages

Live site: **https://dcvlt.github.io/THY666/**.

The owner has made `Dcvlt/THY666` public to use GitHub Pages on the current plan.
Both the source and Git history are public. Pages publishes only the staged site
artifact through GitHub Actions, with HTTPS enforced. Private configuration,
captures and vendor packages remain excluded from source and site builds.

1. Review the public source and assets, commit the intended files and push them.
2. In GitHub **Settings → Pages → Build and deployment**, choose **GitHub Actions**.
   Public repositories can use Pages on GitHub Free; private repositories require
   an eligible plan. Making this repository public is a separate owner decision.
3. Run the **Project site** workflow on the default branch, or push a site change
   there. Pull requests build and check the site without deploying it.
4. Use the URL reported by the deployment job. For the current repository name,
   the expected public project path is `https://dcvlt.github.io/THY666/`.
   That address is not evidence of a successful deployment by itself.

The workflow uses a bounded artifact and SHA-pinned official Actions. Only the
deploy job receives `pages: write` and `id-token: write`. Local asset paths are
relative, so deployment beneath `/THY666/` needs no root-domain rewrite.

The deployment checks also compile the portable C renderer with the pinned
toolchain to verify the authored screenshots. This CI check needs a compiler
download, but no private corpus; the delivered website includes the portable musical-core WebAssembly, but not
the compiler or private musical archive. The
browser updater has its own Rust target/build/test step.

See [GitHub's custom Pages workflow documentation](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
for publishing-source and environment configuration.

## Keep the guide useful

Edit `web/help-topics.js`, not a second website manual. `web/help.js` renders the
same topic data in the Client and public site. Use original descriptions that
answer: what is it for, when would I use it, what do I press, what should happen,
and what is the current limit? Add a short musical example where it helps.

Keep topic IDs stable for external links. Use tables for options, diagrams for
physical actions and synthetic screenshots from the shared C renderer for screen
content. Regenerate changed screen examples using `tools/render_help_examples.mjs`
and check them before publishing. Never use a personal device export or an
identifying screenshot as a public fixture.

```sh
node --test tests/web/help.test.mjs tests/web/site.test.mjs
python -m unittest discover -s tests -p test_site.py -v
npm run build:site
```

## Release links and direct browser installation

The Install page links to [GitHub Releases](https://github.com/Dcvlt/THY666/releases)
and source build instructions. The historical v0.1.0 release provides source;
v0.2.0 is staged as a Musical Expansion prerelease with standalone firmware,
matching source/input and unsigned Client installers. Only assets visible in a
published release are available to visitors; draft assets are not public.
See [Release procedure](releases.md) for packaging and qualification boundaries.

Direct browser installation uses the shared Rust protocol and WebHID adapter.
A Windows device test passed transfer, application checks and explicit settings
restoration with durable readback and full payload comparison. This does not
qualify every configuration or other operating systems. The
[browser installation guide](browser-installation.md) records the scope.

The default installer catalog remains empty. Download the selected prerelease
firmware asset from Releases and choose **Custom file** explicitly; the site does
not silently promote a testing build to the default firmware choice. No firmware
or musical input is included in the Pages bundle.

## Presentation assets and roadmap

The screen carousel draws all seven implemented views (Simple, Detailed, Piano,
Chladni, Orbit, Waves and Circle of fifths) directly from shared C-rendered guide figures. It advances only by explicit controls or arrow keys. The
Client screenshot shows a disconnected session with no device identifiers; refresh
`site/assets/client-preview.png` when the interface changes. Large display titles retain Monoton; navigation and smaller headings use Orbitron,
white on dark surfaces and dark in light mode.

The roadmap marks v0.2 as the available development milestone, with musical
integration additions explicitly still planned within v0.2. v0.3 covers sequencer
expansion; experimental features and MIDI expansion follow. These product milestones
do not change package/build version metadata or imply downloadable release artifacts.
Keep implemented behavior separate from planned work and device qualification.
