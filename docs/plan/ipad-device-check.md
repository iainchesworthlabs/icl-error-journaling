<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# iPad device check: a runbook for the owner

Follow this on a real iPad with a TestFlight build. It is the device checklist (D1 to D12) and the assumptions (A1 to A6)
of [Stage 8](stage-8.md), as steps. Nothing here has run on a device: no agent can run one.

Use the iPad mini if you have it (it is the tightest in landscape) and one 11-inch or 13-inch iPad. Do the whole run on
the mini first. Write down **pass** or **fail** for each step. For a fail, fill in the [bug report](#bug-report-template)
and paste it back to an agent.

## Names in this page

- The first tab is **Errors**. The second board has its own tab. This page calls it **the Interactions board**, the name
  the app shows today. If the app shows another name when you run this, use that name where this page says Interactions.
- The Interactions tab and board exist only when the setup file turns the second board on. If you do not see the tab, it
  is off: use a setup file with `interactions_board_enabled` true (step S2 below, or `config/sample-config.json`).
- "Wide" is a window at least 1100 pt wide and 640 pt high: every iPad in landscape, full screen. "Regular" is 600 pt
  wide or more but not Wide: iPad portrait, and two-thirds Split View. "Compact" is under 600 pt: the phone layout, and
  one-third or half Split View, and Slide Over.

## Before you start

1. **Install** the TestFlight build and open it once. Note the build number (Settings tab, About Field Test) and the iPadOS version
   (Settings, General, About).
2. **Settings, Display & Brightness:** Appearance is Automatic or you will switch it by hand. Settings, Display &
   Brightness, Auto-Lock: Never, for the run.
3. **Settings, Accessibility, Display & Text Size, Larger Text:** note the current size to restore it later.
4. **Rotation lock** is off (Control Centre).
5. **Open this page on a second device**, or print it. Open [`docs/design/screens`](../design/screens) beside you: the
   mock-ups are `Name.ipad-portrait.dc.html`, `Name.ipad-landscape.dc.html`, and `.dark` for dark.
6. Set up a log to work with (step S1). Then you can run the checks in any order, but D7 needs a cold start, so do it
   when the app is not in memory.

### S1. First launch and a working log

1. Open the app. **Expect:** First launch, a 480 pt column in the middle of the screen, with **Import a setup file** and
   **Set up by hand**.
2. Tap **Set up by hand**. Fill in a tester name, a unit prefix (UNIT), and finish.
3. **Expect:** the Errors tab with the board of 12 tiles (3 columns by 4 rows), and a new log.
4. Tap about five tiles. **Expect:** each tap adds one entry and a toast with Undo, and the count on the tile goes up.

**Pass:** all four expects.

### S2. Turn on the second board

1. Get `config/sample-config.json` onto the iPad (AirDrop or Files; step D11 is the check of that, so for now any way
   works).
2. Open it from Files in the app, confirm the import.
3. **Expect:** a second tab (Interactions) appears, and the Errors tab still has its board.

If you cannot, skip the Interactions checks and say so.

## Rules for every screen

These apply in every step below. A step passes only if all of these hold, so do not repeat them in each step:

- Nothing is cut off at an edge, under the status bar, under the home indicator, or under the tab bar.
- No text is cut off or overlaps other text.
- Every button is easy to hit (about the size of a fingertip; the rule is 44 pt).
- Nothing is empty that should have content.
- Nothing flashes in the wrong layout when the window changes size.

## D1. The nine screens, both orientations, light and dark

Do each of the four combinations: portrait light, portrait dark, landscape light, landscape dark. To switch dark and
light, use Control Centre, brightness, Appearance.

For each, open the screen, hold the mock-up beside it, and compare.

| Screen | How to open it | Mock-up | What should be true |
|---|---|---|---|
| Log | The Errors tab | `Main` | Portrait: a 680 pt column: header card, composer, **Last** bar, 3 × 4 board, big tiles. Landscape: the board on the left (about 58 per cent), the log on the right: header card, **Today's log**, the rows, **Share log**. No Last bar in landscape. |
| Review | The Review tab | `Review` | Today's log in a centred reading column. |
| Today's details | Tap the header card | `Header` | A form column with the same fields as the phone, in the same order. The page scrolls. |
| Settings | The Settings tab | `BoardSettings` | The miniature board and the lists in a form column. The large title and Back or Done line up with the column, not the screen edge. |
| History | The History tab | `History` | The list in a reading column. |
| Other (sheet) | On the Errors tab, tap the **Other…** tile | `Other` | A centred card 560 pt wide with four rounded corners, no grabber, over a dimmed screen. |
| Edit entry (sheet) | Tap an entry in Review (portrait) or in the log pane (landscape) | `EditEntry` | The same card. Note: it may be taller than the mock-up; this is a known difference, record it and move on. |
| New day (alert) | See below | `NewDay` | The 338 pt alert card, centred. |
| First launch | See below | `FirstLaunch` | The 480 pt column, centred vertically. The two buttons are the phone's, not the mock-up's cards: that is known. |

To see New day: set the iPad's date to the next day (Settings, General, Date & Time, off Set Automatically), then open the
app. Set the date back afterwards. If this is awkward, skip it and say so.

To see First launch again: delete the app and install it again, or use a second iPad that has no data. Do this last, after
the rest, since it loses the log.

**Pass** per combination: every row true. Record any difference you can see from the mock-up beyond the known ones.

## D2. Rotate with things open

1. Portrait. Tap the note field in the composer, type "rotate test" (do not add it).
2. Tap a tile, so a toast with Undo is up.
3. Tap **Other…**, so the sheet is open.
4. Rotate to landscape. **Expect:** the note text is still there, the sheet is still open and now centred in the new
   window, the toast is still there or has timed out (not left in the wrong place), and the tile count is right.
5. Rotate back. **Expect:** the same.
6. Close the sheet. Tap Undo on a toast that is still up. **Expect:** it undoes that tile's entry only.
7. **Tap burst:** with the iPad in your hand, tap one tile ten times as quickly as you can while rotating the iPad.
   **Expect:** the count on that tile goes up by exactly the number of taps you made, and the log has that many entries
   (check Review). No tap is logged twice and none is lost. If you are unsure how many you made, tap slowly: ten taps,
   rotate on the fifth.

**Pass:** all expects. A lost or doubled entry is the worst fail: stop and report it first.

Assumption **A1** is this step: the window resizes with the iPad and the safe areas follow.

## D3. All four orientations, and the iPhone

1. Turn the iPad all four ways: portrait, upside down, landscape with the home indicator at the right, and at the left.
   **Expect:** the app turns to each, and the layout is right in each (portrait or landscape rules as D1).
2. If you have an iPhone with the build: **Expect** that it stays portrait however you hold it.

## D4. Split View

1. Open Notes. Drag the app from the Dock to the edge of the screen to put it in Split View. Do it in portrait and in
   landscape.
2. Drag the divider through the three sizes: about one third, half and two thirds.

| Window | Class | What should be true |
|---|---|---|
| One third | Compact | The phone layout. All 12 tiles visible above the tab bar. |
| Half | Compact or Regular | Compact if under 600 pt wide: the phone layout. Otherwise Regular: a centred column and larger tiles. |
| Two thirds | Regular | A centred column, larger tiles. |

3. In a short half-screen window on a mini (landscape): **Expect** all 12 tiles are visible above the tab bar, each at least
   about the height of a fingertip. The Last bar may be gone: that is allowed.
4. Attach a hardware keyboard (or use the Smart Keyboard) and detach it. **Expect:** the app works both ways; with the
   keyboard, Return in the note field adds the note and Tab moves between fields on a form.
5. Slide Over: swipe the app in from the right edge over Notes. **Expect:** the phone layout in a narrow window (about 320
   pt), no cut-off tiles.

**Pass:** each window matches its class, and nothing is lost when you drag the divider.

## D5. Stage Manager

Requires an iPad that has Stage Manager (iPad Air or Pro with M-series chips, and some others). Turn it on in Control
Centre. If your iPad has no Stage Manager, say so and skip.

1. Open the app in a window. Drag its corner to make it as small as it will go. **Expect:** it stops at about 375 × 720
   pt. It does not go smaller. (This is assumption **A2**; if it does go smaller, record the size it reached and whether
   the Log tab's bottom row runs under the tab bar.)
2. Drag it to full screen, slowly, then quickly. **Expect:** the layout changes class (Compact, Regular, Wide) with no
   frame in the wrong layout, no lost entry, and the count on the tiles unchanged.
3. With a note typed in the composer, resize the window across a class change. **Expect:** the note is still there.

**Pass:** all expects.

## D6. The software keyboard

Do this in portrait and landscape, with the keyboard docked and then floating (pinch out on the keyboard to float it).

1. Errors tab: tap the note field in the composer. **Expect:** the field stays visible above the keyboard, not hidden.
2. Open Today's details and tap each text field in turn (unit, location and so on). **Expect:** each stays visible.
3. Open **Other…**, or Edit entry, and tap the note field in the sheet. **Expect:** the sheet moves above the keyboard
   and **Cancel** and **Save** can still be reached.
4. Dismiss the keyboard. **Expect:** the sheet and screen return to their place.

**Pass:** the field and the sheet buttons are never hidden.

## D7. Launch and cold start

1. Swipe the app away from the app switcher. In portrait, open it. **Expect:** only a plain background (white in light,
   dark in dark) shows at first, then the board. No phone-sized picture in the middle of the screen. (This is assumption
   **A4**.)
2. Repeat in landscape (hold the iPad in landscape, then open it).
3. Time the start with a stopwatch, from the tap on the icon to the board being usable, and say that is how you measured it (the app has no on-screen timing line that this page can point you to).
   Record the number. There is no budget on the iPad; it is for the record.

**Pass:** only the background colour, then the board, both orientations.

## D8. VoiceOver in landscape

Turn on VoiceOver (Settings, Accessibility, VoiceOver, or ask Siri). Use the landscape layout on the Errors tab.

1. Swipe right through the screen from the top. **Expect**, in this order: the header card, the composer (chips, note
   field, Add), the tiles, then the log pane (Copy whole log, the rows, Share log), then the tabs.
2. **Expect** each tile reads its name and count, each row reads its time and text, and each button has a name.
3. Switch to portrait and swipe through. **Expect:** the order is the phone's: header card, composer, Last bar, tiles, tabs.

**Pass:** the order and every name. Anything read as "button" with no name, or "image", is a fail.

## D9. Larger Text

1. Settings, Accessibility, Display & Text Size, Larger Text, Larger Accessibility Sizes on, and move the slider to the
   largest.
2. In portrait (Regular) and landscape (Wide), open each of the nine screens of D1.
3. **Expect:** no text is cut off. The tile labels stop growing at a cap (about 1.3 times their usual size) and do not
   overflow the tiles. The longest label is "Sequence Out of Order".
4. Restore the text size when done.

**Pass:** no cut-off text anywhere. A screen that must scroll to fit is acceptable if it scrolls.

## D10. Share log, and the popover anchor

The share sheet on an iPad is a popover. It should point at the button that opened it. This is assumption **A3**.

1. Errors tab in landscape: tap **Share log** in the log pane. **Expect:** a popover arrow pointing at that button, not
   at the middle of the screen or a corner.
2. Dismiss it. Review tab (portrait): tap **Share log**. **Expect:** the popover arrow points at the Share log button.
3. History: swipe a past log and tap Share (or the share action). **Expect:** a popover. The arrow may point at the middle
   of the screen: that is known for History and the alert buttons. Record where it points.
4. In any of them, choose a place to share to (use Slack, if installed, or Messages, or Copy). **Expect:** the log text
   arrives, with the header, in order. Post to Slack once to a test channel, since the owner uses that.
5. Rotate while the popover is open, if you can. **Expect:** it moves with the button or closes; it does not stay at an old
   place.

**Pass:** steps 1 and 2 point at their buttons; step 4 arrives. Step 3 is recorded, not a fail.

## D11. A setup file arrives (Files and AirDrop)

Use `config/sample-config.json` from the repository. This is assumption **A6** (the document picker is a sheet on an
iPad without an anchor).

**From Files**

1. Put the file in Files (iCloud Drive or On My iPad).
2. In the app, Settings, Import or export, **Import a setup file** (or, on First launch, **Import a setup file**).
   **Expect:** a document picker appears as a sheet or popover, without a crash, and the file can be selected.
3. Choose the file. **Expect:** a confirm alert, then the import. The tiles, error types and locations match the file.

**From AirDrop**

4. AirDrop the file from a Mac or an iPhone to the iPad. Choose **Open in** the app (or, if the app does not appear in the
   list, note which apps do).
5. **Expect:** the app opens (or comes forward) and asks to confirm the import. Confirm. The same result as step 3.

**Pass:** both routes import. If AirDrop does not offer the app, record it: it may be a document-type setting, not an
iPad problem.

## D12. The app icon

1. Look at the Home Screen. **Expect:** the icon is the dark tile "t" and the white tiled "f", not cropped, with no white
   border and no padding that looks wrong. Compare it with the iPhone's.
2. Drag the app to the Dock. **Expect:** it looks right at that size.
3. Search for it with Spotlight. **Expect:** the icon and the name.

**Pass:** the icon looks right at every size.

## Further checks the plan lists

### The Interactions board on the wide layout

If the second board is on (step S2):

1. Errors tab, landscape. Tap the **Interactions** tab. **Expect:** the same side-by-side layout: the board on the left
   with a title strip that reads **Interactions** in the second board's colour, the log on the right. The tiles are the
   second hue.
2. Tap a tile with a person (for example, "Spoke with [person]"). **Expect:** a person picker as a sheet (a form sheet
   in a wide window), and the entry appears in the log pane with the person's name, the way Review shows it.
3. Portrait: **Expect** the phone layout in a centred column, with the same strip.
4. Rotate with the picker open. **Expect:** the picker moves to the new centre and keeps its choice.
5. Settings, **Interaction types** (the editor): **Expect** a form column, with a sheet to add or edit a type.

**Pass:** each expect, in light and dark.

### Light and dark, together

Switch between light and dark while the app is open, in each orientation. **Expect:** the colours change at once, nothing
is left in the old scheme, and text is readable everywhere (look especially at the tile labels and the toast).

## Assumptions: where each one is checked

| # | Assumption | Step | Pass if |
|---|---|---|---|
| A1 | The window resizes with the iPad, and the safe areas follow | D2, D4, D5 | No cut-off at any edge after any resize |
| A2 | Stage Manager honours the 375 × 720 minimum | D5 step 1 | The window stops at about that size |
| A3 | The share popover points at its button | D10 | The arrow is on the button for steps 1 and 2 |
| A4 | An iPad launch shows only the background colour | D7 | No picture in the middle |
| A5 | App Store Connect accepts the build with four orientations and no full-screen flag | The upload of the build | The build appears in TestFlight with no error mail |
| A6 | The document picker works as a sheet on an iPad | D11 | The picker opens, selects a file, and returns |

A5 is already answered if you are running a TestFlight build made after the plan's package 8.7. If the build you have
was uploaded before, say which build number.

## Bug report template

Copy this for each problem, fill it in, and paste it to an agent. One report per problem. Do not put real names, real
unit numbers or real locations in the text or the screenshot: use the stand-ins from the docs (Sam Example, Alex Sample,
UNIT, North Site). Take screenshots with the iPad's own buttons (Top button and Volume Up) and cover anything real before
you send them.

```text
Step: (for example D4 Split View, half)
iPad: (for example iPad mini A17 Pro)
iPadOS version:
App build:
Orientation: portrait / landscape (which way up) / not applicable
Appearance: light / dark
Window: full screen / Split View (a third, half, two thirds) / Slide Over / Stage Manager (size, if known)
Text size: default / larger (how large)

What I did:
1.
2.
3.

What I expected:

What happened:

How often: always / sometimes / once
Screenshot: (attach; cover anything real)
Anything else: (a hardware keyboard? the second board on? a note being typed?)
```

## Result sheet

| Step | Pass / fail | iPad | iPadOS | Notes |
|---|---|---|---|---|
| S1 first launch and log | | | | |
| S2 second board on | | | | |
| D1 portrait light | | | | |
| D1 portrait dark | | | | |
| D1 landscape light | | | | |
| D1 landscape dark | | | | |
| D2 rotate with things open | | | | |
| D3 four orientations | | | | |
| D4 Split View and Slide Over | | | | |
| D5 Stage Manager | | | | |
| D6 keyboard | | | | |
| D7 launch, cold start (time) | | | | |
| D8 VoiceOver | | | | |
| D9 Larger Text | | | | |
| D10 share popover | | | | |
| D11 setup file by Files and AirDrop | | | | |
| D12 app icon | | | | |
| Interactions board, wide | | | | |
