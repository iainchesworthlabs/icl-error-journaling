# CS Bingo Board – design package v1

The facilitator approved the eight light-mode screens (1 to 8 below) on 7 Nov 2026. They are the visual reference for Stage 4 of the implementation plan. Package 4.2 of Stage 4 added the screens that package lacked (9 to 34) and the dark palette; the owner reviewed every light and dark screen on 7 Nov 2026 and accepted them; Stage 4 builds them on the owner's approval (owner, 7 Nov 2026), and the facilitator approves them on a TestFlight build, with any change she asks for going into a later build (see [Review of the 4.2 drafts](#review-of-the-42-drafts)). All names, sites and error types shown are fictional placeholders; the real values come only from the configuration file at runtime.

## What is here

| Path | Contents |
| --- | --- |
| `tokens.json` | Colours (light and dark), tile palette (light and dark), type scale, spacing, radii, sizes and motion values taken from the screens. |
| `contrast.md` | The hand-checked contrast ratios of every dark text and non-text pair, and the hue check of the dark tiles. |
| `screens/Name.dc.html` | Mock-up source for each screen in light mode: plain HTML with inline styles at iPhone 17 size (402 × 874 pt). |
| `screens/Name.dark.dc.html` | The same screen in dark mode. Every screen below has both files. |

The `.dc.html` files were authored for a design-canvas runtime (`support.js`, not included), so they do not open as working pages on their own. Read them as a precise layout reference: structure, sizes, spacing, colours and copy are all in the inline styles and text. `Main.dc.html` also carries the intended board behaviour in its script block (count badges, undo toast, repeat-tap "×N").

## Screens

| # | File | Screen | Key behaviour |
| --- | --- | --- | --- |
| 1 | `Main.dc.html` | Log (main board) | Header card (unit, site, version, date, facilitator) opens Today's details. Note composer with template chips. "Last" bar shows the latest entry and today's count, and opens Review. 3 × 4 board: 11 error tiles with count badges plus Other. Undo toast for 5 s, "×N" on repeat taps. Tab bar: Log, Review, History, Settings. |
| 2 | `Other.dc.html` | Other error picker | Sheet with search, "Not on your board" types, board types with today's counts, and "Add a new error type". |
| 3 | `Header.dc.html` | Today's details | Facilitator and date (read only), unit number with fixed prefix and numeric keypad, an optional Environment row (None, prod, dev) that opens a short list, location list with Other, software version with inline validation, "Start logging". Errors can be logged before every field is filled. |
| 4 | `ChangeVersion.dc.html` | Software version | Two plain choices: "It changed during the day" (adds a timed entry) or "I typed it wrong" (corrects, no entry). From and To, time defaulting to now, and a preview of the exact log line. |
| 5 | `Review.dc.html` | Today's log | Header block and timeline in export wording, black and white only. Swipe a row for Copy and Delete. Copy-all in the navigation bar, "Share log" button. |
| 6 | `EditEntry.dc.html` | Edit entry | Sheet with Cancel and Save: error type, time, note, a preview of the shared line, and "Delete entry". The facilitator chose a separate sheet over editing in place. |
| 7 | `NewDay.dc.html` | New day prompt | Names the new date; warns when the open log is unshared (never shared, or changed since its last share: "has changed since it was last shared"). "Start a new log", "Share [day]'s log first", "Keep [day]'s log open". |
| 8 | `BoardSettings.dc.html` | Settings | Miniature board: hold and drag to move, tap to swap a type. Rows for error types, note templates, locations, facilitator, time format, and import or export of the setup. |

### Screens added by package 4.2 (drafts)

Each row has a light file and a dark file (`Name.dc.html`, `Name.dark.dc.html`). Bindings to the Stage 3 objects are in the "Screens and what they bind to" table of `docs/plan/stage-4.md`.

