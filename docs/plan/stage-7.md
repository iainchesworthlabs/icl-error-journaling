# Stage 7: screen and flow review

Method, checklist, test list and exit gate for Stage 7 of the [implementation plan](../implementation-plan.md) ("Stages 7
to 12"). Requirement IDs refer to the [PRD](../prd.md). The stage takes in Stage 4 package 4.16, the dark-mode and
accessibility audit and the `ej_screenshots` tool ([stage-4.md](stage-4.md#416-dark-mode-pass-accessibility-audit-and-screenshots)).

**Exit gate:** every flow below walked on an iPhone 17 in light, dark and large text, with VoiceOver for the flows marked
so; every finding fixed or deferred by the owner; the `ej_screenshots` artifact is produced on `develop` and the owner
has compared it with the mock-ups.

**Status:** approved by the owner (PR #137, 9 Nov 2026). The owner's decisions on F3, F19, F20 and F21 are recorded in
[Owner decisions](#owner-decisions-10-nov-2026).

Stages 8 (iPad), 10 (documentation) and 11 (store release) build on the screens this stage settles, so the findings are
also written for them: a finding that only matters to one of them says so in the "Feeds" column.

## What the stage covers

Every screen, sheet and alert of the app, in these flows. Each flow is one walk, on the desktop rig first (screenshots and
tests) and then on the phone.

| # | Flow | Screens, sheets and alerts | PRD |
|---|---|---|---|
| W1 | First launch, import | FirstLaunch, file chooser, ImportConfirm, ImportErrors, Log | PRF-08, PRF-09 |
| W2 | First launch, by hand | FirstLaunch, FirstLaunchManual (About you), LogEmptyBoard | PRF-01, PRF-02, PRF-09 |
| W3 | Board setup | Settings board, BoardSettingsEmpty, BoardTypePicker, ErrorTypeNewOnBoard, BoardLayoutSelected, ErrorTypes, ErrorTypesAdded | PRF-04, PRF-05, BRD-08 |
| W4 | Today's details | Header, EnvironmentSheet, Location Other, Version and Commit, HeaderNewLogRow, StartNewLogConfirm | HDR-01 to HDR-09, DAY-02 |
| W5 | A day of logging | Main, tap and double-tap to log, hold to log with a note (both paths), UndoToast and its ×N, Other picker (search, no match, add), note composer, templates (TemplateValue), LogBanner, WriteFailed, Recovery | BRD-01 to BRD-08, NOTE-01 to NOTE-06, HDR-06, DS-04 |
| W6 | Version change | ChangeVersion, VersionTimeSheet, TimeTumbler | HDR-07, HDR-09, DAY-03 |
| W7 | Review | Review, swipe Copy and Delete, Copy whole log, EditEntry, EditTimeSheet, TimeTumbler12 | REV-01 to REV-04, NOTE-02 |
| W8 | Share, then the next day | Share log, share sheet (device), shared state, NewDay, Keep / Share first / Start | REV-05, DAY-01 |
| W9 | History | History, HistoryEmpty, PastLogReview, Share again, DeleteLog, changed since sharing | DAY-04, DAY-05 |
| W10 | Settings and its editors | Settings list, Facilitator page (unit prefix, totals), Time format, Locations, Note templates, TemplateEdit, TemplateEditInvalid | PRF-01 to PRF-07, PRF-10, PRF-11 |
| W11 | Import and export from Settings | ImportExport, export to Files, ImportConfirm, ImportErrors | PRF-08 |
| W12 | About | The About page of PR #130 (version, commit, licence, components) | NFR-09 |

Each screen is checked for the eight things the owner asked for:

| Check | Desktop evidence | Phone evidence |
|---|---|---|
| Clarity and discoverability: can a first-time user find it without being told | The walk, by reading each screen cold | Owner's walk; facilitator's comments |
| Wording: plain, consistent, the same word for the same thing on every screen, no internal terms (`{value}`, `T|` explained or hidden) | String sweep of `qsTr` in `apps/app/qml` and the model messages | — |
| Consistency with the approved mock-ups in `docs/design/screens/` | `tools/visual/run_capture.py` pairs | V1, V2 of the Stage 4 checklist |
| Dark mode | Dark pairs; `dark.all_screens` | D1 |
| Large text | `ej_screenshots` large mode; `layout.*` tests | T1, V4 |
| VoiceOver | `audit.names_complete`, `app.accessibility` | A1 to A5 |
| Keyboard: the right keyboard, Return does the expected thing, the field is above the keyboard, focus moves on | `app.keyboard_*` tests | K1 |
| Error states: every failure the screen can meet has a message that says what happened and what to do | The rig's failing store; import error fixtures | Write failure is not reproducible on a phone; the banner is checked in the rig only |

## Method

1. **Screenshots.** `ej_screenshots` (below) renders every screen in light, dark and large text. For the screens that
   have a mock-up, `tools/visual/run_capture.py` also renders the mock-up beside the app. The owner gets the artifact of
   each CI run and the pairs in the pull request of each fix.
2. **Desktop walk.** I walk each flow in the rig, screen by screen, against the PRD row and the mock-up, and record
   what I find in the [findings table](#findings). The rig is the real shell over the real Stage 3 services, so the
   flows are the app's own, but the fonts, the keyboard and the share sheet are not the phone's.
3. **Phone walk.** The owner walks the same flows on an iPhone 17 with the TestFlight build, using the
   [device checklist](#device-checklist-the-exit-gate). Findings from the phone go into the same table.
4. **Record.** Each finding the owner accepts becomes a GitHub issue labelled `v1` and `ui` (and `accessibility` where it
   is one), titled "Stage 7: …", and gets a row in the table with the issue link. A finding the owner defers is labelled
   `v2` instead and the row says "deferred". Issues are opened after the owner has read this list, so that a finding the
   owner rejects never becomes an issue.
5. **Fix.** Fixes are small pull requests against `develop`, each with its test and, for a visible change, the capture
   pair before and after. A fix that changes a screen's layout or words updates the mock-up first (as #112 and #115 did),
   and the owner approves the mock-up before the code.

## The screenshot tool (`ej_screenshots`)

What 4.16 asked for, built on the capture rig that already exists (`apps/app/tests/ui/visual_capture_test.cpp`), which
drives the real shell into 39 mock-up states.

- **Screen list.** A file, `tools/ej_screenshots/screens.json`, names every screen state to render, the flow it belongs to
  and the scene that builds it. It is the 39 mock-up states plus the screens that have no mock-up: EnvironmentSheet,
  VersionTimeSheet, EditTimeSheet, the hold-to-log Edit sheet, the Other picker with a search and with no match, the
  Facilitator page, Time format, SetupFile, the About page and the double-tap states once they are merged.
- **Modes.** Each state in three modes: `light`, `dark`, and `large` (light, at a text scale of 3.1, the largest iOS
  accessibility size). The size and the 402 × 874 pt frame of an iPhone 17 are fixed; the scale factor is 2.
- **Program.** A visible Catch2 case, `screenshots.render`, labelled `ui`. It writes the PNGs into `EJ_SCREENSHOT_DIR`
  when that is set and into a temporary folder when it is not, so ctest runs it on every PR (it then only checks that
  every state renders). `tools/ej_screenshots/ej_screenshots.py` runs it and writes an `index.html` that groups the PNGs
  by flow, one row per state and one column per mode.
- **CI.** The Linux UI test job sets `EJ_SCREENSHOT_DIR` and uploads the folder as the artifact `screenshots` (kept 14
  days), on every pull request and on `develop`. The PNGs are not golden files: fonts differ between the runner and the
  phone, so nothing is compared pixel for pixel.
- **Not forgotten.** The test fails when a `.qml` file under `apps/app/qml/screens/` (other than `private/` and
  `launch/`) is not named by any state in `screens.json`, so a new screen cannot ship without a screenshot.
- `tools/visual/run_capture.py` stays for the side-by-side mock-up pairs (it needs Edge, so it runs on the developer
  machine, not in CI).

## Tests

New tests (label `ui` unless stated). Each fix adds its own test as well, named in its issue.

| Test | What it checks | Package |
|---|---|---|
| `screenshots.render` | Every state of `screens.json` renders in the three modes; every screen file is named by a state | 7.1 |
| `dark.all_screens` | Each state loads in the dark scheme, every colour on screen is a `Theme` colour, and text contrast passes the `contrast_test` rule | 7.2 |
| `audit.names_complete` | The accessibility walker over the full-flow scenario (W1 to W11): no unnamed interactive item at any step, and sheets and alerts hold focus | 7.2 |
| `layout.board_fits` | The Log screen at text scales 0.82, 1.0, 1.3, 1.76 and 3.1, with and without the header banner and the toast: all 12 tiles are at least 88 pt square and wholly above the tab bar (NFR-02, V4) | 7.3 |
| `layout.footer_clear` | On every page with a pinned button (Today's details, Software version, Edit entry, Review, PastLogReview), the end of the scrolled content can be scrolled clear of the button | 7.4 |
| `large.no_lost_labels` | At 3.1 no button, swipe action or row value has an elided label of no visible characters | 7.4 |

## Findings

First pass, 9 Nov 2026, from the desktop captures of `develop` at `92080f9`: 39 mock-up pairs in light and dark, and the
same states at text scales 1.76 and 3.1. Not yet looked at: the double-tap tiles (`feature/double-tap-to-log`, not merged),
the About page (#130, not merged), the screens with no capture state (EnvironmentSheet, VersionTimeSheet, the Facilitator
and Time format pages, SetupFile, the Other picker's search), and anything that needs the phone (VoiceOver, the keyboard,
the share sheet, haptics).

Severity: **high** blocks a v1 requirement or loses information; **medium** a real user will trip on it; **low** polish.
"Decision" means the owner chooses before anything is changed.

| ID | Flow | Screen | Finding | Check | Severity | Feeds | Issue |
|---|---|---|---|---|---|---|---|
| F1 | W5 | Log | At text scales of 1.76 and above, the header card, the template chips and the composer grow, and rows 3 and 4 of the board fall under the tab bar. The Log screen does not scroll, so those tiles cannot be reached. NFR-02 and V4 ask for all 12 tiles at every text size. | Large text | high | 8 | [#154](https://github.com/iainchesworthlabs/icl-error-journaling/issues/154) |
| F2 | W5 | LogBanner | At the default text size, with the "Not filled in yet" banner shown, the bottom row of tiles is cut by the tab bar. The mock-up has the same fault. | Mock-up, clarity | high | 8 | [#155](https://github.com/iainchesworthlabs/icl-error-journaling/issues/155) |
| F3 | W5, W4 | LogBanner | The banner lists Commit as not filled in, but the commit is optional (HDR-09: "Leave it empty if you can't get it"). Someone who cannot get the commit has the banner, and F2, all day. Decision: drop Commit from the banner, or keep it. | Clarity | decided: no change | | none (no change) |
| F4 | W10 | Settings | The unit prefix and the "Include a totals line" switch are on the page behind the Facilitator row, and no row in Settings names them. Nobody looking for the unit prefix would open Facilitator. Needs a mock-up: a "Unit prefix" row and an "Export" (totals) row, or a renamed "You and your log" row. | Discoverability | medium | 10 | [#156](https://github.com/iainchesworthlabs/icl-error-journaling/issues/156) |
| F5 | W2 | About you | The field label is the literal text "UNIT prefix", and the field shows a grey placeholder "UNIT", where the mock-up shows the value "UNIT-". A public user does not know what "UNIT" means. Label it "Unit prefix" and show the example as an example ("For example UNIT"). | Wording, mock-up | medium | 10, 11 | [#157](https://github.com/iainchesworthlabs/icl-error-journaling/issues/157) |
| F6 | W5, W11 | TemplateValue, ImportErrors | 4.13a replaced `{value}` with "[answer]" in the editor, but the footnote of the template question sheet still showed `{value}` ("as "Played {value}""). The import error messages keep `{value}` on purpose: they tell someone editing the setup file what to type. | Wording | medium | 10 | [#158](https://github.com/iainchesworthlabs/icl-error-journaling/issues/158) |
| F7 | W6, W4, W7 | Software version, Today's details, Edit entry | Content that scrolls under a pinned button is cut by it with no space below: the last line of the Software version sheet ("Entries after it use …") is half hidden, a check mark of Today's details shows below Start logging, and at large text the "In the shared log" heading of Edit entry is under Delete entry. | Mock-up, large text | medium | | [#159](https://github.com/iainchesworthlabs/icl-error-journaling/issues/159) |
| F8 | W7, W9 | Review, History | At text scale 3.1 the swipe actions (Copy, Delete, Share) show as coloured blocks with no words. | Large text | medium | | [#160](https://github.com/iainchesworthlabs/icl-error-journaling/issues/160) |
| F9 | W5 | Log | At large text the undo toast grows to cover most of the board (all of it at 3.1) for 5 seconds, so the next tap cannot be made. The toast text needs a cap or a one-line layout. | Large text | medium | | [#161](https://github.com/iainchesworthlabs/icl-error-journaling/issues/161) |
| F10 | W5 | Other | "Add a new error type" comes after every type on the board, so with a full board it is off the screen; the mock-up has it under a short list. It should be findable without scrolling (at the top, as in BoardTypePicker's "New error type…", or as a "No match? Add …" row while searching). | Discoverability, mock-up | medium | 10 | [#162](https://github.com/iainchesworthlabs/icl-error-journaling/issues/162) |
| F11 | W3 | BoardTypePicker, ErrorTypeNewOnBoard | In the capture the tab bar is drawn over the lower part of the Settings sheets. This may be the capture's own repaint of the tab bar (`UiRig::repaint_over`) and not the app; to confirm on the phone before anything is changed. | Mock-up | medium, to confirm | | [#163](https://github.com/iainchesworthlabs/icl-error-journaling/issues/163) |
| F12 | W7 | Review | Nothing says the rows can be tapped or swiped, and the copy button at the top is an icon with no word. Low because Edit entry also holds Copy and Delete; a first-use hint or a "Copy log" label would help. | Discoverability | low | 10 | [#164](https://github.com/iainchesworthlabs/icl-error-journaling/issues/164) |
| F13 | W6 | Software version | At 3.1 the Commit label and its value run into each other and the value loses its first characters. | Large text | low | | [#165](https://github.com/iainchesworthlabs/icl-error-journaling/issues/165) |
| F14 | W8 | NewDay | At 3.1 the alert's buttons are below the fold of the alert; it scrolls, but nothing shows that it does. | Large text | low | | [#166](https://github.com/iainchesworthlabs/icl-error-journaling/issues/166) |
| F15 | W3, W10 | Error types, Locations | Wording: the list heading "On your board and in Other · 11" is hard to read; the Locations footnote starts "The Today's details list shows these …". | Wording | low | | [#167](https://github.com/iainchesworthlabs/icl-error-journaling/issues/167) |
| F16 | W11 | ImportExport | The note says "Files or AirDrop", which is iPhone only. (The file is already called `cs-bingo-board-setup.ejconfig`; the old name came from the capture stub's file chooser.) | Wording | low | 9, 11 | [#168](https://github.com/iainchesworthlabs/icl-error-journaling/issues/168) |
| F17 | all sheets | Other, Edit entry, Software version, NewDay | In light mode the scrim behind sheets and alerts is a light grey; the mock-ups use a dark slate. Dark mode matches. | Mock-up | low | | [#169](https://github.com/iainchesworthlabs/icl-error-journaling/issues/169) |
| F18 | W5 | LogEmptyBoard | The Last bar says "No entries yet" with no count; the mock-up shows "0 today". | Mock-up | low | | [#170](https://github.com/iainchesworthlabs/icl-error-journaling/issues/170) |
| F19 | W1, W2 | FirstLaunch | For a public release most people have no setup file, but "Import a setup file" is the main button and nothing says where a file comes from. Decision: keep the order, swap it, or add a line ("Someone in your team may have one"). | Clarity | decided: swap, 7.7 | 10, 11 | [#171](https://github.com/iainchesworthlabs/icl-error-journaling/issues/171) |
| F20 | W5, W7 | Log, Review | The export's `T|` marker is shown on the Last bar and every Review row with no explanation. It is in the approved mock-ups. Decision: keep (it is what the downstream ticket indexer reads, and Review shows the export as it is, REV-01), or hide it on the Log screen. | Clarity | decided: keep, add a preference, 7.10 | 10 | [#172](https://github.com/iainchesworthlabs/icl-error-journaling/issues/172) |
| F21 | W10 | Settings | Unit environments (PRF-11) can only be changed through a setup file; there is no editor. PRF-11 allows this. Decision: add an editor, or leave it to the file and the guide. | Discoverability | decided: add the editor, 7.11 | 10 | [#173](https://github.com/iainchesworthlabs/icl-error-journaling/issues/173) |

Already known and not repeated here: the mock-up's Date line on Today's details has the year and the app's does not; the
mock-ups use two different Settings icons; the app's version line under Settings, which the About page (#130) replaces.

## Work packages

Each package is one pull request into `develop`, squash merged once its checks pass.

| # | Package | Findings | Needs |
|---|---|---|---|
| 7.1 | `ej_screenshots`, `screens.json`, `screenshots.render`, CI artifact; `docs/ci.md` and `docs/testing.md` | — | This page approved |
| 7.2 | `dark.all_screens`, `audit.names_complete`; fixes for anything they find | — | 7.1 |
| 7.3 | The board fits: the Log screen's chrome capped (header card two lines, chips one scrolling row, composer one line) so the board keeps its height; banner layout; toast cap; `layout.board_fits` | F1, F2, F9 | Mock-up for the capped header and the banner (`LogBanner`, `TileLabelsLarge`) |
| 7.4 | Pinned footers and large-text clipping; `layout.footer_clear`, `large.no_lost_labels` | F7, F8, F13, F14 | — |
| 7.5 | Wording: `[answer]` everywhere, the unit prefix label, the Error types heading, the Locations footnote | F5, F6, F15, F16 | — |
| 7.6 | Settings and Other discoverability: unit prefix and totals rows, "Add a new error type" placement | F4, F10 | Mock-ups (`BoardSettings`, `Other`) |
| 7.7 | First launch: "Set up by hand" becomes the main button at the top and "Import a setup file" moves to the bottom; and F12, F17, F18 | F19, F12, F17, F18 | Mock-up (`FirstLaunch`, light and dark) |
| 7.8 | Second pass: the double-tap tiles, the hold sheet, the About page, and the screens with no capture state | new rows | double-tap and #130 merged |
| 7.10 | The error prefix preference (below) | F20 | PRD change in this page's pull request; mock-up of the Settings row and its page |
| 7.11 | The unit environments editor (below) | F21 | Mock-up of the editor |
| 7.9 | Device walk with the owner and the gate record at the end of this page | phone rows | A TestFlight build with 7.3 to 7.8 |

7.1 and 7.2 come first, so that every later package can show its screens. 7.3 to 7.6 can run in parallel; they touch
different files except 7.3 and 7.6, which both touch Settings strings and go one after the other.

## Device checklist (the exit gate)

On an iPhone 17 with the TestFlight build. For each flow W1 to W12: light, dark, and large text (Settings > Accessibility
> Display & Text Size > Larger Text, the largest size). Recorded as device, iOS version, build, result.

| # | Check | Flows |
|---|---|---|
| G1 | Each flow walked in light mode, with no finding left open | W1 to W12 |
| G2 | The same in dark mode, and switching appearance while the app is open | W1 to W12 |
| G3 | The same at the largest text size: nothing cut, every control reachable, all 12 tiles visible | W1 to W12 |
| G4 | VoiceOver: W2, W5, W7 and W9 by VoiceOver alone (Stage 4 A1 to A5) | W2, W5, W7, W9 |
| G5 | Keyboard: the number pad for the unit, Return in the note adds it, the field stays above the keyboard on every sheet | W2, W4, W5, W6, W7, W10 |
| G6 | F11 checked: the tab bar is not over the Settings sheets | W3 |
| G7 | The share sheet and AirDrop of a setup file, start to end | W8, W11 |

## Owner decisions (10 Nov 2026)

The owner approved this page and the findings by merging PR #137, and decided:

- **F3: no change.** The banner keeps listing Commit while it is empty. F2 (the banner pushing the board under the tab bar)
  is still fixed in 7.3, so the banner costs no tiles.
- **F19: swap the order.** On the first-launch screen, "Set up by hand" is the main button at the top and "Import a setup
  file" goes to the bottom. The mock-up `FirstLaunch` changes first (7.7).
- **F20: keep `T|` on the Log screen, and make it a preference.** The Last bar and Review show the line as the export
  writes it (REV-01), so they follow the preference. See [The error prefix](#the-error-prefix-710).
- **F21: add the editor.** See [The unit environments editor](#the-unit-environments-editor-711).
- **The large mode of `ej_screenshots`** was not decided; it uses one size, 3.1, as proposed, and a second size can be
  added to `screens.json` later without code.

### The error prefix (7.10)

A new preference, PRF-13 in the PRD (changed in the pull request that records these decisions). Proposed details, for the
owner to correct in review:

- **What it is.** The text in front of the error type name on each error line of the export: `10:05 T| Wrong Response`.
  The default is `T|`. The space after it is part of the line format, not of the preference.
- **Rules.** At most 12 characters, no line break and no tab; leading and trailing spaces are trimmed. It may be empty, and
  then an error line is `10:05 Wrong Response`.
- **Where it applies.** Every error line the renderer writes: the export, the share, Copy line and Copy whole log, the Last
  bar, Review, PastLogReview, the line previews of Edit entry, and past logs, which re-render as PRF-07 already does for the
  time format. Notes, templated notes and header change lines have no prefix, as now.
- **Settings.** A row "Error prefix" with the current value, on the same page as the totals line (F4 decides where that
  page is). The page says what the prefix is for and shows an example line; a Reset button puts `T|` back. A note on the
  page says that a tool that reads the shared log (the downstream ticket indexer) may need the default.
- **Setup file.** An optional key `error_prefix` under `preferences`. A file without it keeps the default; the export
  writes it only when it differs from the default (as `unit_environments`). A file with the key is refused by a build
  that does not know it (the configuration reader rejects unknown keys), which is acceptable because every user updates
  through the store.
- **Code.** `ERROR_MARKER` in `libs/core/include/error_journaling/export_format.hpp` stops being a constant: the renderer
  takes the prefix from the preferences, and `TimelineModel` uses the same value wherever it splits a line for display.
- **Tests.** Renderer goldens with the default, an empty prefix and a custom one; configuration import and export round
  trip, refusal of a too-long or multi-line value; `SettingsModel` set and reset; a QML test of the page; past logs
  re-rendered after a change.
- **Changelog.** A fragment, `error-prefix.added.md`.

### The unit environments editor (7.11)

PRF-11 changes from "the setup file can replace the list" to "the list is edited in Settings and the setup file can
replace it". Proposed:

- **Where.** A row "Unit environments" in Settings, next to the unit prefix row of F4, showing the count ("2").
- **Editor.** The pattern of the Locations editor: a field and Add at the top, the list with drag handles, Move up and
  Move down for VoiceOver, and remove. The rules are PRF-11's: lower-case letters, digits and dashes, at most 20
  characters, not starting or ending with a dash, none repeated, at least one in the list. Upper case is lowered as it is
  typed; anything else is refused with a plain message under the field.
- **Removing one in use.** A log whose unit already has that environment keeps it; the picker no longer offers it.
- **Tests.** `SettingsModel` add, rename, reorder, remove and the refusals; the editor's QML test with the accessibility
  walker; export of a changed list.
- **Mock-ups first:** `UnitEnvironments` (light and dark) and the Settings rows.

## What cannot be verified here

The development machine has no Mac and no phone. The captures use Inter, not SF Pro, the software renderer, and a stub
file chooser and share sheet; there is no keyboard in them. VoiceOver, the real keyboard, haptics, the share sheet and the
look on the phone's display are checked only in the device walk (G1 to G7).
