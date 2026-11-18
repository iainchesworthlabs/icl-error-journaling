# Accessibility: Voice Control, and one page that records the nine Accessibility Nutrition Labels

The owner wants to declare Apple's Accessibility Nutrition Labels in App Store Connect. Apple's rule, as the old Stage 11
plan put it, is to declare only what has been shown to work. A survey of the app found that most of the work is built and
tested, but nothing records it in one place, and one label has a gap: Voice Control needs each control's accessible name to
contain the label the screen shows (WCAG 2.5.3, label in name), and no test checks that.

This page is the task breakdown and the test list. It is a plan only: no production code is written until the owner has
approved it.

## What is known

The state of the nine labels today, from reading the code and the tests:

| Label | What exists | Tests | Gap |
|---|---|---|---|
| VoiceOver | Role and name on every control; reading order; a VoiceOver activation is one press action | `a11y.names`, `a11y.reading_order`, `audit.names_complete`, `tile.voiceover`, `log.voiceover`, `doubletap.voiceover` | Not run on a device |
| Voice Control | The same names | None that checks label in name | The check; the device pass |
| Larger Text | Dynamic Type to 3.1 times; tile labels, tab labels and the count badge are capped at 1.3 by design (`scaleCap: 1.3` in `Theme.qml`) | `a11y.text_scale` at 1.0, 1.3 and 3.1 | The caps, and a device pass |
| Dark Interface | A dark scheme for every screen | Rig tests in light and dark (`set_dark`), `contrast_test.cpp` | Not run on a device |
| Differentiate Without Colour Alone | Each tile carries a label as well as a colour; state is not colour alone | `a11y.*`, `tile_test.cpp` | Not run on a device |
| Sufficient Contrast | `Theme.contrastPairs` walked in both schemes | `contrast_test.cpp` | Four light pairs are recorded exceptions; another session is fixing them and the Increase Contrast and Bold Text settings. Not touched here |
| Reduced Motion | Animations of 100 ms or less and none that slides when Reduce Motion is on | `motion.reduce` | Not run on a device |
| Captions | The app has no video and no audio | n/a | Not applicable. Must not be claimed |
| Audio Descriptions | The app has no video and no audio | n/a | Not applicable. Must not be claimed |

Tile taps. `Tile.qml` sets `Accessible.onPressAction` to `pressed()` then `tapped()` (or `tapped()` alone for an empty
position). It has no touch-down, so the double-tap rule (two taps, or a hold) is not in the way: a VoiceOver activation is
already a double-tap of the screen reader. Voice Control's "Tap" is delivered to the same action on iOS, which is the
assumption the device pass has to confirm. It is already tested at two levels: `tile.voiceover` (the signal pair, no hint)
and `log.voiceover` / `doubletap.voiceover` in `log_screen_test.cpp` (one entry, one haptic, no hint, a second activation is a
second entry). What is not tested is the step before the press: finding the tile by what a person would say, its label, the
way Voice Control does. That is the new test below.

## Findings that change the brief

1. **`docs/accessibility.md` already exists.** It is the public Accessibility page of the site (marketing template, listed in
   `FIXED` in `tools/ci/check_site_pages.py`, four short bullets, "It lists only what has been checked"). A status table with
   "needs a device pass" and known limits is not copy for a public page.
2. **`docs/plan/stage-11.md` and `stage-4.md` were deleted** in #287 (`docs/plan/` is in `exclude_docs`). The Accessibility
   Nutrition Labels paragraph and the 1.3 cap rationale survive only in git history (`2185fd7^`). The repository now says
   nothing about the labels anywhere. Nothing to "update" there.

## Decision (proposed)

- The record of the nine labels goes in a **new internal page, `docs/accessibility-labels.md`**, listed in `exclude_docs`
  beside `releasing.md` and the other developer pages, so it is not published. Each label has: what the app does, the tests
  that prove it, the known limits, what still needs a device pass, and the proposed declaration (declare, declare after a
  device pass, or not applicable with the reason). Every status is honest: no label is "declare" until the owner has run the
  device checklist, because nothing has been run on a device.
