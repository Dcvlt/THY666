# Desktop dependency license supplements

These are original upstream license texts omitted from some locked crates.io
archives. `manifest.json` binds each text's SHA-256, original URL, repository
revision, package version and crate archive digest. They are copied unchanged;
the hexadecimal filenames deduplicate identical texts.

The objc2 repository notice points to standard MIT terms rather than reproducing
them. Its original notice and the canonical SPDX MIT text are both retained;
the generated inventory also preserves each package's declared upstream authors.

The Mozilla Public License 2.0 text is the versioned official Mozilla text
referenced by `selectors` source headers. `r-efi` ships its full MIT notice in
`AUTHORS`, which the collector includes directly from the checked crate archive.
Windows GNU target import libraries do not record a VCS revision; their
supplements come from the recorded `winapi 0.3.9` repository revision of the same
upstream project.

`tools/build_desktop.py` verifies these identities and the original crate notices
when producing `THIRD_PARTY_LICENSES.txt`. This does not relicense the upstream
work or require choosing every alternative license simultaneously. Review this
manifest when updating the desktop lockfile.
