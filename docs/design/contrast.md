# Contrast checks for the dark palette

Recorded by hand for Stage 4 package 4.2, with the WCAG 2.x relative-luminance formula, until `Theme.contrastPairs` (package 4.1) runs the same pairs as a test. Text needs 4.5 to 1; borders, icons, the badge and the selected tab need 3 to 1. Every pair below passes. Colour names are the keys in `tokens.json`; the hex values are the dark ones. The test (`a11y.contrast`) is the authority now: it walks every pair in four modes (light and dark, each with and without Increase Contrast), and the sections at the end of this page record the light values it led to and the Increase Contrast set.

## Text on its background (dark)

| Pair | Foreground | Background | Ratio | Needs | Result |
|---|---|---|---|---|---|
| Titles, body text on the screen | `#EEF2F6` | `#0E141A` | 16.47 | 4.5 | pass |
| Row text, card text | `#EEF2F6` | `#18212A` | 14.47 | 4.5 | pass |
| Sheet text, raised cards | `#EEF2F6` | `#212C37` | 12.61 | 4.5 | pass |
| Text typed in fields | `#EEF2F6` | `#121A21` | 15.62 | 4.5 | pass |
| Time chip, tumbler | `#EEF2F6` | `#2B3844` | 10.66 | 4.5 | pass |
| Search field text | `#EEF2F6` | `#26323D` | 11.62 | 4.5 | pass |
| Captions, section labels | `#A7B6C3` | `#0E141A` | 8.93 | 4.5 | pass |
| Row values, times | `#A7B6C3` | `#18212A` | 7.85 | 4.5 | pass |
| Sheet captions | `#A7B6C3` | `#212C37` | 6.84 | 4.5 | pass |
| Unselected tumbler rows | `#A7B6C3` | `#2B3844` | 5.78 | 4.5 | pass |
| Placeholder text | `#93A3B0` | `#121A21` | 6.78 | 4.5 | pass |
| Placeholder in a card | `#93A3B0` | `#18212A` | 6.28 | 4.5 | pass |
| Alert body text | `#C7D3DD` | `#18212A` | 10.69 | 4.5 | pass |
| Links, Back, Cancel, selected tab | `#8FC6E6` | `#0E141A` | 10.05 | 4.5 | pass |
| Links in cards, selected tab on the bar | `#8FC6E6` | `#18212A` | 8.83 | 4.5 | pass |
| Links in sheets | `#8FC6E6` | `#212C37` | 7.69 | 4.5 | pass |
| Secondary button label | `#8FC6E6` | `#1B2F3D` | 7.49 | 4.5 | pass |
| Primary button label | `#FFFFFF` | `#2A6F96` | 5.51 | 4.5 | pass |
| Primary button label, pressed | `#FFFFFF` | `#1E5B7A` | 7.41 | 4.5 | pass |
| Header card unit and date | `#FFFFFF` | `#5B2A63` | 10.74 | 4.5 | pass |
| Header card site, version, tester | `#EBDDF0` | `#5B2A63` | 8.25 | 4.5 | pass |
| Delete label on a card | `#FF8F85` | `#18212A` | 7.38 | 4.5 | pass |
| Delete label in a sheet | `#FF8F85` | `#212C37` | 6.43 | 4.5 | pass |
| Error text on the screen | `#FF8F85` | `#0E141A` | 8.40 | 4.5 | pass |
| Swipe Delete label | `#FFFFFF` | `#8C2A22` | 8.50 | 4.5 | pass |
| Toast text | `#FFFFFF` | `#33444F` | 10.09 | 4.5 | pass |
| Toast tick | `#9FD3E8` | `#33444F` | 6.23 | 4.5 | pass |
| Undo button label | `#9FD3E8` | `#3C4D5C` | 5.38 | 4.5 | pass |
| Count badge number | `#14212B` | `#F2F4F7` | 14.86 | 4.5 | pass |
| Review lines, white on black | `#FFFFFF` | `#000000` | 21.00 | 4.5 | pass |
| Review times and markers | `#B4C0CA` | `#000000` | 11.33 | 4.5 | pass |
| Tile label on sky | `#EEF2F6` | `#2E486B` | 8.28 | 4.5 | pass |
| Tile label on lavender | `#EEF2F6` | `#45396A` | 9.12 | 4.5 | pass |
| Tile label on apricot | `#EEF2F6` | `#63462C` | 7.64 | 4.5 | pass |
| Tile label on aqua | `#EEF2F6` | `#225359` | 7.62 | 4.5 | pass |
| Tile label on orchid | `#EEF2F6` | `#62375E` | 8.38 | 4.5 | pass |
| Tile label on butter | `#EEF2F6` | `#5C4E1F` | 7.28 | 4.5 | pass |
| Tile label on steel | `#EEF2F6` | `#424B57` | 7.86 | 4.5 | pass |
| Tile label on periwinkle | `#EEF2F6` | `#333F71` | 8.93 | 4.5 | pass |
| Tile label on sand | `#EEF2F6` | `#514238` | 8.54 | 4.5 | pass |
| Tile label on oat | `#EEF2F6` | `#504835` | 8.05 | 4.5 | pass |
| Tile label on blush | `#EEF2F6` | `#5F3A48` | 8.54 | 4.5 | pass |
| Other tile label | `#A7B6C3` | `#18212A` | 7.85 | 4.5 | pass |
| Other tile label (primary) | `#EEF2F6` | `#18212A` | 14.47 | 4.5 | pass |
| Sheet text | `#EEF2F6` | `#141C24` | 15.28 | 4.5 | pass |
| Sheet captions | `#A7B6C3` | `#141C24` | 8.29 | 4.5 | pass |
| Cancel, Save, links in sheets | `#8FC6E6` | `#141C24` | 9.32 | 4.5 | pass |
| Warning banner text | `#EEF2F6` | `#2F2815` | 13.01 | 4.5 | pass |
| Error banner text | `#EEF2F6` | `#3A1B18` | 13.83 | 4.5 | pass |
| Information banner text | `#EEF2F6` | `#1B2F3D` | 12.28 | 4.5 | pass |
| Settings captions in an alert card | `#A7B6C3` | `#212C37` | 6.84 | 4.5 | pass |
| Alert body text | `#C7D3DD` | `#212C37` | 9.32 | 4.5 | pass |
| Retire this error type | `#FF8F85` | `#212C37` | 6.43 | 4.5 | pass |
| Review times and markers | `#B4C0CA` | `#000000` | 11.33 | 4.5 | pass |
| Review lines | `#FFFFFF` | `#000000` | 21.00 | 4.5 | pass |
| Header card missing-fields strip | `#FFFFFF` | `#5B2A63` | 10.74 | 4.5 | pass |