- The public `docs/accessibility.md` stays short. It gains one bullet for what the new check proves (a control's spoken name
  contains its label, so Voice Control's "Tap <label>" works) and one for VoiceOver names, and nothing it cannot back.
  `check_site_pages.py` is unchanged.
- The old stage pages cannot be edited, so the pointers go where the work now lives: `docs/releasing.md`, "Store listings",
  step 3 (the owner's list of answers the upload cannot give) names the labels and the new page; `store/appstore/README.md`
  gets a row that points to the page. The cap rationale is cited from `Theme.qml`, not from a stage plan.
- A new check `check_label_in_name` (test name `a11y.label_in_name`) in the UI rig, wired into `check_everything`.

### The rule the check applies

For every interactive item that is shown and that a screen reader reaches, the **visible text of the control** must appear
in its `Accessible.name`, ignoring case and runs of whitespace. The visible text is every `Text` in the control (and its own
text if it is one) that is shown, non-empty and contains a letter; text inside a nested interactive control that a screen
reader reaches belongs to that control. The full text is compared, not the elided text. Text with no letter (a count, an icon
glyph, "›") is not a label. Icon-only controls have no visible text, so they are held only by `a11y.names`. The check does
not look at `Accessible.ignored` on the `Text`: it is a rule about what is drawn.

This is the strict reading (every visible word), not "the first line of the control". If it turns up controls where that is
wrong on purpose, they are brought back to the owner; no allowance list is added silently.

## Open questions for the owner

1. **Where does the nine-label record go?** Proposed: the new internal `docs/accessibility-labels.md`, with the public page
   edited lightly (above). The alternative is to put the table on the public `docs/accessibility.md`, which publishes
   "needs a device pass" and the contrast exceptions.
2. **Is "every visible word in the name" the right strictness?** The alternative is "the first line only", which finds less
   and would pass a row whose name drops its second line. Proposed: every visible word.
3. **Fixes change what VoiceOver speaks.** A fix that rewords a name is a user-visible change and gets a `changelog.d/`
   fragment. Names will keep what they say and add the label rather than drop words (a tile stays "Label, n today"). Proposed:
   accepted.
4. **Larger Text and the 1.3 cap.** Tile labels, tab labels and the count badge stop growing at 1.3 times, because their
   geometry is fixed by design (the board is a fixed 4 by 3 grid with no scrolling). Whether that still allows the Larger Text
   label depends on Apple's criteria, which the old plan cited at
   developer.apple.com/help/app-store-connect/manage-app-accessibility/overview-of-accessibility-nutrition-labels. The page
   records the cap as a known limit and the proposed declaration as "owner decides after reading Apple's current criteria".
   This is the owner's call, not the code's.
5. **Two elements match one label while a note is typed.** The board adds "Log <name> with note" for each tile that can be held,
   so "Tap <name>" matches the tile and that element, and Voice Control asks which with numbers. Proposed: leave it, say so in
   the known limits, and check on the device that the numbers appear.

## Tasks and tests

1. `apps/app/tests/ui/rig/ui_checks.hpp` and `ui_checks.cpp`: `check_label_in_name(UiRig&)` with the rule above, a comment
   in the style of the other checks, no private Qt headers if the properties of the items are enough. Add it to
   `check_everything`, so every component and screen test runs it.
2. Stubs. `stubs/UiStubBadLabel.qml` breaks the rule in three ways: a button whose label is "Add" and whose name is
   "Plus", a row of two lines whose name has only the first, a tab whose name is a different word. `stubs/UiStubLabels.qml`
   follows it in the shapes the app uses: a name in another case, "Label, 2 today", a label with a value, an icon-only button,
   a nested control, a label that elides. Both are registered in the stub list the way the other stubs are
   (`apps/app/tests/ui/CMakeLists.txt`).
3. `rig_test.cpp`: `a11y.label_in_name` (`ui`): the check is clean on `UiStubGood` and `UiStubLabels` and finds each of the
   three faults on `UiStubBadLabel`, naming the item, so the check can fail.
4. Fix what the check finds, control by control, in `apps/app/qml/` (components, the screens and their private pieces, the
   Interactions board of Stage 14 and its sheets). Suspects from reading: tab labels, `NavBar` buttons whose
   `accessibleName` differs from their `text` ("Back to Log" over "Log" is fine; the rest is read by the check), the
   `ListRow` and `Chip` rows that set their own name, `SwipeRow` actions, the `UndoToast` button ("Undo, <text>" over
   "Undo"), the `Alert` and `MiniBoard` buttons, `FirstLaunch` and the `PersonPicker`. The check lists the real set; this list
   is not it. Each fix keeps the existing tests' names, or updates them with the reason.
5. `apps/app/tests/ui/voice_control_test.cpp` (`ui`, test names `voicecontrol.*`): on the `LogScreen` rig, a helper finds the
   control a person would address by saying its label (the accessible control whose name is the label or starts with the
   label and a comma), as Voice Control does, and activates it with the press action.
   - `voicecontrol.tap_tile`: for each of the twelve positions of the Errors board and of the Interactions board, saying the
     label logs exactly one entry of that type, one haptic, no tap hint, and "Other" opens the list and logs nothing.
     A second "Tap" is a second entry. A tile that has a count ("Label, 2 today") is still found by its label.
   - `voicecontrol.tap_undo`: after a log, saying "Undo" finds the toast's button, and activating it removes the entry.
   - `voicecontrol.unique_labels`: no two controls on the board answer one label, except a tile and its "with note" element
     (open question 5).
   If the existing tests already pass for the right reason, these add only the lookup by label; they do not repeat
   `log.voiceover`.
6. `docs/accessibility-labels.md`: the page, nine sections of the shape above, plus the list of common tasks that the
   device pass walks (log an entry, undo it, review and edit an entry, add a note, start a new day, change a setting, set up a
   board), and the device checklist below. `mkdocs.yml`: add it to `exclude_docs`. Nothing else lists docs pages, because
   an excluded page is not published; `mkdocs build --strict` is the check.
7. `docs/accessibility.md` (public): the two bullets only.
8. `docs/releasing.md`, `store/appstore/README.md`: the pointers of the Decision. `docs/testing.md`: a row for
   `check_label_in_name` in the table of checks, beside `check_names`, and the `voicecontrol.*` tests in the table of ui
   tests.
9. `changelog.d/`: one fragment if any control's spoken name changes (task 4), none otherwise.

Test labels: `ui` for 1 to 5. There is no `unit` test here, because the logic is in the rig. No `#if`: nothing here is platform
code. No change to `Theme` colours, the bridge, `docs/privacy.md` (nothing stored or sent changes) or the setup file.

## Device checklist (for the owner, on a TestFlight build)

Settings > Accessibility > Voice Control on, then in Field Test:

1. Say "Show numbers": every tile, tab and button in view has a number. "Show names" shows the label of each, the same word
   that the screen shows.
2. Say "Tap Stopped Responding" (or any tile): one entry is logged, the toast appears once, no "Double-tap to log" hint.
   Say it again: a second entry. Say "Tap 3" on a numbered tile: the same.
3. Say "Tap Undo" while the toast shows: the entry is gone.
4. Say "Tap Other": the list opens. Say "Tap Cancel" or "Tap Done" on a sheet. Say "Tap Settings", "Tap Review" and "Tap Log"
   for the tabs.
5. Switch on the Observations board and repeat step 2 on it.
6. With a note being typed, say "Tap Stopped Responding": Voice Control offers numbers for the tile and "Log ... with note";
   either works.
7. Add a note by dictation in a field.
8. Run the same common tasks with VoiceOver on, and with Larger Text at the largest accessibility size, Dark Interface, Reduce
   Motion and Differentiate Without Colour (Settings > Accessibility > Display & Text Size), writing what failed in the page.

Not run in this work: a device of any kind. There is no Mac on the development machine. macOS and iOS are verified by CI only,
and the pull request says so.

## Order of the pull requests

1. This page, on its own. The owner reviews it.
2. After approval, one code pull request: tasks 1 to 9, with CI green, `check_label_in_name` passing on every screen, and the
   page with an honest status for each label.

Checks for the code pull request: `python3 tools/ci/precheck.py`, then `linux-debug` and `linux-asan-ubsan` locally in the
worktree. For this plan: `python3 tools/ci/precheck.py` only, since it changes no code.
