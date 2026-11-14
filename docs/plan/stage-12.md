# Stage 12: Mac and Windows desktop (v2)

Plan for Stage 12 of the [implementation plan](../implementation-plan.md) ("Stages 7 to 12"). Stage 12 turns the Mac and
Windows development builds into products that a tester can install, keep updated, and use with a mouse and keyboard.

**Decision (owner, 10 Nov 2026): desktop support is v2.** Nothing on this page is built for v1, including the Windows CI
leg (12.1). The macOS desktop build stays a development tool, as the PRD says, and Windows stays on the developer
machine only. The page is kept as the starting plan for v2. When v2 is planned, its facts are checked again (Qt, the
stores, the signing services and their prices) and its open [decisions](#decisions-for-the-owner) are answered then.

The text below is the plan as it was written for the v1 decision. It covers effort, risk, and what the owner would need
to provide, and the [recommendation](#recommendation) it made.

**Exit gate (if it ships):** on each platform that ships, an installed and signed build from the release workflow passes
the [desktop checklist](#desktop-checklist-the-exit-gate) on a real machine. It installs without a security warning,
updates to the next build, logs a day, shares or copies the log, imports a setup file, and survives a kill with no lost
entry.

**Status:** deferred to v2 by the owner on 10 Nov 2026. No code is written for v1. In v2, the page is reviewed again and
approved before any code, and building starts after Stage 8's wide layout has merged.

## Recommendation

| Option | Effort | What the owner provides | Risk |
|---|---|---|---|
| A. Both platforms in v1 | 10 PRs, about 3 weeks of agent work, plus two checklist passes | Apple Developer ID and notarisation; a Windows signing route; store accounts if stores are chosen | Medium to high: v1's release date then depends on two more platforms |
| **B. Windows only in v1, Mac in v1.1 (recommended)** | 8 PRs, about 2 weeks | A Windows signing route (Microsoft Store, recommended, or a certificate) | Low to medium |
| C. Mac only in v1 | 7 PRs, about 2 weeks | Developer ID and notarisation (the Apple membership already exists for TestFlight) | Medium: nobody can run the build here; every check needs the owner's Mac |
| D. Defer both to v1.1 | 1 PR now (12.1, the Windows CI leg) | None now | None for v1 |

**Recommended: B, and only if Stages 7 to 11 are on time. Otherwise D.** Windows is the cheaper and safer platform here:

- The development machine is a Windows machine, so the app is built, run and tested here every day. That covers the
  `windows-msvc-*` presets and the platform files in `libs/storage/src/platform/windows/`. An agent can run the whole
  desktop checklist on this machine except for the install from the store.
- There is no Mac. Every Mac check would need the owner's Mac (O4 in Stage 5) and a person at it. That is the same cost
  Stage 5 paid for the iPhone, and it would be paid again for each release.
- An iPad build already runs on Apple-silicon Macs. The App Store offers iPhone and iPad apps on those Macs unless the
  developer opts out ("Designed for iPad"). Once Stage 8 lands, a Mac user has a usable route at almost no cost, which
  makes a separate Mac product worth less in v1. See [Mac route 0](#route-0-the-ipad-app-on-a-mac-no-new-product).

Within option D, the cheap part of the work, a Windows leg in CI, is still worth doing in v1 (see
[12.1](#121-windows-ci-leg)). It keeps the Windows build from breaking while it waits for v1.1.

## Today's starting point

| Area | Mac | Windows |
|---|---|---|
| Presets | `macos-debug`, `macos-release` (`arm64-osx`): "for iterating on the user interface. Nothing is released from it." | `windows-msvc-release`, `windows-msvc-asan`: developer machine only |
| CI | `build-macos` (macOS 26) builds and runs the tests; `macos-xcode27` is experimental; the kill queue runs `macos-debug` | None. `docs/ci.md`: "There is no Windows leg and no desktop package" |
| Window | `Main.qml` opens a `Window` at the design frame, 402 × 874 (an iPhone 17 in portrait) | Same |
| Platform bridge | `libs/ios_bridge/src/platform/desktop/` stubs: share reports `UNAVAILABLE`; the document picker reports `UNAVAILABLE`; haptics do nothing; accessibility settings, background tasks and timing marks are stubs | Same files |
| File choosing | `qml/platform/desktop/FileChooser.qml` uses Qt's `FileDialog`, starting in Documents | Same |
| Storage | `libs/storage/src/platform/macos/` (durable sync, file lock, no per-file protection) | `libs/storage/src/platform/windows/`, run here by the kill and corruption tests |
| Data folder | `QStandardPaths::AppDataLocation` (`~/Library/Application Support/<org>/<app>`) or `--data-dir` | `AppDataLocation`, which on Windows is the roaming `%APPDATA%\<org>\<app>` |
| Packaging | `apps/app/packaging/ios*` only; no desktop bundle metadata, icon, or deploy step | None |
| Release | `release.yml` has `version`, `source`, `ios`, `publish` | Same |

The bridge chooses between `ios/` and `desktop/` (`libs/ios_bridge/CMakeLists.txt`). Mac, Windows and Linux share
`desktop/`. A desktop product needs Mac-specific and Windows-specific files, so Stage 12 adds `src/platform/macos/` and
`src/platform/windows/` next to `desktop/`. Those files are picked with `ICL_PLATFORM`, as `libs/storage` already does,
and Linux keeps `desktop/`. There is no `#if` (AGENTS.md).

## What it takes to make these products

### Window sizing and the wide layout

Stage 8 adds a wide layout: in iPad landscape the board and the log sit side by side, and in portrait the phone layout
is scaled up. The desktop reuses that layout and adds nothing new.

- **Breakpoint, not device.** The shell picks the layout from the window's width and height, not from the platform. If
  Stage 8 keys the layout on "is an iPad" or on orientation, 12.2 first changes it to a size test that both use. That
  change is the one dependency on Stage 8's code, and it should be asked for in Stage 8's review.
- **Sizes.** The minimum window is the phone frame, 402 × 640, so the board and one row of the log fit. Below the
  breakpoint the app shows the phone layout, and above it the wide layout. The first window opens at 1180 × 820 (an
  11-inch iPad in landscape), or at 90 % of the screen if that is smaller.
- **Remembered geometry.** Size, position and maximised state are saved through `QSettings` and restored on the same
  screen. If that screen is gone, the window opens on the primary screen.
- **Live resize.** Crossing the breakpoint while a sheet is open, or while text is being typed, keeps the sheet, the
  draft and the focus. A `ui` test resizes the window across the breakpoint in both directions with a note open.
- **Safe areas and the keyboard.** On the desktop the insets of `SafeFrame` are zero and the on-screen keyboard code
  (`theme/Keyboard.qml`) is idle. A `ui` test checks that no screen leaves a phone-sized gap.
- **High DPI.** Qt 6 uses per-monitor DPI on Windows. A window dragged between a 100 % and a 200 % screen redraws sharp,
  and this is a checklist row. Fonts follow `Theme`; the desktop text size follows the system scale and has no
  separate large-text setting.
- **Title bar.** `Main.qml` sets `Qt.ExpandedClientAreaHint` for the full-screen iPhone. The desktop keeps the system
  title bar, so the flag is set only on iOS, through a property the platform layer provides.

### Mouse and keyboard

Every touch gesture in the app needs a mouse or keyboard equivalent. The table lists today's gestures from
`apps/app/qml`.

| Touch today | Where | Desktop equivalent |
|---|---|---|
| Tap a tile to log | `Tile.qml` | Click. Keys 1–9 and 0 log the tiles in reading order when the board has focus. The press highlight on mouse-down is the feedback (there are no haptics) |
| Hold a tile to log with a note (4.17, BRD-07) | `Tile.qml` `held` | Right-click → "Log with note…", or Shift+click, or Shift+digit. A press-and-hold with the mouse still works |
| Swipe a row left for Copy and Delete | `SwipeRow.qml` | Hover shows Copy and Delete at the row's end. Right-click opens a menu with Copy, Edit and Delete. Delete or Backspace on a focused row deletes with undo. The drag still works |
| Tap a row to edit | `Review.qml`, History | Click or Enter opens Edit. **Double-click is not used for opening**, because one click already opens. It replaces double-tap only where a double-tap exists |
| Repeated taps to show the timing readout | `SettingsAbout.qml` (`tapCount`) | `TapHandler` counts mouse clicks the same way, so nothing changes |
| Drag a sheet down to close | `Sheet.qml` | Esc, a click outside the sheet, and a close button that appears when there is no touch screen |
| Drag to reorder | `EditorRow.qml` | The drag works with a mouse. Alt+Up and Alt+Down move the focused row. Hover shows the grip |
| Spin the time tumbler | `TimeTumbler.qml` | Scroll wheel, arrow keys, and typing the digits |
| Undo toast | `UndoToast.qml` | Ctrl+Z (⌘Z on Mac) does what the toast's Undo does, while the toast shows |
| Tab bar, nav bar | `TabBar.qml`, `NavBar.qml` | Ctrl+1/2/3 (⌘1/2/3) switch tabs; Alt+Left or ⌘[ goes back |

Shortcuts for the whole app:

- Ctrl+C (⌘C) on Review copies the log, the same action as Share on a desktop.
- Ctrl+, (⌘,) opens Settings.
- Ctrl+O (⌘O) imports a setup file, and Ctrl+Shift+S (⌘⇧S) exports one.
- Ctrl+W closes a sheet or the window, and Ctrl+Q (⌘Q) quits.
- F1 opens Stage 10's in-app help.

Shortcuts use Qt's `StandardKey` where one exists, so each platform gets its own keys. They are listed in Stage 10's
help.

General rules:

- **Hover.** Every control that can be clicked has a hover state from the theme tokens: a tint, not a new colour. Hover
  never shows information that is not reachable some other way.
- **Focus.** Tab and Shift+Tab move through every control in reading order, with a visible focus ring from the tokens.
  Stage 7's accessibility audit (4.16) is the baseline, and the desktop adds the keyboard pass.
- **Menus.** On the Mac a native menu bar holds App, File, Edit, View, Window and Help, and the shortcuts sit in it so
  they can be found. On Windows there is no menu bar; the shortcuts are in the right-click menus and the help.
- **Scrolling.** Lists scroll with the wheel and the trackpad at desktop speed, not touch flick speed. A row that
  responds to a drag does not take vertical wheel events.

### The platform layer on desktop

| Capability | Mac (`src/platform/macos/`) | Windows (`src/platform/windows/`) |
|---|---|---|
| Share (REV-05) | The system share picker (`NSSharingServicePicker`) anchored to the Share button. Mail, Messages, Notes and AirDrop are offered, and Slack too if it is installed and offers a share extension. Copy is always beside it | **Copy to the clipboard plus "Save as…"** (a text file). The Windows share UI (`DataTransferManager` through `IDataTransferManagerInterop`) is possible, but few desktop apps take text from it and Slack does not. Recommended: copy and save only |
| What counts as shared (DAY-01) | The picker reports a completed service, so it counts as shared, the same rule as iOS | A copy or a save counts as shared. That matches what the phone does for Copy, and the owner confirms it (decision 3) |
| Haptics | None (the `desktop/` stub) | None |
| File open | `QFileOpenEvent` already reaches `OpenFileFilter`. The bundle's `Info.plist` declares the setup file type (`CFBundleDocumentTypes` and an exported UTI for `.ejconfig`) so Finder opens the file in the app | A file passed on the command line goes to the same filter path. The MSIX manifest, or the installer, registers `.ejconfig`. A second launch hands its file to the running instance (single instance) |
| Picker | `FileDialog`, as today | `FileDialog`, as today |
| Lifecycle (NOTE-03, DAY-01) | See below | See below |
| Accessibility settings | Reduce motion and increase contrast from `NSWorkspace` | Client-area animations (`SPI_GETCLIENTAREAANIMATION`) and high contrast (`SPI_GETHIGHCONTRAST`) |
| Appearance | Light and dark follow the system through `QStyleHints::colorScheme`; a checklist row | Same; Windows 11 reports it to Qt |
| Timing marks | `os_signpost` is not needed; the portable log line is enough | Same |

**Lifecycle.** On a phone the app becomes inactive and is suspended. On a desktop, being active follows window focus,
and the app may stay open for days. Stage 5's `LifecycleCoordinator` stays, and the desktop adds three things:

1. **Inactive is focus loss.** Flushing the note draft on focus loss is cheap and stays. Draining on focus loss is not
   needed, because nothing suspends the process.
2. **A day check without a state change.** A window left open overnight never "becomes active". The app checks the day
   on a timer at the next local midnight (QTimeZone, re-armed after each check), on wake from sleep, and on focus. This
   is the one new piece of portable logic, and it is tested with the fake clock across daylight-saving changes.
3. **Ending.** Closing the last window quits the app, Mac included, because it is a single-window tool. Quit drains
   through `aboutToQuit` as today. Windows log-off and shutdown arrive as `QGuiApplication::commitDataRequest`, which
   flushes and drains before it returns. On the Mac, Sudden Termination is left off, so the system asks the app before
   it quits.

**Single instance.** Two processes on one store would contend for the file lock that Stage 2 built
(`file_lock.cpp`). A second launch therefore finds the first through a `QLocalServer` named per user and per data
folder. It passes along its file argument, if it has one, and exits, and the first window comes to the front. If the
lock is held and no server answers (the first process hung), the second launch shows "Field Test is already open"
and quits without touching the store.

### Where data is stored

| | Mac | Windows |
|---|---|---|
| Store (SQLite and journal) | `~/Library/Application Support/<org>/<app>/`; in the sandbox (store or sandboxed `.dmg`) it is under `~/Library/Containers/<bundle id>/Data/…`, and Qt resolves it | **`QStandardPaths::AppLocalDataLocation`** (`%LOCALAPPDATA%\…`), not today's roaming `AppDataLocation`. A store in a roaming profile can be copied between machines at sign-in and sign-out, which a single-writer journal must not allow. Under MSIX the path is redirected to the package's private folder automatically |
| Settings and window geometry | `QSettings` (the preferences plist) | `QSettings` (the registry, under `HKCU\Software\…`) |
| Protection | No per-file protection (`UNSUPPORTED`), as today. FileVault covers the disk. The folder is created as `0700` | The folder inherits the user-only ACL of `%LOCALAPPDATA%`. BitLocker covers the disk |
| Backups | Time Machine copies it, so this needs saying in the docs (NFR-09). Excluding the store from Time Machine (`NSURLIsExcludedFromBackupKey`) matches the phone, where the store is not in iCloud backup if Stage 5 chose that. Owner decision 4 | OneDrive does not touch `%LOCALAPPDATA%` |
| Uninstall | Dragging the app to the Bin leaves the data, which is normal on a Mac. The help says where it is | MSIX removes the package's data on uninstall. **That would lose entries**, so uninstall asks the user to share today's log first (Stage 10 help, and a row in the checklist). An installer (Inno Setup) leaves the data |

The constants change in `main.cpp`, which takes the folder from a `platform_paths()` function. Each platform defines
that function in its own `src/platform/<os>/` file.

### Updates

| Route | How updates arrive | Work |
|---|---|---|
| Mac App Store, or the iPad app on a Mac | The store | None |
| Mac `.dmg` with Developer ID | **Sparkle 2** (EdDSA-signed appcast). The appcast and the `.dmg` are hosted with the documentation (Stage 11's hosting decision) | Add Sparkle: one framework, and the signing key as an owner secret. Or a "new version available" check that only opens the download page (no secret, no framework, recommended for a first release) |
| Windows Microsoft Store | The store | None |
| Windows MSIX outside the store | An `.appinstaller` file with `OnLaunch` update checks, hosted with the documentation | One XML file generated by the release job |
| Windows signed installer (Inno Setup) | None built in; winget can list it, and the same "new version available" check works | The version check |

The "new version available" check reads a small JSON file from the documentation site. It is a network call the app
does not make today, and the privacy policy (Stage 11) would have to say so. On a store build the check is switched off
through the platform layer.

## Packaging and signing

### Mac

#### Route 0: the iPad app on a Mac (no new product)

After Stage 8, the iPad build runs on Apple-silicon Macs from the Mac App Store unless it is opted out in App Store
Connect.

- **Cost:** a check of the iPad build on the owner's Mac, and the opt-in setting.
- **Limits:** no Intel Macs; touch-first controls with a mouse; Qt's iOS plugin running under the Mac compatibility
  layer has not been checked; no menu bar or window sizing. Keyboard shortcuts are possible only if 12.3 is done for
  iPad hardware keyboards, which also helps iPad users.
- **Decided (owner, 10 Nov 2026): off in v1.** The iPhone and iPad app is not offered on Macs. In App Store Connect,
  "Make this app available" on Apple-silicon Macs is turned off before the first public release (Stage 11). Route 0 is
  considered again with the rest of this page in v2.

#### Route 1: Developer ID, notarised `.dmg` (recommended if a Mac product ships)

- **Build:** a `MACOSX_BUNDLE` app from a new `packaging/macos.cmake` and `packaging/macos/Info.plist.in`. The icon comes
  from the iOS asset catalogue. `qt_generate_deploy_qml_app_script` copies Qt into the bundle. The build is universal
  (`arm64;x86_64`) only if Qt and vcpkg's dependencies build for both, which needs a second triplet; otherwise it is
  `arm64` only, and Intel Macs are not supported (decision 5).
- **Hardened runtime:** required for notarisation. Entitlements are none beyond the defaults. The App Sandbox is
  optional outside the store and recommended (user-selected read and write files only), so the store route stays open.
- **Sign and notarise:** `codesign --options runtime --timestamp` with a Developer ID Application certificate; then
  `xcrun notarytool submit --wait` with an App Store Connect API key; then `xcrun stapler staple` on the app and the
  `.dmg`. `verify_app.sh` gains a macOS mode that runs `spctl -a -vv`, `codesign --verify --deep --strict` and checks
  the staple.
- **Licence:** distribution outside the store does not raise the App Store's GPL conflict. The app is GPL-3.0-or-later,
  and Qt can be linked dynamically on the Mac, so the LGPL obligations are the ordinary ones (source offer, notices).
  `IclNotices` already gathers the notices.

#### Route 2: Mac App Store

- **Requirements:** App Sandbox (mandatory); an Apple Distribution certificate and a Mac App Store profile; a separate
  App Store Connect platform on the same app record, with the same bundle ID for universal purchase; uploaded from CI
  like the iOS build.
- **Problems:** the same GPL and App Store terms conflict that blocks Stage 11 applies, and so does a second App Review.
  Qt's frameworks inside the bundle must pass the store's private-API scan, which Qt usually does and which can only be
  proved by uploading.
- **When to choose it:** if the owner wants one listing for iPhone, iPad and Mac and has settled the licence question
  for Stage 11. Otherwise Route 1.

### Windows

#### Route 1: Microsoft Store with MSIX (recommended)

- **Build:** `qt_generate_deploy_qml_app_script`, which runs `windeployqt`, puts the app and Qt in a folder;
  `makeappx pack` with an `AppxManifest.xml` from `packaging/windows/` makes the `.msix`. The manifest declares the
  `.ejconfig` file type, the app identity and the tile assets, which are generated from the app icon.
- **Signing:** the Store re-signs every package it distributes, so **no code-signing certificate is needed**. A
  self-signed test certificate (made in CI, not a secret) signs the sideload build that the checklist installs.
- **Account:** a Microsoft Partner Center developer account. Microsoft dropped the fee for individual developers in
  2025; a company account needs a one-off fee and verification (check at sign-up). The licence advice the owner takes
  for Stage 11 should cover the Microsoft Store's terms as well; they are generally seen as less of a problem for the
  GPL than Apple's.
- **Certification:** usually 1 to 3 days. It needs the privacy policy URL (Stage 11), an age rating (IARC
  questionnaire) and screenshots at desktop size.
- **Updates and uninstall:** both through the Store. The data is removed on uninstall (see
  [Where data is stored](#where-data-is-stored)).

#### Route 2: a signed installer outside the Store

- **Build:** the same deployed folder, packed with Inno Setup (or MSIX with an `.appinstaller`).
- **Signing:** an OV or EV code-signing certificate. Since June 2023 the private key must sit in a hardware module or a
  cloud signing service, so CI signs through a cloud HSM (DigiCert KeyLocker, SSL.com eSigner) or **Azure Trusted
  Signing**. Azure Trusted Signing is the cheapest at roughly US$10 a month, but at the time of writing it is offered
  to organisations in a limited list of countries and to individuals in the US and Canada; the owner checks
  eligibility from Australia. Without a certificate, SmartScreen blocks the download for every new user.
- **Reputation:** even when signed, a new certificate shows SmartScreen warnings until downloads build reputation. The
  Store avoids that.

## Work packages

Each package is one PR into `develop`. They start after the owner approves this page and after Stage 8's wide layout
has merged. The packages a platform needs are marked: **W** Windows, **M** Mac, **B** both.

| # | Package | For | Depends on |
|---|---|---|---|
| 12.1 | **Windows CI leg.** A `build-windows` job on `windows-2025` with `windows-msvc-release` and the tests, and a `windows-msvc-asan` leg in the full set. It is placed by `.github/ci/plan.json`; `docs/ci.md` and `docs/building.md` updated. Worth doing even under option D | W | – |
| 12.2 | **Window and layout.** The size breakpoint (shared with Stage 8), minimum and first sizes, remembered geometry, the title bar flag through the platform layer, zero insets. `ui` tests for resize across the breakpoint | B | Stage 8 merged |
| 12.3 | **Mouse and keyboard.** Hover states, focus rings, the shortcuts, the right-click menus on tiles and rows, Esc and click-outside for sheets, the wheel on the tumbler, Delete on rows. `ui` tests drive each one with `QTest` key and mouse events. This also gives iPad hardware-keyboard users the shortcuts | B | 12.2 |
| 12.4 | **Desktop lifecycle and single instance.** The midnight and wake timer, `commitDataRequest`, the `QLocalServer` hand-over with the file argument, the "already open" path. Portable logic in `libs/ios_bridge` with fakes; the platform files are thin | B | – |
| 12.5 | **Data folder.** `platform_paths()` per platform, `AppLocalDataLocation` on Windows, and a one-time move of an existing development store (if the old folder has a store and the new one is empty). Integration tests on real folders | B | – |
| 12.6 | **Windows platform files.** `src/platform/windows/`: copy and save as share, accessibility settings, file argument. `changelog.d/` fragment | W | 12.4, 12.5 |
| 12.7 | **Windows package.** `packaging/windows/` (manifest, assets), the deploy and `makeappx` step, a `verify_msix` script (manifest fields, file type, no stray DLLs, notices present), a test-signed artefact on every PR that touches packaging, the Store or signed job in `release.yml`. `docs/releasing.md` | W | 12.6, owner O-W1 or O-W2 |
| 12.8 | **Mac platform files.** `src/platform/macos/`: the share picker, accessibility settings, menu bar, `Info.plist` document types. The `.mm` files are compiled with `-fobjc-arc` like iOS | M | 12.4, 12.5 |
| 12.9 | **Mac package.** `packaging/macos.cmake`, hardened runtime, the deploy script, the `.dmg`, sign, notarise and staple in a `release.yml` `macos` job, `verify_app.sh --macos`, the version check. `docs/releasing.md` | M | 12.8, owner O-M1 |
| 12.10 | **Checklist pass and gate record.** On this machine for Windows; on the owner's Mac for the Mac. Results recorded on this page | B | 12.7 and/or 12.9 |
| 12.11 | **Route 0 check (Mac, iPad app).** No code: the owner runs the iPad TestFlight build on their Mac and records the result. Any fix is a small PR | M | Stage 8 on TestFlight |

Option B is 12.1 to 12.7 and 12.10, with 12.11 as a no-code check. Option C is 12.2 to 12.5 and 12.8 to 12.10, with 12.11. 12.2 and 12.3 are most of the
work and serve both platforms, so the second platform costs about half the first.

## Test list

Labels follow `docs/testing.md`. Every test runs in CI on the platform it concerns: Windows tests in the 12.1 leg, Mac
tests in `build-macos`. Portable tests run on Linux as well.

| Test | Label | What it checks |
|---|---|---|
| `window_layout_breakpoint` | ui | Width above and below the breakpoint chooses the wide or the phone layout; crossing it keeps the open sheet, the draft text and the focus |
| `window_geometry_restore` | ui | Size and position are saved and restored; a missing screen falls back to the primary screen; below-minimum values are raised to the minimum |
| `desktop_insets_zero` | ui | `SafeFrame` insets are zero and no screen has a gap at the top or bottom |
| `keyboard_tile_digits` | ui | 1–9 and 0 log the right tiles; Shift+digit opens the note; nothing logs while a text field has focus |
| `keyboard_shortcuts` | ui | Every shortcut in the table does its action on the screen it belongs to, and nothing on the others |
| `keyboard_focus_order` | ui | Tab visits every control of every screen in reading order and the focus ring is visible |
| `context_menu_row` | ui | Right-click on a Review row offers Copy, Edit and Delete; Delete goes through undo |
| `context_menu_tile` | ui | Right-click and Shift+click on a tile open the note, the same as a hold |
| `sheet_dismiss_desktop` | ui | Esc and a click outside close a sheet and keep its draft, the same as the drag |
| `tumbler_wheel_keys` | ui | The wheel and the arrow keys change the time by one step; typed digits set it |
| `hover_states` | ui | Each clickable component has a hover state, and hover reveals no action that is not also in the menu |
| `day_check_midnight_timer` | unit | Fake clock: the check runs at the next local midnight, re-arms, and handles both daylight-saving changes in the configured zone |
| `day_check_on_wake` | unit | A wake event after midnight runs the check once |
| `commit_data_drains` | unit | `commitDataRequest` flushes the draft and drains before it returns |
| `single_instance_handover` | integration | A second process with a file argument hands the file to the first and exits with success; the first imports it |
| `single_instance_hung_first` | integration | With the lock held and no server, the second process reports and exits without opening the store |
| `platform_paths_windows` | integration | The store is under `%LOCALAPPDATA%`; an old store in the roaming folder is moved once and not again |
| `platform_paths_macos` | integration | The store is under Application Support (or the container), with mode `0700` |
| `share_copy_counts_as_shared` | unit | On a desktop, copy and save mark the log shared (DAY-01), and a cancelled save does not |
| `kill_desktop` | system | The kill harness's short run on `windows-msvc-release`; the Mac is already in the kill queue |
| `verify_msix` | integration | Manifest identity, version and file type; Qt's QML modules present; notices present |
| `verify_app_macos` | integration | Bundle identifier, version, document types, hardened runtime flag, entitlements; signature and staple on the release job only |
| `startup_budget_desktop` | perf | Cold start to a usable board under 1.5 s on the CI runner (NFR-04 applied to desktop, informative) |

### Desktop checklist (the exit gate)

Run on each platform that ships, from the signed release artefact: on this machine for Windows (Windows 11), and on the
owner's Mac for the Mac. Only fictional data is used (Sam Example, UNIT, North Site, Wrong Response).

| # | Check |
|---|---|
| D1 | Installs from the store, or from the `.dmg` or installer, with no security warning (Gatekeeper / SmartScreen) |
| D2 | First launch opens at the first-launch size; setup imports the sample setup file by double-clicking it in Finder or Explorer |
| D3 | A day of logging with the mouse only, then with the keyboard only (digits, Shift+digit note, undo, Delete) |
| D4 | Resize across the breakpoint with a note open; maximise; move between a 100 % and a 200 % screen |
| D5 | Share: the Mac picker posts to Mail or Notes; Windows copies and saves; the log shows as shared |
| D6 | Leave the app open past midnight (or change the clock): the new-day prompt appears without touching the window |
| D7 | End the process from Task Manager / Activity Monitor mid-log: on reopening, every acknowledged entry is there |
| D8 | Sign out of Windows / restart the Mac with the app open and a note half typed: the draft is kept |
| D9 | Launch a second copy: the first comes to the front |
| D10 | Light and dark follow the system; reduce motion and high contrast are honoured |
| D11 | Update to the next build (store, Sparkle or `.appinstaller`, or the version check), with data kept |
| D12 | Uninstall: what happens to the data matches the help text |

## CI jobs

| Job | Workflow | Runs on | When | Needs a secret |
|---|---|---|---|---|
| `build-windows` (release + tests) | `ci.yml` | `windows-2025` | Each PR the plan places it on, and the merge queue | No |
| `build-windows-asan` | `ci.yml` (full set) | `windows-2025` | The merge queue and nightly | No |
| `build-macos` (exists) | `ci.yml` | `macos-26` | As today; gains the `ui` desktop tests | No |
| `package-windows` | `ci.yml` | `windows-2025` | PRs touching `apps/app/packaging/windows/**`; makes a test-signed `.msix` and runs `verify_msix` | No (self-signed in the job) |
| `package-macos` | `ios-build.yml`, or a sibling `macos-build.yml` | `macos-26` | PRs touching `apps/app/packaging/macos*`; builds the unsigned bundle and `verify_app.sh --macos` | No |
| `windows` (release) | `release.yml` | `windows-2025` | Release tags; Store upload (Partner Center API) or signed installer | Yes: Store credentials, or the signing service credentials (owner) |
| `macos` (release) | `release.yml` | `macos-26` | Release tags; sign, notarise, staple, `.dmg` | Yes: Developer ID certificate, App Store Connect API key (owner) |

Every `uses:` is pinned to a full SHA with a version comment, and `docs/ci.md` and `.github/ci/plan.json` change in the
same PR as the job (AGENTS.md). Windows runners are billed at twice the Linux rate on private repositories; this
repository is public, so the cost is queue time rather than money. The vcpkg and ccache caches gain a Windows key.
Writes stay on `develop` only, as the cache notes already set.

## Effort and risk by platform

### Windows

- **Effort:** 12.1 to 12.7 and 12.10, which is 8 PRs. 12.2 and 12.3 are the large ones. About
  2 weeks of agent time, and the checklist is run here.
- **Risks:**
  - **Data removed on MSIX uninstall.** *Mitigation:* the uninstall text, and Share before uninstalling in the help.
    Losing data on purpose at uninstall is not the P1 "lost entry" defect, but the owner should accept it (decision 2).
  - **`windeployqt` misses a QML module.** *Mitigation:* `verify_msix` lists the modules, and D2 runs the installed
    copy, not the build folder.
  - **The signing route is unavailable from Australia** (route 2 only). *Mitigation:* the Store route needs no
    certificate.
  - **Store certification asks for changes.** *Mitigation:* 1 to 3 days a round; it is not on v1's critical path
    unless option A or B is chosen.
- **Overall: low to medium.**

### Mac

- **Effort:** 12.2 to 12.5, 12.8, 12.9 and 12.10, which is 7 PRs; 12.2 to 12.5 are shared with Windows. About 2 weeks
  alone, or 1 week on top of Windows. Each checklist pass needs the owner at a Mac.
- **Risks:**
  - **No Mac here.** The `.mm` files and the bundle are compiled and inspected in CI but never run. Every behaviour
    claim is from documentation until the owner runs D1 to D12. This is the largest risk, and the same one Stage 5
    carried.
  - **Notarisation rejects a Qt binary** (a missing hardened-runtime flag or an unsigned nested framework).
    *Mitigation:* `codesign --deep --strict` in `package-macos` catches most of these before a release.
  - **Universal build.** vcpkg needs an `x64-osx` build as well, which doubles the macOS cache. *Mitigation:* ship
    `arm64` only (decision 5).
  - **Mac App Store and the GPL.** The same open question as Stage 11. *Mitigation:* Route 1.
- **Overall: medium.** Route 0 alone is **low**.

### Effect on v1

Stage 12 shares nothing with the critical path except Stage 8's layout and the Stage 11 decisions (hosting, privacy
policy, licence). It never delays Stages 7 to 11 if it starts after Stage 8 and the owner holds v1 for nothing in it.
Option B or C should be treated as "ships with v1 if ready, otherwise in v1.1", not as a gate on v1.

## Owner actions

None of these are done by an agent (AGENTS.md: secrets, settings and releases belong to the owner).

| # | Action | Needed for | When |
|---|---|---|---|
| O-D | Decide the option (A, B, C or D) and answer the [decisions](#decisions-for-the-owner) | Everything | Before any code |
| O-W1 | **Windows, Store route:** create a Microsoft Partner Center developer account (individual or company), reserve the name "Field Test", fill in the IARC rating, and add the Store submission credentials (an Entra app registration for the Partner Center API) as repository secrets | 12.7 release job | Before 12.7 merges |
| O-W2 | **Windows, outside-the-Store route:** buy an OV code-signing certificate through a cloud HSM service, or open Azure Trusted Signing if eligible; add its credentials as secrets | 12.7 release job | Before 12.7 merges |
| O-M1 | **Mac, Developer ID route:** in the existing Apple Developer account, create a Developer ID Application certificate and an App Store Connect API key with the Developer role for `notarytool`; add both as repository secrets (`MACOS_DEVELOPER_ID_P12_BASE64`, its password, `ASC_API_KEY_*`); register the bundle ID (the iOS one, or a `.mac` variant, decision 6) | 12.9 | Before 12.9 merges |
| O-M2 | **Mac, Store route (only if chosen):** add a macOS platform to the App Store Connect record, an Apple Distribution certificate and a Mac App Store profile | 12.9 | Before 12.9 merges |
| O-M3 | **Mac, Route 0 (v1, decided):** in App Store Connect, under Pricing and Availability, turn off availability on Apple-silicon Macs ("iPhone and iPad Apps on Apple Silicon Macs") | Stage 11 public release | Before the first public release |
| O-H | Host the update feed (`appcast.xml`, `.appinstaller`, or the version JSON) and the downloads with the documentation | 12.7, 12.9 (non-store routes) | With Stage 11's hosting |
| O-C | Run, or be present for, the Mac checklist; sign the gate for each platform | 12.10 | At the end |

## Decisions for the owner

| # | Decision | Recommendation |
|---|---|---|
| 1 | Which option: A both, B Windows only, C Mac only, D defer | **Decided (owner, 10 Nov 2026): desktop support is v2.** The recommendation was B if Stages 7 to 11 were on time, otherwise D, with Route 0 for the Mac and 12.1 now in every case. Route 0 (the iPad app on a Mac) is off in v1 (owner, 10 Nov 2026), a store setting made in Stage 11 (O-M3) |
| 2 | Windows distribution: Microsoft Store (MSIX) or a signed installer | **Microsoft Store**: no certificate, updates and SmartScreen handled, and accepting that uninstall removes the data |
| 3 | On Windows, does a copy or a save count as "shared" for the end-of-day prompt (DAY-01)? | **Yes**, as Copy already does on the phone |
| 4 | Exclude the Mac store from Time Machine, to match the phone's backup rule | Match whatever Stage 5 chose for the phone; if the phone's store is backed up, leave Time Machine alone |
| 5 | Mac architectures: Apple silicon only, or universal | **Apple silicon only** for v1. Intel Macs are then unsupported (Route 0 is Apple silicon only too) |
| 6 | Mac bundle ID: the iOS one (universal purchase, needed for the Mac App Store's shared listing) or a separate one | **The iOS one** keeps the store route open; Developer ID works with either |
| 7 | Updates outside a store: Sparkle or a "new version available" check | **The version check** for v1 (no framework, no extra secret); Sparkle in v1.1 if users ask |
| 8 | Name on desktop | "Field Test", as everywhere else |

## What cannot be verified here

- There is no Mac. Every Mac row is compiled and inspected in CI and run only by the owner.
- The Microsoft Store and Apple notarisation services are reached only from the release workflow, once the owner's
  credentials exist. Until then the packaging is proved with a test-signed `.msix` and an unsigned bundle.
- Store certification, SmartScreen reputation and Gatekeeper messages are described from the vendors' documentation as
  remembered (November 2026). The owner confirms the account fees and the Azure Trusted Signing eligibility when
  signing up.
- Each PR says what has not run on a device (AGENTS.md).
