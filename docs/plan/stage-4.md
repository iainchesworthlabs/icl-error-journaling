# Stage 4: QML UI

Task breakdown and test list for Stage 4 of the [implementation plan](../implementation-plan.md). Each work package is
one pull request into `develop`. Requirement IDs refer to the [PRD](../prd.md). The screens are in the
[design package](../design/README.md); the view models the screens bind to are in [Stage 3](stage-3.md); the haptics,
share sheet and launch screen that touch the same files are in [Stage 5](stage-5.md) and the [spikes](spikes.md).

**Exit gate:** side-by-side review of each screen against the design on an iPhone 17, in light and dark mode, signed off
by the tester.

**Status:** approved by the owner on 2026-11-07 (every recommendation accepted). `AGENTS.md` forbids UI code until the
Stage 2 exit gate has passed, so only package 4.2 (design documents) may start before the gate.

## Scope and conventions

Stage 4 turns the eight approved screens, plus the screens the design package marks as not yet drawn, into custom
Qt Quick components bound to the Stage 3 view models. QML holds presentation only: every rule, every string of export
wording and every piece of time arithmetic is in C++ (Stage 3), and where this page finds a screen that needs a rule the
view models do not offer, it lists the gap ([Gaps in Stage 3](#gaps-in-stage-3-found-while-writing-this-page)) instead of
putting the rule in QML.

- **Layout.** All QML is under `apps/app/qml/`, in one module (`ErrorJournalingApp`, as today), in three folders:
  `theme/` (the token singleton and the text scale), `components/` (one file per component, no knowledge of any view
  model) and `screens/` (one folder per screen; a screen takes the view models it needs as `required property`
  values and is the only layer that knows their names). `Main.qml` is the shell and is written last (4.15). Files are
  `PascalCase.qml`, as Qt requires for types. QML ids, properties, signals and functions are camelCase.
- **Services reach QML as required properties,** never as context properties or global singletons: the root window
  declares `required property` for `session`, `undo`, `board`, `picker`, `timeline`, `history`, `day`, `settings`, `share` and
  `bridge`, `Main.qml` hands each screen only what it uses, and a test creates a screen with whatever it wants to inject
  (decision 3). `AppRoot` (Stage 3, 3.11) supplies them; the names are those of the sketches in `stage-3.md`.
- **CMake.** The QML files are added with `file(GLOB_RECURSE ... CONFIGURE_DEPENDS qml/*.qml)` into the existing
  `qt_add_qml_module`, and the UI tests with `file(GLOB ... CONFIGURE_DEPENDS)` into one test executable `ej_ui_tests`
  in `apps/app/tests/ui/`, as Stage 3 did for `ej_viewmodel`. A package therefore adds its files without editing a shared
  `CMakeLists.txt`. The one file that needs a per-file property (the `Theme` singleton) is set up in 4.1.
- **Controls.** Components are built from plain Qt Quick items and from `QtQuick.Controls.Basic` types that are fully
  re-skinned (custom `background` and `contentItem`), never from a platform or default style (NFR-05). The Basic
  controls are used where they supply what a custom item would have to rebuild: text input behaviour and the keyboard,
  `Popup` and its overlay, `Tumbler`, `StackView`, and the accessibility roles. Every file imports
  `QtQuick.Controls.Basic` explicitly, so no `QQuickStyle` call and no `main.cpp` change is needed (decision 2).
- **No platform tests in QML.** `Qt.platform.os` and its kin are not used. What differs (share available, a document
  picker, a haptic) arrives as a property or a signal of `bridge` or of a Stage 3 object, so one policy check
  (`check_qml_platform.py`, 4.1) can fail a QML file that asks. This is the QML counterpart of the "no `#if`" rule.
- **Strings.** Every visible string is `qsTr()` with `%1` placeholders and no concatenation (NFR-11). A policy check
  (4.1) fails a QML `text:` or `placeholderText:` bound to a bare literal. Strings made in C++ are already `tr()`.
- **Tokens, not numbers.** No colour, size, radius, spacing or duration appears in a component or a screen except as a
  `Theme.*` value. A policy check (4.1) fails a hex colour or a bare pixel literal outside `theme/`.
- **Tests.** C++ is Catch2 (owner decision, Stage 3 decision 3). The QML tests are Catch2 cases too: each loads a
  component or a screen offscreen (`QT_QPA_PLATFORM=offscreen`, software backend, as `app.smoke` already does), injects
  its properties, drives it with synthesised touch and key events and asserts on the item tree. They are labelled `ui`;
  budgets are labelled `perf`. Screens are tested against the real Stage 3 view models over the in-memory repository, the
  `ManualStorageGateway` and `TestClock` of Stage 3 (so a test can look at the screen between "submitted" and
  "committed"); components are tested with plain property values. Qt Quick Test is not used (decision 1).
- **Coverage.** Catch2 does not measure QML. Coverage of the C++ of the rig and helpers is held by the existing floor; the
  QML is covered by the rule that each component and screen has a test file named after it, which a policy check
  enforces (4.1), and by the screenshot set of 4.16.
- **Changelog.** Every package that changes what the tester sees adds a fragment to `changelog.d/`.
- **Fictional data only (NFR-10).** Tests and screenshots use `config/sample-config.json`: Sam Example, UNIT-27,
  North Site, Wrong Response, Mute Delay and the other proposed board types.
- **Licence headers.** Every new file, QML included, carries the SPDX header copied from an existing file.

## Assumptions only a device can confirm

The development machine has no Mac. These are Qt or iOS behaviours the design depends on; each is checked at the stated
place, and the right column is what changes if it is wrong.

| # | Assumption | Checked by | If wrong |
|---|---|---|---|
| U1 | The `SafeArea` attached property of Qt 6.9 and later reports the iPhone 17 insets (59 pt top, 34 pt bottom, Dynamic Island included) and updates on rotation of nothing (portrait only) | First task of 4.1 (printed in a device build); checklist V1 | The shell reads insets from a `bridge` property instead; components use `SafeFrame` only, so the change is one file |
| U2 | Qt's iOS accessibility maps `Accessible.name`, `role`, `description` and `onPressAction` to `UIAccessibility`, so VoiceOver reads and activates a custom item | 4.16 device pass; checklist A1 to A4 | The tile and the rows become `Control`-based types, which carry the mapping |
| U3 | `Accessible` announcements (`QAccessibleAnnouncementEvent`, Qt 6.8 and later) reach VoiceOver on iOS | 4.16; checklist A5 | The toast text is also set as the `Accessible.name` of a focused element and focus moved to it |
| U4 | Qt offers no custom VoiceOver actions (the rotor's "Copy", "Delete") | 4.16 | None: the design never relies on them (see [Accessibility](#accessibility)) |
| U5 | `inputMethodHints: Qt.ImhDigitsOnly` shows the number pad on iOS, and `Qt.inputMethod.keyboardRectangle` gives the keyboard's height so a sheet can lift itself | Checklist K1 | The unit number field uses a validator and the default keyboard; the lift is a fixed fraction |
| U6 | `Qt.styleHints.colorScheme` follows the system appearance live, including while the app is open | Checklist D1 | The bridge gets a colour-scheme property (small, same pattern as 4.3) |
| U7 | iOS runs the app at 120 Hz only when `CADisableMinimumFrameDurationOnPhone` is set in the property list, and Qt Quick animators (not property animations) hold 120 Hz on the render thread | 4.14, checklist M1 | The key is added and the animations use animators; if the render loop still falls short, the sheet is simplified |
| U8 | Dynamic Type is not followed by Qt Quick `Text` by itself, and `UIApplication.preferredContentSizeCategory` is the source | First task of 4.3; checklist T1 | If Qt does follow it, 4.3 shrinks to Reduce Motion alone |
| U9 | Two fingers on two tiles are two independent presses | Checklist P2 | The board uses one `MultiPointTouchArea`; the tile interface does not change |
| U10 | The touch's press timestamp is readable in QML and shares a time base with `NSProcessInfo.systemUptime` | Stage 5, A5 | Only the software interval is measured; no change here |
| U11 | A VoiceOver activation of a tile reaches `Accessible.onPressAction` once, and a double-tap by a finger with VoiceOver off reaches the `TapHandler` as two taps 300 ms or less apart | Checklist A2, P6 | The window (`Theme.motion.doubleTapMs`) is changed; a tile that VoiceOver activates twice is two entries, as it always was |

## Gaps in Stage 3 found while writing this page

The approved screens need five things the Stage 3 page does not give. Each is small. The owner decided (decision 4) to add them to Stage 3, in the addendum at the end of `stage-3.md`, rather than have QML work around them.

1. **Local time to a UTC instant.** The version-change sheet has a time field that defaults to now.
   `LogSession::changeHeader(field, text, atUtcMs)` takes a UTC instant, and turning "14:05 on this log's date in the
   entry's zone" into one is time-zone arithmetic that must not be in QML. Add `changeHeader(field, text, localSeconds)`
   (seconds since local midnight of the log's date, resolved with `TimeZoneProvider::to_utc` exactly as
   `TimelineModel::saveEntry` already does) or an `utcMsForLocalSeconds` invokable. The sheet's "time defaults to now"
   comes from a `nowLocalSeconds` property.
2. **Preview of a header change.** The version sheet shows "the exact log line" before saving. `TimelineModel::previewLine`
   previews an entry that exists. Add `LogSession::previewHeaderChange(field, text, localSeconds)` returning the rendered
   line (or the plain refusal message), built on a scratch copy with `render_entry_line`, so the preview and the saved
   line cannot differ (REV-01).
3. **An entry point for DAY-02.** "A new log can be started at any time from the menu" has no menu in the approved
   screens and no place for it. `LogSession::startNewLog()` exists. The design question is where it lives (decision 8).
4. **Template value prompt data.** NOTE-04 prompts for a value when a chip is tapped. `SettingsModel::templates` must give
   QML the template's id, name, prompt text and the field's input hint as roles; the roles of the four list models are not
   named in Stage 3 ("names as in 3.3"). 3.3 should list them.
5. **Reading the current time as text.** The edit sheet and the version sheet show a time chip ("Now"). Both read it from
   the view models (`nowLocalSeconds`, and the edit sheet's `localSeconds`) and format it with the renderer's time format
   through one invokable, `TimelineModel::formatLocalSeconds(int)`, so QML never formats a time itself (PRF-07).

None of the five blocks 4.1 to 4.6. They block 4.9 (items 1, 2, 4, 5), 4.10 (5), 4.12 and 4.13 (4), and the answer to item 3
decides where one row goes in 4.9.

## Screens and what they bind to

The eight approved screens, then the screens the design package says are not drawn. For every row, the bindings are the
invokables and properties named in `stage-3.md`.

| Screen | Source | PRD | Binds to | Package |
|---|---|---|---|---|
| Log | `Main.dc.html` | BRD-01 to 05, NOTE-01 to 04, HDR-06 | `session` (header text, `missingFields`, `draft`, `recordError`, `addNote`, `addTemplatedNote`, `entryRecorded`, `writeFailed`, `state`, `recoveryNotice`), `board` (roles `typeId`, `label`, `colourSlot`, `count`, `hasCount`, `isOther`, `isEmpty`, `accessibleName`), `undo` (`active`, `text`, `repeatCount`, `generation`, `windowMs`, `undo()`, `dismiss()`), `timeline` (`lastLine`, `entryCount`), `settings.templates` | 4.7 |
| Other picker | `Other.dc.html` | BRD-06 | `picker` (`filter`, `addType`, roles `typeId`, `label`, `section`, `count`, `colourSlot`), `session.recordError` | 4.8 |
| Today's details | `Header.dc.html` | HDR-01 to 06 | `session` (`tester`, `dateText`, `unitText`, `locationText`, `versionText`, `validateHeaderValue`, `correctHeader`), `settings.locations`, `settings.unitPrefix` | 4.9 |
| Software version | `ChangeVersion.dc.html` | HDR-07, DAY-03 | `session.changeHeader`, `correctHeader`, `validateHeaderValue`, and gap items 1 and 2 | 4.9 |
| Review | `Review.dc.html` | REV-01, 03, 04, 05 | `timeline` (rows, `headerText`, `titleText`, `copyLine`, `copyAll`, `deleteEntry`), `undo`, `share` (`available`, `sharing`, `shareOpenLog`, `shareFinished`) | 4.10 |
| Edit entry | `EditEntry.dc.html` | REV-02, NOTE-06 | `timeline` (`entryForEdit`, `previewLine`, `saveEntry`, `deleteEntry`), `picker`/`board` type list | 4.10 |
| New day prompt | `NewDay.dc.html` | DAY-01 | `day` (`promptVisible`, `newDateText`, `openLogDayText`, `openLogEntryCount`, `unshared`, `acceptNewLog`, `keepOpen`, `shareFirst`) | 4.11 |
| Settings and board layout | `BoardSettings.dc.html` | PRF-01 to 07, 10 | `settings` (all properties and invokables), `settings.boardLayout` | 4.12 |
| History (not drawn) | to design, 4.2 | DAY-04, DAY-05 | `history` (rows, `refresh`, `openLog`, `reviewModel`, `requestDelete`, `confirmDelete`), `share.shareLog` | 4.11 |
| Header banner (not drawn) | to design | HDR-06 | `session.missingFields` | 4.7 |
| Template value prompt (not drawn) | to design | NOTE-04 | `settings.templates`, `session.addTemplatedNote` | 4.7 |
| Write-failed and recovery messages (not drawn) | to design | DS-04, NFR-05 | `writeFailed` signals, `session.recoveryNotice`, `session.state` | 4.4 and 4.7 |
| Error type, location and template editors (not drawn) | to design | PRF-03, 04, 06 | `settings` add, rename, move, retire, `validatePattern` | 4.13 |
| First-launch setup and import (not drawn) | to design | PRF-08, PRF-09 | `settings.configured`, `loadImport`, `importPrepared`, `importFailed`, `applyImport`, `discardImport`, `exportTo` | 4.13 |
| Delete-log confirmation (not drawn) | to design | DAY-05 | `history.requestDelete`, `confirmDelete` | 4.11 |
| Dark variants of every screen | to design | NFR-06 | `Theme` | 4.2, 4.16 |

Behaviour rules the screens must follow, taken from the PRD, the design README and the Stage 3 and 5 decisions:

- **A double-tap logs, on the touch of its second tap (4.17a; owner, 2026-11-09).** One tap on a tile with an error type logs
  nothing, because it was too easy to make by accident. Two taps on the same tile no more than `Theme.motion.doubleTapMs`
  (300 ms, from the lift of the first to the touch of the second) apart log one entry, and the tile records it on the touch of
  the second tap, together with the impact haptic and the press animation (Stage 5, decision 1: a finger sliding off cannot
  fire a haptic without an entry). A hold of 500 ms (`Theme.motion.holdMs`) logs too, at the hold (4.17). The count, the
  badge, the "Last" bar and the toast change only after the commit, because the view models change them only then (Stage 3,
  decision 2). The screen therefore calls `bridge.hapticImpact(...)` first and `session.recordErrorEntry(typeId)` second, both
  from the handler of the touch that asks (`Tile.pressed`), and nothing else is in that path (the `perf` tests of 4.5 and 4.7
  guard that, counted from the second tap's touch). The Other tile is a navigation and opens on one tap.
- **No dialog on capture.** A tap opens nothing. A write failure is a banner, not an alert.
- **Confirmation follows the commit.** The toast and the "×N" label come from `undo.text` and `undo.repeatCount`; the
  tile does not count anything itself.
- **The Review text is black and white,** only the tiles are coloured (design README). The timeline rows show
  `timeText` and `lineText`; the screen never rebuilds a line.
- **Edits are on Review, not on Log** (NOTE-02). The edit screen is a sheet with explicit Cancel and Save (REV-02).
- **Destructive actions are behind a swipe or a separate button,** and the swipe's result is undoable through the same
  `undo` toast (REV-03).
- **Empty header fields do not block anything** (HDR-06): the banner lists the fields from `missingFields`; the Log
  screen never disables a tile because of them.
- **Empty board positions are inert tiles** (Stage 3, accepted defaults): `isEmpty` tiles draw as an empty slot, do not
  record and are not read out by VoiceOver as buttons.

## Components

Each component is one file in `apps/app/qml/components/`, takes plain properties and emits signals, and has a test file
`apps/app/tests/ui/<component>_test.cpp`. Every interactive component carries an `Accessible` role and name through
properties, so a screen can set the text. Every component that Stage 5's launch screen needs has a `skeleton` property
that draws shapes only, in neutral colours, with no text, badge or type colour (Stage 5, "Launch screen"): `HeaderCard`,
`NoteComposer`, `LastEntryBar`, `Tile`, `BoardGrid`, `TabBar`.

| Component | Plan row | What it is | Package |
|---|---|---|---|
| `Theme` (singleton) | Design tokens | Colours for light and dark, the 12 tile slots, the type scale, spacing, radii, sizes, motion | 4.1 |
| `SafeFrame` | Safe area | An item that is the window minus the safe-area insets, with a test override | 4.1 |
| `TabBar` | TabBar | Log, Review, History, Settings; 83 pt including the home indicator inset; selected weight; accessible as a tab list | 4.4 |
| `Sheet`, `SheetHost` | Sheet with grabber | A bottom sheet with scrim, grabber, detents, drag-to-dismiss, keyboard lift and spring presentation | 4.4 |
| `Alert` | (Prompt, confirmations) | The 22 pt-radius modal of the new-day prompt and the destructive confirmations | 4.4 |
| `Banner` | (Error and empty states, NFR-05) | An inline message with a tone (information, warning, error), an optional action and a close | 4.4 |
| `NavBar`, `PageStack` | (Settings pages) | Title bar with leading and trailing actions; a push and pop stack with the design's transitions | 4.4 |
| `Tile` | Tile with count badge and press animation | One board tile: type colour slot, label, count badge, `×N` is not here (it is in the toast), press scale 0.96 in 120 ms | 4.5 |
| `BoardGrid` | (Board) | 3 by 4 grid of 12 `Tile`s over `BoardModel`; fixed geometry, never scrolls | 4.5 |
| `UndoToast` | UndoToast | The 52 pt toast: text, remaining-time bar restarting on `generation`, the Undo button, spring in and out | 4.5 |
| `LastEntryBar` | LastEntryBar | The latest line and the count; opens Review | 4.5 |
| `MiniBoard` | MiniBoard for settings | The miniature board: hold and drag to move, tap to swap in a type, plus the VoiceOver alternative | 4.5 |
| `HeaderCard` | HeaderCard | The plum card: unit, site, version, date, tester; opens Today's details | 4.6 |
| `NoteComposer`, `Chip` | NoteComposer with template chips | The text field with Add, the chip row, the 300 ms draft binding | 4.6 |
| `ListRow`, `GroupedList`, `SwipeRow` | ListRow, SwipeRow | 48 pt rows in a 14 pt-radius group; a row that reveals Copy and Delete on swipe | 4.6 |
| `ChoiceCard` | ChoiceCard | The two plain choices of the version sheet, selected state | 4.6 |
| `PrimaryButton`, `SearchField`, `FormField`, `TimeTumbler` | (inputs) | The 54 pt button; the search field; a labelled field with inline validation message; the time input (decision 11) | 4.6 |

### Design tokens (4.1)

`theme/Theme.qml` is a `pragma Singleton`. The values are the ones in `docs/design/tokens.json`; the light values are the
approved ones, the dark values are those designed in 4.2 and added to `tokens.json` (replacing "To be designed in Stage 4").

- `Theme.colour.*` resolve against `Qt.styleHints.colorScheme` (U6) and are the only colours a component uses.
  `Theme.tileColour(slot)` takes the `colourSlot` role of `BoardModel` (0 for Other, 1 to 11), so the type-to-colour rule
  stays in C++ (Stage 3, decision 8).
- `Theme.size.*`, `Theme.radius.*`, `Theme.space.*`, `Theme.font.*` (the type scale) and `Theme.motion.*` mirror the JSON.
- `Theme.textScale` is a number from the bridge (4.3), 1.0 where there is no bridge. `Theme.font.*` return sizes multiplied
  by `Theme.scaled(role)`, which applies the per-role cap in [Dynamic Type](#dynamic-type).
- A test (`theme.matches_tokens`) loads `tokens.json` and the singleton and compares every value, so the two cannot drift.
  The hand-written QML is the implementation and the JSON is the contract for the design.

### Tile (4.5)

```qml
Tile {
    required property string typeId
    required property string label
    required property int colourSlot
    required property int count
    required property bool hasCount
    required property bool isOther
    required property bool isEmpty
    required property string accessibleName
    property bool skeleton: false
    signal pressedDown(real timestamp)    // the touch that asks for the entry (4.17a): the second tap of a double-tap, or a hold
    signal activated()                    // VoiceOver activation, which has no touch-down
    signal held()                         // the finger stayed on the tile for holdMs (500 ms), after pressedDown (4.17)
    signal singleTapped()                 // one tap and no second within doubleTapMs: logs nothing, the screen shows the hint (4.17a)
}
```

(The implementation keeps the names `pressed`, `tapped`, `held` and `singleTapped`; `pressed` is the touch that asks, the
screen connects it to the haptic and then to `recordErrorEntry`.) A `TapHandler` counts the taps of each tile and emits
`pressed` first on the touch of the second tap, or at the end of a hold that nothing has asked for yet. `activated` is the
accessibility press action; the screen routes it through the same two calls, so VoiceOver activation also fires the impact
haptic and records the entry, once: a VoiceOver activation is the screen reader's own double-tap, so it asks for no second.
The press animation is a `ScaleAnimator` (render thread) of 0.96 over 120 ms, started by `pressed` (the second tap's touch, or
the hold; the first tap does not scale the tile) and reversed on release or cancel, and it runs even if the commit is slow. A tile of an empty position has no handler and no accessible
role. The label allows three lines at the default text size and four at larger sizes, shrinks by `fontSizeMode` only down
to a floor, and never elides (NFR-02); a test with the longest configured label guards it.

## Accessibility

NFR-07: VoiceOver labels on all controls, Dynamic Type for list and note text, labels that carry the meaning, tile colours
that avoid red–green pairs and keep text readable. NFR-01: safe areas and the Dynamic Island. This section is the plan
for all of it; the tests are listed under each package and the device checks in [Device checklist](#device-checklist-the-exit-gate).

### VoiceOver

- Every interactive item has `Accessible.role` and a non-empty `Accessible.name`. Tiles use the `accessibleName` role of
  `BoardModel` ("Mute Delay, 2 today"). Rows read their `lineText` with the time first. Icons that have no text have a
  name. Decorative items are `Accessible.ignored`.
- Reading order follows the visual order, top to bottom: header card, banner, composer, "Last" bar, board by row, tab bar.
  The toast, when it appears, is announced (U3) and is also reachable by swiping; its Undo button is named "Undo Mute Delay".
- **No gesture is the only way in.** Qt offers no custom VoiceOver actions (U4), so every swipe and drag has a visible
  alternative that VoiceOver can reach. A review row is a button that opens the Edit sheet, which holds "Delete entry" and
  "Copy line"; the swipe is a shortcut for sighted users. The board-layout screen's drag has a "Move earlier", "Move later"
  and "Replace type" set of buttons on a selected miniature tile (which the screen shows to every user when a tile is
  selected, so the path is tested by everyone). Long-press (BRD-07) is a Should; its alternatives are the Edit sheet of Review and, while a note is typed, the tile's second element "Log <name> with note". The double-tap that logs (4.17a) needs no alternative: VoiceOver's activation gesture is itself a double-tap, and one activation logs one entry.
- State changes are spoken: the count after a tap is part of the announcement of the toast; the header card's name changes
  when a field is filled; the banner is announced when it appears.
- Modal surfaces (Sheet, Alert) take accessibility focus when shown, trap it while open, and return it to the control that
  opened them on dismissal. The scrim is not focusable.
- The 12th tile ("Other") reads "Other, opens the list of error types".

### Dynamic Type

- Source: the system content size category through `bridge.textScale` (4.3), from `UIApplication.preferredContentSizeCategory`
  (U8), as a multiplier with the standard category-to-scale table (xSmall about 0.82 up to accessibility 5 about 3.1).
- What scales, as NFR-07 asks: **list and note text** — review lines, history rows, settings rows, sheets, the note
  field, the toast text and form fields — scale without a cap, and rows grow in height (no row has a fixed height; 48 pt is
  a minimum).
- What is capped: **chrome whose geometry is fixed by design** — the tile labels (cap 1.3, so a four-row board of 104 pt
  tiles still fits above the tab bar on an iPhone 17 and never needs to scroll, which also keeps press-to-record safe), the
  count badge (cap 1.3), the tab labels (cap 1.3) and the header card's unit line (cap 1.5). Above its cap a role stays at
  the cap. The tile's minimum is 88 pt square (NFR-02) and the board stays 3 by 4 at every size.
- At the largest accessibility sizes the Review and History screens, and every sheet, scroll; the Log screen's board does
  not, which is why its fixed parts are capped.
- Layout never assumes a one-line label: no clipped `Text`, no `elide` on a label that carries meaning, and wrapping text
  uses `Text.WordWrap` with the width of its parent.

### Contrast, colour and motion

- **Contrast** is a test, not a judgement: 4.5 to 1 for text and 3 to 1 for non-text indicators (borders, the badge, the
  selected tab), in light and in dark, for every pair the components use, including the 12 tile colours under
  `textPrimary`. The pairs are listed once in `Theme` (`Theme.contrastPairs`) so the test and the screens use one list.
- **Colour is a visual aid only.** The label always carries the meaning (tile rules). Selected state is never colour
  alone (a check mark or weight change as well). The palette has no red or green tile hues; the test checks hue ranges and
  that no two tile colours are closer than a set distance under a simulated deuteranopia, so adjacent tiles remain
  distinguishable by more than colour.
- **Reduce Motion** (4.3 supplies `bridge.reduceMotion`) replaces springs and slides with a 100 ms fade; the tile press
  keeps its scale because it is feedback, not decoration.
- **Bold Text and Increase Contrast** are not specially handled in v1 (decision 14).

### Safe area

`SafeFrame` is the only reader of the safe-area insets (U1). The shell places the status bar zone, the tab bar and the
sheets with it. The header card clears the Dynamic Island; the tab bar's 83 pt includes the 34 pt home indicator inset; a
sheet's grabber sits below the top inset; the keyboard lifts a sheet by `Qt.inputMethod.keyboardRectangle` (U5). A test
overrides the insets (59 and 34 as the design gives, and the same plus 20 pt, as a different phone would) and checks
nothing interactive enters an inset and nothing is clipped. Portrait only: the property list's orientation lock is checked
by the existing policy check (decision 15).

## Motion

The design gives one number (tile press: scale 0.96, 120 ms, ease) and "spring present and dismiss, tuned on device".

- **Animators, not property animations,** for everything that moves per frame (`ScaleAnimator`, `YAnimator`,
  `OpacityAnimator`, `XAnimator`): they run on the render thread and hold the display rate while the GUI thread is busy
  with a commit (U7). Springs are an easing curve (`Easing.BezierSpline` or an `OutBack` pair) tuned on a device, because
  `SpringAnimation` is a GUI-thread property animation. The values are `Theme.motion.*`; 4.14 records them (below) and
  4.18 confirms them on a device.
- Sheet present and dismiss, toast in and out, row swipe release and the page stack's push and pop use those curves.
- The 120 Hz key in the property list (`CADisableMinimumFrameDurationOnPhone`) is added in 4.14 (U7); it is a property
  list edit that Stage 5 packages also make, so one pull request touches it at a time.
- The toast's remaining-time bar restarts on `undo.generation`, so a repeat tap restarts it without a model reset.
- Targets: a tap-to-press visual under one frame at the display rate; no dropped frame during a sheet presentation on an
  iPhone 17 (device checks M1 to M3). CI proves only that no synchronous work is in the press path.

## Tests

Every test is a Catch2 case labelled `ui` unless it says `perf`. `UiRig` (4.1) is the common fixture: an offscreen
`QQuickView`, `load(component, properties)`, `press(item)`, `release`, `tap`, `swipe`, `drag`, `type(text)`, `advance(ms)`
(drives the Stage 3 `TestClock` and `QTest::qWait`-free event processing), `settle()` (runs the manual gateway), a
`tree()` walker that lists every item with its `Accessible` data, and `geometry(item)` in points. Services come from
Stage 3's `Rig`, over the in-memory repository, so a screen test uses the real view models.

Tests specific to each package are under its entry in [Work packages](#work-packages). The cross-cutting ones, written
once in 4.1 and applied to every component and screen by 4.1 and each later package (a screen or component test that
omits them fails the "each file has a test" check):

- **`a11y.names`**: the item tree of every screen and component in every state has an accessible name and role on every
  interactive item, no two sibling controls share a name unless their value differs, and decorative items are ignored.
- **`a11y.targets`**: every interactive item is at least 44 pt by 44 pt including its hit area (tiles at least 88 pt).
- **`a11y.reading_order`**: the accessibility tree's order equals the top-to-bottom, left-to-right order of the items.
- **`a11y.text_scale`**: every screen is rendered at scale 1.0, 1.3 and 3.1; no `Text` is truncated unless its role allows
  it, none leaves the window horizontally, tile labels do not truncate at their cap, and the Log screen does not scroll.
- **`a11y.contrast`**: all pairs of `Theme.contrastPairs` in both modes pass; each tile colour against `textPrimary`
  passes; the palette has no red or green hue and every adjacent pair differs under a simulated deuteranopia.
- **`a11y.safe_area`**: nothing interactive inside an inset; nothing clipped; at insets of 59 and 34 and at 79 and 54.
- **`theme.no_literals`**: no hex colour and no bare pixel size in `components/` or `screens/` (the policy check, run as a test).
- **`i18n.strings`**: every visible string is `qsTr` with no concatenation (the policy check, run as a test).
- **`motion.reduce`**: with `reduceMotion` on, no sheet, toast or page transition lasts longer than 100 ms and none moves.
- **`perf.press_path`**: from a synthetic press to the `recordError` call, p95 over 200 presses under 16 ms on the CI
  runner with the manual gateway; the haptic call is the first of the two (this extends Stage 5's `haptics.press_path`).
- **`perf.first_frame`**: the Log screen in the `LOADING` state shows all 12 tile rectangles in its first frame on the
  software backend, with no count and no text; releasing the gateway changes the labels without moving a rectangle.
- **`perf.scene`**: the Log screen's item count and the number of delegates stay under a budget stored in
  `perf-budget.json` (standard 6.5), so a nested `Repeater` or a `Loader` per tile cannot creep in.

## Work packages

Eighteen packages, one pull request each. Files are disjoint between packages that run in parallel, except where a package
says it edits a file another package also edits (only 4.15, the shell, edits `Main.qml`, and only 4.14 edits the
property list among the Stage 4 packages). Where a package needs a file it does not own, it stops and says so in its
pull request (as Stage 3 specified).

| # | Package | Adds (under `apps/app/` unless noted) | Needs merged first |
|---|---|---|---|
| 4.1 | Foundations | `qml/theme/` (`Theme.qml`, `SafeFrame.qml`), `tests/ui/` rig and cross-cutting tests, `tools/ci/check_qml_*.py`, `docs/testing.md`, `docs/ci.md`, `.github/ci/plan.json`, `tools/` entries; `docs/design/tokens.json` dark section (from 4.2) | Stage 2 gate; Stage 3: 3.1, and the `Rig` of 3.11 in a header the UI tests can include |
| 4.2 | Missing screens and dark mode, as design documents | `docs/design/screens/*.dc.html` (new files), `docs/design/README.md`, `docs/design/tokens.json` | none; docs only, may start now |
| 4.3 | Accessibility settings bridge | `libs/ios_bridge/` (`accessibility_settings.hpp`, `src/platform/ios/accessibility_settings.mm`, `src/platform/desktop/accessibility_settings.cpp`, one line in `platform_services.cpp`, a section in `qml_bridge`), tests, `docs/architecture.md` | Stage 5: 5.1 |
| 4.4 | Containers and navigation components | `qml/components/` (`TabBar`, `Sheet`, `SheetHost`, `Alert`, `Banner`, `NavBar`, `PageStack`), their tests | 4.1 |
| 4.5 | Board components | `qml/components/` (`Tile`, `BoardGrid`, `UndoToast`, `LastEntryBar`, `MiniBoard`), their tests | 4.1 |
| 4.6 | Input and list components | `qml/components/` (`HeaderCard`, `NoteComposer`, `Chip`, `ListRow`, `GroupedList`, `SwipeRow`, `ChoiceCard`, `PrimaryButton`, `SearchField`, `FormField`, `TimeTumbler`), their tests | 4.1 |
| 4.7 | Log screen | `qml/screens/log/`, tests | 4.4, 4.5, 4.6 |
| 4.8 | Other picker | `qml/screens/other/`, tests | 4.4, 4.6 |
| 4.9 | Today's details and Software version | `qml/screens/header/`, tests | 4.4, 4.6; Stage 3 additions 1, 2, 4, 5 |
| 4.10 | Review and Edit entry | `qml/screens/review/`, tests | 4.4, 4.5, 4.6; Stage 3 addition 5 |
| 4.11 | New day prompt and History | `qml/screens/newday/`, `qml/screens/history/`, tests | 4.2 approved by the owner, 4.4, 4.6, 4.10 for the review of a past log |
| 4.12 | Settings and board layout | `qml/screens/settings/` (the list, the formats, board layout), tests | 4.4, 4.5, 4.6 |
| 4.13 | Editors, first launch and import | `qml/screens/settings/editors/`, `qml/screens/setup/`, tests | 4.2 approved by the owner, 4.12; Stage 3 addition 4 |
| 4.14 | Motion and refresh rate | `Theme` motion values (edit of 4.1's file), the curves in the components (small edits), `packaging` property list key | 4.7 to 4.13 merged; a device |
| 4.15 | Shell and navigation | `qml/Main.qml`, `src/main.cpp` wiring if needed, `tests/ui/app_flow_test.cpp` | 4.7 to 4.13; Stage 3: 3.11 |
| 4.16 | Dark mode pass, accessibility audit and screenshots | `tests/ui/` audit tests, `tools/ej_screenshots`, `docs/design/` dark notes | 4.15 |
| 4.17 | Long-press to attach a note (BRD-07, Should) | `qml/screens/log/` additions (the only package that edits 4.7's files), tests | 4.7, 4.10 |
| 4.17a | Double-tap to log (BRD-02, owner request 2026-11-09) | `qml/components/Tile.qml`, `BoardGrid.qml`, `qml/screens/log/` (gesture, hint), `docs/design/tokens.json`, tests | 4.5, 4.7, 4.17 |
| 4.18 | Exit-gate pass | `tools/coverage-floors.json`, the result recorded at the end of this file, `changelog.d/` | all of the above |

**Waves.**

0. 4.2 alone, now, in parallel with Stage 2 and Stage 3. It is documentation and the design review it needs (the owner,
   then the tester) is the slowest step in the stage.
1. 4.1, and 4.3 beside it once Stage 5's 5.1 has merged.
2. 4.4, 4.5 and 4.6 in parallel. They share nothing but `Theme` and `UiRig` from 4.1.
3. 4.7, 4.8, 4.9, 4.10, 4.11 and 4.12 in parallel. 4.11 waits for the approval of the History design and for 4.10 only
   for the past-log review, which it reuses by name. Each screen is tested standalone by the rig, so none waits for the
   shell.
4. 4.13 (after 4.12) and 4.15 in parallel: the editors are pages pushed from Settings, the shell wires the tabs.
5. 4.14, 4.16 and 4.17 in parallel; 4.14 needs a device and the owner's phone.
6. 4.18.

Each package runs `linux-debug` and `linux-asan-ubsan` locally before its pull request (AGENTS.md), and TSan in CI. The
pull request says which parts have not run on a device. A macOS desktop build is where the work is iterated visually
(implementation plan, Stage 4), but there is no Mac here; the design iteration happens on the screenshots of 4.16 and on
the tester's phone, in the TestFlight builds, weekly.

### 4.1 Foundations

The module layout, the globs, `Theme` and `SafeFrame`, `UiRig`, the cross-cutting tests, and the checks that keep QML
honest: `check_qml_platform.py` (no `Qt.platform`), `check_qml_literals.py` (no hex or bare pixel literals outside
`theme/`), `check_qml_strings.py` (`qsTr`), `check_qml_tests.py` (each component and screen has a test file),
`qmllint` with warnings as errors, and `qmlformat --verify`. These run in the Policy job through `precheck.py`, with
`docs/ci.md` and `.github/ci/plan.json` updated (a QML-only change needs the `ui` build). The first task is U1: print the
safe-area values from a device build. The dark values of `tokens.json` arrive here from 4.2.

Tests: `theme.matches_tokens`; `theme.dark_resolves` (switching the scheme in the rig changes every colour); `safe_frame.insets`;
`safe_frame.override`; `rig.*` (the rig itself: press/release timing, tree walker finds a known control); the
cross-cutting tests above run on a stub component; each policy check against a known-good and a known-bad fixture.

### 4.2 Missing screens and dark mode, as design documents

No code. Draw, in the format of the existing `.dc.html` files, the screens the design package lists as not drawn
(History, header banner, first-launch setup, the three editors, the import confirmation and import errors, the delete-log
confirmation, the template value prompt, the write-failed and recovery messages), the entry point for starting a new log
(decision 8), the board layout's selected-tile state with the VoiceOver alternative, and **every screen in dark mode**.
Add the dark values to `tokens.json` and the rules to the design README. The owner reviews the documents, then the
tester approves them; a screen package starts when its design is approved. This is the only package that can run
now, and the dark palette blocks the Stage 4 exit gate (implementation plan, decisions that block a stage).

Checks (`precheck`/docs build only): every colour in the dark section passes the contrast pairs of `Theme.contrastPairs`
once 4.1 lands (until then the author runs the same ratios by hand and records them in the README); the documents use
fictional stand-ins only.

### 4.3 Accessibility settings bridge

A new bridge interface, `AccessibilitySettings`, in the pattern of Stage 5 (an interface header, an `.mm` that does the
one UIKit call, a desktop stub, one line in `make_platform_services()`, a section in `qml_bridge`): `text_scale()` from the
content size category, `reduce_motion()` from `UIAccessibility.isReduceMotionEnabled`, `voice_over_running()` for the
tests and diagnostics, and a change notification for each (`UIContentSizeCategoryDidChangeNotification`,
`UIAccessibilityReduceMotionStatusDidChangeNotification`). `QmlBridge` exposes `textScale`, `reduceMotion` and the notify
signals; `Theme` reads them. The desktop stub returns 1.0 and false, and a test hook sets them. The first task is U8: read
the category and the font QML picks on a device.

Tests (`unit`): the category-to-scale table (every category, monotone, accessibility sizes above 2.0); the wrapper emits one
signal per change and none when the value is the same; the stub; a notification after the bridge is destroyed is ignored.
This is a Stage 5 interface added by Stage 4 (decision 5), so 5.1's file layout and `platform_services.cpp` rebase rules
apply.

### 4.4 Containers and navigation components

`TabBar` (83 pt including the inset, four tabs, selected weight and a second non-colour cue), `Sheet` and `SheetHost`
(scrim, grabber, medium and large detents, drag-to-dismiss, keyboard lift, focus trap, spring), `Alert`, `Banner` with
the three tones and the write-failed and recovery wording slots (the strings themselves come from the view models), `NavBar`
and `PageStack`. `TabBar` has `skeleton`.

Tests: `tab_bar.selection` (tap changes the current tab once, emits once, the selected cue is not colour only);
`tab_bar.geometry` (83 pt, inset respected); `sheet.present_dismiss` (present, drag past the threshold dismisses, drag
short returns, scrim tap dismisses, `onClosed` once); `sheet.keyboard` (a faked keyboard rectangle lifts the content and
no field is covered); `sheet.focus` (focus enters, is trapped, returns to the opener); `alert.modal` (blocks input below
it, default and destructive roles named); `banner.tones` (contrast passes for each tone in both modes); `page_stack.push_pop`;
`motion.reduce` for each; the cross-cutting tests.

### 4.5 Board components

`Tile` (above), `BoardGrid`, `UndoToast`, `LastEntryBar`, `MiniBoard`. `BoardGrid` is the exact geometry of the design:
three columns of (402 − 2 × 16 − 2 × 8) / 3 = 118 pt, tiles 104 pt high, 8 pt gaps, so 440 pt tall, bound to a
12-row `BoardModel`, never a `modelReset`-sensitive structure (the model never resets, Stage 3).

Tests: `tile.press_scale` (scale 0.96 within 120 ms of the touch that asks, back on release and on cancel, with the manual
gateway held, i.e. before any commit; the first tap does not scale the tile); `tile.double_tap` (`pressedDown` fires on the
touch of the second tap, not the release, once, and a first tap or a release alone emits nothing; amended by 4.17a, it was
`tile.press_emits_first`); `tile.slide_off` (a first touch that slides off is no tap; a second that slides off has asked once at
the touch and does not tap); `tile.badge` (visible only when `hasCount`, shows the number, 99 and above reads "99+" with the
full count in the accessible name); `tile.label` (the longest configured label wraps to at most the allowed lines and is not
truncated at scale 1.0 and 1.3); `tile.empty` (no handler, no role, not announced); `tile.other` (dashed border, neutral
colour, the accessible name); `tile.colour` (each slot against `Theme.tileColour`); `grid.geometry` (118 by 104 pt, 8 pt gaps, the
board's height 440 pt, no scrolling at scale 3.1); `grid.model_updates` (a `dataChanged` on one row updates one tile
and a running press animation on another tile is not interrupted); `toast.text_and_bar` (shows `undo.text`, the bar
restarts when `generation` changes and not otherwise, `windowMs` 5,000); `toast.undo` (the button calls `undo()` once,
disabled while `undoing`); `toast.repeat` (the text with "×2" appears without the toast closing and reopening);
`toast.lifecycle` (in and out, announced once); `last_bar.line` (shows `lastLine` and the count, empty-state text for no
entries, opens Review on tap); `mini_board.drag` (hold, then drag from position 3 to 7 calls `moveBoardTile(3, 7)` once
and shows the tile at 7 only after the signal, snaps back on a refusal); `mini_board.swap` (tap a tile, choose a type,
`assignBoardTile`); `mini_board.buttons` (the Move earlier, Move later and Replace type buttons do the same and are the
VoiceOver path); `perf.press_path` for `Tile`.

### 4.6 Input and list components

`HeaderCard` (with `skeleton`, and `missingCount` for a dot or count that is not colour alone), `NoteComposer` and `Chip`
(the `draft` two-way binding, Return and Add, chips from a model), the list family, `ChoiceCard`, the form controls and
`TimeTumbler`. The composer assigns to `draft` on every text change (the 300 ms debounce is in Stage 2's `DraftStore`) and
restores it when the property changes from C++.

Tests: `header_card.fields` (shows the five values, a missing field reads as a placeholder not an empty gap, tapping emits
once); `header_card.skeleton` (shapes only, same geometry); `composer.draft_binding` (typing assigns each change; a
change of the property from C++ updates the text and does not re-assign it, so the draft cannot echo back);
`composer.add` (Add and Return emit `submitted(text)` once, ignore empty and whitespace, keep the text if the call
returns false, clear it if true); `composer.chips` (a chip emits `chipChosen(templateId)`, long names wrap or scroll
without truncating the name read by VoiceOver); `composer.undo_text` (text returned by the note undo is placed in the
field and the cursor at the end); `composer.dictation` (the field accepts committed input-method text; no custom
speech code exists, NOTE-05); `list_row.layout` (48 pt minimum, grows with text, separator rules); `swipe_row.reveal`
(swipe past the threshold reveals Copy and Delete, a short swipe returns, a tap on a revealed action emits once,
a vertical drag scrolls the list and does not open the row); `swipe_row.accessible` (the row is a button; its actions are
reachable without the swipe through the row's own `activated`); `choice_card.select` (exactly one selected, check mark as
well as colour); `form_field.validation` (the message appears inline with the field's accessible description, the Save
state follows); `primary_button.states` (default, pressed, disabled; 54 pt); `search_field.filter` (typing emits
`filterChanged` once per change; clear button); `time_tumbler.values` (hours and minutes, seconds when the time style has
them, 12-hour with meridiem when configured, emits `localSecondsChanged` once per change, VoiceOver adjustable); and the
cross-cutting tests.

### 4.7 Log screen

The screen the tester lives in: `HeaderCard`, the HDR-06 banner, `NoteComposer`, `LastEntryBar`, `BoardGrid`,
`UndoToast`, the template value prompt (a small `Sheet`), the write-failed and recovery `Banner`s, and the `LOADING`
placeholder (all 12 tile rectangles, no text, taps ignored). It owns the press wiring: `onPressedDown` calls
`bridge.hapticImpact(timestamp)` then `session.recordError(typeId)`; `onActivated` does the same; the screen calls
`bridge.prepareHaptics()` when it becomes visible (Stage 5, haptics); a successful note calls `bridge.hapticSuccess()`
from the `noteSaved` path of `session` (after the commit). The Other tile opens the picker. Tapping the header card
opens Today's details, the "Last" bar opens Review.

Tests (`ui`, real view models, manual gateway): `log.loading_placeholder` (state `LOADING`: 12 rectangles, no text, no
handler; releasing shows labels without moving them); `log.tap_flow` (the second tap's touch calls the fake haptic first, then
`recordError`, and the first tap nothing; the count, the toast and the "Last" bar change only after `settle()`; two double-taps on
one tile show "×2"; the screen contains no counter of its own); `log.slide_off` (as `tile.slide_off` at screen level); `log.undo` (Undo removes the
latest entry, closes the toast, count falls); `log.note` (type, Add: the entry appears after the commit, the field clears,
the success haptic follows the commit, the toast "Note added", Undo returns the text to the field); `log.template`
(a chip opens the value prompt with the template's prompt; confirming calls `addTemplatedNote` with the id and value; an
empty value is refused inline); `log.banner` (an empty header shows the banner with the missing fields, taps still work,
filling a field removes it); `log.write_failed` (a failing commit shows the banner, no count, no toast, no success haptic,
the next tap works); `log.recovery_notice` (a non-empty `recoveryNotice` shows once and can be closed); `log.empty_tiles`
(a nine-type configuration shows two inert tiles); `log.other` (the Other tile opens the picker and never records);
`log.voiceover` (activation through `Accessible.pressAction` records once and fires the impact; the toast is announced);
`log.skeleton_matches` (the `skeleton` geometry equals the real geometry of every shape within 0.5 pt, the half of
Stage 5's `launch.matches_screen` that lives here); `log.safe_area` and `log.text_scale` (cross-cutting, at screen level);
`perf.first_frame`, `perf.scene`.

### 4.8 Other picker

A `Sheet` with a `SearchField`, two sections ("Not on your board", "On your board") of `ListRow`s with counts, and
"Add a new error type". Binds to `ErrorPickerModel`. A row records through `session.recordError` and closes the sheet; adding
a type does not record it (Stage 3, decision 12).

Tests: `other.sections` (rows by section, counts, the empty-result message); `other.filter` (typing narrows, case and accent
insensitive via the model; one `modelReset` per change does not flicker the sheet or lose focus in the field);
`other.pick` (tap a row: `recordError` once with that id, the haptic does not fire from this sheet unless the design says
so, the sheet closes, the toast follows the commit); `other.add` (a valid name appears under "Not on your board" and
nothing is recorded; empty, duplicate and over-long names show the model's refusal inline); `other.voiceover` (rows read
"label, n today"); the cross-cutting tests.

### 4.9 Today's details and Software version

The two header sheets. Today's details: tester and date read only, the unit number field with the fixed prefix and
the number pad (digits only; `validateHeaderValue` for the inline message), the location list with Other and free text,
the software version with inline validation, and "Start logging" (which is a close, since nothing blocks logging). A first
fill of an empty field is a Correct (Stage 3). The version sheet: the two choices, From and To, a time field defaulting to
now, and the preview line. Gap items 1 and 2 supply the time conversion and the preview; until they exist this package
cannot be finished.

Tests: `details.fields` (values from the session; the tester and date are not editable); `details.unit_keypad` (the
number pad hint is set, letters never reach the field, the prefix is displayed and not typed, `027` shows the model's
message and nothing is saved); `details.location` (a configured location sets its id, Other shows a free text field and
sends the text); `details.version_validation` (`1.4`, `1.4.2-beta` show the message inline and Save stays disabled until
valid); `details.correct_first_fill` (filling an empty field calls `correctHeader` and adds no timeline entry);
`details.new_log_row` (the DAY-02 row from decision 8 asks for confirmation before `startNewLog`); `version.choices`
(the two plain choices, one selected, texts in the tester's words); `version.change` ("It changed during the day" calls
`changeHeader` with the time chosen, once, and the timeline gains the line); `version.correct` ("I typed it wrong" calls
`correctHeader`, no entry); `version.preview` (the preview equals the saved line byte for byte, updates as the fields
change, shows the refusal for an unchanged value); `version.time_default` (the field starts at now and a time earlier than
now is allowed); `details.keyboard` (the sheet lifts above the keyboard, the active field stays visible); cross-cutting.

### 4.9a Unit environment (PRF-11, HDR-08)

A change request after the visual pass, decided by the owner: the unit may carry an environment, `UNIT-27-prod`. The
environment is optional (`UNIT-27` stays valid), picked from a list and never typed, with "None" as a choice and the default
for a new log. The default list is `prod` and `dev`; the setup file can replace it with the optional preferences key
`unit_environments`.

Task breakdown:

1. **Core.** `UnitNumber` gains an optional environment (lower-case letters, digits and dashes, 1 to 20, no dash at either
   end) and the stored text `27-prod` (`parse_stored`, `stored`). The digits-only `parse` is unchanged, so what a person types
   in the number field can never hold a dash. Header values, the journal record, the renderer and the SQLite repository read
   and write the stored text; nothing in the schema or the journal version changes, and text from older builds (digits only)
   reads as a unit without an environment. `Configuration::m_UnitEnvironments` (default `prod`, `dev`), read and written as
   `preferences.unit_environments`, with `ConfigError` problems for an empty list, a bad name, a name that is too long, and a
   repeated name. The export writes the key only when the list differs from the default.
2. **View models.** `LogSession`: `unitEnvironment`, `validateUnit(digits, environment)` and `correctUnit(digits,
   environment)` (the environment must be a choice, or the one already in force); `unitText` and the edit sheet's footnote
   follow from the core. `SettingsModel::unitEnvironments`. `import_messages`: a plain sentence for each environment problem.
3. **QML.** An "Environment" row under the unit field of Today's details (None, prod or dev, with a chevron) opens
   `EnvironmentSheet`, a short `Sheet.Content` list with None first and the one in force ticked. The choice is saved with the
   number; chosen before there is a valid number, it waits for it.
4. **Design.** `Header.dc.html` and its dark variant show the row.
5. **Carry-forward.** Checked: only the tester carries into a new log (HDR-05, DAY-02), and the unit prefix is a
   preference rather than a part of a log, so the environment starts at None in every new log, like the number.

Tests: `UnitNumber` parse, stored text and limits (`unit-number`, `environment`); the setup-file key (valid, replaced list,
absent, round trip byte for byte, each invalid case with its structured problem); header history, the export header and a
golden file for change lines; the journal record with and without an environment; the storage contract on both
repositories, the SQLite column, a rebuild from the journal, damaged values, and rows written by an older build; the view
models (`validateUnit`, `correctUnit`, the choices, a new log, the import sentences, the footnote, History); and the
screen: `details.environment` (the row, the sheet's choices, choose prod, choose None, the number and the environment saved
together, the setup file's list, VoiceOver names, large text, dark mode, a long list).

### 4.9b Commit of the build (HDR-09)

A change request after the visual pass, decided by the owner: the software version may carry the commit of the build,
`1.4.2 (a1b2c3d)`. The commit is optional, 7 to 12 hexadecimal characters, lower-cased, with a pasted 40-character hash
shortened to 7; an empty one is valid and is listed as still to fill on the Log card, as a reminder that never blocks.

Task breakdown:

1. **Core.** `SoftwareVersion` gains an optional commit and the stored text `1.4.2+a1b2c3d` (`parse(text, commit)`,
   `parse_stored`, `normalize_commit`, `stored`, `display`). `parse(text)` is unchanged, so what a person types in the version
   field can never hold a plus sign. Header values, the journal record, the renderer and the SQLite repository read and write
   the stored text, and the renderer prints `display()` in the header block and in the version change lines. Nothing in the
   schema or the journal version changes, and text from older builds (three components) reads as a version without a commit.
2. **View models.** `LogSession`: `commitText`, `versionDisplayText`, `validateCommit`, `validateVersion(version, commit)`,
   `versionDisplay`, `correctVersion`, `changeVersion` and `previewVersionChange` (the sheet's way to take the pair), and
   `software_commit` in `missingFields` while the commit is empty. `TimelineModel`: the footnote reads `version 1.4.2
   (a1b2c3d)`.
3. **QML.** A Commit field under the version on Today's details, with the owner's hint, saved together with the version
   (typed first, it waits for the version, as the environment waits for the number); a Commit row in the Software version
   sheet's card with the same hint under the card; "Commit" in the Log card's strip.
4. **Design.** `Header`, `HeaderNewLogRow`, `ChangeVersion` (light and dark) and `LogBanner` show the field, the row and the
   strip.
5. **Carry-forward.** Checked: only the tester carries into a new log (HDR-05, DAY-02), so the version does not carry
   and neither does its commit. Within a log the commit is a part of the version value, so it is kept for the day, and a
   Change takes a version and a commit as a pair: the sheet's To and Commit rows start empty, as To always did, so a new
   build never inherits the commit of the old one by accident.

Tests: `SoftwareVersion` commit (7, 12, 40 to 7, upper case, not hexadecimal, 6, 13 and 41 characters, stored text,
text from before commits); header history and corrections with a commit; the export header, the change lines and a golden
file; the journal record with and without a commit and a damaged one; the storage contract on both repositories, the SQLite
column, a rebuild from the journal, rows and journals written by an older build, damaged values; the view models
(`validateCommit`, `validateVersion`, `correctVersion`, `changeVersion`, the preview line, the missing list, a new log, the
footnote); and the screens: `details.commit_field` (the field, the hint, the message, lower-casing and shortening, waiting
for the version, Start logging, a new log, VoiceOver names), `version.commit` (the row, the hint, a change, a correction,
the same pair, a refusal, a plus sign, VoiceOver names), and the Log card's strip.

### 4.10 Review and Edit entry

Review: the header block (monospaced-feeling, black and white), the timeline in export wording using `timeText` and
`lineText` in two columns, `SwipeRow` Copy and Delete, copy-all in the navigation bar, "Share log". Edit entry: a `Sheet`
with Cancel and Save, the error type picker (a list from the model), `TimeTumbler`, the note and attached-note fields,
the preview of the shared line ("In the shared log"), "Delete entry" and "Copy line". The screen binds to a
`TimelineModel`, so the same file serves the open log and a past log from History (`reviewModel`).

Tests: `review.header_and_rows` (the header block equals `headerText`; rows equal `timeText + " " + lineText`; no coloured
element anywhere in the list, REV-01); `review.row_wording` (every row of the golden scenario equals the golden line);
`review.copy` (`copyLine` and `copyAll` called once with the right row; the fake clipboard holds the expected text; a
transient "Copied" confirmation that is announced; nothing is marked shared); `review.delete_swipe` (swipe, Delete: the row
leaves after the commit and the toast "Entry deleted" appears; Undo restores it in place); `review.share` (the button is
absent when `share.available` is false; present, it calls `shareOpenLog` once, is disabled while `sharing`, shows nothing
on `cancelled`, and a `failed` outcome shows a banner); `review.empty` (an empty log shows an empty state and disables
Share and copy-all); `edit.fields` (the sheet shows the entry's current values; the type, time, note and attached note edit
independently); `edit.preview` (the preview line equals `previewLine` for the edit and updates per change; saving then
shows exactly that line in the list); `edit.save_cancel` (Cancel changes nothing and writes nothing; Save calls
`saveEntry` once with the edited map; saving an unchanged sheet is not an edit); `edit.delete` (the button asks no dialog
but uses the undo toast, as REV-03 says); `edit.time_order` (an edit to an earlier time moves the row); `edit.dst`
(editing into the missing hour of 2026-10-04 shows the renderer's result, via the preview); `edit.header_change_entry` (a
header-change entry edits its time and new value only); `review.voiceover` (a row is one button reading the whole line and
opening Edit; Edit offers Copy and Delete); `review.long_log` (a 500-entry log scrolls at the budget in `perf`, delegates
reused); cross-cutting.

### 4.11 New day prompt and History

The new-day prompt is an `Alert` over everything, bound to `day`: it names the new date, says how many entries the open
log has, warns when `unshared`, and offers "Start a new log", "Share [day]'s log first" and "Keep [day]'s log open".
History is a grouped list by `section` with the shared state, a swipe or Edit-mode delete behind `requestDelete` and a
confirmation `Alert`, and an open row that pushes the Review screen bound to `history.reviewModel`.

Tests: `newday.visibility` (the alert follows `promptVisible`; it blocks the tab bar; it is not dismissable by a scrim tap);
`newday.text` (the new date and the old day come from the model and are shown without edits); `newday.unshared` (the warning
and the share-first choice appear only while `unshared`, and the warning disappears the moment a share is recorded while the
prompt is still up); `newday.changed` (a log shared and then added to says it changed since it was last shared and offers to
share again, on the next day or three days on; one shared and not changed says nothing); `newday.actions` (each choice calls its one invokable once; Start a new log on `false` keeps the prompt
and shows the failure; Keep hides it); `newday.share_first_flow` (Share first leaves the prompt up, then Start works);
`history.sections` (newest first, two logs on one date under one heading, shared and unshared rows, the shared time in the
user's format); `history.refresh` (shown on entry, a loading state, one reset without flicker; an error keeps the old list
and shows a banner); `history.open` (a row opens a review of the past log with its lines; editing and copying work there;
back closes the log); `history.reshare` (the row's share calls `shareLog(id)` and its state updates on `shareRecorded`);
`history.delete` (no delete control for the open log; delete asks for confirmation naming the date and entry count;
cancelling deletes nothing; confirming calls `confirmDelete(token)` once; a failed delete keeps the row and shows why);
`history.empty` (no archived logs shows an empty state); cross-cutting.

### 4.12 Settings and board layout

The Settings list (tester, unit prefix, time format with the 24-hour, 12-hour, seconds and meridiem choices, totals
option, the four list rows with counts, import and export rows) and the board layout screen built on `MiniBoard`. Rows
that edit text push editor pages (4.13); this package owns the list, the simple editors it can do inline (tester,
prefix, the time format picker, totals) and the board layout.

Tests: `settings.rows` (values from `settings`, counts on the list rows); `settings.tester` and `settings.prefix`
(editing calls the invokable once; a refusal message is shown inline and the old value stays; the open log's tester
note, "applies from the next log", is shown); `settings.time_format` (all combinations call `setTimeFormat` once and the
sample time on the screen follows, PRF-07); `settings.totals` (the switch calls `setIncludeTotals`); `settings.board_layout`
(drag and swap as in `mini_board.*`, against the real `SettingsModel`: the Log screen's board follows after the signal,
tile colours stay with their types); `settings.board_limit` (an assignment refused by the model shows its message);
`settings.export` (the row emits `exportRequested()`; the result of `exportFinished` is shown); `settings.import_row` (emits
`importRequested()`); `settings.voiceover` (switches read their state, rows read label and value); cross-cutting.

### 4.13 Editors, first launch and import

The error type, location and template editors (add, rename, reorder, retire, `validatePattern` live with `{value}` required),
the first-launch screen (offer to import a file or set up manually, PRF-09), the import confirmation (the summary from
`importPrepared`) and the import error list (`importFailed`, messages in file order). The platform-specific part is a
signal out: `importRequested()` and `exportRequested()`; on the desktop the shell opens Qt's file dialog and on iOS Stage 5's
document picker supplies the path, then calls `settings.loadImport(path)` (decision 6).

Tests: `editor.error_types` (add, rename, retire; a retired type disappears from the list; duplicate and empty names refused
inline; reorder through move buttons as well as drag); `editor.locations`; `editor.templates` (name, prompt, pattern; the live
pattern message from `validatePattern` while typing; `{name}` refused; the preview of the output pattern with a sample
value); `setup.first_launch` (shown when `configured` is false; choosing manual setup asks for tester and prefix and
then dismisses; the Log screen is reachable after); `import.flow` (`loadImport` is called with the chosen path; the summary
screen shows the counts; Apply calls `applyImport`; Cancel calls `discardImport` and changes nothing); `import.errors` (a
malformed file, a future schema version and duplicate ids show the core's messages in file order and nothing changes);
`import.sample` (the sample file imports and the board, the picker and the settings lists show it); `editor.voiceover`;
cross-cutting.

### 4.14 Motion and refresh rate

Tune the curves and durations in `Theme.motion` on a device, convert any property animation that moves per frame to an
animator, add `CADisableMinimumFrameDurationOnPhone` to the property list (U7), and record the numbers. This is the only
package that edits several components after their own packages, and only for animation values; it is sequenced after the
screens so there is no concurrent edit. It needs the owner's phone.

Tests in CI: `motion.animators` (a source check: no `NumberAnimation` on `x`, `y`, `scale` or `opacity` of a component that
moves per frame, only the animator types); `motion.curves` (the token values equal the recorded tuning); `motion.reduce`
(again); `verify_app.sh` / the plist check: the key is present. The frame rate is checklist M1 to M3.

**Status of the tuning.** There was no phone when this package was built, so no value was tuned: the numbers are the
design's (`tokens.json`, `motion`) and this page's, and are recorded as **desktop defaults, to confirm on the iPhone in 4.18**.
Changing one means changing `tokens.json`, `Theme.qml`, this table and `RECORDED` in `theme_test.cpp` together; `motion.curves`
fails if any of the four differs.

#### Recorded motion values

| Value | `Theme.motion` | Recorded | Source |
|---|---|---|---|
| Tile press scale and time | `tilePressScale`, `tilePressMs` | 0.96, 120 ms | The design (`tilePress`); kept under Reduce Motion |
| Tile press curve | `easingPress` | `InOutQuad` | The design's "ease" |
| Double-tap window, hold, hint | `doubleTapMs`, `holdMs`, `tapHintVisibleMs`, `tapHintIntervalMs` | 300 ms, 500 ms, 2000 ms, 5000 ms | The owner's request of 2026-11-09 (4.17a); to confirm on the phone (checklist P6, P7). Times, so Reduce Motion does not change them |
| Sheet present and dismiss | `sheetPresentMs` | 320 ms | Starting value of 4.1 |
| Toast in and out | `toastMs` | 240 ms | Starting value of 4.1 |
| Page push and pop | `pageMs` | 280 ms | Starting value of 4.1 |
| Row swipe release | `swipeReleaseMs` | 200 ms | Starting value of 4.1 |
| Spring curve of sheets, toast, pages and swipe | `easingSpring` | `OutBack`, overshoot 1.70158 (Qt's default; no animation sets its own) | The plan: an easing curve, because `SpringAnimation` is a GUI-thread animation |
| Reduce Motion | `reducedMotionMs`, `easingSpring` | 100 ms, `Linear`, no travel | The plan and the design |
| Undo toast visible | `undoToastVisibleMs` | 5000 ms | The design |

Every animation that moves an item every frame is an Animator (`XAnimator`, `YAnimator`, `ScaleAnimator`, `OpacityAnimator`):
the tile press, the sheet and its scrim, the toast, the row swipe, the Settings page slides, and, from 4.14, the page fades.
`motion.animators` fails a property animation or a `Behavior` on `x`, `y`, `scale` or `opacity`.

**What the owner checks on the phone** is rows M1 to M6 of the device checklist. The values above are the starting point; if a
row fails or feels wrong, change the number in the four places and run the row again.

### 4.15 Shell and navigation

`Main.qml`: the window background from the first frame (the `background` token, so the launch screen hands over to the
same colour), `SafeFrame`, the four tabs on `TabBar`, the `SheetHost`, the stack for Settings pages, the new-day `Alert`
host, the `required property` declarations, and the routing of `importRequested`/`exportRequested` to the desktop file dialog or the iOS
picker. Tab state is in memory only and starts on Log. The shell connects `DayWatcher`'s prompt, the toast host and
the banners to the screens. `main.cpp` is edited only if the root properties need it (coordinate: only one open pull
request edits it, per Stage 5).

Tests: `app.navigation` (each tab shows its screen; Review and History reuse the timeline models; back from a pushed page
returns to Settings); `app.sheets` (header card opens Today's details, Other opens the picker, a row in Review opens Edit,
and only one sheet at a time); `app.full_flow` (the Stage 3 exit-gate day, driven through the UI: set the header, tap, a
note, a version change, review edits, share, the new-day prompt, History; the final export equals `prd-example.txt` through
the same `check_golden`; this is the UI-level twin of Stage 3's headless test and shares its `Rig`); `app.first_frame` (the
window colour is the background token before the first screen is loaded); `app.no_services_missing` (the app starts with
every required property set, and a missing one fails loudly in a test, not silently on a phone); `app.smoke` (existing).

### 4.16 Dark mode pass, accessibility audit and screenshots

Walk every screen in dark and at the two larger text scales, fix what the cross-cutting tests and the eye find, and add
`ej_screenshots`: a desktop tool that renders every screen and sheet in light, dark and large text, with the sample data, to
PNGs that CI uploads as an artifact. The owner and the tester compare these with the design before the phone is
involved; they are not golden files (fonts differ between the runners and the phone). Also the VoiceOver audit: a
checklist table recorded in 4.18.

Tests: `dark.all_screens` (each screen loads in the dark scheme, every colour is a `Theme` colour, contrast passes);
`audit.names_complete` (the tree walker on the full app over the full-flow scenario: no unnamed interactive item at any
step); `screenshots.render` (the tool runs offscreen and writes one file per screen and mode; the set matches the screen list
in this page so a new screen cannot be forgotten).

### 4.17 Long-press to attach a note (BRD-07, Should)

Holding the finger on a tile for 500 ms (`Tile.holdMs`, `Theme.motion.holdMs`) logs the error with a note, in one of two ways.
**Since 4.17a the hold records the entry itself**, at the hold, because the touch no longer records (a single tap logs nothing);
a hold that follows a first tap has recorded at its touch already, as the second tap of a double-tap, and the hold adds the note
to that entry. Which way is decided at the moment the hold completes, by whether the composer has a typed draft (**the owner chose both, 2026-11-09**, after the tester said "you type the note, press and
hold the error, and it will log together"):

1. **A draft is typed: log with it.** `session.attachDraft(entryId)` puts the draft, trimmed, on the entry as its attached
   note (NOTE-06) and, when that has committed, clears the composer. The result is one entry on one line, with the error
   and its note, and never an error plus a separate note. The success haptic of a saved note follows the commit, as it does
   for a note. The toast reads "Mute Delay logged at 10:30 with note", and its **single Undo** removes the entry and puts the
   draft back in the composer (the way a note's undo does, `UndoController::note_attached`). If the attach cannot be saved the
   entry stays as the tap made it, the draft stays, and the write-failed banner says so; nothing typed is lost. A draft that
   is only spaces is no draft. Text typed while the attach commits is kept.
2. **The composer is empty: the Edit sheet.** The Edit entry sheet of Review (the same files, put in the shell's sheet layer
   by the Log screen at the first hold) opens on the entry whose id arrived in `session.entryRecorded`, with the cursor in the
   note field. Save attaches the note (NOTE-06); Cancel leaves the entry as it was.

A **double-tap is unchanged by the hold**: it logs the error alone, leaves the composer as it is, and opens nothing (4.17a). A
press shorter than 500 ms (a tap, which counts towards a double-tap), a finger that slides off the tile before then, a cancelled
touch, Other and an empty position are not holds.

The touch that asks (the hold, or the second tap) records the entry and fires the impact haptic; the hold never records a second
one. `recordErrorEntry` returns the id the entry has been given (the same id `entryRecorded` later reports), and the screen keeps
the latest id of each tile. The hold comes straight after the record, so it always waits for `entryRecorded` of that id and then
does its work (the slow-commit path is the ordinary one). The hold has no haptic of its own: the one impact is the record's, at
the hold, and a second would spoil the timing figures (Stage 5, M2); the success haptic of path 1 is the note's.

**VoiceOver.** Activating a tile logs it once (4.17a), as before. While a draft is typed, each tile that can be held has a second
accessible element, "Log Mute Delay with note", which sends the press and then the hold, so path 1 needs no gesture. With
no draft there is no such element: the way to a note is the Edit sheet of Review (a row opens it), as the accessibility rules
above have it. Qt has no custom VoiceOver actions (U4), so the action is an element of its own, which only a device can
confirm (device checklist A6; P4 and P5 are the hold itself).

**The hint.** The composer's placeholder reads "Type a note, then hold a tile…" (it replaces "Add a note to today’s log…", and
the Add button and the field's name still say what the field is). The mock-ups `Main`, `LogBanner`, `LogEmptyBoard`, `Recovery`,
`TemplateValue`, `TileLabels`, `TileLabelsLarge` and `WriteFailed`, each in light and dark, carry the new text; the `Main`
prototype also holds a tile for 500 ms and shows the toast and the undo of path 1. The skeleton of the launch screen has no
placeholder, so its geometry is untouched.

The hold is timed on the animation clock (a `NumberAnimation` of a property nothing reads, then a `ScriptAction`, in a
`SequentialAnimation` that the press starts and the release, a slide off and a cancel stop), not by a `Timer` or the handler's own
long-press timer, so that the UI rig moves it with `advance`: the rig's `hold(item, duration)` presses, advances virtual time and
releases. (Qt ticks a lone `PauseAnimation` by the wall clock, so it is not used.)

Tests (`ui` unless noted; amended by 4.17a, where the hold records the entry, so a hold's commit is always after it): `longpress.opens_note` (a hold beyond 500 ms opens the sheet for the new entry's
id with the cursor in the note once the commit is in, and a press shorter than that logs and opens nothing; Other, an empty position and a slide off are not
holds); `longpress.records_once` (the hold records one entry, not two, with one impact, on both paths); `longpress.save` (the
note is on the same line in the timeline); `longpress.cancel`; `longpress.slow_commit` (the hold records the entry and the sheet waits for
`entryRecorded` and then opens); `longpress.voiceover` (no gesture needed: activation records once and
opens nothing, and there is no "with note" element without a draft). For a typed draft: `longpress.draft_attaches` (the hold
attaches the draft to the same line, records once and clears the draft), `longpress.draft_undo` (one Undo removes the entry
and puts the draft back), `longpress.draft_tap` (a double-tap leaves the draft alone), `longpress.draft_slow_commit`,
`longpress.draft_failed` (entry and draft stay, banner shown) and `longpress.note_action` ("Log <name> with note" exists
only with a draft, is not on Other, and logs with it). View models (`unit`): `recordErrorEntry`, `attachDraft` (trimmed text,
cleared after the commit, one confirmation, refusals, no overwrite, failure, text typed meanwhile is kept) and
`UndoController::note_attached` (the toast text, the undo hands the draft back, another toast is not touched). Rig: `rig.hold`.

This package is optional: it can slip to 1.1 without changing the gate.

### 4.17a Double-tap to log (BRD-02, BRD-03; owner request, 2026-11-09)

The tester asked for a tile to be logged by a **double-tap** instead of one tap, so that an accidental touch does not
make an entry, and kept **press and hold** (4.17) as the way to log with a note. The owner decided it on 9 Nov 2026 for the build
the tester gets on 10 Nov 2026 (decision 18).

**The gestures** (for a tile with an error type; `Tile` counts them for each tile on its own):

| Gesture | What it does |
|---|---|
| One tap | Logs nothing. Once the double-tap window has gone by with no second tap, the screen shows the hint "Double-tap to log" |
| Two taps, the second no more than `Theme.motion.doubleTapMs` (300 ms) after the lift of the first | Logs one entry. The impact haptic, the press scale and the record are on the **touch of the second tap**. The undo toast is unchanged ("×2" for the same tile) |
| Three taps | Log once: the third is part of the same gesture and does nothing. The fourth tap starts a new double-tap |
| Hold for 500 ms (`Theme.motion.holdMs`) | Logs at the hold, with a note (4.17): a typed draft goes on the entry's line and is cleared, one Undo gives it back; with no draft the Edit entry sheet opens on the new entry's note. The haptic and the press scale are at the hold |
| A tap and then a hold | The second touch has logged (a double-tap), the hold adds the note to that entry: one entry |
| Release before 500 ms | A tap, which counts towards a double-tap |
| Slide off, a cancelled touch | Cancels. A first touch that slides off is no tap and no first half; a second that slides off has asked once, as a touch that slides off always has (Stage 5, decision 1) |
| VoiceOver activation | Logs once, with no second activation: VoiceOver's activation gesture is already a double-tap. "Log <name> with note" is unchanged |
| The Other tile | **Opens its list on a single tap.** It is a navigation, not a log: it records nothing, so an accidental tap costs a dismissed list and not a wrong entry, and a double-tap there would only slow the person down. It has no window, no hold and no hint |
| An empty position | A single tap, as before: it records nothing and opens the board setup |

**The window.** 300 ms, counted from the lift of the first tap to the touch of the second, so a slow tap does not use up the time.
It is a token, `motion.doubleTapMs` in `docs/design/tokens.json` and `Theme.motion.doubleTapMs`, with `holdMs` (500 ms), `tapHintVisibleMs`
(2000) and `tapHintIntervalMs` (5000); the owner can change them in one place after the tester has used it (checklist P6). The window is
timed on the animation clock like the hold (a `NumberAnimation` of a number that nothing reads, then a `ScriptAction`), so that
the rig's `advance` moves it; it needs `rig.steady_time()` for the reason given under 4.17.

**What a tile sends.** `pressed` is now the touch that asks, not the first touch: the second tap of a double-tap, or the end of a
hold that nothing has asked for. `held` follows `pressed` after the hold. `singleTapped` is new: one tap, and the window has run out.
`tapped` still comes on every lift inside the tile (Other uses it). `BoardGrid` sends `tileSingleTapped(row, typeId)`. The press
scale starts with `pressed` and not at the first touch, so a tile only moves when it has been logged; the first tap gives the
hint instead.

**The hint.** `TapHint` (`qml/screens/log/private/`): the toast's card in the toast's colours and text, low (the line and 12 pt
above and below it), with no tick and no button, "Double-tap to log" in `qsTr`, made by the Log screen at the first hint (a `Loader`,
so a board nobody taps wrongly pays one item). It fades in and out (`Theme.motion.toastMs`, which Reduce Motion shortens), stays
2 s, and does not come again within 5 s of the start of the last (the learner is told now and then, not at every tap). It stands
above the undo toast when that is open, and it **takes no touch**: a finger goes through it to the tile under it, because the next thing a
person does after the hint is to double-tap that tile. It is a transient state of the Log screen, drawn in the `Main` mock-up
(light and dark): it shows after a single click, and not after a double click. VoiceOver users do not meet it (they activate).

**The latency budget (NFR-03, BRD-03).** "Tap-to-feedback under 100 ms" is counted from the **touch of the second tap** (or the end
of the hold): the haptic, the press scale and the record are all in `pressed`, which is the first thing the second touch does, so the
figures of 4.5 and 4.7 (`perf.press_path`, p95 under 16 ms) and Stage 5's M2 are measured there. The first tap has no feedback to
time. The 300 ms of the window is a wait for the gesture, not latency, and a person who double-taps feels the haptic when the
second finger lands.

Package 4.17a changes `Tile`, `BoardGrid`, `LogScreen` and its `TapHint`, `Theme`, `tokens.json`, the tests that tapped a tile once
(they double-tap), and the docs. It does not change `LogSession`, `UndoController`, the bridge or the store: `recordErrorEntry` and
`attachDraft` already do what the hold needs.

Tests (`ui`): `tile.double_tap`, `tile.double_tap_window` (250 ms logs, 400 ms is two single taps, the hint comes after the window),
`tile.triple_tap`, `tile.hold` (the hold asks at 500 ms, once; a tap and a hold ask at the second touch only), `tile.slide_off`,
`tile.press_scale` (not at the first tap), `tile.other` (one tap, no hint, no hold), `tile.voiceover`; `grid.signals`; for the screen
`doubletap.logs` (the haptic and the press at the second touch, one entry), `doubletap.window`, `doubletap.triple`,
`doubletap.slide_off`, `doubletap.hint` (the text, nothing logged, no touch taken, at most once in 5 s), `doubletap.hint` above the
toast, `doubletap.hold`, `doubletap.hold_then_tap`, `doubletap.voiceover`, `log.other` (one tap opens, no hint); `perf.press_path` for the Tile and for the
Log screen, from the second tap's touch; `theme.matches_tokens` for the four values. The `longpress.*` cases of 4.17
changed with the hold (it records), and the cases that tapped a tile once now double-tap it.

### 4.13a Plain words in the note template editor (TestFlight feedback)

The tester did not understand "Prompt", "Output pattern" or that `{value}` has to be typed. The model stays as it is
(a template has a name, a question and a line with the answer in it; the file format keeps `value_prompt` and
`output_pattern`, and the stored pattern keeps `{value}`). The editor and its messages change:

1. **Mock-ups first:** `TemplateEdit` and `TemplateEditInvalid`, light and dark, and the `Templates` list rows.
2. **Words:** one sentence at the top ("Templates are quick notes you log often. Tap one on the Log screen and answer one
   question."); the fields are Name ("This is what the chip on the Log screen says."), Question to ask ("This is what you
   are asked when you tap the chip.") and Line in the log; an Insert answer button puts the answer at the cursor, shown as
   `[answer]`; an Example card under the fields says "In the log: 10:30 (Free Text) Played Morning Medley". The list rows say
   "Asks: ... Logs: ...". `validatePattern`, `addTemplate`, `updateTemplate` and the import messages use the same words.
3. **View model:** `patternToField` and `patternFromField` (`{value}` and `[answer]`, doubled braces left alone) and
   `defaultTemplateQuestion` and `defaultTemplateLine` for a new template (the question is "<Name>?" and the line is
   "<Name>: [answer]", each following the name until the person edits it). No file format change.
4. **Accessibility:** Insert answer is a named button whose description says what it puts in; the token is explained in the
   line field's description; large text is covered by the cross-cutting checks.

Tests: `editor.templates` (the labels and hints, the sentence, the example, Insert answer at the cursor, over a selection
and on a refused line, the defaults following the name and stopping once edited, the stored pattern saved as `{value}`),
`editor.voiceover`, the model's `patternToField`/`patternFromField`/default cases and message wording, the import messages,
and the visual pass for `TemplateEdit`, `TemplateEditInvalid` and `Templates`.

### 4.18 Exit-gate pass

Run the checklist on an iPhone 17 in light and dark with the tester, record the results at the end of this file,
raise the UI components' coverage evidence (`tools/coverage-floors.json` for the C++ rig and helpers), add the
`changelog.d/` fragments that are missing, and amend the implementation plan and the PRD wording this page changes (below).
The tester signs off each screen in the pull request.

## Device checklist (the exit gate)

Nothing here can be run on the development machine. Each row is done on an iPhone 17 with a TestFlight or development
build and recorded (device, iOS version, result) under "Device results" at the end of this file.

| # | Check | Covers |
|---|---|---|
| V1 | Each of the eight screens, side by side with its `.dc.html` mock-up, light mode: spacing, radii, colours, copy | Gate; NFR-05 |
| V2 | The same for the screens drawn in 4.2, and every screen in dark mode | Gate; NFR-06 |
| V3 | Safe area: header card clear of the Dynamic Island, tab bar above the home indicator, sheets and keyboard | NFR-01; U1, U5 |
| V4 | The board fits above the tab bar with all 12 tiles at least 88 pt square at every text size | NFR-02 |
| D1 | Switch the system appearance while the app is open; the app follows without a restart | NFR-06; U6 |
| T1 | Dynamic Type from the smallest to the largest accessibility size: list and note text follow, chrome stays at its cap, no clipping | NFR-07; U8 |
| A1 | VoiceOver on: every control is read with a sensible name and role, reading order is right | NFR-07; U2 |
| A2 | VoiceOver: one activation of a tile (no second) records one entry and gives the haptic; the count is in the toast announcement; no hint is announced | BRD-02 to 04; U11 |
| A3 | VoiceOver: a review row opens Edit; Delete and Copy line are reachable; board layout can be edited with the buttons | REV-02 to 04, PRF-05; U4 |
| A4 | VoiceOver: sheets and alerts trap focus and return it | NFR-07 |
| A5 | VoiceOver: the toast and the banner are announced when they appear | U3 |
| A6 | VoiceOver with a note typed: each tile has a second element, "Log <name> with note"; activating it logs the error with the note, once, and clears the note; with no note typed there is none | BRD-07; U4 |
| M1 | Instruments (animation hitches): tile presses (the second tap of a double-tap), sheet present and dismiss, toast, page push at 120 Hz | NFR-03; U7 |
| M2 | A commit in flight (a slow store) while the sheet animates: no dropped frame | NFR-03 |
| M3 | Reduce Motion on: transitions are fades and short | 4.3 |
| M4 | 120 Hz: on a ProMotion phone (iPhone 17) Instruments' Display or the Core Animation frame rate shows 120 Hz during a sheet, a page push and a swipe, not 60 (`CADisableMinimumFrameDurationOnPhone`); check with Low Power Mode off, which caps the phone at 60 Hz | NFR-03; U7 |
| M5 | Feel of the desktop defaults: the tile press (0.96, 120 ms), sheet present and dismiss (320 ms), toast in and out (240 ms), Settings page push and pop (280 ms) and row swipe release (200 ms). The `OutBack` curve overshoots by about 10 percent of the travel: the sheet must not show a gap above it nor the page stack a gap beside the page at the end of the overshoot. Note any value to change and record the final ones in "Recorded motion values" | NFR-03; U7 |
| M6 | Reduce Motion switched on and off while the app is open: sheets, toast and page changes are 100 ms fades with no slide, the tile press still scales, and the animation is the same on the next transition after the switch | 4.3 |
| P1 | Double-tap a tile: the haptic and the press are at the touch of the second tap (timed from it, not from the first), the entry exists once, the count appears after the commit, nothing else opens. A single tap gives no haptic and no press | BRD-02, BRD-03; Stage 5 decision 1 |
| P4 | Hold a tile for half a second with the note box empty: the haptic is at the hold, one entry, the Edit entry sheet opens on it with the keyboard on the note; Save puts the note on the entry's line, Cancel leaves the entry. Lifting before half a second logs nothing | BRD-07, NOTE-06 |
| P5 | Type a note, then hold a tile: the entry is logged with the note on its line and the box clears; one Undo removes the entry and the note comes back into the box. A double-tap on a tile with a note typed logs the error alone and leaves the box alone. Sliding off before half a second does neither | BRD-07 |
| P2 | Two fingers on two tiles at once record two entries | U9 |
| P3 | Tap a tile, then press it again and slide off before lifting: one entry, one haptic, and the toast offers Undo. A first tap that slides off logs nothing and shows no hint | Stage 5 decision 1 |
| P6 | The double-tap window: double-tap quickly and at a relaxed pace with the thumb and with the finger, a dozen times each; note how many miss and whether 300 ms (`doubleTapMs`) is right. Three quick taps log one entry, the fourth starts another. Tap two different tiles quickly: no entry | BRD-02; U11 |
| P7 | One tap on a tile: nothing is logged and "Double-tap to log" shows after a moment, above the undo toast if it is open; a finger through it reaches the tile; it does not show again for five seconds. Other opens on one tap | BRD-02 |
| K1 | Number pad appears for the unit number; the sheet lifts above the keyboard; dictation works in the note field | HDR-02, NOTE-05; U5 |
| F1 | The launch image hands over to the first frame with no visible jump (with Stage 5's K1 to K3) | NFR-04 |
| R1 | Cold start to a usable board stays under 1.5 s with the real UI (the gate figure is Stage 5's) | NFR-04 |

## What cannot be verified here

The development machine has no Mac and no phone. CI runs the QML on a desktop, offscreen, with the software renderer and
fakes. Not proved by any test on this page:

- **How it looks.** Fonts (SF Pro on the phone, a different font on the runner), anti-aliasing, the real colours under the
  display's colour management, and the Dynamic Island clearance. The screenshots of 4.16 and the tester's review are
  how this is judged, and the gate is her sign-off.
- **VoiceOver itself.** The tests prove the accessible tree (names, roles, order, targets). They do not prove what iOS
  speaks, the rotor behaviour, announcements (U3) or focus return. Device rows A1 to A6.
- **Dynamic Type, Reduce Motion and the appearance switch.** The tests inject scale, motion and scheme into the rig. That iOS
  delivers the changes to Qt and that Qt applies them is U6 and U8.
- **Frame rate and animation feel.** CI proves no synchronous work is in the press path and that the right animator types
  are used. It proves nothing about 120 Hz, hitches, or the feel of a spring (U7; M1 to M6).
- **Touch.** Synthesised touch events are not a finger: palm rejection, a slide-off, two simultaneous fingers, hold
  timing and whether 300 ms is the right window for a double-tap are P1 to P3, P6 and U9.
- **The keyboard and dictation.** The faked keyboard rectangle is not the real one (U5, K1).
- **Safe areas on a real phone** (U1) and the launch handover (F1).
- **macOS and iOS builds.** Verified in CI only; the pull requests say what has not run on a device.

## Cross-cutting

- **Documentation.** `docs/testing.md`: `UiRig`, the `ui` and `perf` cases of this stage and how to run them (4.1).
  `docs/ci.md`: the QML checks (4.1). `docs/architecture.md`: the QML layout and the required-property wiring (4.1, 4.15).
  `docs/building.md` changes only if a CMake option or preset does (none planned). `docs/design/`: the new screens, the dark
  tokens and the screenshot instructions (4.2, 4.16).
- **Changelog fragments.** Each screen package adds one fragment; the first is the first user-visible change in the project.
- **Preprocessor baseline.** `tools/preprocessor-baseline.json` must not rise (QML adds none; the `Q_PROPERTY` and
  `Q_OBJECT` macros in 4.3 are not directives).
- **Coverage.** The C++ in the rig and helpers is covered by the existing floor; 4.18 raises it if it rose.
- **Wording this page changes** (accepted by the owner on 2026-11-07; applied in 4.18):
  - Implementation plan, Stage 4, step 5: "label sizes that respond to the system text size where the layout allows"
    becomes the rule in [Dynamic Type](#dynamic-type) (list and note text scale without a cap; fixed-geometry chrome is capped).
  - Implementation plan, test layers: "QML tests" are Catch2 `ui` cases over the real view models (decision 1).
  - PRD NFR-02: "labels of up to three lines" and `tokens.json` `tileLabel.maxLines` 4 are reconciled as in question 9.
  - Stage 5, 5.7: the haptic and share call lines are written in Stage 4 (decision 6), so 5.7 shrinks to the import
    wiring, the launch tracker and the device pass.

## Decisions (decided by the owner, 2026-11-07)

The owner accepted every recommendation on this page on 2026-11-07. The numbering of the former open questions is kept,
so references such as "decision 4" elsewhere on the page and in other pages resolve to the decision with that number.

1. **How are QML screens tested?** Options: Qt Quick Test (`TestCase` in QML, run with a Qt-supplied runner), or Catch2
   cases that load QML offscreen and drive it from C++. **Decided: Catch2 offscreen.** One runner, one set of labels
   and the sanitiser/TSan builds as for all other tests (Qt Quick Test's runner is its own `main`), the real Stage 3 view models and fakes are
   plain C++ objects the case already owns, and it matches the owner's Catch2 decision. The cost is writing the rig (4.1) and
   asserting from C++ rather than QML. (owner, 2026-11-07)
2. **Qt Quick Controls or plain items?** Options: only plain items, or `QtQuick.Controls.Basic` types fully re-skinned.
   **Decided: Basic controls re-skinned where they supply behaviour (text input, `Popup`, `Tumbler`, `StackView`),
   plain items elsewhere (Tile, board, toast).** No default look is ever visible (NFR-05); the controls save rebuilding input and
   accessibility roles. Importing `QtQuick.Controls.Basic` per file avoids a `QQuickStyle` call and a `main.cpp` edit. (owner, 2026-11-07)
3. **How do the view models reach QML?** Options: context properties, registered singletons, or required properties from the root.
   **Decided: required properties passed down explicitly.** A screen's dependencies are visible in its first lines,
   a test injects exactly what it needs, and `qmllint` can check the types. (owner, 2026-11-07)
4. **Add the five Stage 3 gaps to Stage 3?** (UTC from local time, header-change preview, template roles, time formatting
   invokable, and the entry point for new log.) **Decided: yes,** as an addendum owned by 3.4 and 3.7 (items 1, 2, 5), 3.3
   (item 4) and the design (item 3), before 4.9. The alternative, doing the time arithmetic in QML, breaks the rule that QML holds no logic and
   risks a wrong time at daylight saving. (owner, 2026-11-07)
5. **Add an `AccessibilitySettings` bridge capability (text scale, Reduce Motion) to Stage 5's bridge, owned by Stage 4 (4.3)?**
   **Decided: yes.** NFR-07 requires Dynamic Type and Stage 5 has no item for it. Without it the fallback is
   a fixed scale, which fails NFR-07. It follows Stage 5's pattern and is small. (owner, 2026-11-07)
6. **Who writes the haptic, share and import call lines in QML: Stage 4 or Stage 5's 5.7?** **Decided: Stage 4,**
   against the `qml_bridge` methods that already exist from the spike (`prepareHaptics`, `hapticImpact`, `hapticSuccess`, the
   share through Stage 3's `ShareService`), so a screen is finished and testable in one pull request; the import and export
   rows emit signals and the shell routes them (desktop file dialog now, the Stage 5 picker later). 5.7 then keeps the launch
   tracker, the picker hookup and the device pass. The owner amends `stage-5.md` when accepting. (owner, 2026-11-07)
7. **Press-to-record and VoiceOver activation.** Stage 5 decision 1 already makes the tile record on touch-down.
   **Decided:** VoiceOver activation takes the same two calls (haptic, then record) so behaviour does not depend on how
   the tile was reached. The accidental-touch risk is handled by the undo toast, as in the approved design. (owner, 2026-11-07)
8. **Where does "start a new log" (DAY-02) live?** The design package has no menu. **Decided: a row at the bottom of
   Today's details, "Start a new log for another unit or site", behind a confirmation `Alert`** (it archives the open log),
   because that is where the tester is already looking at the unit and site. A menu on the Log screen is a new control
   she did not approve. To be drawn in 4.2. (owner, 2026-11-07)
9. **Tile label lines.** NFR-02 says up to three lines that never truncate; `tokens.json` says four. **Decided:** three lines
   at the default size, four at larger sizes up to the 1.3 cap, never truncated, with a test using the longest label of the
   sample configuration ("Sequence Out of Order"). Tile width is 118 pt, which holds it. (owner, 2026-11-07)
10. **Dark palette direction (blocks the exit gate).** **Decided:** keep the plum header card and the deep blue primary
    as the brand anchors; a near-black blue-grey background and raised surfaces; tile colours as the same 11 hues at low
    lightness and moderate saturation with light text (the tile rule "text is always `textPrimary` on the tile colour" holds, with the dark-mode `textPrimary`); no red or green; Other stays neutral with a dashed border; the
    Review text white on black, as the light mode is black on white. Drawn and contrast-checked in 4.2, approved by the tester. (owner, 2026-11-07)
11. **Time input control.** **Decided:** a custom two-column `Tumbler` (hours, minutes, with seconds and meridiem when
    the time format has them), re-skinned, VoiceOver adjustable. A text field for times invites a typo that the model must
    reject; the native `UIDatePicker` is not reachable from Qt Quick without a bridge item. (owner, 2026-11-07)
12. **Visual regression.** **Decided: no pixel golden files.** Fonts and rendering differ between the runner and the phone, so
    pixel files would fail for the wrong reasons. Instead: geometry tests against the token numbers, the contrast and
    accessibility tests, the screenshot artifact of 4.16 for review, and the device pass. The one pixel comparison in the
    project stays the launch image of Stage 5, which has no text. (owner, 2026-11-07)
13. **Who designs the screens that are not drawn?** **Decided:** drafted as `.dc.html` mock-ups in 4.2 in the
    existing format, reviewed by the owner, then approved by the tester, before the screen package starts. The cost of
    redrawing is much lower than the cost of rebuilding QML. (owner, 2026-11-07)
14. **Bold Text and Increase Contrast.** **Decided:** not handled in v1 (NFR-07 asks for VoiceOver and Dynamic Type); the
    tokens already pass 4.5 to 1. Revisit if the tester uses them. (owner, 2026-11-07)
15. **Orientation and iPad.** **Decided:** portrait only on iPhone, as NFR-01 and the plan have it; the layout uses
    `SafeFrame` and token sizes so a later iPad layout is a new set of screens, not a rewrite of components. (owner, 2026-11-07)
16. **Long-press (BRD-07, Should).** **Decided:** build it last (4.17) because it depends on the Edit sheet and has no
    effect on the gate; it can slip to 1.1. (owner, 2026-11-07) **Amended (owner, 2026-11-09), "both":** with a note typed in
    the composer, holding a tile logs the error with that note as one entry and clears the composer, and one Undo removes the
    entry and gives the note back; with the composer empty, the hold opens the Edit sheet on the new entry, focused on the
    note, as first planned. A plain tap is unchanged. VoiceOver gets "Log <name> with note" while a note is typed.
    **Amended (owner, 2026-11-09), double-tap:** the hold now records the entry itself, at the hold, because a single tap no
    longer logs (decision 18); a draft or the Edit sheet is then as above.
17. **Desktop build in CI.** The plan says the UI is iterated on a macOS desktop build; the Linux runner builds it too.
    **Decided:** keep the QML free of anything desktop-only except the dev file dialog in the shell, which 4.15 isolates in
    one file the iOS build does not load. (owner, 2026-11-07)
18. **Double-tap to log (BRD-02, 4.17a).** The tester asked for it, to avoid accidental entries. **Decided (owner,
    2026-11-09):** a tile with an error type is logged by two taps within 300 ms (`motion.doubleTapMs`), and a single tap logs
    nothing and shows the hint "Double-tap to log", at most once in 5 s. Press and hold (500 ms) stays, and records at the hold.
    The haptic, the press and the record are on the touch of the second tap, and the latency budget (NFR-03) is counted from it. The Other tile, which
    is a navigation, opens on a single tap. A VoiceOver activation logs once, because the activation gesture is itself a double-tap.
    Stage 5 decision 1 (record on the touch that asks, so that a finger that slides off cannot fire a haptic without an entry) and
    decision 7 (VoiceOver takes the same two calls) hold, with "the touch that asks" being the second tap or the hold.
