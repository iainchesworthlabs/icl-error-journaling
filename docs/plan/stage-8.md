# Stage 8: iPad, portrait and landscape

Task breakdown, test list and device checklist for Stage 8 of the [implementation plan](../implementation-plan.md)
(Stages 7 to 12, added on 9 Nov 2026). Each work package is one pull request into `develop`. Requirement IDs refer to the
[PRD](../prd.md).

**Exit gate:** the iPad mock-ups are approved; the app is built to them and checked on an iPad in portrait and landscape,
in light and dark ([device checklist](#device-checklist-the-exit-gate)); the phone layout is unchanged, so every phone test
and `launch.matches_screen` still pass.

**Status:** approved by the owner on 9 Nov 2026, with the mock-ups as drawn and the recommendations of
[Decisions for the owner](#decisions-for-the-owner): Split View and Stage Manager supported, the board on the left in
landscape, the Review tab kept, and the background colour only as the iPad launch screen. Building starts with 8.1.

There is no Mac and no iPad on the development machine. CI compiles the iOS build and runs the desktop build. Everything
below about how iPadOS behaves comes from Apple's documentation or Qt's source as remembered. It is unproven until the
[device checklist](#device-checklist-the-exit-gate) has been run.

## The owner's scope

- The app runs on an iPad in portrait and landscape. A phone stays portrait only.
- In iPad landscape, the board ("the tappy thing") and the log sit side by side.
- In iPad portrait, the phone layout grows to fit the larger screen. Columns have a maximum width, and the tiles are
  larger.
- This plan decides whether the app supports Split View and Stage Manager (see [Multitasking](#multitasking-split-view-and-stage-manager)).

## Mock-ups

The mock-ups are in `docs/design/screens`. There are 36 files: nine screens, each in portrait and landscape, in light and
dark. They are named `Name.ipad-portrait.dc.html` and `Name.ipad-landscape.dc.html`, and the dark versions add `.dark`.
The design frame is an iPad Air 11-inch: 820 × 1180 pt in portrait and 1180 × 820 pt in landscape. It has a 24 pt
status bar and a 20 pt home indicator.

Each screen has many files, and they all follow the same rules. So they are not written by hand like the phone mock-ups.
`tools/visual/ipad_mockups.py` generates them. It takes every colour from `docs/design/tokens.json` and every size from the
[layout rules](#layout-rules) below. To change a rule, change the script and this page together, then run the script again.
`--check` fails if a file has been edited by hand. `tools/visual/render_mockups.py` now renders each page at the size in
its `$preview`, so the command below makes PNGs of the iPad pages, as it does for the phone pages:

```bash
python tools/visual/render_mockups.py --only Main.ipad-portrait Main.ipad-landscape
```

| Mock-up | Portrait (820 × 1180) | Landscape (1180 × 820) |
|---|---|---|
| `Main` (Log) | The phone layout in a 680 pt column: header card, composer, Last bar, and a 3 × 4 board of 218 × 152 pt tiles. The toast sits under the board. | The board on the left (58 per cent): the composer, then the 3 × 4 board of 207 × 134 pt tiles. The log on the right: the header card, then "Today's log", the rows, and Share log. There is no Last bar, because the log shows the last entry. |
| `Review` | Today's log in a 720 pt reading column. | The same column, centred. |
| `Header` (Today's details) | The phone's page in a 640 pt column, with the same fields in the same order. | The same column. The page scrolls, as it does on the phone. |
| `BoardSettings` (Settings) | The miniature board and the lists in a 640 pt column. | The same column. |
| `History` | The list in a 720 pt column. | The same column. |
| `Other` (sheet) | A form sheet over the Log screen. | A form sheet over the side-by-side Log. |
| `EditEntry` (sheet) | A form sheet over Review. | A form sheet over the side-by-side Log (a row of the log pane opens it). |
| `NewDay` (alert) | The phone's alert card, centred. | The same alert card. |
| `FirstLaunch` | A 480 pt column, centred vertically. | The same column. |

These screens are not drawn, because they follow the same rules with nothing new:
- the other sheets (Template value, Change version, Time tumbler, Environment, Error type edit, Board type picker,
  New error type) are form sheets;
- the other alerts (Delete log, Start new log, Import confirm) are the phone's alert card, centred;
- the editors (Error types, Locations, Templates, Template edit) and Import or export are pages in the form column;
- Past log review is the Review column;
- First launch by hand is the First launch column;
- the banners (Write failed, Recovery, Log banner) sit inside the header card or float over it, as on the phone. In
  landscape, the header card is at the top of the log pane.

If the owner wants any of these drawn, it is one function in the script.

## Layout approach

### Layout classes from the window's size

The layout is chosen from the size of the window in points. It is never chosen from the platform, the device or the
orientation. There is no `#if`, no `Qt.platform.os` and no check for "is iPad". The window's size is the only input, so:
- the desktop window of Stage 12 gets the same layouts when it is resized (the Stage 12 plan asks for this);
- an Android tablet in Stage 9 gets them too;
- a narrow Split View or Stage Manager window on an iPad gets the phone layout;
- a test picks a layout by setting the window's size.

UIKit's size classes cannot make this choice. An iPad's full screen is "regular" in both orientations, so the size class
cannot tell portrait from landscape. Qt does not expose size classes anyway.

| Class | When | What it looks like |
|---|---|---|
| `Compact` | width < 600 pt | The phone layout, unchanged. The iPhone is always here. |
| `Regular` | 600 pt ≤ width, and not `Wide` | The phone layout in centred columns with maximum widths, and larger tiles. iPad portrait is here. |
| `Wide` | width ≥ 1100 pt and height ≥ 640 pt | The Log screen with the board and the log side by side. Other screens are as in `Regular`. iPad landscape is here. |

Where the iPads fall, in points:

| iPad | Portrait | Class | Landscape | Class |
|---|---|---|---|---|
| iPad mini (A17 Pro) | 744 × 1133 | Regular | 1133 × 744 | Wide |
| iPad (A16), iPad Air 11-inch | 820 × 1180 | Regular | 1180 × 820 | Wide |
| iPad Pro 11-inch (M4) | 834 × 1210 | Regular | 1210 × 834 | Wide |
| iPad Air 13-inch | 1024 × 1366 | Regular | 1366 × 1024 | Wide |
| iPad Pro 13-inch (M4) | 1032 × 1376 | Regular | 1376 × 1032 | Wide |

The 1100 pt line puts every iPad in portrait in `Regular`. That includes the 13-inch iPads, which are 1024 or 1032 pt
wide in portrait. It puts every iPad in landscape in `Wide`. A half-screen Split View (about 507 to 683 pt wide) is
`Compact` or `Regular`. A two-thirds Split View is `Regular`.

The height condition keeps a short, wide window (a desktop window, or Stage Manager) out of the side-by-side layout. At
640 pt the board would have tiles shorter than the 88 pt minimum, so such a window uses the column instead.

A new QML singleton, `Layout` (`apps/app/qml/theme/Layout.qml`), holds:
- the class;
- the derived sizes from the table below;
- the window size it was computed from.

`AppShell` binds `Layout.windowWidth` and `Layout.windowHeight` to its own size, in the same way it binds
`Theme.textScale` to the bridge. On a phone the values in `Compact` are exactly today's `Theme` values. That is how the
phone layout stays unchanged: a component reads `Layout.screenMargin` where it reads `Theme.space.screenMargin` today, and
in `Compact` the two are the same number. The theme literal check (`check_qml_literals.py`) applies to `Layout.qml` as it
does to `Theme.qml`.

### Layout rules

The mock-up script uses these numbers, and so will `Layout.qml`. `Compact` is today's phone value.

| Rule | Compact | Regular | Wide |
|---|---|---|---|
| Screen margin | 16 | 24 | 24 |
| Log column (Log screen) | full width | ≤ 680, centred | two panes: board 58 per cent, log the rest, 20 apart |
| Form column (Today's details, Settings, editors, Import or export) | full width | ≤ 640, centred | ≤ 640, centred |
| Reading column (Review, History, past log) | full width | ≤ 720, centred | ≤ 720, centred |
| First launch column | full width | ≤ 480, centred vertically | ≤ 480, centred vertically |
| Tile gap | 8 | 12 | 12 |
| Tile height | 104, or less in a window too short for it, not below 88 | 0.7 × tile width, from 104 to 152 | fills the board pane's height, at least 88 |
| Tile label | 13.5 pt, cap 1.3 | 17 pt, cap 1.3 | 17 pt, cap 1.3 |
| Count badge | 24 pt, 13 pt text | 28 pt, 15 pt text | 28 pt, 15 pt text |
| Last bar | shown | shown | hidden (the log pane shows the last entry) |
| Undo toast | above the tab bar | under the board, ≤ 520 wide, centred | at the foot of the log pane, above Share log |
| Tab bar | 49 + inset, full width | 49 + inset, tabs in the 680 column | as Regular |
| Sheets | bottom sheet with detents | form sheet: 560 wide, centred, as tall as its content, at most the height less 64 above and below | as Regular |
| Alerts | card hung from the alert anchor | the same card, centred vertically | as Regular |
| Other text | unchanged | unchanged | unchanged |

Text sizes other than the tile label do not change. iPadOS uses the same type sizes as iOS, and Dynamic Type still
applies. The tile label is larger because the tiles are larger and are read at arm's length on a table.

The board keeps its 3 × 4 shape in every class. The facilitator finds a tile by where it is (spatial memory, decision 9
of the design package), so a tile is in the same place on the phone and on the iPad, in both orientations. A 4 × 3 board
in landscape was considered and rejected for this reason.

In landscape, the header card moves to the top of the log pane. It describes the log, and moving it there gives the board
pane enough height: on an iPad mini in landscape, the tiles are 119 pt tall with the card in the log pane and 100 pt
with it over the board. The composer stays over the board, because hold-to-log-with-note (4.17) uses the composer's text.

## What changes in the shell and the components

| Item | Change |
|---|---|
| `theme/Layout.qml` (new) | The class and the sizes above, from the window's size. |
| `shell/AppShell.qml` | Binds `Layout` to its size. Gives each tab its column. The tab bar's tabs sit in the log column. The Review toast is placed by class. |
| `Main.qml` | No change to the window. Its starting size stays the phone frame. The platform sets the real size on iOS, and the desktop window can be resized (Stage 12 decides its minimum). |
| `components/BoardGrid.qml`, `Tile.qml` | Tile height, gap, label size and badge come from `Layout`. The board's `implicitWidth` comes from its column, not `Theme.frame`. |
| `screens/log/LogScreen.qml` | One screen with two arrangements, chosen by `Layout.wide`: the column (Compact and Regular), or the board pane and the log pane. The items are moved, not made again, so a rotation keeps the draft note, the focus, the toast and its undo, a held tile, and an open sheet. |
| `screens/log/private/LogPane.qml` (new) | The log pane: the header card, the title row with the entry count and Copy, the rows of today's log in Review's wording and black and white, and Share log. It binds to the same `timeline` model as Review and reuses Review's row (swipe for Copy and Delete, tap for Edit entry). It scrolls to the newest entry when one is added. |
| `components/Sheet.qml`, `SheetHost.qml` | A form-sheet mode when `Layout.compact` is false: centred, 560 wide, four rounded corners, no grabber or detents, and the same scrim. A sheet that is open during a rotation moves to its new place. |
| `components/UndoToast.qml` | Its width and anchor come from the screen that holds it, not from `Theme.frame`. |
| Pages (`NavBar`, `PageStack` pages, `GroupedList`, `FormField`, `MiniBoard`, editors, History, Review) | Content goes in the form or reading column. The nav bar stays full width, with its title centred on the column. |
| `screens/setup/FirstLaunch*.qml` | The 480 column, centred vertically. |
| `screens/launch/LaunchSkeleton.qml` | Unchanged. The launch image is phone only (see [Launch screen](#launch-screen-on-ipad)). |
| `Theme.frame` | Stays as the phone's design frame, for the phone tests and the launch image. There are 37 uses of `Theme.frame` in 27 QML files, mostly implicit sizes. 8.1 checks each one and moves those that are really "the window's size" to `Layout`. |

All of this is QML. The only native code is two small bridge changes, each in `src/platform/ios/` with an empty
desktop version: the minimum window size ([Multitasking](#multitasking-split-view-and-stage-manager)) and the anchor of the
share popover (assumption A3).

## Multitasking: Split View and Stage Manager

**Recommendation: support it.** Remove `UIRequiresFullScreen`, and support all four orientations on the iPad. The
reasons:

1. **iPadOS 26 is ending full-screen-only apps.** With the iPadOS 26 windowing system, Apple deprecated
   `UIRequiresFullScreen` and says it will be ignored in a future release. After that, every app must work in a window of
   any size. Building it now costs less than being forced to later.
2. **It costs little with this layout approach.** A narrow window is `Compact`, which is the phone layout and is already
   tested. A two-thirds window is `Regular`. Nothing extra is drawn.
3. **Apple requires all four orientations for multitasking.** If `UIRequiresFullScreen` is absent, the app must list all
   four orientations for the iPad, or App Store Connect rejects the build (ITMS-90474). So in landscape the app turns both
   ways, and in portrait it turns both ways too. The phone keeps portrait only. The iPhone's list and the iPad's list are
   separate keys.
4. **It suits facilitators.** A facilitator can keep notes or a chat open beside the board.

What stays off:
- **More than one window** (`UIApplicationSupportsMultipleScenes` stays false). The storage of Stage 2 has one writer: one
  open log and one journal. Two windows of the app would be two sessions over one log. Stage Manager still resizes the one
  window.
- **Picture in Picture and external displays.** They are not relevant.

What is needed:
- **Short windows in `Compact`.** The phone layout has fixed tile heights and does not scroll. On the iPhone 17 it fits
  with room to spare, but a Split View window on an iPad mini in landscape is only 744 pt tall. With the iPad's insets
  (24 and 20) the phone layout needs about 787 pt. So in `Compact` the tiles get shorter when the window is too short for
  104 pt tiles, down to the 88 pt minimum. On every iPhone the tiles stay 104 pt, so the phone does not change.
- **A minimum window size.** iPadOS lets a Stage Manager window be smaller than any phone. The bridge sets the window
  scene's `sizeRestrictions.minimumSize` to 375 × 720 pt, the smallest size at which the board fits with 88 pt tiles.
  It is one call in `libs/ios_bridge/src/platform/ios/` and an empty desktop version.
- **Resizing tests** at Split View sizes, and while the size changes (T8-5).

If the owner prefers full screen only, `UIRequiresFullScreen` stays and the iPad lists four orientations. The layout work
is the same. Only T8-5 and the minimum size go away, until iPadOS stops honouring the key.

## Info.plist and packaging

`apps/app/packaging/ios/Info.plist.in`:

| Key | Today | Stage 8 |
|---|---|---|
| `UISupportedInterfaceOrientations` (iPhone) | Portrait | Portrait (unchanged) |
| `UISupportedInterfaceOrientations~ipad` | Portrait | Portrait, Portrait upside down, Landscape left, Landscape right |
| `UIRequiresFullScreen` | true | removed (or kept, if the owner chooses full screen only) |
| `UIApplicationSupportsMultipleScenes` | absent (false) | absent; a comment says why |
| `UILaunchScreen` | colour and image | unchanged; the image becomes phone only (below) |

`TARGETED_DEVICE_FAMILY` is already `1,2` (`apps/app/packaging/ios.cmake`), so the TestFlight build already runs on an
iPad, in portrait and full screen. Once a version that supports the iPad is on the App Store, iPad support cannot be
removed. That is already true, and Stage 8 makes the iPad version good.

`tools/ci/check_ios_plist.py` gains rules for:
- the iPhone list is portrait only;
- the iPad list has all four orientations when `UIRequiresFullScreen` is absent;
- `UIApplicationSupportsMultipleScenes` is not true.

`packaging/ios/verify_app.sh` checks the same rules in the built bundle.

## Launch screen on iPad

The phone's launch image is a picture of the Log screen's skeleton at 402 × 874 pt (5.8). It is kept true by
`launch.image_current` and `launch.matches_screen`. A picture cannot do the same on an iPad:
- `UILaunchScreen` shows one image;
- an asset catalogue can choose an image by device, but not by orientation, since both iPad orientations are
  regular × regular;
- the iPad's first frame can be portrait or landscape, at five screen sizes, or any window size in Stage Manager.

**Recommendation:**
- The `LaunchBoard` image set gets the `iphone` idiom instead of `universal`. The iPad then has no matching image, and
  shows only `LaunchBackground`, the board's background colour in light and dark.
- The iPhone is unchanged, and so are its tests.
- A plain launch in the background colour is what Apple's guidelines ask for, and a 402 pt phone board would look wrong
  in the middle of a 1180 pt screen.
- The cold-start budget (NFR-04) is measured on the phone. On the iPad, the checklist records the time without a budget.

The alternative is a launch storyboard drawn per size class, with a skeleton for each class. It cannot match landscape
and portrait at the same time on an iPad, and a storyboard needs Xcode on a Mac to check. It is not proposed.

A new test, `launch.ipad_plain`, reads `Contents.json` and checks that the image set has no `universal` or `ipad`
images.

## App Store requirements for the iPad

Stage 11 owns the listing. These are the parts that depend on this stage:

| Requirement | Where it is met |
|---|---|
| iPad screenshots: the 13-inch iPad size (2064 × 2752 portrait or 2752 × 2064 landscape) is needed when the app runs on an iPad. Smaller iPads use scaled copies. | Stage 11's `ej_screenshots` renders the app at 1032 × 1376 and 1376 × 1032 pt at 2x. Those sizes are `Regular` and `Wide`. Stage 11 asks for the landscape side-by-side layout for screenshot 8. |
| All four iPad orientations when the app is not full screen only (ITMS-90474) | Info.plist (above) |
| A launch screen for every device | `UILaunchScreen` (above) |
| The app icon on the iPad | The single 1024 px icon in `AppIcon.appiconset` covers every iPad size. The checklist confirms how it looks on the Home Screen. |
| App Review: the app is usable on an iPad, with no cut-off, letterboxing or phone-sized layout (Guideline 2.4.1, and the layout tests of App Review) | This stage |
| No iPad-only capability in `UIRequiredDeviceCapabilities` | Unchanged: `arm64` only |

Hardware keyboard and pointer support are not App Store requirements. They are in Stage 12, for the desktop. The iPad gets
whatever Stage 12 adds to the shared QML. In this stage, Return in the composer adds the note (as on the phone keyboard),
and Tab moves between form fields. Both are already Qt's defaults.

## Work packages

Each package is one squash-merged pull request with tests. Every package keeps the phone layout unchanged: the phone UI
tests, `launch.matches_screen` and the visual capture of the phone screens must pass with no change to their expected
values.

| # | Package | Contents | Tests |
|---|---|---|---|
| 8.1 | Layout classes | `Layout.qml`, bound by `AppShell`. The audit of `Theme.frame` uses. The UI rig gains a window size and safe-area insets (left, right, top, bottom) for each test. `ej_ui_tests` capture at iPad sizes. There is no visible change. | T8-1, T8-2 |
| 8.2 | Regular: columns | The log, form, reading and first-launch columns. The tab bar in the column. Margins. | T8-3, T8-4, T8-9 |
| 8.3 | Regular: board | Tile size, gap, label and badge from `Layout`. The toast under the board. Shorter tiles in a short `Compact` window. | T8-3, T8-6, T8-7 |
| 8.4 | Form sheets and alerts | `Sheet` and `SheetHost` form-sheet mode. Alerts centred. The keyboard with a form sheet. | T8-8 |
| 8.5 | Wide: side by side | `LogScreen`'s second arrangement and `LogPane`. The Last bar hidden. The toast in the log pane. Rows open Edit entry. The share popover's anchor (A3). | T8-3, T8-10, T8-11 |
| 8.6 | Resizing | State kept across a change of size or class. Keyboard and sheet placement after a rotation. The minimum window size through the bridge. | T8-5, T8-12 |
| 8.7 | Info.plist and launch | Orientations. `UIRequiresFullScreen` removed. The phone-only launch image. The plist checks. A `changelog.d` fragment ("Runs on iPad in portrait and landscape"). | T8-13, T8-14 |
| 8.8 | Visual pass and exit gate | The capture of the app against the iPad mock-ups. Fixes. The device checklist on an iPad. | the [checklist](#device-checklist-the-exit-gate) |

The order is 8.1, then 8.2 to 8.5 (8.4 can go in parallel with 8.2 and 8.3), then 8.6, 8.7 and 8.8. Package 8.7 goes in
after 8.5, so that a TestFlight build never offers landscape without the landscape layout.

## Tests

The labels are as in `docs/testing.md`. Every UI test runs in the offscreen app at the window size it names. The sizes:
- Phone: 402 × 874, the design frame, with insets 59 and 34.
- The smallest window: 375 × 720, insets 24 and 20.
- iPad portrait: 744 × 1133, 820 × 1180 and 1032 × 1376, insets 24 and 20.
- iPad landscape: 1133 × 744, 1180 × 820 and 1376 × 1032, insets 24 and 20.
- Split View: 507 × 1180, 694 × 820 and 320 × 1180 (the narrowest slide-over), insets 24 and 20.

| ID | Test | Label | What it proves |
|---|---|---|---|
| T8-1 | `layout.classes` | unit | The class at each size in the tables above, including the edges (599, 600, 1099, 1100 wide; 639 and 640 high). In `Compact`, every derived value equals today's `Theme` value. |
| T8-2 | `layout.no_platform` | unit | `check_qml_platform.py` passes. `Layout.qml` reads no `Qt.platform` and nothing of the device. |
| T8-3 | `ipad.screens_fit` | ui | At every iPad size and class, each tab's root screen: nothing is outside the window or under the safe area; no text is cut off (the `a11y.text_scale` check); every touch target is at least 44 pt; the board's 12 tiles are visible without scrolling. |
| T8-4 | `ipad.compact_is_phone` | ui | At 507 × 1180, the geometry of the Log screen's header card, composer, Last bar, tiles and tab bar matches the phone's, relative to the window. At 507 × 744 and 375 × 720, the 12 tiles are visible above the tab bar and at least 88 pt high. |
| T8-5 | `ipad.resize_keeps_state` | ui | With a draft note, focus in the composer, a toast with Undo, an open Other sheet, and Review scrolled half way: resize through Regular, Wide, Compact and back. Each piece of state is still there, Undo still undoes, and no entry is added or lost (count before equals count after). |
| T8-6 | `ipad.tiles` | ui | Tile sizes in Regular and Wide at each iPad size: at least 88 pt high, the same 3 × 4 positions as the phone, and the longest label ("Sequence Out of Order") not cut at 17 pt and at the 1.3 cap. |
| T8-7 | `ipad.tap_latency` | perf | The press path (4.12) at 1180 × 820 keeps the phone's budget: a tile press reaches the session on the press. |
| T8-8 | `ipad.form_sheet` | ui | Each sheet at Regular and Wide is 560 wide, centred, inside the safe area, and above the keyboard (simulated keyboard heights 313 and 398). Cancel and Save are reachable, the scrim tap does what it does on the phone, and VoiceOver order is the sheet only (`a11y.reading_order`). |
| T8-9 | `ipad.reading_order` | ui | In Regular and Wide, VoiceOver order on the Log screen is the same as on the phone in Regular. In Wide it is: composer, board, then header card, log, Share log. Tab bar last. |
| T8-10 | `ipad.log_pane` | ui | In Wide: a tap logs and the new row appears at the end of the log pane and is scrolled into view. Undo removes it. A row opens Edit entry. Swipe gives Copy and Delete. Share log calls the share service with today's text. The pane uses only the review colours (no tile colour). |
| T8-11 | `ipad.toast_clear` | ui | In Regular and Wide, the visible toast covers no tile and no row of the log pane. |
| T8-12 | `ipad.keyboard_rotation` | ui | With the keyboard up over the composer, a change from Wide to Regular keeps the field visible and focused. |
| T8-13 | `plist.orientations` | unit | `check_ios_plist.py`: the iPhone is portrait only, the iPad has four orientations, there is no `UIRequiresFullScreen` (unless the owner keeps it), and multiple scenes are off. |
| T8-14 | `launch.ipad_plain` | unit | The launch image set is `iphone` only. `launch.image_current` and `launch.matches_screen` are unchanged and pass. |
| T8-15 | Visual pass | none (tool) | `tools/visual/run_capture.py --only Main.ipad-landscape ...` pairs each iPad mock-up with the app in the same state. The differences are reviewed in 8.8. It is not a pass or fail test, the same as for the phone. |

The phone suites run unchanged in every package. A package that changes a phone test's expected value is wrong, unless
the owner agrees the change.

## Device checklist (the exit gate)

Run on a TestFlight build on an iPad, ideally an iPad mini, which is the tightest in landscape, and one 11-inch or
13-inch. Record pass or fail and the iPadOS version.

| # | Check |
|---|---|
| D1 | Portrait and landscape, light and dark: each of the nine mock-up screens matches its mock-up (the owner's eye, with the mock-up open beside it). |
| D2 | Rotate with a draft note, a toast, and an open sheet. Nothing is lost, and the sheet re-centres. Rotate during a tap burst. Every tap is logged once. |
| D3 | Upside-down portrait and both landscapes work. The iPhone still does not rotate. |
| D4 | Split View: one third, half and two thirds, beside Notes. Each matches its class. The app can be used with a hardware keyboard attached and detached. |
| D5 | Stage Manager: drag the window from the smallest size to full screen. The layout changes class without a flash of the wrong layout or a lost entry. The window cannot be made smaller than 375 × 720. |
| D6 | The software keyboard, docked and floating, over the composer and over each form sheet. The field stays visible. |
| D7 | Launch in both orientations. Only the background colour shows, then the board. Record the cold-start time from the app's timing line. |
| D8 | VoiceOver in landscape: the order is as T8-9, and every tile, row and button is named. |
| D9 | Larger Text at the largest size in Regular and Wide: no text is cut off, and tile labels stop at the 1.3 cap. |
| D10 | Share log from the log pane, from Review and from History. The share popover points at the button that opened it (assumption A3). Post to Slack. |
| D11 | Import a setup file from Files on the iPad, and through AirDrop. |
| D12 | The app icon on the Home Screen and in the Dock. |

## Assumptions only a device can confirm

| # | Assumption | Checked by | If wrong |
|---|---|---|---|
| A1 | Qt's iOS plugin resizes the `QWindow` when the iPad rotates, and when a Split View or Stage Manager window is resized. The safe-area margins follow. | D2, D4, D5 | Report it upstream, and have the bridge pass the scene's size to the window (one `.mm` change). |
| A2 | `windowScene.sizeRestrictions.minimumSize` is honoured in Stage Manager on iPadOS 26. | D5 | The Log screen scrolls below 375 × 720, rather than cutting off the bottom row. |
| A3 | On an iPad, the share sheet is a popover. The Stage 5 bridge (`share.mm`) anchors it to the whole view and gives no `sourceRect`, so it does not crash, but the popover points at the middle or the corner of the screen rather than at the button. | D10 | In 8.5, the share call takes an anchor rectangle from QML (the Share log button, or History's Share), which becomes `sourceRect`. The phone ignores it. |
| A4 | An asset catalogue image set with only `iphone` images shows no image on an iPad launch screen, and does not fail the build. | D7, and the Xcode build in CI | Keep `universal`, and add an `ipad` image of the background colour only. |
| A5 | App Store Connect's validation rejects a build that drops `UIRequiresFullScreen` without four iPad orientations, and accepts this one. | The first TestFlight upload after 8.7 | Change the orientations to what the error message asks for. |
| A6 | The document picker (Stage 5) is presented as a form sheet on an iPad, without an anchor. | D11 | Give it an anchor, as for A3. |

## Coordination with the other stages

- **Stage 7 (screen and flow review)** changes the same screens. This stage builds on what Stage 7 merges. Each package
  rebases on `develop` before it is opened. The iPad changes are mostly in sizes and anchors, which Stage 7's copy and flow
  fixes rarely touch. When Stage 7 changes a phone mock-up of one of the nine screens, the iPad script is updated in the
  same week (the copy is in the script).
- **Stage 9 (Android)** gets the classes for tablets. A Play tablet screenshot at 1200 × 1920 px (600 × 960 dp) is
  `Regular`. One at 2560 × 1600 px (1280 × 800 dp) is `Wide`.
- **Stage 10 (documentation)** can use the rendered iPad mock-ups, or the capture, for the guide's iPad pages.
- **Stage 11 (store release)** needs the 13-inch screenshots in both orientations from this stage's layout. Screenshot 8
  is the side-by-side layout. It can start drafting from the mock-ups now.
- **Stage 12 (desktop)** reuses the classes and the wide layout, as its plan asks. The desktop window's minimum size is
  Stage 12's decision. Below 1100 × 640 it gets the column.

## Decisions for the owner

1. **Multitasking:** support Split View and Stage Manager with all four iPad orientations (recommended), or full screen
   only.
2. **Which side the board is on in landscape:** left (as drawn), or right. Left keeps the phone's reading order (composer,
   board, then the log). Right puts the board under a right-handed facilitator's hand when the iPad is flat on a table. A
   setting for it is possible, but is not proposed for v1.
3. **The Review tab in landscape:** kept (as drawn), so the four tabs are the same in every orientation, even though the log
   pane repeats today's log. The alternative is to hide it in `Wide`, which means the tab bar changes on rotation.
4. **Launch screen on iPad:** the background colour only (recommended).
5. **The mock-ups:** approve them, or ask for changes. Changes are made in `tools/visual/ipad_mockups.py`.