| # | File | Screen | PRD | What it shows |
| --- | --- | --- | --- | --- |
| 9 | `History` | History | DAY-04 | Past logs newest first, grouped under the local date (two logs of one date under one heading), unit and place, entry count, version, and the shared state as a tick plus time or an empty circle plus "Not shared" (not colour alone). A log that was shared and changed since shows the empty circle and "Changed since sharing", and its Review line reads "Changed since sharing on 5 Nov at 16:58". A swipe reveals Share and Delete. The open log is not listed. |
| 10 | `HistoryEmpty` | History, empty | DAY-04 | The empty state with a one-line explanation. |
| 11 | `PastLogReview` | A past log's review | DAY-04 | The Review screen bound to `history.reviewModel`: back to History, copy-all, header block, rows, "Shared 5 Nov at 16:58" and "Share log again". Same black-and-white rule as Review; rows open Edit entry. |
| 12 | `DeleteLog` | Delete-log confirmation | DAY-05 | An alert naming the date, unit, place and entry count, with "Delete log" and "Cancel". Not dismissed by a tap outside it. |
| 13 | `LogBanner` | Log with empty header fields | HDR-06 | A strip inside the header card, "Not filled in yet: UNIT number, Location, Version". It sits in the card, not as a separate banner, because the board is 440 pt tall and a banner would push the fourth row under the tab bar. Tiles stay live. Binds to `session.missingFields`. |
| 14 | `TemplateValue` | Note template value prompt | NOTE-04 | A sheet over the Log screen with the template prompt, a focused field, the line it will add, and Cancel and Add. The sheet sits above the keyboard. |
| 15 | `WriteFailed` | Write failed | NFR-05 | An error banner that floats over the header card (so the board never moves), saying what was not saved and what to do. No dialog. |
| 16 | `Recovery` | Recovery notice | DS-04 | A warning banner floating over the header card with what was recovered and "Check the log in Review". Binds to `session.recoveryNotice`. |
| 17 | `HeaderNewLogRow` | Today's details, scrolled to the end | DAY-02 | The row "Start a new log for another unit or site" at the bottom (decision 8), with a one-line explanation. |
| 18 | `StartNewLogConfirm` | Start a new log, confirmation | DAY-02 | An alert: what happens to the open log, who carries over, "Start a new log", "Share this log first" (shown when unshared) and "Cancel". |
| 19 | `TimeTumbler` | Time tumbler, 24-hour | REV-02, HDR-07 | A sheet with Hours and Minutes columns, a selection band, a "Now" chip and the time as the log will show it (decision 11). VoiceOver: each column is adjustable (swipe up or down). |
| 20 | `TimeTumbler12` | Time tumbler, 12-hour with seconds | PRF-07 | Hours, Minutes, Seconds and AM/PM columns, as the chosen time format requires. |
| 21 | `ErrorTypes` | Error types editor | PRF-04 | Add row, the list with colour swatch, drag handle and Move earlier and Move later arrows (so reordering needs no drag), and a note on the 11-tile board. |
| 22 | `ErrorTypeEdit` | Rename or retire an error type | PRF-04 | A sheet with the name field and its inline refusal (duplicate name), the colour that stays with the type, and "Retire this error type". |
| 23 | `Locations` | Locations editor | PRF-03 | Add field, list with handles and Remove buttons. |
| 24 | `Templates` | Note templates list | PRF-06 | Each template with the question it asks and the line it logs ("Asks: ... Logs: ... [answer]"); "Add a template". |
| 25 | `TemplateEdit` | Edit a template | PRF-06, NOTE-04 | One sentence on what a template is; Name, Question to ask, Line in the log with an Insert answer button (the answer shows as `[answer]`), and an Example card with the line as the log will show it. |
| 26 | `TemplateEditInvalid` | Template pattern refused | PRF-06 | The same page with the inline message from `validatePattern`, no example, Save disabled. |
| 27 | `FirstLaunch` | First launch | PRF-09 | "Import a setup file" or "Set up by hand", with one line on what a setup file holds. |
| 28 | `FirstLaunchManual` | First launch, by hand | PRF-09 | Facilitator and UNIT prefix, then Continue. |
| 29 | `ImportExport` | Import or export | PRF-08 | The two rows, what a setup file does and does not contain, and the "Exported to Files" confirmation. |
| 30 | `ImportConfirm` | Import confirmation | PRF-08 | The counts from `importPrepared`, the warning that the setup is replaced and past logs are not, "Replace my setup" and Cancel. |
| 31 | `ImportErrors` | Import refused | PRF-08 | The core's messages in file order, "Nothing was changed", "Choose another file". Binds to `importFailed`. |
| 32 | `BoardLayoutSelected` | Board layout, a tile selected | PRF-02 | The selected miniature tile (outline, lift and a tick, not colour alone) and the Move earlier, Move later and Replace type buttons that are the VoiceOver path. |
| 33 | `TileLabels` | Longest label, default size | NFR-02 | "Sequence Out of Order" on the board at 13.5 pt: three lines, never cut (decision 9). The dashed note is an annotation, not UI. |
| 34 | `TileLabelsLarge` | Longest label, largest size | NFR-02, NFR-07 | The same board at the 1.3 cap (17.5 pt). A tile holds four lines at that size. |

Screen 7 (`NewDay`) was already drawn, and the original eight are unchanged in light mode.

## Dark mode

Decision 10 of the Stage 4 plan, drawn in `*.dark.dc.html` and held in `tokens.json` under `colour.dark` and the `darkHex` of each tile. The ratios are in [`contrast.md`](contrast.md).

