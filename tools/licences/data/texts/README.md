# Licence texts

One file per SPDX identifier (`<id>.txt`), read by `tools/licences/notices.py` for the About page. The app's own text is in
`LICENSES/`, and `GPL-3.0-only` is served from the same file (`textAliases` in `../manifest.json`). Each text applies to the component
it belongs to, not to Field Test.

Provenance, so a reviewer knows what was copied and what was typed:

| file | source |
|---|---|
| `Apache-2.0`, `FTL`, `HPND`, `HPND-sell-variant`, `Imlib2`, `MIT-open-group`, `Unicode-3.0`, `X11`, `MPL-2.0`, `IJG`, `Libpng`, `libpng-2.0` | copied from the notices of another project of the same owner (its `third_party/notices/licences/`), which took them from the SPDX licence list, vcpkg's `share/<port>/copyright` of libpng, eigen3 and the libjpeg-turbo source, as its own README there records |
| `MIT`, `BSD-2-Clause`, `BSD-3-Clause`, `Zlib` | the standard SPDX template wording, without a copyright line; the page prints each component's own copyright line above the text |
| `LicenseRef-*` | `extractedText` of the SPDX SBOMs in the Qt 6.12.0 kits |
| `LGPL-3.0-only` | `LICENSES/LGPL-3.0-only.txt` of another project of the same owner, from the SPDX licence list through REUSE |
| `GPL-2.0-only` | `COPYING.GPLv2` of the xz project (the standard text), as installed with Git for Windows |
| `blessing` | the four lines of SQLite's source header |

A text that is only a template (`MIT`, `BSD-*`, `Zlib`, `FTL`, `HPND`) is the licence's wording; the real copyright holders are in the
component's own copyright line, from the Qt SBOM or the vcpkg port.
