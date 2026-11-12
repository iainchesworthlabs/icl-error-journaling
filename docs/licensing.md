# Licences and the About page

What the app is licensed under, how the About page learns which third-party components the iPhone binary contains, and how to keep
that right. This page is engineering documentation. It is not legal advice and it takes no decision for the owner.

## What the app is

Field Test is GPL-3.0-or-later (`LICENSE`, `LICENSES/GPL-3.0-or-later.txt`, and an SPDX header in every source file). The About
page names the licence and opens its full text from the bundled notices.

## Where the About page gets its facts

| Fact | Source |
|---|---|
| Version | `PROJECT_VERSION` of `CMakeLists.txt`, through `version_text()` |
| Build number | iOS: `ERROR_JOURNALING_IOS_BUILD_NUMBER`, the same value as `CFBundleVersion` (the project version when empty). Elsewhere `$GITHUB_RUN_NUMBER`, else `unknown` |
| Commit, branch or tag | `git rev-parse --short=7 HEAD`, `git describe --tags --exact-match`, the branch; else `$GITHUB_SHA` and `$GITHUB_REF_NAME`; else `unknown` and nothing |
| Date | `$SOURCE_DATE_EPOCH`, else the date of the commit, else `unknown`. Never the time of the build |
| Platform, storage | the bridge (`ios` or `desktop-stub`) and `storage::backend_name()` |
| Qt | `qVersion()` at run time |
| Source address | `HOMEPAGE_URL` of `project()` |
| Components and texts | `tools/licences/notices.py`, below |

`tools/licences/build_info.py` writes the first group into a source file of the build directory on every build, and only touches the
file when a value changed. A tree without git, or a machine without it, is not an error.

## How the list of components is made

```
 CMake file API: the link line of ej_app ─┐
 vcpkg installed tree (status, info/*.list, ├─► tools/licences/notices.py ──► about/third-party-notices.json  ─► resource :/about/…
   share/<port>/copyright)                  │      (build time, before the link)   about/THIRD-PARTY-NOTICES.txt
 tools/licences/data/qt-modules.json ───────┤
 tools/licences/data/manifest.json ─────────┤
 tools/licences/data/texts/<SPDX id>.txt ───┘
```

- **What is shipped is derived.** Every input of the resolved link line is classified: first-party (the build directory), a vcpkg port
  (the installed tree says which port owns the file), Qt (the library file name is looked up in `qt-modules.json`), or a system
  library. Anything else stops the build. A static Qt links the plugins and QML modules as archives or objects; those are attributed
  by name and by where they sit under the Qt prefix.
- **Qt.** `qt-modules.json` is a snapshot of the SPDX SBOMs that Qt installs in each kit (`<kit>/sbom/*.spdx.json`),
  made by `tools/licences/qt_sbom_manifest.py` from the Windows, macOS, iOS, Android and WebAssembly kits of Qt 6.12.0. Each module and
  plugin has its licence expression and its edges to what it contains: the third-party sources Qt attributes (FreeType, HarfBuzz, libpng,
  PCRE2, SQLite, zlib and so on). The closure of the linked modules over those edges is the list. Qt's SBOM lists the sources a module
  contains; it does not say which of them the compiler used for iOS, so the list is what Qt says the linked modules contain.
- **vcpkg.** A linked port brings its `share/<port>/copyright` as its text. A port that is installed, declared in `vcpkg.json` and neither
  linked nor in `testOnlyPorts` fails the build, so a new dependency cannot be forgotten (and `catch2` is named as the test-only one).
  On iOS the manifest has no runtime dependency.
- **Texts.** One file for each SPDX id in `tools/licences/data/texts/` (provenance in the README there), the app's own text from
  `LICENSES/`, and the ids that need no text (`textExemptions` of `manifest.json`: the public-domain dedication CC0-1.0 and Qt's
  commercial offer). A licence id with no text fails the build.
- **Linking.** `static` or `dynamic` is read from the file on the link line (`.a` and `.o` are static; `.dylib`, `.so`, `.dll`
  are dynamic). `verify_app.sh` checks that the iOS bundle holds no dynamic library, so "static" is true of the bundle too.

## Tests

| Test | What it keeps true |
|---|---|
| `about.notices` | The generator, strict, over the link line of the desktop build: no unaccounted library, no licence without a text |
| `about.build_info` | The commit is seven hex digits or `unknown` |
| `about.tools` | `tools/tests/test_notices.py`: made-up iPhone, desktop and vcpkg link lines; a made-up repository, with and without git |
| `about.qt_snapshot` | `qt-modules.json` against the SBOMs of the Qt kit, when it has them (Windows, macOS) |
| iOS build | `ej_notices` is strict; the job summary prints the list and every link input; `verify_app.sh` checks the bundle |

## Maintaining it

| Change | What to do |
|---|---|
| Add a vcpkg dependency that ships | Nothing: it is picked up from the link line. A port with no known licence expression gets a line in `portLicences` of `manifest.json` |
| Add a vcpkg dependency used only by tests or tools | Add it to `testOnlyPorts` |
| Link another Qt module or plugin | Nothing, if the snapshot knows it; the build fails if it does not |
| Upgrade Qt | `python -I tools/licences/qt_sbom_manifest.py --sbom ios=<qt ios>/sbom --sbom windows=… --out tools/licences/data/qt-modules.json`, review the diff, update `expectedQtVersion` in `manifest.json` |
| A licence text is missing | Add `tools/licences/data/texts/<id>.txt` and say in its README where it came from |
| Add a bundled font, icon set or other asset that is not ours | Add it to the notices by hand; the generator only sees what is linked (there is none today) |
