# Third-party notices

## Tabler Icons

FLX renders system-interface glyphs from Tabler Icons Webfont 3.49.0, embedded as `third_party/icons/tabler/tabler-icons.ttf` and installed as `/share/fonts/tabler-icons.ttf` in the guest. The associated license is preserved at `third_party/icons/tabler/LICENSE` and copied to `/share/fonts/TABLER-LICENSE.txt`. The upstream project is [tabler/tabler-icons](https://github.com/tabler/tabler-icons) and the icon set is MIT licensed.

The vendored stylesheet `third_party/icons/tabler/tabler-icons.css` is kept as the upstream name-to-codepoint reference for the subset used by FLX. The guest does not load CSS or a web font at runtime; it rasterizes the TTF directly through FLX's existing stb_truetype path.

## QuickJS

The native FLX browser embeds the QuickJS source release dated 2026-06-04 from [bellard.org/quickjs](https://bellard.org/quickjs/), under `third_party/quickjs/`. QuickJS is MIT licensed; the upstream license is preserved at `third_party/quickjs/LICENSE`. FLX creates a separate runtime per page, limits memory and execution time, and does not expose filesystem, process, native module, or network APIs to scripts. This supplies the ECMAScript engine; the browser's DOM and Web API coverage remains intentionally smaller than a full desktop browser.
