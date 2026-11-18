# Accessibility: contrast, Increase Contrast and Bold Text

The owner wants to declare Apple's Accessibility Nutrition Labels for the app. A survey found dark mode, Dynamic Type,
Reduce Motion, VoiceOver names and "not colour alone" already supported and tested. Sufficient Contrast is only partly
met, and two system settings are not followed at all. This page plans the work in three parts. It is the plan only: no
production code is written until the owner approves it.

The Voice Control label-in-name checks and the accessibility evidence page are another piece of work and are not touched
here.

## What is known

- `Theme.contrastPairs` (`apps/app/qml/theme/Theme.qml`) is the one list of pairs. `contrast_test.cpp` walks it in light
  and dark and requires 4.5 to 1 for text and 3 to 1 for a non-text indicator. In light it holds four `exception`
  entries, and the test pins their number (`exceptions == (dark ? 0 : 4)`):

  | Pair | Now | Needs |
  |---|---|---|
  | Other tile border on the screen (`otherTileBorder` `#9AA8B4` on `#F2F4F7`) | 2.2 | 3.0 |
  | Other tile border on a card (on `#FFFFFF`) | 2.4 | 3.0 |
  | Placeholder in a field (`textPlaceholder` `#6B7A86` on `#F7F9FB`) | 4.2 | 4.5 |
  | Placeholder on a card (on `#FFFFFF`) | 4.4 | 4.5 |

  The dark palette passes all four.
- `AccessibilityState` (`libs/ios_bridge/include/error_journaling/accessibility_settings.hpp`) carries the text scale,
  Reduce Motion and VoiceOver. Three platform files fill it: `platform/ios/accessibility_settings.mm`,
  `platform/android/accessibility_settings.cpp` (through `EjAccessibility.java`, with the decisions in portable C++ in
  `android_system_plan.cpp`) and `platform/desktop/accessibility_settings.cpp` (a stub that returns the defaults). `QmlBridge`
  exposes each field with a change signal, and `AppShell.qml` binds `Theme.textScale` and `Theme.reduceMotion` to it.
- Stage 4, decision 14 (7 Nov 2026) decided "Bold Text and Increase Contrast are not handled in v1; revisit if the tester
  uses them". The owner now reverses that. The planning pages were removed from the tree by #287, so decision 14 can only
  be read in history (`git show 2185fd7^:docs/plan/stage-4.md`, lines 277 and 1042); see open question 4.
- `Theme.FontRole` takes an integer `weight` (and `selectedWeight`) per role; roles use 400, 500, 600 and 700.

## Part 1: the four light exceptions

**These are approved light tokens.** The owner confirms the new values before they are built.

Proposed (measured with the WCAG formula; the test confirms them):

| Token | Approved | Proposed | On the screen `#F2F4F7` | On a card `#FFFFFF` | On a field `#F7F9FB` | Needs |
|---|---|---|---|---|---|---|
| `otherTileBorder` | `#9AA8B4` | `#7C8B98` | 3.17 | 3.50 | n/a | 3.0 |
| `textPlaceholder` | `#6B7A86` | `#637380` | n/a | 4.89 | 4.63 | 4.5 |

Each is the smallest step of the same blue-grey hue that clears the rule with a little margin, so the look stays close to
the approved design. The placeholder stays lighter than `textSecondary` (`#5A6B78`), so a hint still reads as a hint. The
border value is a separate token from `borderStrong`; `borderStrong` (`#9AA8B4`) is left as it is, which raises question 1.

## Part 2: Increase Contrast

A new `AccessibilityState::m_IncreaseContrast`, read like the other fields:

- iOS: `UIAccessibilityDarkerSystemColorsEnabled()` (the C function behind `UIAccessibility.isDarkerSystemColorsEnabled`),
  with `UIAccessibilityDarkerSystemColorsStatusDidChangeNotification` added to the observers.
- Android: `Settings.Secure` `high_text_contrast_enabled` ("High contrast text"), read in `EjAccessibility.java` and
  watched with the content observer that the class already uses for Remove animations. The setting is there on API 21
  and later, but it is a hidden key; if a device does not have it, the answer is off. The decision (any non-zero value is
  on) sits in `android_system_plan.cpp`, so that a test covers it on the desktop.
- Desktop: the stub returns off, as for the other fields. Tests give the bridge their own `AccessibilitySettings`.
- `QmlBridge`: `increaseContrast` property, `increaseContrastChanged` signal, emitted from the same compare that emits
  the others.