## Non-text pairs (dark)

| Non-text pair | Colour | Against | Ratio | Needs | Result |
|---|---|---|---|---|---|
| Other tile dashed border, field outline | `#74869A` | `#0E141A` | 4.96 | 3.0 | pass |
| Other tile dashed border on a card | `#74869A` | `#18212A` | 4.36 | 3.0 | pass |
| Selected tab icon, check marks, focus ring | `#8FC6E6` | `#0E141A` | 10.05 | 3.0 | pass |
| Primary button fill against screen | `#2A6F96` | `#0E141A` | 3.36 | 3.0 | pass |
| Count badge against the lightest-coloured tile (sky) | `#F2F4F7` | `#2E486B` | 8.45 | 3.0 | pass |
| Count badge against the brightest tile (butter) | `#F2F4F7` | `#5C4E1F` | 7.43 | 3.0 | pass |

| Warning icon and outline | `#D2A23A` | `#2F2815` | 6.25 | 3.0 | pass |
| Error icon and outline | `#FF8F85` | `#3A1B18` | 7.05 | 3.0 | pass |
| Information icon and outline | `#8FC6E6` | `#1B2F3D` | 7.49 | 3.0 | pass |
| Field outline on a sheet card | `#74869A` | `#212C37` | 3.80 | 3.0 | pass |
| Focus ring of a text field | `#8FC6E6` | `#141C24` | 9.32 | 3.0 | pass |
| Primary button against a sheet | `#2A6F96` | `#141C24` | 3.12 | 3.0 | pass |

The 11 tile colours are not held to 3 to 1 against the screen: they are large filled shapes whose edge is not the only cue (the label, the position and the count carry the meaning), which is also true of the approved light tiles (for example sky `#DCE8F7` on `#F2F4F7` is about 1.2 to 1). The Other tile has a 1.5 pt dashed border instead, which passes (4.96 and 4.36 above).

## New light tokens (4.2 added them for the new components)

| Pair | Foreground | Background | Ratio | Needs | Result |
|---|---|---|---|---|---|
| Warning banner text | `#14212B` | `#FFF4D6` | 14.95 | 4.5 | pass |
| Error banner text | `#14212B` | `#FDECEA` | 14.32 | 4.5 | pass |
| Information banner text | `#14212B` | `#EAF1F5` | 14.35 | 4.5 | pass |
| Retire this error type | `#B42318` | `#FFFFFF` | 6.57 | 4.5 | pass |
| Warning icon | `#946B0C` | `#FFF4D6` | 4.39 | 3 | pass |
| Error icon | `#B42318` | `#FDECEA` | 5.75 | 3 | pass |
| Swipe Delete label | `#FFFFFF` | `#B42318` | 6.57 | 4.5 | pass |

The approved light tokens are unchanged and were not re-measured.

## Light tokens darkened by the accessibility work

The light Other tile border and the light placeholder fell short of the rules (they were recorded as exceptions in `Theme.contrastPairs`). They are darker now, in the same blue-grey hue, and no exception is left. `borderStrong` (the outline of a switch and of a choice card, and the chevrons) takes the Other border's value, so that the outline of a switch is a 3 to 1 indicator as well.

