# Accessibility Nutrition Labels

What Field Test does for each of the nine labels that App Store Connect lets an app declare, the tests that prove it, what is
known to fall short, what still needs a device, and what to declare. It is a working page for the owner and is not published
(it is in `exclude_docs`). The public page is `accessibility.md`; it says only what is checked.

Apple's own wording of what each label requires is at
developer.apple.com/help/app-store-connect/manage-app-accessibility/overview-of-accessibility-nutrition-labels. Read it
again before declaring: this page records what the app does, not what Apple accepts.

**Nothing here has run on a device.** There is no Mac on the development machine, and every test named below runs in the
UI rig, against Qt's accessibility interface and not against VoiceOver or Voice Control. A label is "declare after a device
pass" until the owner has run the checklist at the end and written down what failed.

## The common tasks

Apple asks that the common tasks can be done with the feature on. For Field Test they are:

1. Log an error: double-tap a tile, or hold it to add a note, and see the toast.
2. Undo the last entry.
3. Review today's entries, edit one and delete one, and copy the log.
4. Add a note to the log, by typing or by dictation.
5. Start a new day and open a past log from History.
6. Change today's details (the unit, the place, the software version, the tester).
7. Change the board and the error types in Settings, and import or export the setup file.
8. On the Observations board (when it is switched on): log an observation and name the people.

## Summary

| Label | Proposed declaration |
|---|---|
| VoiceOver | Declare after a device pass |
| Voice Control | Declare after a device pass |
| Larger Text | Owner decides, after the device pass and Apple's criteria (the 1.3 cap below) |
| Dark Interface | Declare after a device pass |
| Differentiate Without Colour Alone | Declare after a device pass |
| Sufficient Contrast | Do not declare yet: four light pairs fall short |
| Reduced Motion | Declare after a device pass |
| Captions | Not applicable: the app has no video and no audio. Do not claim it |
| Audio Descriptions | Not applicable: the app has no video and no audio. Do not claim it |

## VoiceOver

**Today.** Every interactive item has a role and a name, and a screen reader reaches it. The tab bar, the board, the rows,
the sheets and the toast are named; a tile reads "Label, n today"; the board's second element for a held tile reads
"Log Label with note". A VoiceOver double-tap is one press action, which for a tile is one entry (it is already the
double-tap of the screen reader, so the double-tap rule of the board is not in its way). The reading order is the order on
the screen. The toast is announced.

**Tests.** `a11y.names`, `a11y.reading_order` and `a11y.targets` run on every component and screen test through
`check_everything`; `tile.voiceover`, `log.voiceover`, `doubletap.voiceover` and `longpress.voiceover` (a tile, one entry, no
hint); `settings.voiceover`, `setup.voiceover` and `editor.voiceover` (rows read their label and value, the switch its state).

**Known limits.** None known. The rig reads Qt's accessible tree, which is what Qt hands to iOS; it does not run VoiceOver.

**Device pass.** The common tasks, with VoiceOver on. Check the tile name with the count, the toast announcement, the sheets
(focus moves into them and back), and the Observations board.

## Voice Control

**Today.** Voice Control says "Tap <label>" where the label is the accessible name of a control, so the name has to contain the
words the screen shows (WCAG 2.5.3, label in name). `a11y.label_in_name` checks it on every screen: every word a control
shows, in order, ignoring case and anything that is not a letter or digit, is in its name. A tile that has a count reads "Label,
n today" and keeps the label first. The press that Voice Control sends is the one VoiceOver sends, so a "Tap" on a tile is one
entry. That Voice Control reaches the press action of a Qt control is an assumption until the device pass.

**Tests.** `a11y.label_in_name` (`rig_test.cpp` shows it fails on a name that is another word, a name that drops a line, and a
tab named otherwise, and passes on the shapes the app uses), run through `check_everything`; `voicecontrol.tap_tile` (each tile
of the board is found by its label and logs one entry of its type; Other opens the list and logs nothing), `voicecontrol.tap_undo`,
`voicecontrol.tap_observation` and `voicecontrol.unique_labels` in `log_screen_test.cpp`.

**How the check reads a control.** The words are the `Text` items inside it. Not counted: text in a nested control that a screen
reader reaches (that control owns it), text with no letter in it (a count, a glyph), a text field's placeholder (a hint that
goes when typing starts, not the field's label) and the texts of a container role that only watches the finger (a group, a
list, a pane, a dialog). An icon-only control has no label and is held to `a11y.names`.

**Known limits.**

- While a note is typed, the board adds "Log <name> with note" for each tile that can be held. "Tap <name>" then matches the tile
  and that element, and Voice Control shows numbers to choose between them. Both work.
- A multi-line row (a History row, the card at the top of the Log screen, a choice on the version sheet) has all its lines in
  its name, so the name is long.