- **Anchors.** The plum header card `#5B2A63` and the deep blue primary keep their place. The primary button fill is a step lighter, `#2A6F96`, so it stays 3 to 1 against the near-black screen; its white label is 5.5 to 1. Links, Back, Cancel, check marks and the selected tab use `primaryText` `#8FC6E6`, because the fill colour is too dark to read as text.
- **Surfaces.** Near-black blue-grey: screen `#0E141A`, raised card `#18212A`, sheet `#141C24` with cards `#212C37` on it, so a card is lighter than what is behind it at every level.
- **Text.** `textPrimary` `#EEF2F6`, `textSecondary` `#A7B6C3`, placeholder `#93A3B0`. All 4.5 to 1 or better on every surface they sit on.
- **Tiles.** The same 11 hues at lightness 24 to 32 per cent and saturation 14 to 50 per cent, with `textPrimary` on them (7.3 to 9.1 to 1). The count badge flips to light (`#F2F4F7` with dark text) because the tiles are now the dark element. No red or green hue.
- **Other.** Stays neutral: `surface` fill and a 1.5 pt dashed `borderStrong` border (4.3 to 1 or better).
- **Review.** White on black (`#FFFFFF` on `#000000`, 21 to 1), with times and markers `#B4C0CA`. Still no colour in the list; the cards have a hairline `#2A2F34` border so they read on black.
- **Destructive.** Delete text is `#FF8F85` on a card; a Delete button or swipe action has a `#8C2A22` fill with a white label. Red is used only for destructive actions and errors, never for a tile.
- **Banners.** Three tones, each with an icon and an outline as well as a fill: information (deep blue tint), warning (dark amber), error (dark red-brown). Text on all of them is `textPrimary`.
- **Scrims** are near-black in dark mode, so a sheet or alert is lighter than the dimmed screen behind it.

How the dark variants of the original eight screens were made: each light colour was replaced by the dark token with the same role (screen, card, sheet card, text, secondary text, border, link, tile slot), with no layout change.

## Review of the 4.2 drafts

1. The owner opens the `.dc.html` files (see "What is here" for how) and the dark ones beside them, checks them against `docs/plan/stage-4.md` and the PRD, and asks for changes. Redrawing at this stage is cheap; rebuilding QML is not.
2. The facilitator approves the screens and the dark palette after the owner is satisfied. The approval is recorded by changing the first paragraph of this file and the "source" line of `tokens.json`.
3. A screen package of Stage 4 starts when the owner has approved the screens it builds (owner decision, 7 Nov 2026). The facilitator's approval comes from using them on TestFlight; what she asks for is fixed in a later build. The dark palette also gates the Stage 4 exit gate.
4. Package 4.1 turned the contrast table into a test (`Theme.contrastPairs` and `contrast_test.cpp`, which run every pair in both schemes). A change to a dark value updates `contrast.md` in the same pull request. The test also holds the approved light palette to the same rule, and finds four pairs under it: the border of the Other tile (about 2.2 to 1 on the screen, 2.4 on a card, against 3) and the placeholder text (about 4.2 to 1 in a field, 4.4 on a card, against 4.5). They are recorded in `Theme` as exceptions at the ratio they have, so they cannot get worse, and are for the owner to decide. Under a simulated deuteranopia the light slots 4 and 7 (aqua and steel) are 0.3 apart, which is the same colour; the label carries the meaning of a tile, never the colour.
5. Package 4.1 added to `tokens.json` what the QML needs so that it writes no literal: the colours `textOnPrimary`, `textOnHeaderCard`, `textOnDestructive` and `toastText` (white, as in `contrast.md`), the sizes `tileWidthPt` 118 and `tileMinimumPt` 88, the `scaleCap` of the four capped type roles and the range of the text scale, and starting values for the motion of a sheet, a toast, a page and a swipe, which package 4.14 recorded as desktop defaults (with the easing curves, in `motion.easing`), to be confirmed on the iPhone in 4.18; `motion.curves` holds them.

## Human factors behind the layout

- **Thumb zone.** The board fills the lower part of the screen for one-handed use; the rarely changed header sits at the top.
- **Spatial memory.** Fixed positions and a colour per error type let the facilitator find a tile without reading. Colours follow the type when the board is reordered.
- **No interruptions on capture.** A tap never opens a dialog. Feedback is immediate (haptic, press animation, "Last" bar) and the undo toast confirms what was logged and when.
- **Slip detection.** A second tap on the same tile while its toast is visible shows "×2", making accidental double taps obvious.
- **Recognition over recall.** Template chips start common notes; Other lists every type with search.
- **Correct later, not now.** Edits and deletes live on the review screen; destructive actions are behind a swipe or a separate button.
- **Plain-language branching.** The version-change sheet asks what happened in the facilitator's words and previews the line it will write.
- **Protect the end-of-day hand-off.** The new-day prompt flags an unshared log and offers to share it first.

## Decisions taken during design review

- Board is 3 columns × 4 rows in portrait (the original concept was 4 × 3 landscape); 4 columns would break the 88 pt minimum and truncate labels.
- Template chip names: "Played song" and a second template (the real name is in the private configuration).
- Editing uses a separate sheet (screen 6), not in-place editing.
- The log and review text is black and white; only the tiles are coloured.