| Pair | Before | Now | Ratio now | Needs | Result |
|---|---|---|---|---|---|
| Other tile border on the screen (`#F2F4F7`) | `#9AA8B4` (2.21) | `#7C8B98` | 3.17 | 3.0 | pass |
| Other tile border on a card (`#FFFFFF`) | `#9AA8B4` (2.43) | `#7C8B98` | 3.50 | 3.0 | pass |
| Outline of a switch or a choice card on the screen | `#9AA8B4` (2.21) | `#7C8B98` | 3.17 | 3.0 | pass |
| Outline of a switch or a choice card on a card | `#9AA8B4` (2.43) | `#7C8B98` | 3.50 | 3.0 | pass |
| Placeholder in a field (`#F7F9FB`) | `#6B7A86` (4.19) | `#637380` | 4.63 | 4.5 | pass |
| Placeholder on a card | `#6B7A86` (4.42) | `#637380` | 4.89 | 4.5 | pass |

**Not in the list, found on the way.** The placeholder of the search field sits on `searchBackground` (`#E3E8ED`), where `#637380` is about 4.0 to 1 and the old value about 3.6 to 1. It is not a pair of `Theme.contrastPairs` in the ordinary mode, because adding it would need an exception; with Increase Contrast on it is a pair and passes. It is open for the owner (a lighter search background, or a darker placeholder for that field).

## Increase Contrast (`colourIncreasedContrast` in `tokens.json`)

With the system's Increase Contrast (iOS) or High contrast text (Android) on, `Theme.increaseContrast` replaces the colours listed in `colourIncreasedContrast`; a key that is not listed keeps its value. Text on every background it is used on reaches **7 to 1**; borders, dividers, outlines, the Other tile border and the selected indicators reach **4.5 to 1**, and the pairs of the lines and of the search field join the list. `a11y.contrast` walks all of it in both schemes.

| Key | Light | Dark |
|---|---|---|
| `textPrimary` | unchanged | `#FFFFFF` |
| `textSecondary`, `reviewTextSecondary` | `#3C4A56` | `#C2CDD6`, `#C9D3DB` |
| `textPlaceholder` | `#3A4955` | `#BAC6D0` |
| `textBody` | `#2E3C47` | `#D8E1E8` |
| `primaryText` (links, selected tab) | `#174A66` | `#A9D6EE` |
| `destructive` | `#9A1B12` (the swipe Delete fill follows) | `#FFA79F` |
| `primary`, `primaryPressed`, `textOnPrimary` | unchanged | `#8FC6E6`, `#6FB3DB`, `#0E141A` |
| `bannerWarningBorder`, `bannerErrorBorder` | `#7A5508`, `#9A1B12` | `#E3B94F`, unchanged |
| `border`, `borderStrong`, Other tile border | `#5C6B77` | `#8A9CAE` |
| `divider`, `tabBarBorder`, `reviewDivider` | `#5F6E7A` | `#8A9CAE` |
| `toastAccent`, `toastButton` | `#BFE3F2`, unchanged | `#BFE3F2`, `#2C3B47` |
| `interactionAccent` | `#0B5A5E` | unchanged |

**The dark primary button turns light.** A dark fill that holds the white label at 7 to 1 cannot also stand 4.5 to 1 from the dark screen, so with Increase Contrast on the dark primary fill is `#8FC6E6` with a `#0E141A` label (about 10 to 1 both ways). The light primary button keeps `#1E5B7A` with a white label (7.41 to 1; 6.7 to 1 from the screen). The tile fills do not change; the label on a tile is `textPrimary`, which is white in dark, and so reaches 7 to 1 on every tile of both boards.

## Tile hues in dark

Each slot keeps the hue of its light colour, at HSL lightness 24 to 32 per cent and saturation 14 to 50 per cent. Hue angles: sky 214, lavender 255, apricot 28, aqua 187, orchid 306, butter 46, steel 214, periwinkle 228, sand 24, oat 42, blush 337. None is in the red range (345 to 15) or the green range (75 to 165). Blush at 337 is the closest to red and is the one to look at on a device (see "For the owner" in the pull request). The deuteranopia distance test of package 4.16 decides whether adjacent tiles stay distinguishable; sky, steel and periwinkle are the nearest trio, as in light.

## Interactions board (Stage 14, I-5)

The second board uses a second family of 11 tile fills (`interactionPalette` in `tokens.json`: cornflower, iris, peach, seafoam, rose, gold, slate, indigo, clay, khaki, pink) and one accent for its strip and tab (`interactionAccent`: `#0F6F73` in light, `#7FD1D2` in dark). `Theme.contrastPairs` runs the same rules on them as on the error tiles, in both schemes, and `tests/ui/contrast_test.cpp` (`theme.interaction_hues`) checks that no tile is red or green, that none equals an error tile, and that the closest pair under the deuteranopia simulation stays above the floors of the error tiles (0.25 light, 1.4 dark). The measured closest pairs are recorded here once the test has run (see the pull request); the label carries the meaning of a tile and never the colour.
