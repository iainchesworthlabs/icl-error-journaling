# About page

Task breakdown, test list and device checklist for the About page (PRF-12), one pull request into `develop`. It is not a stage of the
[implementation plan](../implementation-plan.md): the owner asked for it after the first TestFlight build ("There should be an about
page somewhere with the standard git information, license, licenses for used components, etc. All I see is the 0.1.0 line on the
bottom of preferences"), and the request is this page's brief. The owner reviews it with the pull request.

**Status:** built, with the pull request. Nothing here has run on a device (there is no Mac); CI compiles the iOS build and checks the bundle.

## What it adds

| Part | Where | Notes |
|---|---|---|
| The way in | `SettingsList.qml`: the row "About CS Bingo Board", in a group of its own after Import or export setup | The version line at the foot of the list stays, as a plain caption (`SettingsAbout.qml`) |
| The page | `SettingsAboutPage.qml` | This build, Licence, Source code, Open-source components |
| A licence text | `SettingsLicencePage.qml`, `SettingsComponentRow.qml` | For the app and for each component: facts, copyright, source, then the text of each licence as paragraphs |
| The timing readout | the Version row of the About page | Five quick taps, as before; read as text, not as a control |
| The model | `viewmodel/about_model.{hpp,cpp}` | QtCore only; reads the notices when first needed |
| Build facts | `tools/licences/build_info.py`, `cmake/EjAbout.cmake`, `src/build_info.hpp` | Commit, branch or tag, build number, date of the commit; `unknown` without git |
| Notices | `tools/licences/notices.py` and `tools/licences/data/` | From the file API link line, the Qt SBOM snapshot and the vcpkg tree; strict |
| Mock-ups | `docs/design/screens/About*.dc.html`, light and dark, and the About row on `BoardSettings` | Capture scenes `About` and `AboutLicence` |

## Decisions

1. **The foot of Settings keeps the version line**, as a plain caption: it costs a line and tells a person without opening a page which
   build they have. It no longer takes taps.
2. **The list is Qt's own accounting, grouped.** Qt is one row for each repository (Base, Declarative, ...) with the modules used in its
   page; what Qt bundles (FreeType, HarfBuzz, libpng, PCRE2, SQLite, zlib, ...) is a row of its own, each with its version and licence.
3. **No election of a licence option.** Qt offers its modules under several licences. The page shows the expression Qt publishes (with
   "Qt commercial licence" in words) and the text of each non-commercial option. Which option the app is distributed under is the
   owner's decision, taken with legal advice, and not the page's.
4. **The build stops on a gap.** An unknown linked library, a vcpkg port with no copyright, a licence with no text: `ej_notices` is strict
   (`ERROR_JOURNALING_NOTICES_STRICT`), so the iOS build is the check for the iPhone.
5. **The date is the commit's** (or `SOURCE_DATE_EPOCH`), so a build is reproducible; the row is called "Built".
6. **The browser** is `Qt.openUrlExternally`, through a seam of the shell (`urlOpener`) so a test opens nothing. The bridge has no URL call and
   needs none.

## Tests

| Case or test | Label | What |
|---|---|---|
| `test_about_model.cpp` | unit | facts, lazy notices, the list, a page with one licence, a choice, a dedication and an own file; `app`; an unknown id; unreadable notices; paragraphs |
| `settings.about: ...` (11 cases) in `settings_screen_test.cpp` | ui | the row and Back; no model, no row; the caption; the facts; unknown values and no branch; unreadable notices; five taps; the source request; the licence text; a component page; a dedication and an own file; light and dark checks; VoiceOver names and roles; the largest text size |
| `app.timing`, `app.about` in `app_flow_test.cpp` | ui | the readout through the shell; the source address through the opener |
| `about.notices`, `about.build_info`, `about.tools`, `about.qt_snapshot` | unit | `docs/licensing.md` |
| `tools/tests/test_notices.py` | unit | the generator over made-up link lines (iPhone, desktop, Xcode paths, vcpkg), the commit and date rules with and without git |
| iOS build | CI | `ej_notices` strict; the job summary lists the components and every link input; `verify_app.sh` finds no dynamic library |

## Not run on a device

- the five taps on a TestFlight build, VoiceOver reading of the rows and of a licence text, the text sizes on a phone;
- the source row opening Safari (`Qt.openUrlExternally` on iOS);
- the About page's scroll and pushes at 120 Hz.