- A control with an icon and no text can be reached by "Show numbers" and by its name, but has no visible label to say.

**Device pass.** The checklist below.

## Larger Text

**Today.** The app follows the system text size up to 3.1 times, the largest iOS size. Text that is not fixed chrome grows
with it and wraps; nothing is cut off; the board stays 4 by 3 with no scrolling.

**Tests.** `a11y.text_scale` through `check_everything` at 1.0, 1.3 and 3.1 on every screen: no `Text` is cut off, taller than
its box or past the window sideways.

**Decided limit.** The owner has decided to keep the cap of 1.3 times on the tile labels, the tab labels and the count badge
(`scaleCap: 1.3` on those roles in `Theme.qml`). Their geometry is fixed by design (a 4 by 3 board of fixed tiles with no
scrolling; a tab bar of fixed height), so they stop growing at 1.3 and everything else keeps growing. This is a decided
limit, not an open gap. Whether Apple accepts Larger Text with such a cap is a question about Apple's criteria, which the owner
reads before declaring.

**Device pass.** The common tasks at the largest accessibility size and at 1.3: the board, the tab bar, a sheet, a long row.

## Dark Interface

**Today.** The app follows the system appearance; every colour is a `Theme` token with a light and a dark value.

**Tests.** `theme.dark_resolves` and `theme.matches_tokens`; the component and screen tests run `check_everything` in light and
in dark (`set_dark`), and `a11y.contrast` walks every pair of `Theme.contrastPairs` in both.

**Known limits.** None known beyond the contrast pairs below.

**Device pass.** Every screen and sheet in dark, including the launch screen.

## Differentiate Without Colour Alone

**Today.** Each tile has a label and a count as well as a colour; no tile is red or green; two tiles differ for a person with
deuteranopia. The selected choice has a check in its ring and not only a border; a missing field of the details card
says so in words.

**Tests.** `a11y.contrast` (the tile colours, no red or green, deuteranopia floor), `tile_test.cpp`, `choice_card_test.cpp`.

**Known limits.** None known.

**Device pass.** Turn on Differentiate Without Colour (Settings, Accessibility, Display & Text Size) and walk the tasks.

## Sufficient Contrast

**Today.** `a11y.contrast` walks every pair of colours that a component puts one on another, in both schemes, at 4.5 to 1 for text
and 3 to 1 for a non-text indicator.

**Known limits.** Four light pairs are under the rule: the border of the Other tile and the placeholder text, each on two
backgrounds. They are recorded in `Theme.contrastPairs` with the ratio they have, so that they cannot get worse. A separate piece
of work is fixing them and adds Increase Contrast and Bold Text; this page does not touch it.

**Proposed declaration.** Do not declare until those four pairs are fixed and the device pass has run with Increase Contrast.

## Reduced Motion

**Today.** With Reduce Motion on, no animation lasts more than 100 ms and none slides: sheets, pages and the alert fade, and the
banner, the title bar and the tab bar have nothing that moves. Animation is by `Theme` motion tokens only.

**Tests.** `motion.reduce` on each component that moves (the alert, the banner, the title bar, the page stack, the sheet, the
sheet host, the tab bar, the other picker), `motion.animators` (no animation outside the animator types), `motion.curves`.

**Known limits.** The press feedback of a tile (a brief, small scale) does not depend on Reduce Motion. Judge it in the device pass.

**Device pass.** Turn on Reduce Motion and open and close every sheet and screen.

## Captions

Not applicable. The app has no video and no audio, so it has nothing to caption. Do not claim it.

## Audio Descriptions

Not applicable. The app has no video, so it has nothing to describe. Do not claim it.

## Device checklist

For the owner, on a TestFlight build. Write what failed in this page.

Voice Control (Settings, Accessibility, Voice Control, on):

1. Say "Show numbers". Every tile, tab and button in view has a number. "Show names" shows the same words the screen shows.
2. Say "Tap Stopped Responding" (or any tile). One entry is logged, the toast appears once, and there is no "Double-tap to log"
   hint. Say it again: a second entry. Say the number of a tile: the same.
3. Say "Tap Undo" while the toast shows. The entry is gone.
4. Say "Tap Other": the list opens. Say "Tap Cancel" or "Tap Done" on a sheet. Say "Tap Settings", "Tap Review" and "Tap Log".
5. Switch on the Observations board and repeat step 2 on it.
6. With a note being typed, say "Tap Stopped Responding". Voice Control offers numbers for the tile and "Log … with note".
7. Add a note by dictation in a field.

For each other label, switch it on and run the common tasks above: VoiceOver; Larger Text at the largest accessibility size;
Dark Interface; Reduce Motion; Differentiate Without Colour; and, after the contrast work, Increase Contrast.