- `AppShell.qml`: `Binding { target: Theme; property: "increaseContrast"; value: root.bridge.increaseContrast }` beside
  `textScale` and `reduceMotion`; `Theme` gains the input `property bool increaseContrast: false`.

**The stronger token set.** In `tokens.json` each of light and dark gets a sibling set (`colourIncreasedContrast`, same keys
as the base set for the keys that change). `Theme.colour` reads the stronger value for a key when `increaseContrast` is on,
and the base value otherwise. The aim is at least 7 to 1 for text and 4.5 to 1 for borders, outlines and dividers, on every
background the key is used on. Keys that change: `textSecondary`, `textPlaceholder`, `textBody` (if below 7), `primaryText`
(a link; light `#1E5B7A` is 6.7 on the screen), `destructive` (light `#B42318` is 6.0 on the screen), `border`,
`borderStrong`, `divider`, `tabBarBorder`, `otherTileBorder`, the field outline, the banner borders and any text pair that
the measurement shows under 7. Starting values, to be settled by the test:

| Key | Light now | Light proposed | Dark now | Dark proposed |
|---|---|---|---|---|
| `textSecondary` | `#5A6B78` (4.5 to 5.5) | `#3F4E5A` (7.0 to 8.6) | `#A7B6C3` (5.8 to 8.9) | `#C2CDD6` (7.4 to 11.5) |
| `textPlaceholder` | `#637380` after Part 1 | `#4A5A66` | `#93A3B0` | `#B0BEC9` |
| `primaryText` | `#1E5B7A` | `#174A66` | `#8FC6E6` (6.5 on the time chip) | lighter, to be measured |
| `destructive` | `#B42318` | `#9A1B12` | `#FF8F85` | `#FFA79F` |
| `border`, `borderStrong`, `otherTileBorder` | `#C9D3DC`, `#9AA8B4`, `#7C8B98` | about `#5C6B77` (5.0 to 5.5) | `#34434F`, `#74869A` | about `#8A9CAE` (5.0 to 6.6) |
| `divider` | `#E8ECF0` | same hue, at 4.5 to 1 | `#27333E` | same hue, at 4.5 to 1 |

White-on-fill pairs (primary button, swipe Delete, header card, toast) already pass 7 to 1 or are changed with the fill; the
test lists what fails and the set is adjusted until nothing does. The tile fills do not change (they are not held to a
ratio against the screen, `docs/design/contrast.md`); the Other tile border does.

`Theme.contrastPairs` takes the mode into account: in the stronger mode every `text` pair needs 7 and every `indicator`
pair needs 4.5, and the test walks four modes (light, dark, light with Increase Contrast, dark with Increase Contrast).

## Part 3: Bold Text

- iOS: `UIAccessibilityIsBoldTextEnabled()` and `UIAccessibilityBoldTextStatusDidChangeNotification`.
- Android: `Configuration.fontWeightAdjustment` (API 31 and later; 0 where the field is absent), the setting that "Bold
  text" writes. The decision (an adjustment of 300 or more is on) sits in `android_system_plan.cpp`. A change arrives with
  the configuration change that already carries the font scale.
- `AccessibilityState::m_BoldText`, `QmlBridge::boldText` and `boldTextChanged`, `Theme.boldText`, bound in `AppShell.qml`.
- `FontRole`: a `weight` and a `selectedWeight` become `effectiveWeight(w)` = the next step up when `boldText` is on:
  400 to 500, 500 to 600, 600 to 700, 700 to 800, 800 to 900 (capped at 900; Qt maps to the nearest weight a font has).
  One step is how iOS treats it: the system raises the weight by a step, not to bold throughout.
- A bolder font is wider, so fixed-width chrome could clip. The check is made at every role and at the largest text scale
  (3.12) with Bold Text on, using `check_text_scale` and the existing rig.

## Open questions for the owner

1. **Should `borderStrong` change in light?** The switch outline in Settings (`SettingsSwitchRow.qml`) and the choice card
   border (`ChoiceCard.qml`) use `borderStrong` `#9AA8B4`, which is about 2.2 to 1 on the screen. They are not in
   `contrastPairs`, so no test fails today. Strictly they are non-text indicators that want 3 to 1. Recommendation: add them
   to the list and darken `borderStrong` to the same `#7C8B98`, in this piece of work. The chevrons use the same colour and
   are decoration, so they would darken too; that is the cost.
2. **The Part 1 values.** `#7C8B98` and `#637380`, as the table above. The owner can pick others.
3. **Android.** `high_text_contrast_enabled` is a hidden key and Bold Text needs API 31. The plan reads both where they
   exist and treats their absence as off. Say if Android should be left out of this piece of work.
