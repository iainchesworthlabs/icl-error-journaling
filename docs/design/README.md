# ICL Error Journaling – design package v1

The facilitator approved these light-mode screens on 7 Nov 2026. They are the visual reference for Stage 4 of the implementation plan. All names, sites and error types shown are fictional placeholders; the real values come only from the configuration file at runtime.

## What is here

| Path | Contents |
| --- | --- |
| `tokens.json` | Colours, tile palette, type scale, spacing, radii, sizes and motion values taken from the screens. |
| `screens/*.dc.html` | Mock-up source for each screen: plain HTML with inline styles at iPhone 17 size (402 × 874 pt). |

The `.dc.html` files were authored for a design-canvas runtime (`support.js`, not included), so they do not open as working pages on their own. Read them as a precise layout reference: structure, sizes, spacing, colours and copy are all in the inline styles and text. `Main.dc.html` also carries the intended board behaviour in its script block (count badges, undo toast, repeat-tap "×N").

## Screens

| # | File | Screen | Key behaviour |
| --- | --- | --- | --- |
| 1 | `Main.dc.html` | Log (main board) | Header card (unit, site, version, date, facilitator) opens Today's details. Note composer with template chips. "Last" bar shows the latest entry and today's count, and opens Review. 3 × 4 board: 11 error tiles with count badges plus Other. Undo toast for 5 s, "×N" on repeat taps. Tab bar: Log, Review, History, Settings. |
| 2 | `Other.dc.html` | Other error picker | Sheet with search, "Not on your board" types, board types with today's counts, and "Add a new error type". |
| 3 | `Header.dc.html` | Today's details | Facilitator and date (read only), unit number with fixed prefix and numeric keypad, location list with Other, software version with inline validation, "Start logging". Errors can be logged before every field is filled. |
| 4 | `ChangeVersion.dc.html` | Software version | Two plain choices: "It changed during the day" (adds a timed entry) or "I typed it wrong" (corrects, no entry). From and To, time defaulting to now, and a preview of the exact log line. |
| 5 | `Review.dc.html` | Today's log | Header block and timeline in export wording, black and white only. Swipe a row for Copy and Delete. Copy-all in the navigation bar, "Share log" button. |
| 6 | `EditEntry.dc.html` | Edit entry | Sheet with Cancel and Save: error type, time, note, a preview of the shared line, and "Delete entry". The facilitator chose a separate sheet over editing in place. |
| 7 | `NewDay.dc.html` | New day prompt | Names the new date; warns when the open log has not been shared. "Start a new log", "Share [day]'s log first", "Keep [day]'s log open". |
| 8 | `BoardSettings.dc.html` | Settings | Miniature board: hold and drag to move, tap to swap a type. Rows for error types, note templates, locations, facilitator, time format, and import or export of the setup. |

Not drawn yet, to be designed in Stage 4 to match: the History list, the banner shown when header fields are empty (HDR-06), first-launch setup (PRF-09), the error-type, template and location editors, the recovery message (DS-04), and all dark-mode variants.

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
