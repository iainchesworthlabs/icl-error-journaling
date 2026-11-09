# Stage 13: foldable phones

This page sets out the task breakdown, test list, emulator checklist and decisions for Stage 13 of the
[implementation plan](../implementation-plan.md). The owner added the stage on 10 Nov 2026. Each work package is one pull
request into `develop`. Requirement IDs refer to the [PRD](../prd.md).

**Exit gate:**
- the owner approves the foldable mock-ups;
- the app is built to them;
- the [emulator checklist](#emulator-checklist-the-exit-gate) passes on the Android Studio foldable emulators;
- the iPhone Duo checks pass in Xcode's Device Hub;
- folding or unfolding loses no state: [T13-6](#tests) and checks E5 to E8;
- the phone and iPad layouts are unchanged, so every phone and iPad test still passes with the same expected values.

**Status:** draft for the owner's review. No code is written until the owner approves this page. Building needs Stage 8's
adaptive layout (8.1 to 8.6) and Stage 9's Android build (9.1 and 9.7) on `develop` first.

Nothing on this page has run on a foldable. There is no foldable device, no Mac and no Android emulator image on the
development machine. Statements about Android come from Android's documentation and the Qt 6.12 kit on the machine.
Statements about the iPhone Duo come from Apple's announcement and developer pages of September 2026. Anything only a
device or a simulator can confirm is listed in [Assumptions](#assumptions-only-a-device-or-simulator-can-confirm), each
with a fallback.

## The owner's scope

- The owner asked for a session "for iPhone Duo and other Android foldables". The layout is "some cross between what we
  have on the iPad and what we have on the phone".
- **Book-style:** Galaxy Z Fold, Pixel Fold, OnePlus Open and the iPhone Duo. Folded, they use a narrow or short cover
  screen. Unfolded, the inner screen is nearly square, and it can be held half folded.
- **Flip-style:** Galaxy Z Flip and Razr. Unfolded, they are a tall phone. The small outer screen is optional.
- **Dual-screen:** Surface Duo. This is low priority, and the [recommendation](#dual-screen-surface-duo) is no specific
  support.
- **Keeping state:** folding or unfolding must not lose the open sheet, the typed note, the scroll position or the entry
  being edited. A lost note draft is a defect.

## What the platforms say

### The iPhone Duo

Apple announced the iPhone Duo on 10 September 2026. It shipped on 23 October 2026, with iOS 27.1.

Facts from Apple:
- **Displays:** an inner display of 7.6 inches (7.58) and an outer display of 5.4 inches (5.36). "Both displays share the
  same aspect ratio". Apple's newsroom gives no point sizes.
- **Shape:** the outer display "is wider and shorter than the display on other iPhone devices" (Human Interface
  Guidelines, *Designing for iPhone Duo*).
- **Status bar and controls on the side:** the status bar and the camera sit on the side of the display. The system moves
  toolbars and tab bars to the side on the outer display, and on the inner display in landscape. The inner display in
  portrait keeps horizontal bars.
- **Size classes:** compact width on the outer display, regular width on the inner display. Apple says: "Don't choose a
  layout by idiom" and "Don't treat 'regular width' as 'iPad'".
- **The fold:** it is a *reserved region* of kind *division*. It is active when the device is partly open, and inactive
  when the device is fully open. Apple reports it through `UIView.ReservedRegion` (UIKit) and `ReservedRegion` (SwiftUI).
  Apple advises: "Avoid extreme layout changes as people fold the device", and "In a grid-style layout, prefer an even
  number of columns".
- **The SDK matters:**
  - An app built with the iOS 26 SDK or earlier opens "in the center of the display, with empty space surrounding the
    window".
  - An app built with the iOS 27 SDK resizes, but avoids the status bar.
  - An app built with the iOS 27.1 SDK uses the full display.
- **Screenshots:** App Store Connect lists iPhone Duo screenshots at 1398 × 2034 and 2034 × 1398 pixels (outer display),
  and 2007 × 2853 and 2853 × 2007 pixels (inner display). They are "required for any app using the iOS 27.1 SDK or later"
  from April 2027.

Apple publishes no point sizes. This plan uses **669 × 951 pt** for the inner display and **466 × 678 pt** for the outer
display. Those figures are the screenshot sizes divided by 3, and every one divides exactly. They are derived, not
published, and Device Hub confirms them (assumption I1). The layout rules below depend on the window's size, not on these
figures, so a different real value changes only which row of a table a size falls in.

### Android foldables

- **Window size classes** (Android's breakpoints):
  - width: compact < 600 dp ≤ medium < 840 dp ≤ expanded;
  - height: compact < 480 dp ≤ medium < 900 dp ≤ expanded.

  Android says "most large unfolded inner displays" are medium width in portrait and expanded width in landscape.
- **Google's reference foldable:** the adaptive-app tests name one foldable size, **841 × 701 dp** (the Pixel Fold).
  This plan draws its unfolded mock-ups at that size, landscape and portrait. Exact sizes of other devices are recorded
  from their emulator profiles in 13.1, not quoted here from memory.
- **Postures:** Jetpack WindowManager reports each fold as a `FoldingFeature`, with:
  - bounds;
  - an orientation (vertical or horizontal);
  - a state (`FLAT` or `HALF_OPENED`);
  - whether it is separating (`isSeparating`);
  - whether it hides content (occlusion `NONE` or `FULL`).

  Tabletop is half open with a horizontal fold. Book is half open with a vertical fold.
- **Orientation:** with target API 36, Android 16 ignores `screenOrientation` on displays at least 600 dp in their
  smallest width. Stage 9's notes say the temporary opt-out "won't apply when targeting API level 37".

### What Qt 6.12 exposes

Nothing about folds. This was checked in the Qt 6.12.0 Android kit at `C:\Repos\common\qt\6.12.0\android_arm64_v8a`:
- **Headers:** no `QtGui` header mentions a fold, a hinge or a posture.
- **Java:** the kit's jars (`Qt6Android.jar`, `Qt6AndroidQuick.jar`) contain no `androidx.window` class.
- **Gradle:** the Gradle template depends only on `androidx.core`.

Qt does handle the configuration change. Its manifest template lists
`orientation|screenLayout|screenSize|smallestScreenSize|density|…` in `configChanges`. So the activity is not recreated
on a fold, and Qt only resizes the window (assumption Q1). On iOS, Qt 6.12 has no API for `UIView.ReservedRegion`, which
is new in iOS 27.

So the hinge comes from a small platform shim behind an interface, with no `#if` (see [Posture](#posture-and-hinge)).

## Mock-ups

The mock-ups are in `docs/design/screens`. There are 26 files: 13 pages, each in light and dark. They are named
`Name.fold-…dc.html` and `Name.flip-…dc.html`.

`tools/visual/fold_mockups.py` generates them. It reuses the building blocks of Stage 8's `ipad_mockups.py`, takes every
colour from `tokens.json`, and every size from the [layout rules](#layout-rules) below. `--check` fails if a file has been
edited by hand. They render like the iPad pages:

```bash
python tools/visual/render_mockups.py --only Main.fold-open Main.fold-book Main.fold-tabletop
```

The frames are design frames, named by their shape, not device specifications. The half-folded pages draw the fold as an
annotation: a band with a dashed line, marked FOLD. The short cover screen draws the side status bar as a placeholder.
Its real width comes from the safe area.

| Mock-up | Frame | What it shows |
|---|---|---|
| `Main.fold-open` | Unfolded, landscape, 841 × 701 | `Wide` with the **rail**: the composer and the 3 × 4 board on the left (128 × 116 pt tiles), the header card and today's log on the right, and the tabs in a rail on the trailing edge. The toast is at the foot of the log. |
| `Main.fold-open-portrait` | Unfolded, portrait, 701 × 841 | `Wide` with the tab bar at the foot: 114 × 139 pt tiles, and a 266 pt log pane. |
| `Main.fold-cover` | Narrow cover screen, 344 × 882 | The phone layout, unchanged except for its width: 99 pt wide tiles, and the chips wrap when they need to. |
| `Main.fold-short` | Short cover screen, 466 × 678 (iPhone Duo outer) | The phone layout with the **rail** beside the side status bar. The gaps are 8 pt, and the toast takes the Last bar's place. The tiles are 89 pt. |
| `Main.fold-book` | Half folded, book, 841 × 701 | The board in the leading half and the log in the trailing half, each 12 pt clear of the fold. |
| `Main.fold-tabletop` | Half folded, tabletop, 701 × 841 | The board on the flat lower half, under the hand. The upright upper half has the header card and the composer side by side, a five-row **log strip**, and the tab bar, all clear of the fold. |
| `Main.flip-tabletop` | Flip phone, tabletop, 412 × 915 | The same rule on a flip phone: the header card and the composer stacked, a two-row log strip, the tab bar, then the fold, then the board. |
| `Review.fold-open` | 841 × 701 | Today's log in the reading column, with the rail. |
| `Header.fold-open` | 841 × 701 | Today's details in the form column. It covers the tabs, as on the phone, so there is no rail, and the page scrolls. |
| `BoardSettings.fold-open` | 841 × 701 | Settings in the form column, with the rail. The page scrolls. |
| `Other.fold-open` | 841 × 701 | The Other picker as a form sheet, centred, no taller than the window less 64 pt above and below. It scrolls inside. |
| `Other.fold-book` | Book | The same sheet in the trailing half, clear of the fold, with the board dimmed but in sight in the leading half. |
| `EditEntry.fold-tabletop` | Tabletop | Edit entry with a note being typed. The sheet is in the upright half and the keyboard is in the flat half. This is the state that must survive a fold. |

These screens are not drawn, because they follow the rules with nothing new:
- History, the editors and Import or export are `Review` and `BoardSettings`;
- First launch is Stage 8's centred column (it is not drawn, so the app's name stays out of new mock-ups);
- the other sheets and alerts follow `Other.fold-open` and `Other.fold-book`;
- an alert is centred in its half;
- `Regular` on the iPhone Duo's inner display in portrait (669 × 951) is Stage 8's iPad portrait layout, with the
  height clamp below.

## Breakpoints: one set of rules for phone, foldable and tablet

Stage 8 chooses the layout from the window's size alone. This stage keeps that, adds no device check, and widens two of
Stage 8's rules. The window's size, its safe-area insets and the fold (if any) are the only inputs.

| Class or variant | When (sizes in pt, or dp on Android) | Stage 8 | Stage 13 |
|---|---|---|---|
| `Compact` | width < 600 | The phone layout | Unchanged |
| `Regular` | 600 ≤ width, and not `Wide` | The column layout, with tiles 0.7 × their width (104 to 152 pt) | Also capped by the height that is left, never below 88 pt (as `Compact` already is). No iPad size changes. |
| `Wide` | height ≥ 640, and either width ≥ 1100, or width ≥ 648 and width ≥ 0.8 × height | Board and log side by side, width ≥ 1100 only | Adds the second condition: near-square and landscape windows from 648 pt wide |
| **Rail** (new) | height < 720 and width ≥ 400, not in tabletop posture | none | The tab bar becomes a 72 pt rail on the trailing edge, or on the edge with the larger side inset |
| **Book** (new) | A separating vertical fold, and each half ≥ 330 wide | none | The board in the leading half and the log in the trailing half, split at the fold |
| **Tabletop** (new) | A separating horizontal fold, the lower half ≥ 416 tall and the upper half ≥ 362 tall | none | The board below the fold; the header card, composer, log strip and tab bar above it |

Why these numbers:
- **648 wide:** the board pane takes 58 per cent of the width (Stage 8). At 648 pt its tiles are 104 pt wide, the width of
  the phone's tiles less its 14 pt padding. Below that, the side-by-side tiles would be narrower than the phone's.
- **0.8 × height:** this keeps every iPad in portrait out of `Wide`. The squarest iPad in portrait is the 13-inch, at
  1032 × 1376, a ratio of 0.75. Near-square unfolded screens are in: 701 × 841 is 0.83.
- **720 high:** this is Stage 8's minimum iPad window height. Below it, a bar at the foot costs the board its fourth row:
  - at 466 × 678 with the bar at the foot, the tiles would be 73 pt, under the 88 pt minimum;
  - with the rail, they are 89 pt.

  Apple's guidelines put the tab bar on the side on the iPhone Duo's outer display, and on its inner display in
  landscape, for the same reason.
- **Width ≥ 400 for the rail:** narrower than that, the rail would leave tiles under 94 pt wide. Such a window keeps the
  bar at the foot, and the Log screen scrolls (Stage 8's fallback below the minimum size).
- **The posture minimums:** these are the sizes at which a half still holds its part at the 88 pt tile minimum. If a half
  is smaller, the posture layout is not used. The normal layout then applies, and the [fold rule](#posture-and-hinge)
  still keeps controls off the fold.

Where the sizes fall:

| Window | Stage 8 | Stage 13 | Note |
|---|---|---|---|
| iPhone 17, 402 × 874; every iPhone ≥ 400 × 720 | Compact | Compact | Unchanged |
| iPhone SE, 375 × 667 (iOS 18 still supports it) | Compact | Compact, no rail | Unchanged. The board does not fit at this height today, which is passed to Stage 7 as a finding. |
| Every iPad, full screen, portrait (744 to 1032 wide) | Regular | Regular | Unchanged |
| Every iPad, full screen, landscape | Wide | Wide | Unchanged; all are ≥ 744 high, so no rail |
| Split View 507 × 1180, 320 × 1180, 507 × 744 | Compact | Compact | Unchanged |
| **Split View 694 × 820** | Regular | **Wide** | Changes. The column there has 207 × 101 pt tiles. Side by side gives 113 × 134. |
| **Stage Manager or desktop window shorter than 720** (e.g. 1000 × 700) | Regular or Wide | Wide, **with the rail** | Changes |
| Play tablet screenshots, 600 × 960 and 1280 × 800 dp | Regular, Wide | Regular, Wide | Unchanged (Stage 11) |
| iPhone Duo outer, 466 × 678 (derived) | Compact | Compact with the rail | New |
| iPhone Duo inner, portrait 669 × 951 (derived) | Regular | Regular, tiles 199 × 134 | New (height clamp) |
| iPhone Duo inner, landscape 951 × 669 | Regular | Wide with the rail | New |
| Reference foldable, 841 × 701 dp | Regular | Wide with the rail | New |
| Reference foldable, portrait, 701 × 841 dp | Regular | Wide | New |
| Narrow cover screen, e.g. 344 × 882 | Compact | Compact | The phone layout; see [narrow widths](#narrow-and-short-cover-screens) |
| Flip phone, unfolded, e.g. 412 × 915 | Compact | Compact | The phone layout, portrait only |

Two Stage 8 behaviours change, and neither is a full-screen iPad layout:
- a Split View window of about 694 × 820 becomes side by side;
- a window shorter than 720 pt gets the rail.

That is [decision 1](#decisions-for-the-owner). `T13-2` pins the table, so any other change to a phone or iPad size fails
a test.

**In code.** `Layout.qml` (8.1) gains:
- the second `Wide` condition;
- the `Regular` height clamp;
- `rail` (a boolean) and `railEdge` (left or right);
- `posture` (`None`, `Book` or `Tabletop`);
- `foldRect` (the band to keep clear, in window coordinates).

It reads the fold from the `Posture` singleton below, and nothing of the platform. `check_qml_platform.py` (T8-2)
applies to it unchanged.

### Layout rules

These add to Stage 8's [layout rules](stage-8.md#layout-rules). Any value that is not listed here is Stage 8's value.

| Rule | Compact | Compact with the rail | Wide with the rail | Book | Tabletop |
|---|---|---|---|---|---|
| Screen margin | 16 | 16 | 24 | 24 | 24 (16 in Compact) |
| Gaps between blocks | 12 | 8 | 12 | 12 | 8 |
| Tile gap | 8 | 8 | 12 | 12 | 8 |
| Tile label, badge | 13.5 pt, 24 pt | 13.5 pt, 24 pt | 17 pt, 28 pt | 17 pt, 28 pt | as the class (13.5 or 17) |
| Tile height | Stage 8 (104, down to 88 when short) | fills what is left, ≥ 88 | fills the board pane, ≥ 88 | fills the leading half, ≥ 88 | fills the lower half, ≥ 88 |
| Tabs | bar at the foot | rail, 72 wide, trailing | rail | rail if short, else bar | bar at the foot of the upper half |
| Last bar | shown | shown; the toast replaces it in place while shown | none (the log pane) | none (the log pane) | grows into the **log strip**: the newest rows that fit, newest last; a tap opens Review |
| Toast | above the tab bar | in the Last bar's place | foot of the log pane | foot of the log pane | over the log strip's title and oldest rows, never the newest |
| Header card | top | top | top of the log pane | top of the log pane | top of the upper half; beside the composer when the upper half is ≥ 600 wide |
| Sheets | bottom sheet | bottom sheet | form sheet, centred in the window | form sheet in the trailing half | form sheet in the upper half (the keyboard takes the lower half) |
| Alerts | Stage 8 | Stage 8 | centred | centred in the trailing half | centred in the upper half |
| Fold band | none | none | none | 12 pt each side of the fold line, or the hinge's own bounds if larger | as Book |

The board stays 3 × 4 in every posture, as in Stage 8 and for the same reason: spatial memory. Apple's advice to prefer an
even number of columns is met another way: the board is never split by the fold, because it sits wholly in one half.

### Narrow and short cover screens

- **Narrow** (a cover screen about 344 pt wide, or any Compact window from 320):
  - the phone layout is unchanged, and only the widths shrink;
  - the template chips wrap, as `NoteComposer` already does;
  - the tile label must not be cut. The tile padding drops from 12 to 10 below 360 pt wide, if T13-10 shows
    "Sequence Out of Order" cut at the 1.3 text cap;
  - there is no new layout.
- **Short** (height < 720, width ≥ 400): the rail, 8 pt gaps, and the toast in the Last bar's slot. On the iPhone Duo's
  outer display, the side status bar is a safe-area inset. The rail goes on that edge, inside the inset, as Apple's own
  bars do. The edge is chosen from the insets: the larger horizontal inset wins, and if they are equal, trailing.
- **Smaller than any of these** (a flip phone's outer screen, if the user lets apps run there, or a split-screen half on
  a phone): the Log screen scrolls (Stage 8's fallback). No layout is designed for it. Nothing is hidden, and no state is
  dropped.

## Posture and hinge

### The interface

`libs/ios_bridge` gets one more Stage 5-style interface. Like the others, it has a portable core, platform files chosen
by CMake, and no `#if`:

```cpp
// libs/ios_bridge/include/error_journaling/posture.hpp (sketch)
namespace ej
{
enum class FoldOrientation { VERTICAL, HORIZONTAL };

struct Fold
{
    QRectF m_Bounds;              // window coordinates, in points (dp on Android)
    FoldOrientation m_Orientation;
    bool m_Separating = false;    // half open, or a hinge with a gap
};

class PostureSource               // one per platform
{
public:
    virtual ~PostureSource() = default;
    virtual std::vector<Fold> folds() const = 0;
    virtual void set_on_changed(std::function<void()> callback) = 0;
};

enum class Posture { NONE, BOOK, TABLETOP };

// Portable and unit-tested: the posture, from the window's size and the folds (rules above).
Posture classify_posture(QSizeF window, std::span<const Fold> folds);
}
```

A `PostureModel` QObject wraps the source and is registered as the `Posture` QML singleton. It exposes `posture`,
`foldRect` and `foldOrientation`. `Layout.qml` binds to it.

| Platform | `src/platform/<os>/posture.*` | What it does |
|---|---|---|
| Android | `posture.cpp` with Java in `EjActivity` | `EjActivity` collects `WindowInfoTracker.windowLayoutInfo(activity)` and calls a `native` method with each `FoldingFeature`: bounds in pixels, orientation, state, `isSeparating`, occlusion. C++ converts pixels to dp with the window's device-pixel ratio, in a pure function (T13-12). It needs one Gradle dependency, `androidx.window:window`, in the Android package's `build.gradle`. |
| iOS | `posture.mm` | It observes the Qt view's layout. On each layout it reads `reservedRegions(kind:options:)` for divisions, which needs iOS 27. The check is `if (@available(iOS 27.0, *))`, a runtime check, not the preprocessor. It converts each active division to a `Fold`. On iOS 26 and earlier it returns nothing. |
| Desktop | `posture.cpp` | No folds. |
| Tests | `FakePostureSource` | Tests set folds by hand. The UI rig gains `Rig::set_folds(...)`. |

`tools/ci/check_platform_layout.py` already pairs the platform directories (Stage 9 extends it to Android). The new file
is in every directory.

### The fold rule

When the posture is `Book` or `Tabletop`, or when any separating fold crosses the window, nothing interactive may
intersect the fold band. That covers tiles, the composer, sheet and alert buttons, tab items, the toast and its Undo, and
rows. The rule holds even in the fallback layout, where a half is too small for the posture layout. T13-5 checks it.
- **Hinges with a gap** (a dual-screen device spanning both screens): the band is the hinge's own bounds, which hide
  content.
- **Flat folds** (`FLAT`, not separating), and the iPhone Duo fully open: the fold is ignored, as both platforms advise.
- **Occlusions** (the iPhone Duo's inner camera when it is on): ignored. The app does not use the camera, and the outer
  camera is inside the safe area (assumption I3).

### Moving between arrangements

Apple advises against "extreme layout changes" as the device folds. So the posture layouts keep each item's place where
they can:
- **Book** is `Wide` with the split moved to the fold.
- **Tabletop** keeps the board at the foot, under the hand, as on the phone. Only the header card, the composer and the
  log move up.

A change of posture animates with the existing page motion, or with none when Reduce Motion is on.

## Keeping state across a fold

This is a data-safety requirement. Losing the open log's entries is already a P1 defect (`AGENTS.md`). Losing a typed
note draft is treated the same way here.

**Why it is possible.**
- On Android, Qt declares the configuration changes, so a fold resizes the same window and does not recreate the
  activity (assumption Q1).
- On iOS, the scene moves between displays, and Qt keeps the `QQuickWindow` (assumption I2).
- The QML tree is never rebuilt. The shell's tab `Loader`s stay active once made (`AppShell.qml`), and Stage 8's 8.6 moves
  items between arrangements instead of making them again.

**What this stage adds, item by item:**

| State | How it is kept | Test |
|---|---|---|
| The note draft in the composer | It is already in `DraftStore` (NOTE-03), written within 300 ms and flushed when the app goes inactive. A fold changes neither the item nor its text. Focus and cursor position are restored after the relayout (below). | T13-6, E5 |
| Text typed in an open sheet (Edit entry's note, Template value's answer, a new error type's name) | Each sheet's text is held in the sheet's controller object, which outlives a relayout, and not only in the `TextField`. The sheet stays the same item when it changes mode (bottom sheet to form sheet, window to half). | T13-6, T13-8, E6 |
| The open sheet | `SheetHost` keeps it open, and re-places it by the new class and posture. | T13-6, T13-8 |
| The entry being edited | Edit entry keeps the entry's id and its unsaved changes in its controller. The count of entries is the same before and after every step. | T13-6, E6 |
| Focus, cursor and the keyboard | The shell records the focused field and its cursor and selection on every focus change. After a size or posture change, it gives focus back and asks for the input method again. A fold can drop the keyboard on either platform (assumptions Q2 and I2). | T13-6, E7 |
| Scroll position | Review, History, the log pane, the log strip and the editors keep their top visible row (its index and offset), and restore it after the relayout, because rows reflow at a new width. The log pane and the strip keep "scrolled to the newest" if they were. | T13-6, E8 |
| The toast and its Undo | The toast keeps its timer, and Undo still undoes. | T13-6 |
| Tab, pushed pages, Settings stack | These are unchanged, because the `Loader`s and `PageStack`s are never reset. | T13-6 |
| A tap during a fold | The board logs on the press, so a touch the fold cancels logs nothing more. The toast still offers Undo. | T13-7 |

**Not covered:** text typed in an open sheet is not written to disk. It survives a fold, but not the process being
killed while the app is in the background. Folding a Galaxy Z Fold can send the app to the background if the user has
not chosen "continue on cover screen". That is a lifecycle event, and the composer's draft is flushed on it. Writing
sheet text to disk is a storage change (the Stage 2 gate), and is [decision 5](#decisions-for-the-owner).

## Orientation

**Recommendation:** portrait on a phone-sized display, and every orientation on an unfolded one, chosen at run time from
the display's smallest width. This is the same 600 line that Android 16 uses.

Why:
1. **Android already does this for unfolded screens.** With target API 36, Android 16 ignores the portrait lock when the
   smallest width is at least 600 dp, and the opt-out ends at target 37. Fighting it would mean letterboxing, which
   fails Play's Tier 3 test `T-Config_Orientation`.
2. **Tabletop needs it.** On a book-style foldable, tabletop is the unfolded screen turned so that the fold runs across.
   A portrait-locked app cannot be used in tabletop on a device whose inner screen is landscape by nature (the reference
   foldable is 841 × 701).
3. **The layouts already cover it.** Every unfolded size is `Wide` or `Regular`, in both orientations, and each has a
   mock-up.
4. **A phone stays a phone.** Cover screens and flip phones are under 600 dp in their smallest width, so they stay
   portrait, as the owner wants for phones.

How:
- **Android.**
  - The manifest's `screenOrientation` becomes `unspecified`.
  - `EjActivity` calls `setRequestedOrientation(PORTRAIT)` when `smallestScreenWidthDp` < 600, and `UNSPECIFIED`
    otherwise. It does this on create and on each configuration change.
  - The smallest width does not change on rotation, so this cannot loop.
  - This also covers Android 9 to 15, where the manifest lock would be honoured and the inner screen letterboxed.
  - The decision is a pure function, `requested_orientation(int smallest_width_dp)`, which is unit-tested (T13-14).
  - The temporary opt-out property is not set.
  - This changes Stage 9's decision 2 for unfolded foldables, from (a) to (b): they get the adaptive layout, and they are
    not excluded. That is [decision 2](#decisions-for-the-owner).
- **iOS (iPhone Duo).** Apple's Duo pages say nothing about `UISupportedInterfaceOrientations`. The iPhone list stays
  portrait only until it has been checked in Device Hub (assumption I4). If iOS rotates or letterboxes a portrait-only app
  on the inner display, the iPhone list gains the landscapes, and the bridge locks to portrait at run time in `Compact`.
  That uses the scene's supported orientations, which is the same rule as on Android. The iPad list is Stage 8's.

## Flip phones

Unfolded, a flip phone is a tall `Compact` phone, and it gets the phone layout, portrait only. Half open on a table, it is
in tabletop (`Main.flip-tabletop`): the board on the lower half, and the log strip and the tabs on the upper half. The
small outer screen is not designed for, as described under [narrow and short cover screens](#narrow-and-short-cover-screens).
Apps run there only when the user allows it in the phone's settings.

## Dual-screen (Surface Duo)

**Recommendation: no specific support, and no testing.**
- The Surface Duo is no longer sold, and it no longer gets Android updates.
- Its hinge reports as a separating `FoldingFeature`, with the hinge's bounds as the band. So if the app is spanned across
  both screens, the Book and Tabletop rules already keep the board and the controls off the gap.
- Single-screen use is a `Compact` phone.

The plan spends no emulator time or device check on it.

## Store requirements

| Store | Requirement | Where it is met |
|---|---|---|
| Google Play | **Adaptive app quality, Tier 3 (Adaptive ready).** The app fills the window and is not letterboxed or in compatibility mode. It keeps state through rotation, fold and resize (`Config_Changes`, `Config_Combinations`). It is fully usable in multi-window mode (`Multi-Window_Functionality`). It handles a hardware keyboard and basic mouse or trackpad use. | This stage: [orientation](#orientation), [state](#keeping-state-across-a-fold), checklist E1 to E12 |
| Google Play | **Tier 2 (Adaptive optimized)** is needed for Play's large-screen badge. It asks for optimised layouts at every size, such as two panes and navigation rails. The side-by-side Log and the rail go most of the way. | Not a v1 goal. [Decision 4](#decisions-for-the-owner) asks whether to aim for it. |
| Google Play | Test on the reference foldable, 841 × 701 dp, with the **Pixel Fold** emulator | The [emulator checklist](#emulator-checklist-the-exit-gate) |
| App Store | iPhone Duo screenshots, inner and outer, are required from April 2027 for apps built with the iOS 27.1 SDK | Stage 11's `ej_screenshots` renders 669 × 951 and 466 × 678 pt at 3x, and their landscapes. These are added to Stage 11's list. |
| App Store | The app must be built with the **iOS 27.1 SDK (Xcode 27)**. Built with the iOS 26 SDK, it shows small and centred on the Duo's inner display. | 13.6 moves the iOS build and release jobs to Xcode 27. Today `ios-build.yml` runs on `macos-26`, and `ci.yml` has an experimental `macos-xcode27` leg for the desktop. Assumption I5 is that Qt 6.12 builds with it. |

## Work packages

Each package is one squash-merged pull request with tests. Every package keeps the phone and iPad layouts unchanged. The
phone UI tests, the Stage 8 iPad tests, `launch.matches_screen` and the visual capture of the phone and iPad screens must
pass with no change to their expected values, apart from the two window sizes of decision 1 once the owner agrees them.

| # | Package | Contents | Needs | Tests |
|---|---|---|---|---|
| 13.1 | Classes and rail | The second `Wide` condition, the `Regular` height clamp, and the rail mode of `TabBar` with its edge from the insets. Short `Compact` gaps and the toast in the Last bar's slot. Narrow-width checks. The UI rig gains the foldable sizes and side insets. Emulator profiles are measured, and the sizes recorded here. | 8.1 to 8.5 | T13-1, T13-2, T13-4, T13-9, T13-10 |
| 13.2 | Posture interface | `posture.hpp`, `classify_posture`, `PostureModel` and the `Posture` singleton, the desktop source and the fake. `Rig::set_folds`. `Layout.posture` and `foldRect`. There is no visible change. | 13.1 | T13-3 |
| 13.3 | Posture layouts | `LogScreen`'s Book and Tabletop arrangements, which move items and do not make them again. The log strip, a new `LogStrip.qml` that reuses the log pane's rows. Sheets and alerts placed by region. The fold rule. | 13.2, 8.6 | T13-5, T13-8, T13-11, T13-16 |
| 13.4 | State across a fold | Sheet text in the sheet controllers. Focus, cursor and input method restored. Scroll anchors. The combination tests. | 13.3 | T13-6, T13-7 |
| 13.5 | Android shim and orientation | `androidx.window` in Gradle. `EjActivity`'s `WindowInfoTracker` listener calls the native method, which calls `posture.cpp`. The run-time orientation rule. Manifest checks. Emulator runs (checklist E1 to E12). A `changelog.d` fragment ("Adapts to foldable phones, folded, unfolded and half folded"). | 9.1, 9.7, 13.3 | T13-12, T13-14 |
| 13.6 | iOS: iPhone Duo | The iOS build and release move to Xcode 27 and the iOS 27.1 SDK (`ios-build.yml`, `release.yml`, `docs/ci.md`). `posture.mm` reads the reserved regions. The orientation outcome of I4. Device Hub checks D1 to D6, which need a Mac. | 13.3, Stage 8's 8.7 | T13-13, checklist D |
| 13.7 | Visual pass and exit gate | The capture of the app against the foldable mock-ups (`run_capture.py`). Fixes. The emulator and Device Hub checklists. | all | T13-15, checklists |

The order is 13.1, 13.2, 13.3, 13.4, then 13.5 and 13.6 in parallel, then 13.7. Package 13.5 must not ship the
run-time orientation rule before 13.3. Otherwise an unfolded phone would turn to landscape without the posture layouts.
The rest of the layout is already there from Stage 8.

## Tests

The labels are those in `docs/testing.md`. Every `ui` test runs in the offscreen app at the size it names. The rig places
the folds with `Rig::set_folds`.

**Sizes:**
- 841 × 701 and 701 × 841 (insets 24 and 20);
- 951 × 669 and 669 × 951;
- 466 × 678 (right inset 44, top 12, bottom 20);
- 344 × 882 and 320 × 800 (inset 40 and 20);
- 412 × 915;
- Book: 841 × 701 with a vertical fold at x = 420;
- Tabletop: 701 × 841 with a horizontal fold at y = 420, and 412 × 915 with a horizontal fold at y = 457;
- a hinge with a gap: 1280 × 900 with a 28 pt vertical hinge.

| ID | Test | Label | What it proves |
|---|---|---|---|
| T13-1 | `layout.classes_fold` | unit | The class, the rail and its edge at each size above, and at the edges of each condition: widths 399 and 400, 599 and 600, 647 and 648, 1099 and 1100; heights 639 and 640, 719 and 720; a ratio just under and just over 0.8. |
| T13-2 | `layout.stage8_sizes` | unit | Every Stage 8 test size has the class in the [table](#breakpoints-one-set-of-rules-for-phone-foldable-and-tablet). Only the two rows of decision 1 differ from Stage 8. In `Compact` without the rail, every derived value equals today's `Theme` value. |
| T13-3 | `posture.classify` | unit | `classify_posture` for: no fold; a flat fold; a half-open vertical and horizontal fold; halves too small; a hinge with a gap; two folds (the first separating one wins); a fold outside the window (none). |
| T13-4 | `fold.screens_fit` | ui | At each size and posture, each tab's root screen: nothing is outside the window or under the safe area; no text is cut off; every touch target is at least 44 pt; the 12 tiles are visible without scrolling and at least 88 pt high. |
| T13-5 | `fold.nothing_on_fold` | ui | In Book, Tabletop and with the hinge, no interactive item's rectangle meets the fold band. That covers tiles, the composer, the tab items or rail, the toast, rows, and each sheet's and alert's buttons, with the sheet open. |
| T13-6 | `fold.keeps_state` | ui | The fold sequence: cover, then open, book, tabletop, open, cover, then open in portrait. The state going in: a draft note with focus and the cursor mid-word; Edit entry open with an unsaved note; Review scrolled to row 5 of 30; Settings with an editor pushed; a toast with Undo. After every step: each `TextField` is the same object with the same text, cursor and focus; the sheet is open on the same entry with its note; Review's top row is the same; the pushed page is there; the entry count is unchanged; Undo removes the toasted entry. |
| T13-7 | `fold.combinations` | ui | Play's `Config_Combinations`: a resize then a posture change, and the other way round; a fold during a sheet's open animation; a fold during a press on a tile; a fold while the keyboard is moving. The same checks as T13-6. A cancelled press adds no entry. |
| T13-8 | `fold.sheet_placement` | ui | Each sheet in Book is in the trailing half, and in Tabletop in the upper half, inside the safe area and clear of the band. It is above a simulated keyboard (313 and 398 pt). Cancel and Save are reachable. The reading order is the sheet only. |
| T13-9 | `fold.rail` | ui | The rail is shown exactly when the rule says, and is never shown in Tabletop. It goes on the edge with the larger side inset. Its targets are at least 44 pt. It is last in the reading order, as the tab bar is. |
| T13-10 | `fold.narrow_labels` | ui | At 320, 344 and 360 wide, at text scale 1.0 and at the 1.3 cap: "Sequence Out of Order" is not cut, and the chips wrap without clipping. |
| T13-11 | `fold.reading_order` | ui | Book: composer, board, header card, log, Share log, then the tabs. Tabletop: header card, composer, log strip, board, then the tabs. |
| T13-12 | `posture.android_convert` | unit | The pure conversion of a `FoldingFeature`'s bounds in pixels and its fields to a `Fold` in dp, at densities 2.625 and 3.5, including a window offset in multi-window. |
| T13-13 | `posture.ios_convert` | unit | The pure conversion of reserved-region frames to `Fold`: active divisions only; occlusions ignored. |
| T13-14 | `android.orientation_policy` | unit | `requested_orientation` at smallest widths 599 and 600. A manifest check in `tools/ci`: `screenOrientation` is unspecified, `resizeableActivity` is not false, and no orientation opt-out property is set. |
| T13-15 | Visual pass | none (tool) | `run_capture.py` pairs each foldable mock-up with the app in the same size and posture. The differences are reviewed in 13.7. |
| T13-16 | `fold.tap_latency` | perf | The press path at 841 × 701 in Book keeps the phone's budget. A posture change event does no work on the press path. |

## Emulator checklist (the exit gate)

There is no foldable here. The Android checks run on the Android Studio emulator, on `E:\android\`, which Stage 9 sets
up. The foldable system image needs a few more gigabytes there.

The profiles:
- **Phone > Pixel Fold.** This is the profile Google's adaptive-app tests name.
- A fold-in device with an outer display.
- A horizontal flip.
- **Resizable.**

The emulator's fold and unfold controls and its hinge-angle sensor (Extended controls, Virtual sensors) set the posture.
The exact controls are confirmed with the installed emulator version (assumption Q3). Record pass or fail, the emulator
version and the system image.

| # | Check |
|---|---|
| E1 | Each Main mock-up's size and posture matches its mock-up, light and dark (the owner's eye, with the mock-up open beside it). |
| E2 | Unfold and fold repeatedly during a tap burst. Every tap is logged once, and the count in Review matches. |
| E3 | Rotate the unfolded device: all four orientations work. The cover screen and the flip stay portrait. |
| E4 | Book and Tabletop (hinge angle about 90°): the layout changes, nothing sits on the fold, and it changes back when flat. |
| E5 | Type a note in the composer, then fold, unfold, and go to tabletop. The text, the cursor and the keyboard come back. Kill the app from the cover screen and relaunch: the draft is there (NOTE-03). |
| E6 | Open Edit entry, change the note, then fold and unfold. The sheet is still open on the same entry, with the change. Save writes it. |
| E7 | With Other, Template value and Edit entry each open: book, tabletop, flat. Each sheet moves to its half and stays usable with the keyboard. |
| E8 | Scroll Review half way, then fold and unfold: the same rows are in view. |
| E9 | Split screen beside another app, at each divider position, folded and unfolded. Everything is usable, as Play's Tier 3 requires. |
| E10 | A hardware keyboard and a mouse on the Resizable emulator (Play Tier 3): typing, Tab, clicks, scroll. |
| E11 | TalkBack in Book and Tabletop: the order is as T13-11. |
| E12 | Largest font and display size on the cover screen and the inner screen: no text is cut off, and tile labels stop at the cap. |

**iPhone Duo (Device Hub in Xcode 27, on a Mac; there is none here):**

| # | Check |
|---|---|
| D1 | Outer display: the rail sits beside the side status bar, and the tiles are at least 88 pt. Record the window size and insets, which confirm or correct I1. |
| D2 | Inner display, portrait and landscape: `Regular` and `Wide` with the rail, as drawn. The app fills the display (built with the iOS 27.1 SDK). |
| D3 | Partly folded, both ways: the reserved region arrives, and the Book or Tabletop layout follows. Fully open: it goes away. |
| D4 | Open and close with a draft, an open sheet and Review scrolled: as E5 to E8. |
| D5 | Orientation (assumption I4): what iOS does with the portrait-only list on the inner display. |
| D6 | VoiceOver order, as T13-11. |

A check on a real foldable is recommended before release but not required by this gate. That is
[decision 3](#decisions-for-the-owner).

## Assumptions only a device or simulator can confirm

| # | Assumption | Checked by | If wrong |
|---|---|---|---|
| Q1 | Qt 6.12's Android activity is not recreated on a fold (its `configChanges` covers it), and `QWindow` is resized in place, including a density change between the cover and inner screens | E2, E5 | Check for a second `onCreate` in the log. If the activity is recreated, find the missing change from the configuration diff, and add it to `configChanges` in our manifest. The draft is safe either way (NOTE-03), but a sheet's text is not. |
| Q2 | The keyboard comes back after a fold when the shell refocuses the field | E5 | The bridge calls `QInputMethod::show()` after the resize settles |
| Q3 | The emulator's fold controls and hinge sensor drive `WindowInfoTracker` as a device would | E4 | Use the Resizable emulator's posture options, or record the gap for a device check |
| A1 | `WindowInfoTracker` reports a `FoldingFeature` while the activity is a Qt activity with no Compose or View layout of its own | E4 | Read `WindowLayoutInfo` from the `QtLayout` view's window instead |
| I1 | The iPhone Duo's windows are 466 × 678 pt (outer) and 669 × 951 pt (inner), and the outer side inset is about 44 pt | D1, D2 | The rules are by size, so the layout follows. Only the tables here and Stage 11's screenshot sizes change. |
| I2 | Qt keeps the `QQuickWindow` when the scene moves between the outer and inner display, and resizes it | D4 | The bridge passes the scene's new size to the window (Stage 8's A1 fallback) |
| I3 | The outer camera and the side status bar are inside the safe-area insets Qt reports | D1 | The bridge adds the occlusion region to the insets it passes to `SafeFrame` |
| I4 | The iPhone's portrait-only orientation list does not stop the inner display being used in landscape | D5 | Add the landscapes to the iPhone list, and lock to portrait in `Compact` at run time through the bridge |
| I5 | Qt 6.12's iOS plugin builds and runs with Xcode 27 and the iOS 27.1 SDK | 13.6's CI run | Wait for the first Qt release that supports it, and keep the iOS 26 SDK until then. The Duo then shows the app small and centred. |

## Coordination with the other stages

- **Stage 8 (iPad).**
  - This stage builds on `Layout.qml`, Wide, form sheets and 8.6's state keeping.
  - It changes Stage 8's rules only as in [decision 1](#decisions-for-the-owner).
  - It reuses `ipad_mockups.py`'s blocks, so a change to the copy there changes the foldable pages on the next run of
    `fold_mockups.py`.
- **Stage 9 (Android).**
  - 13.5 extends `EjActivity` (9.7) and the Android `build.gradle` (9.1).
  - It replaces the orientation of 9.7 with the run-time rule, which changes Stage 9's decision 2 for foldables.
- **Stage 7 (review).** A finding is passed on: on the iPhone SE (375 × 667), which iOS 18 still supports, the Log screen
  does not fit today.
- **Stage 10 (documentation).** The guide can show the book and tabletop layouts from the mock-ups.
- **Stage 11 (store release).**
  - It adds the iPhone Duo screenshot sizes (inner and outer, both orientations).
  - It moves the release build to Xcode 27.
  - It can add foldable screenshots to the Play listing.
- **Stage 12 (desktop).** It gets the rail for short windows and the second `Wide` condition.

## Decisions for the owner

1. **The two Stage 8 window-size changes.** A Split View window of about 694 × 820 becomes side by side, and any window
   shorter than 720 pt gets the rail. Recommended: accept both, so that one set of rules covers every device. The
   alternative keeps Stage 8 exactly, and adds foldable-only rules keyed on the fold. That is a device check in all but
   name.
2. **Orientation on unfolded foldables.** Recommended: every orientation at a smallest width of 600 or more, portrait
   below, decided at run time. This replaces Stage 9's decision 2 (a) for foldables, so foldables are not excluded and no
   opt-out is set. Tablets follow the same rule.
3. **The exit gate without a device.** Recommended: emulators and Device Hub for the gate, plus one real foldable
   (borrowed, or a TestFlight or Play internal tester who owns one) before the public release.
4. **Play's large-screen badge (Tier 2).** Recommended: not a v1 goal. Tier 3 is the gate.
5. **Sheet text on disk.** Recommended: keep sheet text in memory only. It survives a fold, but not a kill in the
   background. Writing it to disk is a storage change, which would go to a later stage.
6. **The flip phone's outer screen and the Surface Duo.** Recommended: no design and no testing for either. A screen that
   small scrolls, and a spanned Surface Duo follows the fold rule.
7. **The mock-ups.** Approve them, or ask for changes. Changes are made in `tools/visual/fold_mockups.py`.

## Sources

- Apple Newsroom, "Apple unveils iPhone Duo" (10 September 2026):
  <https://www.apple.com/au/newsroom/2026/09/apple-unveils-iphone-duo/>
- Apple, "Three steps to make your app shine on iPhone Duo": <https://developer.apple.com/iphone-duo/prepare/>
- Apple, "Preparing your app for iPhone Duo":
  <https://developer.apple.com/documentation/technologyoverviews/preparing-your-app-for-iphone-duo>
- Apple, Human Interface Guidelines, "Designing for iPhone Duo":
  <https://developer.apple.com/design/human-interface-guidelines/designing-for-iphone-duo>
- Apple, App Store Connect screenshot specifications:
  <https://developer.apple.com/help/app-store-connect/reference/app-information/screenshot-specifications>
- Android, "Use window size classes":
  <https://developer.android.com/develop/ui/compose/layouts/adaptive/use-window-size-classes>
- Android, "Make your app fold aware": <https://developer.android.com/guide/topics/large-screens/make-apps-fold-aware>
- Android, Adaptive app quality guidelines and Tier 3:
  <https://developer.android.com/docs/quality-guidelines/adaptive-app-quality>
