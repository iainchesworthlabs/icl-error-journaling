# Stage 10: Documentation

Task breakdown, test list and exit gate for Stage 10 of the [implementation plan](../implementation-plan.md), "Stages 7
to 12". Each work package is one pull request into `develop`. Requirement IDs refer to the [PRD](../prd.md).

**Exit gate:** the user guide is published at its public address, the support and privacy pages are live at the addresses
Stage 11 gives the stores, every setup-file field in `libs/core` is documented, and in-app help is reachable from every
screen in the [entry point table](#where-people-get-stuck-and-what-the-app-does-there).

**Status:** draft for owner review. No guide pages and no help code are written until the owner approves this page.

The flows are still changing. Double-tap to log (4.17a) and the About page (PR #130) are in flight, and the Stage 7
review will change more. So this stage builds the structure, the setup-file reference, the privacy and support pages and
the checks first, because none of them depends on how a screen looks. The pages that describe screens are filled in after
Stage 7's fixes have merged, with screenshots from `ej_screenshots`.

## What the stage covers

| Plan row | Section | Work package |
|---|---|---|
| A user guide online | [The guide](#the-guide-audience-and-structure), [Hosting](#hosting) | 10.1, 10.3, 10.4 |
| The setup file format | [Setup file reference](#the-setup-file-reference) | 10.2 |
| A getting-started guide and an FAQ | [The guide](#the-guide-audience-and-structure) | 10.3, 10.4 |
| Support and privacy URLs for Stage 11 | [Support and privacy](#support-and-privacy-pages) | 10.1 |
| Short in-app help where people get stuck | [In-app help](#in-app-help) | 10.5, 10.6 |
| Docs that stay in step with the app | [Keeping docs in step](#keeping-the-docs-in-step) | 10.2, 10.6, 10.7 |
| Exit gate | [Exit gate](#exit-gate) | 10.8 |

## The guide: audience and structure

**Audience.** Facilitators who use the app in sessions, and the person who prepares their setup file. They are not
developers. They know the robot, the sessions and the error types; they do not know JSON, Git or Qt.

**Voice.** Plain English, second person, short sentences, the app's own words for its buttons (copied from the QML, not
paraphrased). One task per section, in the order someone meets it. A screenshot where a screen is first shown, not one
per paragraph. No requirement IDs, no file paths except in the setup-file reference, and no developer terms.

**Fictional stand-ins only** (NFR-10): Sam Example, UNIT, North Site and South Site, Wrong Response and the other sample
types, "the robot", "the downstream ticket indexer". Screenshots use `config/sample-config.json`. The confidential-terms
check covers `docs/` already, so it covers the guide.

**Platforms.** One guide for iPhone, iPad and Android. Where a step differs (the share sheet, opening a setup file from
AirDrop, Files or a file manager, a two-column iPad layout), the page says so in a short "On Android" or "On iPad" note
rather than in separate pages. Stage 8 and Stage 9 add those notes as they land.

**Pages** (under `docs/guide/`, each a single page with anchored sections):

| Page | File | What it covers |
|---|---|---|
| Home | `index.md` | What the app is for, in three sentences. Links to the pages below, with Getting started first. |
| Getting started | `guide/getting-started.md` | Install, first launch, the two ways to start (open a setup file you were given, or set up by hand), and your first entry. |
| Setting up the board | `guide/setup.md` | Settings in order: the board layout (placing, moving, changing the type of a tile, empty positions), the Error types list (add, rename, retire, what Other holds), Locations, note templates (name, the question, the line in the log, the example), your name, the unit prefix, the time format, totals. Import and export of a setup file. |
| A day of logging | `guide/logging.md` | Double-tap a tile to log; why one tap does nothing (and the hint); hold a tile to log it with a note; Other and its search; typing a note and Add; template chips; the toast, ×N and Undo; the Last bar. |
| Today's details | `guide/todays-details.md` | The header card: unit number and environment, location, software version and its commit, changing the version during the day (and from what time), the date and the facilitator. |
| Sharing and the next day | `guide/sharing.md` | Review: the timeline, editing and deleting an entry, Share. What the shared text looks like (a sample in stand-ins) and what the downstream ticket indexer reads. The new-day prompt and what happens to an unshared log. |
| History | `guide/history.md` | Past logs: opening, sharing again, deleting, and what is kept. |
| The setup file | `guide/setup-file.md` | The `.ejconfig` format, for whoever prepares a file: a complete example, then the [field reference](#the-setup-file-reference), the rules (ids, unique names, board positions) and every message the app can show on import, with the fix. |
| FAQ | `guide/faq.md` | Short questions with short answers (see the [seed list](#faq-and-troubleshooting-seed-lists)). |
| Troubleshooting | `guide/troubleshooting.md` | Symptom, cause, fix: the write-failed banner, recovery after a crash, an import refused, a share that did not go, an empty board, a missing tile. |
| Privacy | `privacy.md` | See [Support and privacy](#support-and-privacy-pages). Linked from the stores and the app. |
| Support | `support.md` | See [Support and privacy](#support-and-privacy-pages). Linked from the stores and the app. |

The developer pages (PRD, plans, design, building, architecture, testing, CI, releasing) stay in the same site under a
"For developers" section of the navigation, after the guide. The present `docs/index.md` becomes
`docs/developers.md`, and `docs/index.md` becomes the guide's home.

### FAQ and troubleshooting seed lists

These come from the TestFlight feedback so far and are added to by the Stage 7 findings.

- FAQ: Why does one tap not log? How do I put an error type on the board? What is Other? How do I add a note to an entry
  I already logged? Can I undo a delete? What happens if I forget to share before the next day? Can two phones share a
  log? Where is my data and is it backed up? How do I move my setup to a new phone? Does the app need the internet?
  How do I get the setup file I was sent into the app? What does "(modified)" next to the commit on the About page mean?
- Troubleshooting: every `ConfigError::Problem` and `Code` with the message the app shows and the fix; the
  write-failed banner; the recovery screen; a share sheet that closes without sharing (the log stays unshared); a tile
  that shows a plus; the board shows the wrong unit or version for the day.

## Hosting

**Recommendation: keep the one MkDocs Material site on GitHub Pages from this repository, with the guide at its root.**

What is there already: `mkdocs.yml` (Material theme, `strict: true`), `.github/workflows/docs.yml` (builds the site on
every pull request that touches `docs/`, and on a push to `main` builds and deploys it with the reusable workflows of
`icl-common-infra`), and Pages is enabled with the workflow as its source and HTTPS enforced. The address is
`https://iainchesworthlabs.github.io/icl-error-journaling/`. It returns 404 today because `main` has not had a release
yet.

Why this, and not the alternatives:

- **A separate site generator or a second repository** would be a second toolchain and a second place for the reference
  to drift from `libs/core`. The guide's checks need to read the code in the same pull request.
- **A wiki** has no pull requests, no checks and no review.
- **Deploying from `main`** (as today) means the public guide describes the released app, not work in progress on
  `develop`. A pull request that changes a flow updates the guide in the same pull request, and both reach users at the
  same release.

**Public addresses** (with `use_directory_urls`, the MkDocs default):

| Page | Address | Used by |
|---|---|---|
| Guide home | `https://iainchesworthlabs.github.io/icl-error-journaling/` | App ("User guide"), store descriptions |
| Support | `https://iainchesworthlabs.github.io/icl-error-journaling/support/` | App Store support URL, Play contact page, app |
| Privacy | `https://iainchesworthlabs.github.io/icl-error-journaling/privacy/` | App Store privacy policy URL, Play privacy policy, app |
| Setup file | `https://iainchesworthlabs.github.io/icl-error-journaling/guide/setup-file/` | Import errors screen |

The app holds these addresses in one place (a C++ constant exposed to QML, like the About page's source link), so a
change of domain is one edit and one test.

**Owner actions:**

1. Nothing to enable: Pages is on, with the workflow source. Confirm that the `github-pages` environment allows
   deployments from `main` (the first deploy shows it).
2. Publish: the guide goes live with the first release to `main`. App Review and Play review fetch the support and
   privacy URLs, so a release to `main` that contains 10.1 has to happen before the Stage 11 submission. If that is too
   late, the owner can choose decision 2's alternative.
3. Decide on a custom domain (decision 3) before Stage 11 submits, because a store URL that changes later needs a new
   submission. A custom domain needs a DNS record and the Pages domain setting, both owner actions.
4. Choose the support contact (decision 4).

## Support and privacy pages

Both land in 10.1, first, in final words, because they do not depend on the screens and Stage 11 needs them.

**Support page.** What the app is, a link to the guide and the FAQ, how to report a problem (what to include: the app
version and commit from the About page, the device, what happened, and never the contents of a real log or setup file),
and the contact the owner chooses (decision 4). It says how soon to expect a reply only if the owner sets one.

**Privacy page.** Short and literal, and it must match what the code does. It states:

- What is stored: the logs (entries, notes, the day's header), the setup (error types, locations, templates,
  preferences) and the app's settings. Nothing else.
- Where: only on the device, in the app's own storage, with the platform's file protection (Stage 5, data protection;
  the Android equivalent from Stage 9).
- What leaves the device: only what the user shares with the share sheet or exports as a setup file, and only to where
  they send it. The app makes no network requests of its own, has no accounts, analytics, advertising, tracking or crash
  reporting. Links to the guide, support and source code open in the browser.
- Backups: whether the platform's device backup includes the app's data. This is a fact to confirm, not to assume: iOS
  includes the app's documents in iCloud and computer backups unless they are excluded, and Android's auto backup
  depends on the manifest Stage 9 writes. 10.1 reads both from the code and states them; a later change to either updates
  the page in the same pull request (a row of [If you change X](#keeping-the-docs-in-step)).
- Deleting: deleting a log in the app deletes it; deleting the app deletes everything it stored.
- Children, contact for privacy questions (the support contact), and the date of the last change.

The page is the source for Stage 11's App Privacy answers and Play data-safety form ("Data not collected"); Stage 11
links to it rather than restating it.

## The setup file reference

The `.ejconfig` file is what `ej::import_configuration` reads (`libs/core/src/configuration.cpp`, schema version 1). The
reference in `guide/setup-file.md` has a table per object, generated by hand and checked by CI (10.2):

| Object | Fields (required in bold) |
|---|---|
| Top level | **`schema_version`** (1), **`preferences`**, **`error_types`**, **`locations`**, **`note_templates`** (the three lists are required but may be empty) |
| `preferences` | **`facilitator`** (up to 200 characters), **`unit_prefix`** (up to 20, no `-` or `#`), `unit_environments` (a list of names, not empty, default `["prod", "dev"]`), `time_format`, `include_totals` (true or false) |
| `preferences.time_format` | `style` (`"24h"` or `"12h"`), `seconds` (true or false), `meridiem` (`"upper"` or `"lower"`) |
| `error_types[]` | **`id`**, **`name`** (up to 200), `retired`, `board_position` (1 to 11, each used once) |
| `locations[]` | **`id`**, **`name`** (up to 200), `sort_order` (whole number), `retired` |
| `note_templates[]` | **`id`**, **`name`** (up to 200, unique), **`value_prompt`** (up to 200), **`output_pattern`** (up to 500, with one `{value}`; `{{` and `}}` for a brace), `icon` (up to 32, lower-case letters, digits and dashes), `sort_order` |

The page also states the file-wide rules (UTF-8, a byte-order mark is accepted, at most 1 MB and 500 entries per list,
an unknown key is an error, ids are UUIDs and not all zeros and unique, live names unique ignoring case), what each
default is, and every error message by `Problem` with its fix. It explains what the facilitator sees in the app for each
field, in the app's words ("Question to ask" for `value_prompt`, "Line in the log" for `output_pattern`, "Insert answer"
for `{value}`), because the file keeps the old names (4.13a).

The table above is this plan's reading of the code on `develop` at 92080f9; the page is written from the code, not from
this table, and the check in 10.2 is what holds it.

The guide's complete example is a fenced block in the page, not a file: `tools/ci/check_config_files.py` refuses any
tracked `*.ejconfig` other than `config/sample-config.json`, and that rule stays. The example is the sample config.

## Screenshots

From `ej_screenshots` (Stage 4, 4.16), which renders every screen and sheet offscreen in light, dark and large text with
the sample data. Stage 7 brings it if 4.16 has not.

- A small list, `docs/guide/screenshots.json`, names the shots the guide uses: screen, scheme (light only in the guide;
  the theme shows them on a light or dark page), and the scenario state (for example "Log, three entries, toast showing").
- `tools/docs/guide_screenshots.py` runs `ej_screenshots` for that list and writes PNGs to `docs/guide/img/`, resized
  to a phone width the page can show, and optimised. They are committed, so the site build needs no Qt.
- They are fictional by construction (the sample config, the fixed clock of the UI tests). The confidential-terms check
  runs on the image file names and alt text; the images are from the sample data only, and are never taken on a phone
  with a real setup.
- Every image has alt text that says what the screen shows. A Stage 8 iPad shot is added where the layout differs.
- A check (10.7) fails when the guide names an image that is not in the list, or the list names a screen
  `ej_screenshots` does not know. Regenerating them is a step in the release checklist (`docs/releasing.md`) and in the
  pull request template's "user-visible change" line.

## In-app help

### Principles

- **Help at the point of doubt, in the app's own place.** A short sentence on the screen where it is needed, or a help
  sheet a tap away. Never a tour, and never a dialog on a capture path ("No interruptions on capture", design README).
- **Works offline.** Sessions run on sites without a reliable connection. The in-app help is text in the app (`qsTr`
  strings, checked by `check_qml_strings.py`); the online guide is "more", never the only answer.
- **Short.** A help sheet is a heading, two to five short paragraphs or steps, and "Read more in the guide". Anything
  longer belongs in the guide.
- **Accessible.** Every help button has an accessible name ("Help with the board layout"), the sheet's text is
  paragraphs that VoiceOver reads in order and that reflow at every text size, and nothing relies on colour.

### Where people get stuck and what the app does there

From the TestFlight feedback (a user could not find how to put a type on the board, and missed Replace type and the
Error types list; the template editor's words confused the facilitator), the double-tap change (one tap now logs
nothing) and the screens most likely to be met once. Stage 7's findings add rows before 10.5 starts.

| Screen | What confused people, or might | Already in place | Stage 10 adds |
|---|---|---|---|
| Log, first days | One tap logs nothing (4.17a); the hold for a note; Other; Undo | The single-tap hint; the note placeholder "Type a note, then hold a tile…"; the empty-board card (35) | A first-run tip card (below), and a help button in the Log nav area if the Stage 7 layout has room; otherwise the Settings help |
| Settings, board layout | Placing and changing a type; empty positions | The hint under the board; "Change error type" (32); empty positions as buttons (36) | Help button: "Setting up the board" (placing, moving, changing, what Other holds) |
| Error types list | That the list is the way to add a type; what "on the board" means | The "Added to the board at position N" banner (39) | One line at the top of the list ("Types with a position are on the board. The rest are in Other.") and the same help |
| Note templates and the editor | Question, Line in the log, Insert answer | The plain words and the Example card (4.13a) | Help button: "Note templates", with one worked example |
| Today's details | What unit, environment, version and commit are for; changing the version mid-day | Field labels | Help button: "Today's details" |
| Import or export, import errors | What a setup file is; how to fix a refused file | The messages in file order (31) | A "How to fix a setup file" row that opens the setup-file page of the guide; a help button on Import or export |
| Review | Swipe to edit or delete; what Share sends | Row actions | Help button: "Review and sharing" |
| History, New day | What happens to an unshared log | The prompt's own text | No button; the Settings help covers them |
| Settings | Where all help is | — | A "Help" group: Quick help (the in-app topics as a list), User guide (online), Support (online), Privacy (online) |

"Help button" means one shared component in the nav bar's trailing slot of that page (a question-mark `Icon` in a
44 pt target), opening a `HelpSheet` with the topic. One component and one topic list (`HelpTopics`, a QML singleton of
id, title, paragraphs and guide anchor) so the Settings list and each button show the same text.

### The first-run tip

A dismissible information card above the board on the Log screen, in the `Banner` information tone, until the user
dismisses it or has logged on three days, whichever comes first. Three short lines: "Double-tap a tile to log it.",
"Hold a tile to log it with a note.", "Undo is on the message that appears.", and a Close button. It takes the space
of no tile (the board shifts down within the layout's spare height, or the card replaces the Last bar until the first
entry: the mock-ups decide). It is never shown again after Close, is stored in the app's settings (not the log), and is
reset by nothing except deleting the app. It is not shown on the empty-board state, which has its own card. This is
decision 5.

### Mock-ups first

Package 10.5 draws, in `docs/design/screens/`, light and dark like the rest, and with `tools/visual/run_capture.py`
pairs:

| File | Screen |
|---|---|
| `HelpSheet` | A help sheet (the board layout topic) with "Read more in the guide" |
| `HelpSheetLarge` | The same at the largest text size |
| `SettingsHelp` | The Settings list with the Help group, and the Quick help list |
| `LogFirstRunTip` | The Log screen with the tip card, and after Close |
| `ErrorTypesHelp` | The Error types list with its top line and help button |
| `ImportErrorsHelp` | Import refused, with the "How to fix a setup file" row |

The owner approves them (and the facilitator, as for 4.2) before 10.6 writes QML. The design README's screen table
gets rows 40 onwards.

## Keeping the docs in step

| Check | What it does | Where |
|---|---|---|
| `check_setup_doc.py` | Reads every key list the parser accepts (`reject_unknown` sets and top-level fields in `configuration.cpp`) and the limits (`MAX_NAME`, `MAX_PREFIX`, `MAX_PATTERN`, `MAX_ICON`, `CONFIG_MAX_*`, `CONFIG_BOARD_POSITIONS`, `CONFIG_SCHEMA_VERSION`), and fails when a key or a limit is not in the reference tables of `guide/setup-file.md`, or the page documents a key the parser does not accept. | `tools/ci/`, in `precheck.py` and the Policy job |
| `config.guide_example` | A core unit test that takes the JSON block of `guide/setup-file.md`, imports it with `import_configuration` and expects no errors, and that it equals `config/sample-config.json` after `export_configuration`. | `libs/core/tests`, label `unit` |
| `config.guide_messages` | Every `ConfigError::Problem` and `Code` has a row in the page's message table (by its enumerator name in an HTML comment), so a new message cannot ship undocumented. | Same test file |
| MkDocs strict build | Broken internal links and anchors fail the pull request (`strict: true`, already on). | `docs.yml` |
| `check_help_links.py` | Every guide anchor the app opens (`HelpTopics`, the import-errors row, the Settings rows) exists as a heading in the built site's pages, and every in-app help topic has a guide section. | `tools/ci/` |
| `help.topics` (UI) | Every screen in the entry point table has its help button or line, with an accessible name, and its sheet opens and closes; the tip shows once and stays closed after Close. | `apps/app/tests/ui`, label `ui` |
| `check_guide_screenshots.py` | The guide's images, `screenshots.json` and `ej_screenshots`' screen list agree. | `tools/ci/` |
| Confidential terms | Already covers `docs/`; it now also runs over `docs/guide/screenshots.json` and alt text. | Existing |

And `AGENTS.md`'s table gets rows:

| You change | Also update |
|---|---|
| A user-visible behaviour | The guide page for that screen, and its screenshots (as well as the changelog fragment) |
| The setup-file format in `libs/core` | `docs/guide/setup-file.md` (the checks fail until it is) |
| What the app stores, sends or backs up | `docs/privacy.md` |
| In-app help text | The matching guide section |

The pull request template's checklist gets "Guide updated, or not user-visible".

## Work packages

| WP | Title | Contents | Depends on | Can start |
|---|---|---|---|---|
| 10.1 | Site structure, privacy and support | `mkdocs.yml` navigation (guide first, "For developers" after), `docs/index.md` as the guide home, `docs/developers.md`, the guide's structure as an unpublished outline (`docs/guide/outline.md`, so no empty page goes public), `privacy.md` and `support.md` in final words, the `AGENTS.md` rows. As built, the URL constant in the app moved to 10.6, where the first link to these pages is made, so 10.1 is docs only and needs no changelog fragment. | Owner decisions 1 to 4 | Now |
| 10.2 | Setup-file reference and its checks | `guide/setup-file.md` in full, `check_setup_doc.py` with its tests in `tools/tests/`, `config.guide_example` and `config.guide_messages`. | 10.1 | Now |
| 10.3 | Screen pages | Getting started, Setting up the board, A day of logging, Today's details, Sharing and the next day, History, with screenshots, `screenshots.json`, `guide_screenshots.py`. iPad and Android notes as Stages 8 and 9 land. | Stage 7 fixes merged; `ej_screenshots` | After Stage 7 |
| 10.4 | FAQ and troubleshooting | Both pages, seeded as above, with the Stage 7 findings. | 10.2, 10.3 | After Stage 7 |
| 10.5 | In-app help mock-ups | The six mock-ups, light and dark, and the design README rows. | Stage 7's final screens | After Stage 7; owner approval before 10.6 |
| 10.6 | In-app help | `HelpTopics`, `HelpButton`, `HelpSheet`, the Settings Help group, the per-screen buttons and lines, the first-run tip, `check_help_links.py`, the `help.*` UI tests, `docs/testing.md` for any new label use. A changelog fragment. | 10.5 approved; 10.3 for the anchors | After 10.5 |
| 10.7 | Screenshot check | `check_guide_screenshots.py` and the release-checklist step. | 10.3 | With 10.3 |
| 10.8 | Exit gate | The checklist below, run after the first release to `main` that contains 10.1 to 10.7. | All | Last |

10.1 and 10.2 do not wait for Stage 7 and land first, so Stage 11 has its URLs and the reference is right whatever
the screens become. 10.1 is a docs pull request; 10.2 touches `libs/core/tests` and `tools/`, so it runs the
`linux-debug` and `linux-asan-ubsan` presets.

## Test list

| Name | Label or kind | What passes |
|---|---|---|
| `check_setup_doc` | Policy check, with `tools/tests/test_check_setup_doc.py` | Every accepted key and limit is in the reference; no extra key; the tests cover a missing key, an extra key, a changed limit |
| `config.guide_example` | `unit` | The guide's example imports with no errors and round-trips to the sample config |
| `config.guide_messages` | `unit` | Every `Problem` and `Code` has a row in the message table |
| MkDocs strict build | Docs job | No broken link or anchor, no page outside the navigation that should be in it |
| `check_help_links` | Policy check, with tests | Every anchor the app opens exists; every help topic has a guide section |
| `help.entry_points` | `ui` | Every screen in the entry point table has its help button or line, with an accessible name |
| `help.sheet` | `ui` | A help sheet opens from each button with that topic's title, reads in order, reflows at the largest text size, closes; "Read more" calls the URL seam with the topic's address and opens nothing in the test |
| `help.settings_group` | `ui` | The Help group lists the topics and the three online rows |
| `help.first_run_tip` | `ui` | Shown on the first day with a board; not on the empty board; gone after Close and after three days of logging; not back after a restart |
| `help.offline` | `ui` | Every help topic's text is in the app (no network seam is called to show it) |
| `dark.all_screens`, `audit.names_complete` | `ui` (4.16) | Extended to the new sheet, group and tip |
| `check_guide_screenshots` | Policy check | Images, list and screen set agree |
| Confidential terms | Policy check | Clean over the guide, the image list and alt text |

## Exit gate

Run after the first release to `main` that contains 10.1 to 10.7:

1. The guide's home, support and privacy pages load at the [public addresses](#hosting) (or the custom domain), over
   HTTPS, on a phone.
2. `check_setup_doc`, `config.guide_example` and `config.guide_messages` pass on that commit.
3. On an iPhone (and an iPad and an Android phone once Stages 8 and 9 are in): every entry point in the table opens its
   help, offline (aeroplane mode); "Read more" and the Settings rows open the right guide page.
4. The facilitator reads Getting started and A day of logging and says what is unclear; that is fixed or the owner
   defers it.
5. The privacy page is checked against the code once more by the owner (storage, backups, no network), and Stage 11's
   App Privacy and data-safety answers match it.

## Decisions for the owner

1. **Hosting.** One MkDocs site on this repo's Pages, guide at the root and developer pages under "For developers"
   (recommended), or the guide alone on the site and the developer pages left in the repository only.
2. **When the site publishes.** On a release to `main`, so the guide matches the released app (recommended), or from
   `develop` until the first public release, so the support and privacy URLs are live sooner. The second changes
   `docs.yml` (a workflow change, `docs/ci.md`).
3. **Custom domain.** None, the `github.io` address (recommended for now), or a domain the owner holds. To be fixed
   before Stage 11 submits.
4. **Support contact.** An email address the owner gives (shown on the support page), GitHub issues, or both. Apple
   expects a way to contact the developer on the support page; facilitators may not have GitHub accounts, so an email
   address is the better primary.
5. **The first-run tip.** On (recommended: one tap logs nothing now, and the hint only appears after the tap that did
   nothing), or off, relying on the single-tap hint and the Settings help.
6. **Language.** English only for v1 (recommended); the guide and help strings are `qsTr` and Markdown, so a translation
   can follow.

## Assumptions to confirm

- `ej_screenshots` (4.16) exists by the time 10.3 starts, and can render a chosen state (a toast showing, a sheet open).
  If not, 10.3 adds the states it needs to the tool.
- The About page (PR #130) lands with the URL seam the help's "Read more" uses (`Qt.openUrlExternally` through the
  shell). If it does not, 10.6 adds that seam.
- The reusable docs workflow of `icl-common-infra` builds `mkdocs.yml` at the repository root and publishes `site/`;
  10.1 confirms that moving `index.md` needs no change there.
- Whether the app's data is in the device backup on iOS and Android, for the privacy page (see above).