4. **Where to record the reversal of decision 14.** `docs/plan/stage-4.md` and `stage-11.md` no longer exist in the tree
   (#287), so the edits the request names cannot be made there. Recommendation: this page is the record (it names decision
   14 and says the owner reversed it on the date of approval), `docs/design/contrast.md` gets a section for the
   stronger set, and `docs/testing.md` and `docs/architecture.md` change as listed below. Say if the stage-4 page should be
   restored for a one-line edit instead.

## Tasks and tests

The plan is its own pull request. The code is a second pull request, in these steps (one commit each):

1. **Tokens (Part 1).** `docs/design/tokens.json`: the two light values, the stronger sets. `Theme.qml`: the matching values and
   the `exception` helper removed along with the four entries. `docs/design/contrast.md`: the four rows, the new section.
   Tests (`ui`): `contrast_test.cpp` requires `exceptions == 0` in both schemes; `theme_test.cpp` `theme.matches_tokens`
   compares every value of the stronger sets too.
2. **Bridge (Parts 2 and 3).** `AccessibilityState` gains `m_IncreaseContrast` and `m_BoldText`. iOS, Android (Java and C++)
   and the desktop stub read them. `android_system_plan.{hpp,cpp}` gains `android_increase_contrast(int)` and
   `android_bold_text(int adjustment)`. `QmlBridge` gains the two properties and signals. Tests (`unit`, `test_ios_bridge.cpp`):
   the plan functions at their edges (0, 1, 299, 300, negative); a fake `AccessibilitySettings` that changes each field
   raises the matching signal once and no other; an unchanged read raises none; the two properties default to false.
3. **Theme and shell.** `Theme.increaseContrast` and `Theme.boldText` inputs, `AppShell.qml` bindings, the shell header comment.
   Tests (`ui`): flipping the bridge flips `Theme.colour.textSecondary`; the shell follows a change at run time.
4. **Contrast in the stronger mode.** `Theme.contrastPairs` per mode; `contrast_test.cpp` walks four modes and requires 7 and
   4.5; the list still holds the tile and badge pairs in every mode. Tests (`ui`).
5. **Bold Text.** `FontRole` weight step and the cap at 900. Tests (`ui`): every role at 400, 500, 600 and 700 reports the next
   step with the setting on and its own weight with it off; `selectedWeight` follows; no role clips at text scale 1.0 and
   3.12 with Bold Text on and with it off (`check_text_scale`); both settings on at once.
6. **Documents.** `docs/testing.md` (the new tests and what each mode walks); `docs/architecture.md` (the bridge layout: two new
   fields and the Java and plan functions); `docs/guide/` (a short section in the page that describes display and text,
   saying the app follows Increase Contrast, Bold Text and their Android equivalents), with `docs/guide/screenshots.json`
   untouched unless a pictured screen changes; `docs/privacy.md` does not change, because nothing is stored or sent;
   `changelog.d/contrast-light-tokens.changed.md`, `increase-contrast.added.md` and `bold-text.added.md`.

Checks before each push: `python tools/ci/precheck.py` (this machine has `python`, not `python3`), the `unit` and `ui` labels
on `linux-debug` and `linux-asan-ubsan` from inside the worktree, `clang-format` on changed files, and no new preprocessor
directive and no platform `#if` (all the differences sit in `src/platform/<os>/` and in the Java file).

## Device checklist (for the code pull request; none of it has run)

No Mac and no phone runs here. The iOS and Android code compile in CI only. The pull request says so, and lists:

1. iPhone, Settings > Accessibility > Display & Text Size: turn on **Increase Contrast** and **Bold Text**, then go through each
   screen (Log, History, Today's details, Settings and its editors, the Import sheet, the first-run tip, a banner, a toast,
   Review) in light and in dark, looking for text that is faint, a border that vanishes, or a label that clips.
2. The same with Bold Text alone and Increase Contrast alone.
3. Turn the setting on and off with the app open (the app, not the Settings app, switching): the colours and weights change
   without a restart. Put the app in the background, change the setting, and come back.
4. Largest accessibility text size with Bold Text on: nothing clipped on the Log tiles, the header card, the tab bar or a sheet.
5. Android (if kept): Settings > Accessibility > High contrast text, and Bold text (API 31 and later), the same screens.
6. Light mode, no setting on: the Other tile border and a field's placeholder look close to before, only a little firmer.
7. VoiceOver spot check, to see that nothing about names changed.
